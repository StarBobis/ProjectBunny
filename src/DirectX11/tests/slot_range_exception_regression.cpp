#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;

class RangeLogFailure final : public std::runtime_error {
public:
    RangeLogFailure() : std::runtime_error("intentional exception at actual range logging boundary") {}
};

// Uses the existing virtual production boundary. No log text is examined:
// each element is announced at the outer indent, then its copy/ref operation
// runs inside SlotRangeIndent. A null unless_null element never enters it.
class ThrowingRangeContext final : public HackerContext {
    CommandListState& state_;
    unsigned throw_on_ = 0;
    unsigned nested_elements_ = 0;
    int outer_indent_ = 0;
    bool nested_ = false;
public:
    ThrowingRangeContext(ID3D11Device1* device, ID3D11DeviceContext1* context, CommandListState& state)
        : HackerContext(device, context), state_(state) {}

    void Arm(unsigned element)
    {
        throw_on_ = element;
        nested_elements_ = 0;
        outer_indent_ = state_.extra_indent;
        nested_ = false;
    }
    unsigned NestedElements() const { return nested_elements_; }

    void FrameAnalysisLog(char*, ...) override
    {
        if (!throw_on_) return;
        if (state_.extra_indent <= outer_indent_) {
            nested_ = false;
            return;
        }
        if (nested_) return;
        nested_ = true;
        if (++nested_elements_ == throw_on_) {
            throw_on_ = 0; // Recovery runs through the same command/context.
            throw RangeLogFailure();
        }
    }
};

class ExceptionDevice final : public BatchDevice {
public:
    ThrowingRangeContext* throwing = nullptr;
    ExceptionDevice()
    {
        auto replacement = std::make_unique<ThrowingRangeContext>(native.Get(), context.Get(), state);
        replacement->SetHackerDevice(device);
        throwing = replacement.get();
        wrapper = std::move(replacement);
        device->SetHackerContext(wrapper.get());
        state.mHackerContext = wrapper.get();
    }
};

class AnalysisScope {
    const bool original_ = G->analyse_frame;
public:
    AnalysisScope() { G->analyse_frame = true; }
    ~AnalysisScope() { G->analyse_frame = original_; }
};

ULONG references(IUnknown* object)
{
    object->AddRef();
    return object->Release();
}

void verify_exception_prefix(ExceptionDevice& device, bool copy)
{
    const UINT colours[] = {0x71452311u, 0x83674529u, 0x95678341u};
    auto a = texture(device, colours[0]), b = texture(device, colours[1]), c = texture(device, colours[2]);
    auto previous = texture(device, 0x22446688u);
    auto previous_view = srv(device, previous.Get());
    const ULONG a_refs = references(a.Get()), b_refs = references(b.Get()), c_refs = references(c.Get());
    const ULONG old_view_refs = references(previous_view.Get());
    const int original_indent = device.state.extra_indent;
    device.state.extra_indent = 5;
    {
        Registry registry;
        const std::wstring pool_name = copy ? L"poolrange_exception_copy" : L"poolrange_exception_ref";
        auto& pool = registry.pool(pool_name, device, PoolIndexType::STATIC, 4);
        // No source view is supplied. Both copy and ref take the ordinary
        // CopyResourceToResource path inside the real indentation scope.
        assign(device, pool.GetResource(0, false, true, true), a.Get());
        assign(device, pool.GetResource(1, false, true, true), nullptr);
        assign(device, pool.GetResource(2, false, true, true), b.Get());
        assign(device, pool.GetResource(3, false, true, true), c.Get());
        for (UINT slot = 3; slot <= 6; ++slot) set_slot(device, L'p', slot, previous_view.Get());
        CommandList commands;
        append(commands, L"ps-t[3:6]", (copy ? L"copy unless_null " : L"ref unless_null ") + pool_name + L"[0:3]");
        require_batch(commands.commands.size() == 1, "range exception: one actual range command");
        ShaderResourceBatchStats interrupted;
        device.state.slot_batch_stats = &interrupted;
        device.throwing->Arm(2);
        bool caught = false;
        try {
            commands.commands.front()->run(&device.state);
        } catch (const RangeLogFailure&) {
            caught = true;
        } catch (...) {
            device.state.slot_batch_stats = nullptr;
            throw;
        }
        device.state.slot_batch_stats = nullptr;
        require_batch(caught && device.throwing->NestedElements() == 2,
            "range exception: real virtual log throws inside later nonempty element");
        require_batch(device.state.extra_indent == 5,
            "range exception: scoped indentation is restored during unwinding");
        require_batch(interrupted.get_calls == 1 && interrupted.set_calls == 1,
            "range exception: previously completed prefix is really published once before rethrow");
        auto first = bound_resource(device, L'p', 3);
        require_batch(first && first.Get() != previous.Get() && (copy ? first.Get() != a.Get() : first.Get() == a.Get()),
            "range exception: earlier success reaches actual native binding");
        require_pixels(device, first.Get(), colours[0]);
        first.Reset();
        for (UINT slot = 4; slot <= 6; ++slot)
            require_batch(get_slot(device, L'p', slot).Get() == previous_view.Get(),
                "range exception: skipped hole, throwing element and untouched tail retain prior native views");
        const auto recovered = execute(commands, device);
        require_batch(recovered.get_calls == 1 && recovered.set_calls == 1 && device.state.extra_indent == 5,
            "range exception: identical command recovers with one native packet and balanced indent");
        require_batch(get_slot(device, L'p', 4).Get() == previous_view.Get(),
            "range exception: recovery still preserves the unless_null hole");
        const UINT slots[] = {3, 5, 6};
        ID3D11Resource* expected[] = {a.Get(), b.Get(), c.Get()};
        for (unsigned i = 0; i < 3; ++i) {
            auto actual = bound_resource(device, L'p', slots[i]);
            require_batch(actual && (copy ? actual.Get() != expected[i] : actual.Get() == expected[i]),
                "range exception: recovery applies each nonempty source to its intended native slot");
            require_pixels(device, actual.Get(), colours[i]);
        }
        // Release native bindings and the command's recycled per-element cache
        // before Registry releases its element references. Sources remain held
        // independently so the actual COM counts can be observed afterwards.
        device.context->ClearState();
        commands.commands.clear();
    }
    device.context->Flush();
    require_batch(references(a.Get()) == a_refs && references(b.Get()) == b_refs && references(c.Get()) == c_refs &&
        references(previous_view.Get()) == old_view_refs,
        "range exception: actual source/view references return to their independent-owner counts after cleanup");
    device.state.extra_indent = original_indent;
}
} // namespace

void test_dx11_slot_range_exception_regression()
{
    try {
        AnalysisScope analysis;
        ExceptionDevice device;
        verify_exception_prefix(device, false);
        verify_exception_prefix(device, true);
        std::fputs("PASS: real range ref/copy exception prefix, untouched slots, indentation, retry and COM cleanup\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL slot range exception: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
