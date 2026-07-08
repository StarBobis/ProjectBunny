# DX11/DX12 Mod Compatibility Mechanism Report

## Conclusion

From first principles, the DX11 path works for this mod because 3DMigoto's DX11 command-list system behaves like a mature runtime VM: it parses variables, conditions, key bindings, custom shaders, resource `copy/ref`, shader constants, draw/dispatch commands, and texture overrides as one connected script surface.

The DX12 path is faster because the project keeps the hot path narrow: command-list runtime state is sharded, thread-local cached, and compiled into draw/dispatch decisions so repeated draws do not rediscover the same state. However, this speed comes with a compatibility cost: DX12 cannot safely emulate DX11-style `cs-t`, `cs-u`, `Resource = ref`, and custom shader commands unless they are lowered into explicit descriptor/root-signature/resource-state work.

## Why DX12 Can Avoid Frame Drops More Easily

DX12 exposes lower-level control than DX11. This project takes advantage of that by tracking only the state needed by each hook, caching repeated decisions, and avoiding broad per-draw interpretation. The key mechanism is:

1. Command-list state is stored per command list instead of reconstructed globally.
2. Runtime lookup uses shards and TLS cache to reduce lock and map pressure.
3. Draw/dispatch decisions are compiled and cached per PSO/generation.
4. IA hash state and compute binding snapshots are cached by epoch/serial.
5. Hot-path timing is accumulated by stage and reset at frame boundaries.

That makes the DX12 path more predictable: most frames pay for a few pointer/cache checks, not a full command-list script evaluation.

## Why DX11 Handles This Mod But DX12 Did Not

The target INI uses many DX11 3DMigoto VM features:

- `[Key*]` input/menu control.
- `[Present]` command-list flow.
- `[CustomShaderComputeShapes*]` compute shader sections.
- `if/elif/else/endif` control flow.
- `$var`, `global persist $var`, `post $var`, `local $var`.
- `x88 = $BoobsSize` shader constants.
- `cs = ./res/Shapes.hlsl`, `cs-t50`, `cs-t51`, `cs-u5`.
- `ResourceX = ref/copy ...`.
- `Dispatch`, `Draw`, `DrawIndexed`, `CheckTextureOverride`.

DX11 can hide much of this behind immediate-context style binding. DX12 cannot. A DX12 implementation must know which descriptor heap, root parameter, resource state, copy operation, UAV barrier, and synchronization point each command implies.

## Current Static Coverage

The added analyzer reports this target INI as:

- Supported: 612 / 1649, 37.1%.
- Compatibility-recognized but not fully executed: 987 / 1649, 59.9%.
- Unsupported: 50 / 1649, 3.0%.

The remaining unsupported entries are almost entirely `[Key*]` hotkey/menu declarations. The important rendering/script surface is now mostly visible to DX12 as structured actions, but many of those actions are intentionally no-op until the execution VM is implemented.

## Practical Root Cause For This Mod

Your suspicion is correct: the DX12 side was missing large parts of the INI mechanism. The most important missing pieces are not just "parsing more keys"; they are the execution semantics behind those keys:

1. Variable evaluation and control-flow execution.
2. Key/input state driving menu and slider variables.
3. Custom shader compilation/binding/execution.
4. `copy/ref/null` resource aliasing semantics.
5. DX12 descriptor binding for CBV/SRV/UAV slots.
6. Resource barriers and UAV ordering around compute shape edits.

## Risk And Next Slice

The current change improves parse visibility and prevents silent loss, but it does not make the mod fully functional yet. The next compatibility slice should implement a small DX12 command-list execution VM for variables/control flow first, then resource binding/copy/ref, then custom compute shader dispatch with explicit barriers.

Microsoft DX12 references used for the reasoning:

- https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures
- https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
