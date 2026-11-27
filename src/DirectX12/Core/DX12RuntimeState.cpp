#include "DX12RuntimeState.h"
#include "Diag/DX12Diag.h"
#include "ExpressionFunctions.h"
#include "FrameRate.h"
#include "MigotoCommandVmUtils.hpp"

namespace {
const uint64_t launchTicks = migoto::GetSystemTicks();
uint64_t previousTicks = launchTicks;
FPSCounter frameRate;
std::mutex frameTimingMutex;
}

uint64_t DX12RuntimeState::BeginPresent(uint64_t ticks)
{
	std::lock_guard lock(frameTimingMutex);
	const auto frame = static_cast<uint32_t>(GetPresentCount());
	frameRate.Update(ticks);
	SeedExpressionRandom(frame, static_cast<uint32_t>(ticks));
	MigotoCommandVmUtils::frameTiming.store({
		migoto::SecondsBetween(launchTicks, ticks), migoto::SecondsBetween(previousTicks, ticks),
		frameRate.GetFPS(), static_cast<float>(frame)}, std::memory_order_release);
	previousTicks = ticks;
	return ticks;
}

LONG DX12RuntimeState::IncrementPresentCount(uint64_t ticks)
{
	std::lock_guard lock(frameTimingMutex);
	const auto frame = static_cast<uint32_t>(presentCount.load(std::memory_order_relaxed)) + 1u;
	auto sample = MigotoCommandVmUtils::frameTiming.load(std::memory_order_relaxed);
	sample.frameNumber = static_cast<float>(frame);
	SeedExpressionRandom(frame, static_cast<uint32_t>(ticks));
	MigotoCommandVmUtils::frameTiming.store(sample, std::memory_order_release);
	// Draw workers that claim the next frame must see its sample and seed.
	presentCount.store(static_cast<LONG>(frame), std::memory_order_release);
	return static_cast<LONG>(frame);
}

// Runtime mode transitions are logged through the unified diagnostics facade,
// which cannot be included from DX12RuntimeState.h (the diagnostics header
// itself depends on the runtime state), so this one definition lives out of
// line. In non-TIMING builds the log call compiles to nothing.

void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = runtimeMode.exchange(value);
	if (old != value) {
		DX12_LOG("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}
