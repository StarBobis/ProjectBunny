#include "McpApmiProducer.h"
#include "DLLMainHook.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

CNktHookLib cHookMgr;
HINSTANCE migoto_handle = nullptr;

namespace {
using namespace Dx11Mcp::ApmiProducer;
using Submit = void (*)(void*, void*, void*);
using Decode = uint32_t (*)(void*, void*);

struct Device {
    alignas(8) std::array<unsigned char, 0x2560> object{};
};

struct Stream {
    alignas(8) std::array<unsigned char, 0x158> object{};
    void* buffer = nullptr;
    void* pose = nullptr;
    unsigned submissions = 0;
};

struct Pose {
    uint64_t tag = 0;
    const void* data = nullptr;
    size_t count = 0;
};

static_assert(offsetof(Pose, data) == 8 && offsetof(Pose, count) == 16);
constexpr uint64_t trace_id = 27;
Upload actual, mismatch;
unsigned uploads = 0;

template<class T>
void Store(void* destination, size_t offset, T value)
{
    memcpy(static_cast<unsigned char*>(destination) + offset, &value, sizeof(T));
}

template<class T>
T Load(const void* source, size_t offset)
{
    T value;
    memcpy(&value, static_cast<const unsigned char*>(source) + offset, sizeof(T));
    return value;
}

void Require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

__declspec(noinline) void FixtureBackend(void*, void*, void* pose)
{
    const auto& args = *static_cast<Pose*>(pose);
    actual = ObserveUpload(trace_id, args.data, static_cast<unsigned>(args.count * 64));
    mismatch = ObserveUpload(trace_id, args.data, 4);
    ++uploads;
}

Submit volatile call_backend = FixtureBackend;

__declspec(noinline) void FixtureProducer(void* device, void* buffer, void* pose)
{
    if (!Load<unsigned char>(device, 0x2550)) {
        call_backend(device, buffer, pose);
        return;
    }
    auto* stream = Load<Stream*>(device, 0x2558);
    stream->buffer = buffer;
    stream->pose = pose;
    ++stream->submissions;
}

Submit volatile call_producer = FixtureProducer;

__declspec(noinline) uint32_t FixtureDecoder(void* device, void* stream)
{
    const auto* packet = static_cast<Stream*>(stream);
    call_backend(device, packet->buffer, packet->pose);
    return 0x274b;
}

Decode volatile call_decoder = FixtureDecoder;

void Positions(Stream& stream, uint32_t write, uint32_t write_limit, uint32_t read, uint32_t read_limit)
{
    Store(&stream, 0x148, write);
    Store(&stream, 0x14c, write_limit);
    Store(&stream, 0x154, 0xfffffff0u);
    Store(&stream, 0x108, read);
    Store(&stream, 0x10c, read_limit);
    Store(&stream, 0x114, 0u);
}
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 2) {
        // An isolated image-mapping check. The fixture directory uses the
        // expected executable name and never enters Unity initialization.
        if (!LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES)) {
            std::fprintf(stderr, "Cannot map UnityPlayer without running it (Win32=%lu)\n", GetLastError());
            return 1;
        }
        const auto error = Install();
        if (error) {
            std::fprintf(stderr, "%s\n", error);
            return 1;
        }
        Require(!Install(), "repeating preparation does not install a second detour");
        std::puts("PASS: installed the version-gated detours on the exact UnityPlayer image");
        return 0;
    }
    const auto error = InstallFixture(reinterpret_cast<void*>(FixtureProducer),
        reinterpret_cast<void*>(FixtureDecoder), reinterpret_cast<void*>(FixtureBackend));
    Require(!error, error);
    Device device;
    Stream stream;
    unsigned buffer = 17;
    std::array<unsigned char, 64> bytes{};
    Pose pose{1, bytes.data(), 1};
    Store(&device, 0x2550, static_cast<unsigned char>(1));
    Store(&device, 0x2558, &stream);

    Positions(stream, 32, 40, 16, 24);
    call_producer(&device, &buffer, &pose);
    Require(call_decoder(&device, &stream) == 0x274b, "decoder returns the native value with the probe idle");
    Require(uploads == 1 && !actual.enqueue, "idle probe forwards without storing an enqueue");

    Begin(trace_id, 12);
    std::thread enqueue([&] { call_producer(&device, &buffer, &pose); });
    enqueue.join();
    Require(call_decoder(&device, &stream) == 0x274b, "decoder returns the native value with capture enabled");
    Require(uploads == 2 && stream.submissions == 2, "hooked code executes each original call once");
    Require(std::strcmp(actual.state, "queue_position_and_references") == 0 && actual.enqueue,
        "wrapped absolute queue position and two references match");
    Require(actual.enqueue->position == 16 && actual.enqueue->thread != actual.consumer_thread &&
        actual.enqueue->qpc <= actual.consumer_qpc && !actual.enqueue->frames.empty(),
        "producer stack and thread precede a distinct consumer");
    Require(!mismatch.enqueue && std::strcmp(mismatch.state, "backend_upload_arguments_differ") == 0,
        "an unrelated upload inside the backend cannot inherit the enqueue ID");
    const auto finished = End();
    Require(finished.enqueued == 1 && finished.paired == 1, "first capture has one paired packet");

    Begin(trace_id, 8);
    call_producer(&device, &buffer, &pose);
    call_producer(&device, &buffer, &pose);
    call_decoder(&device, &stream);
    Require(!actual.enqueue && std::strcmp(actual.state, "ambiguous_queue_evidence") == 0,
        "reused absolute position is ambiguous");
    Require(End().ambiguous == 1, "ambiguity is visible");

    Begin(trace_id, 8);
    Positions(stream, 32, 32, 16, 16);
    call_producer(&device, &buffer, &pose);
    call_decoder(&device, &stream);
    Require(actual.enqueue && std::strcmp(actual.state, "references_candidate") == 0 &&
        !actual.position_known && !actual.enqueue->position_known, "refill path only reports a reference candidate");
    Require(End().candidates == 1, "unproven pairing remains a candidate in counters");

    Begin(trace_id, 8);
    Store(&device, 0x2550, static_cast<unsigned char>(0));
    call_producer(&device, &buffer, &pose);
    Require(actual.enqueue && std::strcmp(actual.state, "synchronous_call") == 0,
        "inline backend call follows the producer thread scope");
    Require(End().paired == 1, "inline capture closes");
    call_decoder(&device, &stream);
    Require(!actual.enqueue, "completed capture leaves no thread-local pairing on the next decode");
    return 0;
}
