// F-136: texture data hashing must prove the declared layout before reading.
//
// Production code intentionally has no SEH handler. The only __try below is in
// this test, so the pre-fix access violation is reported as a failed assertion
// instead of killing the whole regression host.
#include "ResourceHash.h"
#include "globals.h"
#include "slot_range_test_support.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace dx11_slot_test;

// Captures warnings for one refusal without writing to the real log file.
class HashLogScope {
	FILE *saved_file = LogFile;
	LogVerbosity saved_level = gLogVerbosity;
public:
	FILE *capture = std::tmpfile();
	HashLogScope()
	{
		require_batch(capture != nullptr, "F136: actual warning capture");
		LogFile = capture;
		gLogVerbosity = LogVerbosity::WARNING;
	}
	~HashLogScope()
	{
		LogFile = saved_file;
		gLogVerbosity = saved_level;
		std::fclose(capture);
	}
	std::string text()
	{
		require_batch(!std::fflush(capture) && !std::fseek(capture, 0, SEEK_SET),
			"F136: read completed warning stream");
		std::string result;
		char buffer[1024];
		size_t count = 0;
		while ((count = std::fread(buffer, 1, sizeof(buffer), capture)) != 0)
			result.append(buffer, count);
		return result;
	}
};

// The requested bytes end exactly at a PAGE_NOACCESS boundary. Any read past
// the declared span raises an access violation that the probe can observe.
class GuardedSpan {
	void *allocation = nullptr;
public:
	uint8_t *data = nullptr;
	explicit GuardedSpan(size_t size)
	{
		SYSTEM_INFO info{};
		GetSystemInfo(&info);
		require_batch(size <= info.dwPageSize, "F136: fixture fits one page");
		allocation = VirtualAlloc(nullptr, info.dwPageSize * 2,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		require_batch(allocation != nullptr, "F136: guarded texture fixture");
		auto *guard = static_cast<uint8_t*>(allocation) + info.dwPageSize;
		DWORD previous = 0;
		if (!VirtualProtect(guard, info.dwPageSize, PAGE_NOACCESS, &previous)) {
			VirtualFree(allocation, 0, MEM_RELEASE);
			allocation = nullptr;
			throw std::runtime_error("F136: protect the byte after the declared span");
		}
		data = guard - size;
		for (size_t i = 0; i < size; ++i)
			data[i] = static_cast<uint8_t>(0x5a + i);
	}
	~GuardedSpan()
	{
		if (allocation)
			VirtualFree(allocation, 0, MEM_RELEASE);
	}
};

// Kept free of C++ objects requiring unwinding so /EHsc permits __try.
DWORD ProbeTexture3DHash(const D3D11_TEXTURE3D_DESC *desc,
	const D3D11_SUBRESOURCE_DATA *initial, uint32_t *hash)
{
	DWORD code = 0;
	__try {
		*hash = CalcTexture3DDataHash(desc, initial);
	}
	__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
	}
	return code;
}

std::vector<uint8_t> Pattern(size_t count, uint8_t seed = 0x31)
{
	std::vector<uint8_t> bytes(count);
	for (size_t i = 0; i < count; ++i)
		bytes[i] = static_cast<uint8_t>(seed + i * 37);
	return bytes;
}

uint32_t ExpectedCrc(const std::vector<uint8_t> &bytes, size_t length)
{
	require_batch(length <= bytes.size(), "F136: expected span fits fixture");
	return crc32c_append(0, bytes.data(), length);
}

// Keeps the global texture_hash_version out of the picture for the legacy
// fixtures, and restores whatever an earlier audit item left behind.
class TextureHashVersionScope {
	int saved = G->texture_hash_version;
public:
	TextureHashVersionScope() { G->texture_hash_version = 0; }
	~TextureHashVersionScope() { G->texture_hash_version = saved; }
};

void Test3DSlicePitchGuard()
{
	D3D11_TEXTURE3D_DESC desc{};
	desc.Width = desc.Height = desc.Depth = 8;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_BC1_UNORM;

	// Packed BC1 8x8 slice pitch is 32 bytes. The caller only promises 16
	// bytes per slice (128 bytes total) and the next page is inaccessible.
	GuardedSpan bytes(128);
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = bytes.data;
	initial.SysMemPitch = 16;
	initial.SysMemSlicePitch = 16;

	HashLogScope log;
	uint32_t hash = 0xffffffffu;
	const DWORD code = ProbeTexture3DHash(&desc, &initial, &hash);
	const std::string text = log.text();
	std::fprintf(stderr, "F136: BC1 8x8x8 SysMemSlicePitch=16 guarded probe: "
		"exception=0x%08lx hash=%08x\n", static_cast<unsigned long>(code), hash);
	require_batch(code == 0,
		"F136: a slice pitch below the packed slice must not raise an access violation");
	require_batch(hash == 0,
		"F136: a rejected 3D slice pitch must return the unhashed sentinel");
	require_batch(text.find("SysMemSlicePitch") != std::string::npos,
		"F136: the 3D slice pitch refusal names the violated field");
}

void Test2DPitchTooSmall()
{
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = desc.Height = 4;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	const auto bytes = Pattern(32);
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = bytes.data();
	initial.SysMemPitch = 8; // packed row is 16 bytes

	HashLogScope log;
	const uint32_t hash = CalcTexture2DDataHash(&desc, &initial);
	const std::string text = log.text();
	require_batch(hash == 0, "F136: SysMemPitch below the packed row is refused");
	require_batch(text.find("SysMemPitch") != std::string::npos,
		"F136: the 2D pitch refusal names the violated field");
}

void Test2DOverHardLimit()
{
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = 65536; // D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION is 16384
	desc.Height = 1;
	desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	const auto bytes = Pattern(262144);
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = bytes.data();
	initial.SysMemPitch = 262144;

	HashLogScope log;
	const uint32_t hash = CalcTexture2DDataHash(&desc, &initial);
	const std::string text = log.text();
	require_batch(hash == 0, "F136: a texture above the D3D11 hard limit is refused");
	require_batch(text.find("Width") != std::string::npos,
		"F136: the hard-limit refusal names the offending dimension");
}

void TestLegalHashesUnchanged()
{
	TextureHashVersionScope version;
	HashLogScope log;

	// 1D R8 16: one contiguous format-size * width span.
	{
		const auto bytes = Pattern(16);
		D3D11_TEXTURE1D_DESC desc{};
		desc.Width = 16;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		require_batch(CalcTexture1DDataHash(&desc, &initial) == ExpectedCrc(bytes, 16),
			"F136: legal 1D hash is unchanged");
	}

	// Hard numeric anchor: the published CRC-32C check vector for
	// "123456789" is 0xE3069283. This pins the hash algorithm itself rather
	// than only the byte span.
	{
		const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
		D3D11_TEXTURE1D_DESC desc{};
		desc.Width = 9;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = check;
		require_batch(CalcTexture1DDataHash(&desc, &initial) == 0xE3069283u,
			"F136: legal 1D hash matches the published CRC-32C check vector");
	}

	// 2D RGBA8 4x4: legacy v1.2 length is the first 16 texels only.
	{
		const auto bytes = Pattern(64);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 4;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 16;
		require_batch(CalcTexture2DDataHash(&desc, &initial) == ExpectedCrc(bytes, 16),
			"F136: legal legacy 2D hash is unchanged");
	}

	// 2D BC1 8x8: packed length (32) is shorter than the legacy texel count,
	// so the row walk hashes both 16-byte block rows.
	{
		const auto bytes = Pattern(32);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 8;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_BC1_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 16;
		require_batch(CalcTexture2DDataHash(&desc, &initial) == ExpectedCrc(bytes, 32),
			"F136: legal compressed 2D hash is unchanged");
	}

	// 2D array: legacy path still only reads the wrapped legacy length from
	// the first subresource.
	{
		const auto bytes = Pattern(128);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 4;
		desc.ArraySize = 2;
		desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 16;
		require_batch(CalcTexture2DDataHash(&desc, &initial) == ExpectedCrc(bytes, 32),
			"F136: legal multi-slice 2D hash is unchanged");
	}

	// 3D RGBA8 4x4x4: legacy path hashes width*height*depth bytes.
	{
		const auto bytes = Pattern(256);
		D3D11_TEXTURE3D_DESC desc{};
		desc.Width = desc.Height = desc.Depth = 4;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 16;
		initial.SysMemSlicePitch = 64;
		require_batch(CalcTexture3DDataHash(&desc, &initial) == ExpectedCrc(bytes, 64),
			"F136: legal uncompressed 3D hash is unchanged");
	}

	// 3D BC1 4x4x4: packed length is shorter than the legacy texel count, so
	// the contiguous packed 32 bytes are hashed.
	{
		const auto bytes = Pattern(32);
		D3D11_TEXTURE3D_DESC desc{};
		desc.Width = desc.Height = desc.Depth = 4;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_BC1_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 8;
		initial.SysMemSlicePitch = 32;
		require_batch(CalcTexture3DDataHash(&desc, &initial) == ExpectedCrc(bytes, 32),
			"F136: legal compressed 3D hash is unchanged");
	}

	// Accurate 2D mode hashes every row of the first subresource.
	{
		const auto bytes = Pattern(64);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 4;
		desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D11_SUBRESOURCE_DATA initial{};
		initial.pSysMem = bytes.data();
		initial.SysMemPitch = 16;
		require_batch(CalcTexture2DDataHashAccurate(&desc, &initial) == ExpectedCrc(bytes, 64),
			"F136: legal accurate 2D hash is unchanged");
	}

	require_batch(log.text().empty(), "F136: legal layouts do not warn");
}

void TestUpdateKeepsOldHash()
{
	BatchDevice device;

	D3D11_TEXTURE3D_DESC desc{};
	desc.Width = desc.Height = desc.Depth = 4;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	const auto bytes = Pattern(256);
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = bytes.data();
	initial.SysMemPitch = 16;
	initial.SysMemSlicePitch = 64;

	ComPtr<ID3D11Texture3D> texture;
	require_batch(SUCCEEDED(device.device->CreateTexture3D(&desc, &initial, &texture)) && texture,
		"F136: create a real 3D texture for the CPU-update check");
	const uint32_t before = GetResourceHash(texture.Get());
	require_batch(before != 0, "F136: the created texture has a real hash");

	const auto updated = Pattern(256);
	HashLogScope log;
	UpdateResourceHashFromCPU(texture.Get(), updated.data(), 16, 16);
	const std::string text = log.text();
	require_batch(GetResourceHash(texture.Get()) == before,
		"F136: a refused CPU update keeps the previous hash instead of publishing 0");
	require_batch(text.find("SysMemSlicePitch") != std::string::npos,
		"F136: a refused CPU update warns about the slice pitch");
}

} // namespace

void test_dx11_texture_hash_layout_regression()
{
	Test3DSlicePitchGuard();
	Test2DPitchTooSmall();
	Test2DOverHardLimit();
	TestLegalHashesUnchanged();
	TestUpdateKeepsOldHash();
}
