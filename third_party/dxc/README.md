# dxc - runtime HLSL compiler drop

`dxcompiler.dll` + `dxil.dll`, Microsoft DirectXShaderCompiler (DXC),
FileVersion `1.8.2502.11`.

These two files are a **runtime dependency of d3d12.dll**, not a build-time
convenience:

- `[CustomShaderX]` mod elements (the SSMT UI panel among them) are compiled
  to SM6 (`vs_6_0` / `ps_6_0` / ...) when the element first runs, by
  `src/DirectX12/ModRuntime/DX12CustomShaderCompile.h`.
- The loader probes the **directory of d3d12.dll first**
  (`SelfModuleDirectory()`), then falls back to the plain DLL search order,
  and loads `dxil.dll` from the same directory as the loaded
  `dxcompiler.dll` — the pair must stay together and match in version. A
  foreign `dxcompiler.dll` picked up from the search path (e.g. a
  self-contained VulkanSDK build) can fail-fast on the second in-process
  compile and take the game down.
- Without any SM6 backend the compile degrades to a logged failure and the
  element is skipped: the panel does not draw, but nothing crashes.

Deployment:

- `scripts/build-cmake.ps1` copies the pair next to every deployed
  `d3d12.dll` (dev/test folders).
- `publish_release.ps1` ships the pair inside the release zip, for the same
  reason: end users have no other way to obtain it.

## License

The binaries are Microsoft's official DXC release, distributed under the
LLVM Release License (NCSA-style). Redistribution requires the upstream
license notice to travel with the binaries: drop the `LICENSE.TXT` from the
official DXC release archive into this folder. `publish_release.ps1` then
packages it as `dxc-LICENSE.TXT` and warns when it is absent.
