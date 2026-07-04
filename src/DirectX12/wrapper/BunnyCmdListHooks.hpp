#pragma once

#include <Windows.h>
#include <d3d12.h>

#include "BunnyVTableSwap.hpp"
#include "DX12BindingTracker.hpp"
#include "DX12CommandListComputeBindingFlow.hpp"
#include "DX12CommandListFlowDiagnostics.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12CommandListStateCapture.hpp"
#include "DX12CommandListStateFlow.hpp"
#include "DX12DispatchHookFlow.hpp"
#include "DX12DrawH#include "DX12HookManager.hpp"
#include "DX12HookRegistry.hpp"
#include "DX12HotPathState.hpp"
#include "DX12IaReplacementDrawExecutor.hpp"
#include "DX12IaReplacementExecutor.hpp"
#include "DX12IaReplacementDrawScope.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderHunt.hpp"
#include "types/DX12CommandListHookSlotTypes.hpp"
#include "types/DX12IaDrawInvocation.hpp"
#include "types/DX12IaReplacementExecutorCallbacks.hpp"
#include "types/DX12HookTypes.hpp"
#include "ia/DX12IaCommandSignatures.hpp"

// BunnyCmdListHooks - DX11 orig_vtable 对齐 Hook 实现
// NktHookLib 安装 + BunnyVTableSwap 缓存 trampoline
// Hook 函数: GetOrig(vtable, slot) O(1) 获取原始函数指针

class BunnyCmdListHooks
{
public:
using Slot = DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList;

// 获取缓存的原始函数（trampoline）
template<typename T>
inline static T GetOrig(ID3D12GraphicsCommandList *cmd, UINT slot)
{
void *vtable = *reinterpret_cast<void**>(cmd);
return reinterpret_cast<T>(BunnyVTableSwap::GetCachedOriginal(vtable, slot));
}

// ---- Hook 函数 ---------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hook_Close(ID3D12GraphicsCommandList *This)
{
auto orig = GetOrig<HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*)>(This, Slot::Close);
if (!DX12HotPathState::skipAll)
DX12CommandListFlowDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Close", This, "");
HRESULT hr = orig ? orig(This) : E_FAIL;
if (!DX12HotPathState::skipAll) {
auto tracking = DX12CommandListStateCapture::GetTrackingState(This);
if (tracking.recordBindingEvents)
DX12BindingTracker::RecordStateEvent(This, "close");
}
return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_Reset(ID3D12GraphicsCommandList *This,
ID3D12CommandAllocator *allocator, ID3D12PipelineState *initialState)
{
auto orig = GetOrig<HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*)>(This, Slot::Reset);
if (!DX12HotPathState::skipAll)
DX12CommandListFlowDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::Reset", This, " allocator=%p initialState=%p", allocator, initialState);
HRESULT hr = orig ? orig(This, allocator, initialState) : E_FAIL;
if (SUCCEEDED(hr) && !DX12HotPathState::skipAll) {
auto tracking = DX12CommandListStateCapture::GetTrackingState(This);
if (tracking.trackBindings || DX12ModNeedsPreSkinningUavProbe())
DX12BindingTracker::ResetCommandList(This, initialState);
if (tracking.trackPsoState)
DX12CommandListRuntime::RememberPipelineState(This, initialState);
if (tracking.trackHuntIa || DX12ModHasActiveShaderOverrides())
DX12CommandListRuntime::ResetIa(This);
if (tracking.trackPsoState || tracking.trackHuntIa)
DX12HuntResetCommandList(This, initialState);
}
return hr;
}

static void STDMETHODCALLTYPE Hook_ClearState(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pipelineState)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*)>(This, Slot::ClearState);
if (!DX12HotPathState::skipAll)
DX12CommandListFlowDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ClearState", This, " pso=%p", pipelineState);
if (orig) orig(This, pipelineState);
if (!DX12HotPathState::skipAll) {
auto tracking = DX12CommandListStateCapture::GetTrackingState(This);
if (tracking.recordBindingEvents)
DX12BindingTracker::RecordStateEvent(This, "clear_state");
if (DX12ModNeedsPreSkinningUavProbe())
DX12BindingTracker::ResetCommandList(This, pipelineState);
if (tracking.trackPsoState)
DX12CommandListRuntime::RememberPipelineState(This, pipelineState);
if (tracking.trackHuntIa)
DX12CommandListRuntime::ResetIa(This);
if (DX12HuntIsEnabled())
DX12HuntResetCommandList(This, pipelineState);
}
}

static void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D12GraphicsCommandList *This,
UINT vc, UINT ic, UINT sv, UINT si)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT)>(This, Slot::DrawInstanced);
if (DX12HotPathState::skipAll || !(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DRAW_WORK)) {
if (orig) orig(This, vc, ic, sv, si);
return;
}
DX12_PROFILE_SCOPE(DrawInstanced);
DX12IaReplacementExecutorCallbacks iaCallbacks = MakeDirectCallbacks(This);
DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(vc, ic, sv, si);
DX12DrawHookFlow::ExecuteHook(This, draw, iaCallbacks);
}

static void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D12GraphicsCommandList *This,
UINT ic, UINT inc, UINT si, INT bv, UINT si2)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT)>(This, Slot::DrawIndexedInstanced);
if (DX12HotPathState::skipAll || !(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DRAW_WORK)) {
if (orig) orig(This, ic, inc, si, bv, si2);
return;
}
DX12_PROFILE_SCOPE(DrawIndexedInstanced);
DX12IaReplacementExecutorCallbacks iaCallbacks = MakeDirectCallbacks(This);
DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(ic, inc, si, bv, si2);
DX12DrawHookFlow::ExecuteHook(This, draw, iaCallbacks);
}

static void STDMETHODCALLTYPE Hook_Dispatch(ID3D12GraphicsCommandList *This, UINT x, UINT y, UINT z)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT)>(This, Slot::Dispatch);
if (!(DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DISPATCH_WORK)) {
if (orig) orig(This, x, y, z);
return;
}
DX12_PROFILE_SCOPE(Dispatch);
DX12DispatchHookFlowDispatch::Fn dispatchFn = [](ID3D12GraphicsCommandList *cmd, UINT dx, UINT dy, UINT dz) {
auto o = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT)>(cmd, Slot::Dispatch);
if (o) o(cmd, dx, dy, dz);
};
DX12DispatchHookFlow::ExecuteHook(This, x, y, z, dispatchFn);
}

static void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D12GraphicsCommandList *This, D3D12_PRIMITIVE_TOPOLOGY topology)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY)>(This, Slot::IaSetPrimitiveTopology);
if (DX12HotPathState::skipAll) { if (orig) orig(This, topology); return; }
DX12_PROFILE_SCOPE(IASetPrimitiveTopology);
DX12CommandListStateFlow::CapturePrimitiveTopology(This, topology);
if (orig) orig(This, topology);
}

static void STDMETHODCALLTYPE Hook_SetPipelineState(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pso)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*)>(This, Slot::SetPipelineState);
if (DX12HotPathState::skipAll && !DX12ModHasActiveShaderOverrides()) {
if (orig) orig(This, pso);
DX12CommandListRuntime::RememberPipelineState(This, pso);
return;
}
DX12_PROFILE_SCOPE(SetPipelineState);
DX12CommandListStateFlow::CapturePipelineState(This, pso);
if (orig) orig(This, pso);
DX12CommandListRuntime::RememberPipelineState(This, pso);
}

static void STDMETHODCALLTYPE Hook_ResourceBarrier(ID3D12GraphicsCommandList *This, UINT n, const D3D12_RESOURCE_BARRIER *b)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*)>(This, Slot::ResourceBarrier);
if (DX12HotPathState::skipAll) { if (orig) orig(This, n, b); return; }
DX12CommandListStateFlow::CaptureResourceBarrier(This, n, b);
if (orig) orig(This, n, b);
}

static void STDMETHODCALLTYPE Hook_ExecuteBundle(ID3D12GraphicsCommandList *This, ID3D12GraphicsCommandList *bundle)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*)>(This, Slot::ExecuteBundle);
if (!DX12HotPathState::skipAll)
DX12CommandListFlowDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ExecuteBundle", This, " bundle=%p", bundle);
if (orig) orig(This, bundle);
}

static void STDMETHODCALLTYPE Hook_SetDescriptorHeaps(ID3D12GraphicsCommandList *This, UINT count, ID3D12DescriptorHeap *const *heaps)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*)>(This, Slot::SetDescriptorHeaps);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, count, heaps); return; }
DX12_PROFILE_SCOPE(SetDescriptorHeaps);
DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(This, count, heaps);
if (orig) orig(This, count, heaps);
}

static void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList *This, ID3D12RootSignature *rs)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*)>(This, Slot::SetComputeRootSignature);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, rs); return; }
DX12_PROFILE_SCOPE(SetComputeRootSignature);
DX12CommandListComputeBindingFlow::CaptureRootSignature(This, rs, false);
if (orig) orig(This, rs);
}

static void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList *This, ID3D12RootSignature *rs)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*)>(This, Slot::SetGraphicsRootSignature);
if (DX12HotPathState::skipAll) { if (orig) orig(This, rs); return; }
DX12_PROFILE_SCOPE(SetGraphicsRootSignature);
DX12CommandListComputeBindingFlow::CaptureRootSignature(This, rs, true);
if (orig) orig(This, rs);
}

static void STDMETHODCALLTYPE Hook_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_DESCRIPTOR_HANDLE h)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)>(This, Slot::SetComputeRootDescriptorTable);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, idx, h); return; }
DX12_PROFILE_SCOPE(SetComputeRootDescriptorTable);
DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(This, idx, h, false);
if (orig) orig(This, idx, h);
}

static void STDMETHODCALLTYPE Hook_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_DESCRIPTOR_HANDLE h)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE)>(This, Slot::SetGraphicsRootDescriptorTable);
if (DX12HotPathState::skipAll) { if (orig) orig(This, idx, h); return; }
DX12_PROFILE_SCOPE(SetGraphicsRootDescriptorTable);
DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(This, idx, h, true);
if (orig) orig(This, idx, h);
}

// SetComputeRootConstantBufferView
static void STDMETHODCALLTYPE Hook_SetComputeRootCBV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetComputeRootConstantBufferView);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetComputeRootConstantBufferView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_CBV, addr, false, "SetComputeRootConstantBufferView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_SetGraphicsRootCBV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetGraphicsRootConstantBufferView);
if (DX12HotPathState::skipAll) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetGraphicsRootConstantBufferView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_CBV, addr, true, "SetGraphicsRootConstantBufferView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_SetComputeRootSRV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetComputeRootShaderResourceView);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetComputeRootShaderResourceView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_SRV, addr, false, "SetComputeRootShaderResourceView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_SetGraphicsRootSRV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetGraphicsRootShaderResourceView);
if (DX12HotPathState::skipAll) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetGraphicsRootShaderResourceView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_SRV, addr, true, "SetGraphicsRootShaderResourceView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_SetComputeRootUAV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetComputeRootUnorderedAccessView);
if (DX12HotPathState::skipAll && !DX12ModNeedsPreSkinningBindingTracking()) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetComputeRootUnorderedAccessView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_UAV, addr, false, "SetComputeRootUnorderedAccessView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_SetGraphicsRootUAV(ID3D12GraphicsCommandList *This, UINT idx, D3D12_GPU_VIRTUAL_ADDRESS addr)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS)>(This, Slot::SetGraphicsRootUnorderedAccessView);
if (DX12HotPathState::skipAll) { if (orig) orig(This, idx, addr); return; }
DX12_PROFILE_SCOPE(SetGraphicsRootUnorderedAccessView);
DX12CommandListComputeBindingFlow::CaptureRootDescriptor(This, idx, D3D12_ROOT_PARAMETER_TYPE_UAV, addr, true, "SetGraphicsRootUnorderedAccessView");
if (orig) orig(This, idx, addr);
}

static void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList *This, const D3D12_INDEX_BUFFER_VIEW *view)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*)>(This, Slot::IaSetIndexBuffer);
if (DX12HotPathState::skipAll) { if (orig) orig(This, view); return; }
DX12CommandListStateFlow::CaptureIndexBuffer(This, view);
if (orig) orig(This, view);
}

static void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList *This, UINT start, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*)>(This, Slot::IaSetVertexBuffers);
if (DX12HotPathState::skipAll) { if (orig) orig(This, start, count, views); return; }
DX12CommandListStateFlow::CaptureVertexBuffers(This, start, count, views);
if (orig) orig(This, start, count, views);
}

static void STDMETHODCALLTYPE Hook_ExecuteIndirect(ID3D12GraphicsCommandList *This,
ID3D12CommandSignature *sig, UINT maxCount, ID3D12Resource *argBuf, UINT64 argOff,
ID3D12Resource *countBuf, UINT64 countOff)
{
auto orig = GetOrig<void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64)>(This, Slot::ExecuteIndirect);
if (!DX12HotPathState::skipAll)
DX12CommandListFlowDiagnostics::LogDX12Call("ID3D12GraphicsCommandList::ExecuteIndirect", This, " sig=%p max=%u", sig, maxCount);
if (orig) orig(This, sig, maxCount, argBuf, argOff, countBuf, countOff);
}

// ---- IaReplacement 直通回调 ----------------------------------------

static DX12IaReplacementExecutorCallbacks MakeDirectCallbacks(ID3D12GraphicsCommandList *cmd)
{
DX12IaReplacementExecutorCallbacks cb;
void *vtable = *reinterpret_cast<void**>(cmd);
cb.drawInstanced = reinterpret_cast<DX12IaCommandSignatures::DrawInstanced>(BunnyVTableSwap::GetCachedOriginal(vtable, Slot::DrawInstanced));
cb.drawIndexedInstanced = reinterpret_cast<DX12IaCommandSignatures::DrawIndexedInstanced>(BunnyVTableSwap::GetCachedOriginal(vtable, Slot::DrawIndexedInstanced));
cb.dispatch = reinterpret_cast<DX12IaCommandSignatures::Dispatch>(BunnyVTableSwap::GetCachedOriginal(vtable, Slot::Dispatch));
cb.setIndexBuffer = reinterpret_cast<DX12IaCommandSignatures::SetIndexBuffer>(BunnyVTableSwap::GetCachedOriginal(vtable, Slot::IaSetIndexBuffer));
cb.setVertexBuffers = reinterpret_cast<DX12IaCommandSignatures::SetVertexBuffers>(BunnyVTableSwap::GetCachedOriginal(vtable, Slot::IaSetVertexBuffers));
cb.recordReplacementDraw = [](bool indexed) { DX12Profiling::RecordIaReplacementDraw(indexed); };
cb.recordReplacementDispatch = []() { DX12Profiling::RecordIaReplacementDispatch(); };
return cb;
}

// ---- 安装：NktHookLib + BunnyVTableSwap 缓存 -----------------------

static void Install(ID3D12GraphicsCommandList *commandList)
{
if (!commandList) return;

void *vtable = *reinterpret_cast<void**>(commandList);

struct HookEntry { UINT slot; void *hook; };
static const HookEntry hooks[] = {
{Slot::Close,                                   reinterpret_cast<void*>(Hook_Close)},
{Slot::Reset,                                   reinterpret_cast<void*>(Hook_Reset)},
{Slot::ClearState,                              reinterpret_cast<void*>(Hook_ClearState)},
{Slot::DrawInstanced,                           reinterpret_cast<void*>(Hook_DrawInstanced)},
{Slot::DrawIndexedInstanced,                    reinterpret_cast<void*>(Hook_DrawIndexedInstanced)},
{Slot::Dispatch,                                reinterpret_cast<void*>(Hook_Dispatch)},
{Slot::IaSetPrimitiveTopology,                  reinterpret_cast<void*>(Hook_IASetPrimitiveTopology)},
{Slot::SetPipelineState,                        reinterpret_cast<void*>(Hook_SetPipelineState)},
{Slot::ResourceBarrier,                         reinterpret_cast<void*>(Hook_ResourceBarrier)},
{Slot::ExecuteBundle,                           reinterpret_cast<void*>(Hook_ExecuteBundle)},
{Slot::SetDescriptorHeaps,                      reinterpret_cast<void*>(Hook_SetDescriptorHeaps)},
{Slot::SetComputeRootSignature,                 reinterpret_cast<void*>(Hook_SetComputeRootSignature)},
{Slot::SetGraphicsRootSignature,                reinterpret_cast<void*>(Hook_SetGraphicsRootSignature)},
{Slot::SetComputeRootDescriptorTable,           reinterpret_cast<void*>(Hook_SetComputeRootDescriptorTable)},
{Slot::SetGraphicsRootDescriptorTable,          reinterpret_cast<void*>(Hook_SetGraphicsRootDescriptorTable)},
{Slot::SetComputeRootConstantBufferView,        reinterpret_cast<void*>(Hook_SetComputeRootCBV)},
{Slot::SetGraphicsRootConstantBufferView,       reinterpret_cast<void*>(Hook_SetGraphicsRootCBV)},
{Slot::SetComputeRootShaderResourceView,        reinterpret_cast<void*>(Hook_SetComputeRootSRV)},
{Slot::SetGraphicsRootShaderResourceView,       reinterpret_cast<void*>(Hook_SetGraphicsRootSRV)},
{Slot::SetComputeRootUnorderedAccessView,       reinterpret_cast<void*>(Hook_SetComputeRootUAV)},
{Slot::SetGraphicsRootUnorderedAccessView,      reinterpret_cast<void*>(Hook_SetGraphicsRootUAV)},
{Slot::IaSetIndexBuffer,                        reinterpret_cast<void*>(Hook_IASetIndexBuffer)},
{Slot::IaSetVertexBuffers,                      reinterpret_cast<void*>(Hook_IASetVertexBuffers)},
{Slot::ExecuteIndirect,                         reinterpret_cast<void*>(Hook_ExecuteIndirect)},
};

void **vtableSlots = static_cast<void**>(vtable);
for (const HookEntry &entry : hooks) {
void *target = vtableSlots[entry.slot];
void *original = nullptr;

// NktHookLib 安装 Hook（安全的函数代码 patch）
DX12HookRegistry::HookFunction(&original, target, entry.hook, "BunnyCmdListHook");

// 缓存 trampoline 到 BunnyVTableSwap
BunnyVTableSwap::CacheOriginal(vtable, entry.slot, original);
}
}
_IASetVertexBuffers)},
			{Slot::ExecuteIndirect,                         reinterpret_cast<void*>(Hook_ExecuteIndirect)},
		};

		for (const HookEntry &entry : hooks)
			BunnyVTableSwap::InstallHook(originalVTable, entry.slot, entry.hook);

		// 替换对象的 VTable 指针
		BunnyVTableSwap::ApplyToObject(commandList,
			BunnyVTableSwap::EnsureCustomVTable(originalVTable));
	}

	// ── 卸载 ─────────────────────────────────────────────────────────
	static void Uninstall(ID3D12GraphicsCommandList *commandList)
	{
		if (!commandList)
			return;
		BunnyVTableSwap::RestoreObject(commandList);
	}
};
