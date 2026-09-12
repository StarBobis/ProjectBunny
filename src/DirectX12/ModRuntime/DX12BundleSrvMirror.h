#pragma once

// ---------------------------------------------------------------------------
// Bundle draw-SRV replacement: record-time mirror + ExecuteBundle parent replay.
//
// A bundle cannot switch to a private descriptor heap by itself: at most one
// CBV_SRV_UAV heap is bound when the bundle executes, and every descriptor
// handle referenced by the bundle's recorded commands must resolve into the
// heaps the parent list has bound at ExecuteBundle time. The direct-list
// approach (mirror tables, swap heaps, draw, lazily restore) is therefore
// impossible inside a bundle - which is why ApplyConfig/ApplyPending used to
// refuse bundle lists outright ("bundle_requires_parent_replay") and texture
// replacements silently went missing for games that draw through bundles,
// while IA geometry replacement (no descriptor involvement) kept working.
//
// Design:
//
//  Record time (bundle list):
//   - A mirror session is created lazily on the bundle's first descriptor
//     table bind / SetDescriptorHeaps / patched draw, gated on configured
//     texture overrides (zero cost when no texture mods are loaded).
//   - EVERY CBV/SRV/UAV root-table bind recorded into the bundle is mirrored
//     into the session-owned shader-visible heap and the emitted handle is
//     rewritten to the mirror copy; the shadow trackers keep the game's
//     logical handles so plan building and matching stay in game terms.
//   - A patched draw additionally builds patch copies (a table copy with the
//     mod SRVs written over the patched slots) inside the same session heap
//     and binds those handles for the draw; Restore rebinds the unpatched
//     session mirrors immediately - a bundle has no deferred restore, every
//     handle in the recorded stream must stay valid for the whole bundle.
//   - SetDescriptorHeaps inside the bundle is rewritten to the session heap
//     (the record-time sampler heap is kept as-is).
//
//  Execute time (parent list, ExecuteBundle):
//   - The parent binds the session heap (+ sampler heap), mirrors and rebinds
//     any parent tables the bundle inherits (root params the bundle never
//     binds itself), and refreshes mirror contents whose source-heap version
//     moved since recording, re-applying the mod overlays to patch copies.
//   - After execution the parent's own heaps and root tables are physically
//     restored; the lifecycle hook then resets the parent's shadow state as
//     before. The session heap is retained for the parent list until
//     submission through the usual pending-submission machinery.
//
//  Lifetime: sessions are keyed by the bundle list and die with Reset /
//  Unregister; the heap is retired through the pre-skin fence drain and may be
//  adopted by later sessions or the descriptor ring once no in-flight
//  submission references it. Config reloads deliberately do NOT kill sessions:
//  a recorded bundle may legally outlive a reload, and its stream still
//  references the session heap. A reload-generation mismatch at exec instead
//  degrades the bundle to original textures by refreshing every patch copy
//  from its game source without re-applying overlays.
//
// This used to be a fragment spliced into ModRuntime/DX12ModRuntimeInternal.h
// at a fixed line, carrying every implementation inline. Those implementations
// are in ModRuntime/DX12BundleSrvMirror.cpp beside the other entry points of
// this subsystem, so this header is declarations only and can be read on its
// own; the session state it works on lives in
// Core/ModState/DX12DrawSrvMirrorState.h.
// ---------------------------------------------------------------------------
#include "../Core/ModState/DX12DrawSrvMirrorState.h"

#include <vector>

class DX12BundleMirrorUtils
{
public:
	// Cheap global gate: bundle mirroring only matters when texture overrides
	// exist at all. Two volatile reads keep non-modded games at zero cost.
	static bool GateOpen();

	// Allocate (or adopt) the session heap. Adopts the smallest sufficient
	// retired heap whose GPU references provably drained - heap creation is a
	// millisecond-scale driver mapping, so steady-state bundle re-recording
	// must come from the recycle path, same contract as the ring rewind.
	static ID3D12DescriptorHeap *CreateSessionHeap(
		ID3D12Device *device, UINT minCapacity, UINT *capacityOut);

	// Fetch or create the mirror session for a bundle list. The session heap
	// is sized off the first source heap seen (plus patch headroom) and can
	// never grow afterwards - handles into it are baked into the recorded
	// bundle stream, so a reallocation would invalidate them.
	static DX12BundleMirrorSession *EnsureSessionLocked(
		DX12BundleMirrorSessionShard &shard,
		ID3D12GraphicsCommandList *commandList,
		UINT firstSourceHeapDescriptors);

	// Mirror a source table into the session heap (idempotent per table
	// handle). Contents are copied at record time; exec-time refresh re-copies
	// them if the source heap's version moved.
	static bool EnsureTableMirroredLocked(
		DX12BundleMirrorSession &session,
		UINT64 gpuStart,
		const DX12DescriptorHeapSummary &sourceHeap,
		UINT count,
		UINT *mirrorOffsetOut);

	// Refresh mirrored contents whose source heap version moved. Returns true
	// when the copy was refreshed (patch copies then re-apply their overlays).
	static bool RefreshTableLocked(
		DX12BundleMirrorSession &session,
		SIZE_T cpuStart,
		ID3D12DescriptorHeap *sourceHeap,
		UINT64 *sourceHeapVersion,
		UINT mirrorOffset,
		UINT count,
		bool force);

	static void ApplyPatchOverlaysLocked(
		DX12BundleMirrorSession &session,
		const DX12BundlePatchCopy &copy);

	// Mirror a parent-list table the bundle inherits (the bundle never binds
	// that root parameter itself) and rebind it on the parent to the session
	// mirror, so the inherited binding resolves into the session heap too.
	static void MirrorInheritedTableLocked(
		DX12BundleMirrorSession &session,
		ID3D12GraphicsCommandList *parent,
		const CommandListBindingState &parentSnapshot,
		const RootTableState &tableState,
		bool compute,
		UINT rootParameterIndex);

	static void RefreshSessionLocked(DX12BundleMirrorSession &session);
};

// Bundle variants of the draw-SRV apply/restore. They are declared in
// DX12DrawSrvReplacementUtils.h because that is what calls them on bundle
// lists, and this header needs the plan types that file defines, so the two
// reference each other rather than including each other.
