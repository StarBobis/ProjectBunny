// F-201b audit: DXBC/DXIL shader-model container parsing.
//
// The test calls the production helpers in ShaderTools/DX12ShaderModelParse.h
// (the same header DX12ShaderDump.cpp includes), so the wrapping-offset rule
// is exercised where it is implemented, not in a copy.
//
// Red on the pre-fix code: the 0xffffffff offset fixtures end at a committed
// page boundary followed by an uncommitted reservation, so the old additive
// `uint32` bound check passes and the chunkFourCC memcpy faults.
// Green after the fix: every wrapping offset is skipped and the parser returns
// false without touching memory outside the supplied blob.
#include "DX12ShaderModelParse.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

constexpr size_t kContainerHeaderSize = 32;
constexpr size_t kChunkTableEntrySize = sizeof(uint32_t);

int gFailures = 0;

void Expect(bool condition, const char *what)
{
	if (condition)
		return;
	std::fprintf(stderr, "FAIL: %s\n", what);
	++gFailures;
}

void WriteU32(Bytes &bytes, size_t offset, uint32_t value)
{
	if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
		throw std::runtime_error("F-201b fixture write outside its buffer");
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

struct ChunkSpec
{
	const char *fourCC;
	uint32_t versionToken;
	size_t bytes;
};

Bytes MakeContainer(const std::vector<ChunkSpec> &chunks)
{
	size_t total = kContainerHeaderSize + kChunkTableEntrySize * chunks.size();
	for (const ChunkSpec &chunk : chunks) {
		if (chunk.bytes < 12)
			throw std::runtime_error("F-201b fixture chunk is shorter than a chunk header");
		total += chunk.bytes;
	}

	Bytes bytes(total);
	std::memcpy(bytes.data(), "DXBC", 4);
	WriteU32(bytes, 20, 1);
	WriteU32(bytes, 24, static_cast<uint32_t>(total));
	WriteU32(bytes, 28, static_cast<uint32_t>(chunks.size()));

	size_t offset = kContainerHeaderSize + kChunkTableEntrySize * chunks.size();
	for (size_t i = 0; i < chunks.size(); ++i) {
		const ChunkSpec &chunk = chunks[i];
		WriteU32(bytes, kContainerHeaderSize + kChunkTableEntrySize * i, static_cast<uint32_t>(offset));
		std::memcpy(bytes.data() + offset, chunk.fourCC, 4);
		WriteU32(bytes, offset + 4, static_cast<uint32_t>(chunk.bytes - 8));
		WriteU32(bytes, offset + 8, chunk.versionToken);
		offset += chunk.bytes;
	}
	return bytes;
}

// Places the fixture so its last byte is the last byte of a committed page.
// The following page stays reserved but uncommitted, so any read at a wild
// offset faults instead of silently returning heap garbage.
class GuardedFixture
{
public:
	explicit GuardedFixture(const Bytes &bytes)
	{
		SYSTEM_INFO info = {};
		GetSystemInfo(&info);
		const size_t page = info.dwPageSize;
		if (bytes.size() >= page)
			throw std::runtime_error("F-201b fixture must fit in one page");

		const size_t reserve = static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) + 1 + 2 * page;
		reservation_ = VirtualAlloc(nullptr, reserve, MEM_RESERVE, PAGE_NOACCESS);
		if (!reservation_)
			throw std::runtime_error("F-201b cannot reserve guarded offset address space");
		if (!VirtualAlloc(reservation_, page, MEM_COMMIT, PAGE_READWRITE)) {
			VirtualFree(reservation_, 0, MEM_RELEASE);
			reservation_ = nullptr;
			throw std::runtime_error("F-201b cannot commit the fixture page");
		}
		data_ = static_cast<uint8_t*>(reservation_) + page - bytes.size();
		if (!bytes.empty())
			std::memcpy(data_, bytes.data(), bytes.size());
	}

	~GuardedFixture()
	{
		if (reservation_)
			VirtualFree(reservation_, 0, MEM_RELEASE);
	}

	GuardedFixture(const GuardedFixture&) = delete;
	GuardedFixture& operator=(const GuardedFixture&) = delete;

	uint8_t *data() const { return data_; }

private:
	void *reservation_ = nullptr;
	uint8_t *data_ = nullptr;
};

void ExpectModel(const void *data, size_t size, const char *expected, const char *what)
{
	std::string model = "unchanged";
	const bool found = dx12_shader_parse::GetShaderModelFromBytes(data, size, &model);
	if (!expected) {
		Expect(!found && model == "unchanged", what);
		return;
	}
	Expect(found && model == expected, what);
}

void TestValidModels()
{
	const Bytes ps = MakeContainer({{"SHDR", 0x00000050, 12}});
	ExpectModel(ps.data(), ps.size(), "ps_5_0", "F-201b: 12-byte SHDR header still yields ps_5_0");

	const Bytes shex = MakeContainer({{"SHEX", 0x00010051, 16}});
	ExpectModel(shex.data(), shex.size(), "vs_5_1", "F-201b: SHEX header still yields vs_5_1");

	const Bytes dxil = MakeContainer({{"DXIL", 0x00050060, 16}});
	ExpectModel(dxil.data(), dxil.size(), "cs_6_0", "F-201b: DXIL header still yields cs_6_0");

	for (uint32_t kind = 0; kind <= 6; ++kind) {
		const uint32_t token = (kind << 16) | 0x50;
		const Bytes chunk = MakeContainer({{"SHDR", token, 12}});
		const char *models[] = {"ps_5_0", "vs_5_0", "gs_5_0", "hs_5_0", "ds_5_0", "cs_5_0", nullptr};
		char message[128];
		std::snprintf(message, sizeof(message),
			"F-201b: program kind %u keeps its previous model result", kind);
		ExpectModel(chunk.data(), chunk.size(), models[kind], message);
	}

	const Bytes wide = MakeContainer({{"SHDR", 0xffffffff, 12}});
	ExpectModel(wide.data(), wide.size(), nullptr, "F-201b: unknown program kind leaves the model untouched");
}

void TestWrappingOffsets()
{
	const uint32_t offsets[] = {
		0xffffffffu, 0xfffffffeu, 0xfffffffdu, 0xfffffffcu, 0xfffffff8u, 0xfffffff4u,
	};
	for (const uint32_t offset : offsets) {
		Bytes dxbc = MakeContainer({{"SHDR", 0x00000050, 16}});
		WriteU32(dxbc, kContainerHeaderSize, offset);
		GuardedFixture guardedDxbc(dxbc);
		char message[160];
		std::snprintf(message, sizeof(message),
			"F-201b: SHDR chunk offset 0x%08x is rejected before the chunk memcpy", offset);
		ExpectModel(guardedDxbc.data(), dxbc.size(), nullptr, message);

		Bytes dxil = MakeContainer({{"DXIL", 0x00000060, 16}});
		WriteU32(dxil, kContainerHeaderSize, offset);
		GuardedFixture guardedDxil(dxil);
		std::snprintf(message, sizeof(message),
			"F-201b: DXIL chunk offset 0x%08x is rejected before the chunk memcpy", offset);
		ExpectModel(guardedDxil.data(), dxil.size(), nullptr, message);
	}
}

void TestHeaderBoundaries()
{
	const Bytes dxbc = MakeContainer({{"SHDR", 0x00000050, 12}});
	ExpectModel(dxbc.data(), dxbc.size(), "ps_5_0",
		"F-201b: SHDR chunk ending exactly at the end of the blob remains readable");
	ExpectModel(dxbc.data(), dxbc.size() - 1, nullptr,
		"F-201b: one byte short of the 12-byte SHDR header is rejected");

	const Bytes dxil = MakeContainer({{"DXIL", 0x00000060, 16}});
	ExpectModel(dxil.data(), dxil.size(), "ps_6_0",
		"F-201b: DXIL chunk ending exactly at the end of the blob remains readable");
	ExpectModel(dxil.data(), dxil.size() - 1, nullptr,
		"F-201b: one byte short of the 16-byte DXIL header is rejected");

	const Bytes shortDxil = MakeContainer({{"DXIL", 0x00000060, 12}});
	ExpectModel(shortDxil.data(), shortDxil.size(), nullptr,
		"F-201b: the existing 16-byte DXIL requirement is preserved");

	Bytes atEnd = MakeContainer({{"SHDR", 0x00000050, 16}});
	WriteU32(atEnd, kContainerHeaderSize, static_cast<uint32_t>(atEnd.size()));
	ExpectModel(atEnd.data(), atEnd.size(), nullptr, "F-201b: chunk offset at the blob end is rejected");
	WriteU32(atEnd, kContainerHeaderSize, static_cast<uint32_t>(atEnd.size()) + 1);
	ExpectModel(atEnd.data(), atEnd.size(), nullptr, "F-201b: chunk offset past the blob end is rejected");
}

void TestTruncatedBlobs()
{
	const Bytes valid = MakeContainer({{"SHDR", 0x00000050, 16}});
	ExpectModel(valid.data(), valid.size(), "ps_5_0", "F-201b: the complete 52-byte fixture parses");
	// The single code chunk starts right after the table and needs 12 readable
	// bytes (fourCC, size, version token).  A prefix that still contains all 12
	// is a complete metadata read, not a truncation failure: sizes 48..51 must
	// return ps_5_0.  Shorter prefixes must fail without reading past their size.
	const size_t chunkOffset = kContainerHeaderSize + kChunkTableEntrySize;
	const size_t completeHeader = chunkOffset + 12;
	for (size_t size = 0; size < valid.size(); ++size) {
		const Bytes truncated(valid.begin(), valid.begin() + size);
		GuardedFixture guarded(truncated);
		const char *expected = size >= completeHeader ? "ps_5_0" : nullptr;
		char message[160];
		std::snprintf(message, sizeof(message),
			"F-201b: a %zu-byte prefix %s without reading past its size", size,
			expected ? "keeps the complete 12-byte code header" : "fails");
		ExpectModel(guarded.data(), size, expected, message);
	}
}

void TestChunkSelection()
{
	const Bytes twoCode = MakeContainer({{"SHDR", 0x00000050, 12}, {"SHDR", 0x00010050, 12}});
	ExpectModel(twoCode.data(), twoCode.size(), "ps_5_0",
		"F-201b: the first code chunk keeps winning as before");

	const Bytes descriptorFirst = MakeContainer({{"RDEF", 0, 16}, {"SHDR", 0x00010050, 12}});
	ExpectModel(descriptorFirst.data(), descriptorFirst.size(), "vs_5_0",
		"F-201b: a leading non-code chunk is skipped");

	Bytes invalidFirst = descriptorFirst;
	WriteU32(invalidFirst, kContainerHeaderSize, 0xfffffff8u);
	GuardedFixture guarded(invalidFirst);
	ExpectModel(guarded.data(), invalidFirst.size(), "vs_5_0",
		"F-201b: an invalid offset is skipped and the later code chunk is still found");

	const Bytes descriptorOnly = MakeContainer({{"RDEF", 0, 16}});
	ExpectModel(descriptorOnly.data(), descriptorOnly.size(), nullptr,
		"F-201b: a container without a code chunk leaves the model untouched");

	Bytes badTag = MakeContainer({{"SHDR", 0x00000050, 12}});
	std::memcpy(badTag.data(), "DXBc", 4);
	ExpectModel(badTag.data(), badTag.size(), nullptr, "F-201b: a wrong container tag fails");

	Bytes wideCount = MakeContainer({{"RDEF", 0, 16}});
	WriteU32(wideCount, 28, 0xffffffffu);
	ExpectModel(wideCount.data(), wideCount.size(), nullptr,
		"F-201b: a chunk count larger than the table fails without iterating out of bounds");
}

void TestInputForms()
{
	std::string model = "unchanged";
	Expect(!dx12_shader_parse::GetShaderModelFromBytes(nullptr, 64, &model) && model == "unchanged",
		"F-201b: null data preserves the failure output");
	const Bytes valid = MakeContainer({{"SHDR", 0x00000050, 12}});
	Expect(!dx12_shader_parse::GetShaderModelFromBytes(valid.data(), valid.size(), nullptr),
		"F-201b: null output is rejected");

	Bytes unaligned(valid.size() + 1);
	std::memcpy(unaligned.data() + 1, valid.data(), valid.size());
	ExpectModel(unaligned.data() + 1, valid.size(), "ps_5_0",
		"F-201b: an unaligned blob still parses through the memcpy-based reads");

	Bytes metadataOnly = MakeContainer({{"SHDR", 0x00000050, 12}});
	WriteU32(metadataOnly, 20, 0);
	WriteU32(metadataOnly, 24, 0);
	ExpectModel(metadataOnly.data(), metadataOnly.size(), "ps_5_0",
		"F-201b: metadata-only extraction policy is unchanged");
}

void RunF201b()
{
	TestValidModels();
	TestWrappingOffsets();
	TestHeaderBoundaries();
	TestTruncatedBlobs();
	TestChunkSelection();
	TestInputForms();
}

struct AuditItem
{
	const char *id;
	void (*run)();
};

const AuditItem kAuditItems[] = {
	{"F-201b", RunF201b},
};

int DumpModelFile(const char *path)
{
	FILE *file = std::fopen(path, "rb");
	if (!file) {
		std::fprintf(stderr, "Cannot open shader file: %s\n", path);
		return 2;
	}
	std::fseek(file, 0, SEEK_END);
	const long length = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (length < 0) {
		std::fclose(file);
		return 2;
	}
	Bytes bytes(static_cast<size_t>(length));
	const size_t read = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
	std::fclose(file);
	if (read != bytes.size()) {
		std::fprintf(stderr, "Cannot read complete shader file: %s\n", path);
		return 2;
	}

	std::string model;
	if (!dx12_shader_parse::GetShaderModelFromBytes(bytes.data(), bytes.size(), &model))
		return 1;
	std::printf("%s\n", model.c_str());
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc == 3 && !std::strcmp(argv[1], "--audit-item")) {
		for (const AuditItem &item : kAuditItems) {
			if (std::strcmp(argv[2], item.id))
				continue;
			gFailures = 0;
			item.run();
			if (gFailures) {
				std::fprintf(stderr, "FAIL audit item: %s (%d checks)\n", item.id, gFailures);
				return 1;
			}
			std::printf("PASS audit item: %s\n", item.id);
			return 0;
		}
		std::fprintf(stderr, "Unknown audit item: %s\n", argv[2]);
		return 2;
	}

	if (argc == 3 && !std::strcmp(argv[1], "--dump-model"))
		return DumpModelFile(argv[2]);

	std::fprintf(stderr,
		"Usage: dx12_shader_model_audit [--audit-item F-201b | --dump-model <shader.bin>]\n");
	return 2;
}
