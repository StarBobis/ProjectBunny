#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Dx11Mcp::ApmiProducer {

constexpr unsigned MaxFrames = 24;

struct Frame {
    std::string module;
    uintptr_t rva;
};

struct Enqueue {
    uint64_t id = 0;
    uint64_t queue_id = 0;
    int64_t qpc = 0;
    uint32_t thread = 0, position = 0;
    bool position_known = false, stack_limited = false;
    unsigned unresolved_frames = 0;
    std::vector<Frame> frames;
};

struct Upload {
    const char* state = "outside_palette_consumer";
    std::shared_ptr<const Enqueue> enqueue;
    int64_t consumer_qpc = 0;
    uint32_t consumer_thread = 0, position = 0;
    bool position_known = false;
};

struct Totals {
    unsigned enqueued = 0, omitted = 0, consumed = 0, paired = 0, candidates = 0, ambiguous = 0;
};

// Returns null on success, or a stable diagnostic code for the pipe.
const char* Install() noexcept;
void Begin(uint64_t trace_id, unsigned frames);
Totals ReadTotals() noexcept;
Totals End() noexcept;
Upload ObserveUpload(uint64_t trace_id, const void* source, unsigned bytes) noexcept;

#ifdef DX11_APMI_PRODUCER_TEST
const char* InstallFixture(void* producer, void* decoder, void* backend) noexcept;
#endif

} // namespace Dx11Mcp::ApmiProducer
