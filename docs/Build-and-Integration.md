# ProjectBunny 构建与集成

本文说明 ProjectBunny 的仓库构成、构建产物、平台矩阵、开发部署与发布开关、日志轮转、测试与文档站发布流程。

**读者**：DLL 使用者、Mod 作者、仓库维护者。

**范围**：本文只覆盖"如何构建、如何部署、如何验证、如何发布"。INI 语法与 Mod 编写见 [`../usage/`](../usage/README.md) 与 [`ini-language-extensions/`](ini-language-extensions/README.md)；Release 的版本号递增、GitHub Release 草稿与上传细节见 [Release-Package.md](Release-Package.md)，本文只列出与之对应的开关，不重复发布流程。

**约定**：文中所有路径、目标名、开关名、INI 键名均按仓库中的原样书写（大小写与下划线有实际意义）。命令示例默认在仓库根目录执行。

> 本文只描述经源码与脚本核对过的事实。文中未标注"已验证"的命令，本文作者**没有**在本轮运行过；游戏内行为、帧率与实机兼容性仍需在目标游戏中确认。

---

## 1. 仓库构成

### 1.1 顶层目录

| 路径 | 内容 |
| --- | --- |
| [`CMakeLists.txt`](../CMakeLists.txt) | 顶层工程定义（项目名 `3DMigotoDX12`，版本 `1.3.16`） |
| [`CMakePresets.json`](../CMakePresets.json) | 6 个配置预设：Ninja 的 x64 Debug/Release 与 Win32 Debug/Release，另有 MSBuild（VS 17 2022）的 x64 Debug/Release |
| [`cmake/`](../cmake/) | `CompilerOptions.cmake`、`FindNektra.cmake`、`GenerateDX11Version.cmake` |
| [`src/`](../src/) | 各运行时源码：`DirectX11`、`DirectX12`、`DirectXGI`、`D3DCompiler_46`、`D3DCompiler`、`D3D_Shaders`、`BinaryDecompiler`、`HLSLDecompiler`、`common` |
| [`3rdparty/`](../3rdparty/) | `Nektra`（预编译）、`pcre2`（预编译）、`DirectXTK`、`crc32c-hw-1.0.5`（源码编译） |
| [`third_party/dxc/`](../third_party/dxc/) | 官方 DXC 运行时投放（`dxcompiler.dll` + `dxil.dll`），见 [`README.md`](../third_party/dxc/README.md) |
| [`config/d3dx.ini`](../config/d3dx.ini) | 出厂配置模板（DX12 版） |
| [`Dependencies/`](../Dependencies/) | DX11 侧模板 `d3dx.ini` 与 `d3dcompiler_47.dll` 参考件 |
| [`scripts/`](../scripts/) | 构建、部署、发布、文档与测试脚本 |
| [`build_config.json`](../build_config.json) | 开发部署目标表 |
| [`usage/`](../usage/) | VitePress 2 用户文档站 |
| [`docs/`](../docs/) | 仓库内部文档（本文件所在目录） |
| [`.github/workflows/pages.yml`](../.github/workflows/pages.yml) | 文档站 CI |
| [`模拟环境/`](../模拟环境/) | DX11 代理加载宿主等测试宿主源码 |

### 1.2 产物矩阵

| CMake 目标 | 输出文件名 | 类型 | C++ 标准 | 主要用途 |
| --- | --- | --- | --- | --- |
| `DirectX11` | `d3d11.dll` | SHARED | C++17 | DX11 代理 DLL，3DMigoto/XXMI 主运行时 |
| `DirectX12` | `d3d12.dll` | SHARED | C++20 | DX12 代理 DLL，本分支的 DX12 运行时 |
| `dxgi` | `dxgi.dll` | SHARED | C++17 | DXGI 包装器（`CreateDXGIFactory*` 等） |
| `D3DCompiler_46` | `d3dcompiler_47.dll` | SHARED | C++17 | D3DCompiler 包装器，输出名沿用 `_47` |
| `BinaryDecompiler` | — | STATIC | C++17 | DXBC 反汇编/反射，供 DX11 链接 |
| `DirectXTK` | — | STATIC | C++17 | 文本/精灵渲染，供 DX11 链接 |
| `crc32c` | — | STATIC | C++17 | CRC32C 硬件实现，供 DX11/DX12 链接 |
| `D3D_Shaders` | `D3D_Shaders.exe` | EXE | C++17 | 离线着色器汇编/反汇编工具 |
| `dxc`（非 CMake 目标） | `dxcompiler.dll` + `dxil.dll` | 预编译投放 | — | DX12 `[CustomShaderX]` 的 SM6 运行时编译器 |

平台与配置维度：

| 维度 | 取值 | 来源 |
| --- | --- | --- |
| 平台 | `x64`、`Win32` | `build-cmake.ps1 -Platform`；`CMakePresets.json` 提供两套 |
| 配置 | `Debug`、`Release` | `build-cmake.ps1 -Configuration` |
| 运行时库 | Release `/MT`，Debug `/MTd`（静态 CRT） | [`cmake/CompilerOptions.cmake`](../cmake/CompilerOptions.cmake) |
| 产物目录 | 构建树 `<buildDir>/src/<Target>/`，再由脚本复制到 `x64/<Config>`（Win32 为 `x32/<Config>`） | `build-cmake.ps1`；生成脚本的 `TARGET_FILE` |

> **产物先落在构建树里**：`CMakeLists.txt` 为 Debug/Release 逐配置设置了 `CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONF}`、`CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONF}`、`CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONF}`，但后缀是**混合大小写**（`Debug` / `Release`），而 CMake 查表时用**大写**后缀（`CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE`）。两者对不上，这三组变量**没有生效**，各 DLL 仍留在目标自己的构建目录里。
>
> 已核对（2026-11-18，一次干净的 x64 MSVC 配置：Ninja + `Release`，`BUILD_TESTING=OFF`、`DX12_RELEASE_TIMING=OFF`）：配置成功并生成 `build.ninja`，其中 `TARGET_FILE = src\DirectX11\d3d11.dll`、`TARGET_FILE = src\DirectX12\d3d12.dll`（均相对构建目录），`TARGET_PDB` 同在构建树内；`D3DCompiler_46` 与 `dxgi` 目标同样是 `src\<Target>\<dll>` 形式。**该次检查只做了配置，没有编译任何 DLL**，因此这里只能证明产物的落盘路径，不能证明链接结果。
>
> 因此 `build-cmake.ps1` 的复制步骤是**有效**的：它把构建树里的目标复制到 `x64/<Config>`（Win32 为 `x32/<Config>`）。相应地，顶层那三个 `OUTPUT_DIRECTORY` 变量目前是无效的——是否改成大写后缀属于代码修改，不在本文档范围内，见[待确认事项](#11-待确认事项)。

### 1.3 各 DLL 的组成与导出面

**DX11（`d3d11.dll`）** — [`src/DirectX11/CMakeLists.txt`](../src/DirectX11/CMakeLists.txt)

- 链接：`BinaryDecompiler`、`DirectXTK`、`Nektra::NktHookLib`、`crc32c`、`XINPUT9_1_0.lib`、`d3dcompiler.lib`、`DXGI.lib`、`Shlwapi.lib`、`Dbghelp.lib`，以及按架构/配置选择的 pcre2 预编译库（`pcre2-8-64.lib` / `pcre2-8-64d.lib` / 32 位对应项）。
- 直接编译进目标（而非链接库）的共享源文件：[`src/common/iid.cpp`](../src/common/iid.cpp)、[`src/common/ini_parser_lite.cpp`](../src/common/ini_parser_lite.cpp)、[`src/common/util.cpp`](../src/common/util.cpp)、[`src/D3D_Shaders/Assembler.cpp`](../src/D3D_Shaders/Assembler.cpp)、[`src/D3D_Shaders/SignatureParser.cpp`](../src/D3D_Shaders/SignatureParser.cpp)、[`src/HLSLDecompiler/DecompileHLSL.cpp`](../src/HLSLDecompiler/DecompileHLSL.cpp)。
- 预处理器定义（关键项）：`MIGOTO_DX=11`、`CRC32C_STATIC=1`、`PCRE2_STATIC`、`PCRE2_CODE_UNIT_WIDTH=8`、`DX11_VERSION_GENERATED=1`、`_USRDLL`，以及来自 `set_target_common_definitions` 的 `UNICODE`/`_UNICODE`；Debug 额外 `_DEBUG_LAYER=1`（宏名带前导下划线）。
- 导出面由 [`d3d11Wrapper.def`](../src/DirectX11/d3d11Wrapper.def) 固定：按序号导出 `D3D11CreateDevice`、`D3D11CreateDeviceAndSwapChain`、`D3DKMT*`、`OpenAdapter10*`、`D3D11Core*`、`D3D11On12CreateDevice`，另加非序号的 `CBTProc`（外部 Loader 入口）。
- 链接期强制引用 `CreateDXGIFactory`（x64）或 `_CreateDXGIFactory@8`（x86），避免被优化掉。
- MSVC 构建（Debug 与 Release **都是**）链接 `/MAP`，用于把崩溃 RVA 映射回函数名；条件是 `if(MSVC)`，不是 Debug 专属。

**DX12（`d3d12.dll`）** — [`src/DirectX12/CMakeLists.txt`](../src/DirectX12/CMakeLists.txt)

- 链接：`Nektra::NktHookLib`、`crc32c`、`dxgi.lib`、`d3dcompiler.lib`、`windowscodecs.lib`、`ole32.lib`、`User32.lib`、`Gdi32.lib`、`comctl32.lib`、`Dwmapi.lib`、`Shlwapi.lib`、`dbghelp.lib`。
- 共享源文件：[`src/common/ini_parser_lite.cpp`](../src/common/ini_parser_lite.cpp)（DX12 复用 DX11 的 INI 解析器）。
- 预处理器定义：`MIGOTO_DX=12`、`_USRDLL`。
- MSVC 附加开关：`/bigobj`（Debug 符号超出默认 COFF 段上限）、Release 追加 `/Gw` 与 `/Zc:inline`。
- 导出面由 [`d3d12Wrapper.def`](../src/DirectX12/d3d12Wrapper.def) 固定：`D3D12CreateDevice`、`D3D12GetDebugInterface`、`D3D12GetInterface`、`D3D12CreateRootSignatureDeserializer` 等按序号 `@99`–`@117` 导出，另加 `CBTProc`。
- `DX12_RELEASE_TIMING=ON` 时额外编译 [`Core/Diag/DX12CrashReport.cpp`](../src/DirectX12/Core/Diag/DX12CrashReport.cpp)（崩溃自诊断）。

**DXGI（`dxgi.dll`）** — [`src/DirectXGI/CMakeLists.txt`](../src/DirectXGI/CMakeLists.txt)

- 链接：`Nektra::NktHookLib`、`d3d11.lib`、`dxgi.lib`、`d3dcompiler.lib`、`comctl32.lib`、`XINPUT9_1_0.lib`；Release 追加 `dinput8.lib`。
- 定义：`DXGI_LOGGING=1`、`DIRECTINPUT_VERSION=0x0800`、`DIRECTX11_EXPORTS`；Debug 追加 `_DEBUG_LAYER=1`、`HOOK_SYSTEM32=1`。
- 链接期 `/INCLUDE:D3D11CreateDevice`。
- 导出面由 [`d3dxgiWrapper.def`](../src/DirectXGI/d3dxgiWrapper.def) 固定：`CreateDXGIFactory`、`CreateDXGIFactory1`、`CreateDXGIFactory2`、`DXGID3D10*`、`D3DKMT*`、`OpenAdapter10*` 等。

**D3DCompiler（`d3dcompiler_47.dll`）** — [`src/D3DCompiler_46/CMakeLists.txt`](../src/D3DCompiler_46/CMakeLists.txt)

- 源文件取自 [`src/D3DCompiler/d3dcWrapper.cpp`](../src/D3DCompiler/d3dcWrapper.cpp)；只链接 `d3dcompiler.lib`（转发到系统 `d3dcompiler_47.dll`）。
- 导出面由 [`D3DCompiler_46.def`](../src/D3DCompiler_46/D3DCompiler_46.def) 固定：`D3DCompile`、`D3DCompile2`、`D3DAssemble`、`D3DDisassemble*`、`D3DReflect`、`D3DGetBlobPart` 等，按序号 `@1`–`@25` 导出。

**第三方编译器（DXC）**

- `third_party/dxc/dxcompiler.dll` 与 `dxil.dll` 是 **`d3d12.dll` 的运行时依赖**，不是构建期便利工具。`[CustomShaderX]` 元素首次运行时由 [`DX12CustomShaderCompile.h`](../src/DirectX12/ModRuntime/DX12CustomShaderCompile.h) 编译到 SM6；加载器优先探测 `d3d12.dll` 所在目录，再回退到系统 DLL 搜索顺序，并从 `dxcompiler.dll` 同目录加载 `dxil.dll`。
- 编译是**两级后端链**：先试 DXC（`CompileWithDxc`），失败再试 `d3dcompiler_47` 的 SM6 profile（`CompileWithD3DCompiler`）。缺 `dxcompiler.dll` 时仍可能由第二条路径成功；只有两级都失败才会跳过该元素。
- 两级都失败时的运行时错误文本是 `no SM6 compiler for <profile> (d3dcompiler needs dxil.dll, dxcompiler.dll not found)`；该失败会被缓存，之后同一文件只报 `cached compile failure for <path>`（见 [`DX12CustomShaderCompile.h`](../src/DirectX12/ModRuntime/DX12CustomShaderCompile.h)）。
- 仓库内不放置来历不明的 `dxcompiler.dll`：VulkanSDK 自带的自包含版在第二次进程内编译时会 fail-fast（`0xC0000409`）并带走游戏进程。

**第三方库来源**

| 库 | 形态 | 说明 |
| --- | --- | --- |
| `Nektra`（`NktHookLib`） | 预编译 `.lib`（x86/x64 × Debug/Release） | [`cmake/FindNektra.cmake`](../cmake/FindNektra.cmake) 按 `CMAKE_SIZEOF_VOID_P` 与配置选择；头文件缺失时只警告不报错 |
| `pcre2` | 预编译 `.lib` | DX11 链接，按架构/配置选择 `pcre2-8-32/64` 与 `d` 后缀调试版 |
| `DirectXTK` | 源码编译 | 仅 DX11 链接，提供 `SpriteBatch`、DDS 读取等 |
| `crc32c-hw-1.0.5` | 源码编译 | DX11 与 DX12 共用 |

---

## 2. 构建系统

### 2.1 CMake 结构

顶层 [`CMakeLists.txt`](../CMakeLists.txt)（共 38 行）节选：

```cmake
# 节选自顶层 CMakeLists.txt（该文件共 38 行）
cmake_minimum_required(VERSION 3.20)
project(3DMigotoDX12 VERSION 1.3.16 ...)   # 省略 DESCRIPTION / LANGUAGES
include(CTest)                     # 提供 BUILD_TESTING（默认 ON）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 逐配置设置输出目录：后缀是混合大小写，CMake 按大写查表，因此不生效（见 1.2）
foreach(CONF Debug Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONF} ${PROJECT_SOURCE_DIR}/x64/${CONF})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONF} ${PROJECT_SOURCE_DIR}/x64/${CONF})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONF} ${PROJECT_SOURCE_DIR}/x64/${CONF})
endforeach()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)           # §2.3 全局编译选项的来源
find_package(Nektra)               # 3rdparty/Nektra 预编译 hook 库

add_subdirectory(3rdparty/crc32c-hw-1.0.5)
add_subdirectory(3rdparty/DirectXTK)
add_subdirectory(src/BinaryDecompiler)
add_subdirectory(src/D3D_Shaders)
add_subdirectory(src/D3DCompiler_46)
add_subdirectory(src/DirectXGI)
add_subdirectory(src/DirectX11)
add_subdirectory(src/DirectX12)
```

DX12 目标在自己的 `CMakeLists.txt` 内以 `target_compile_features(DirectX12 PRIVATE cxx_std_20)` 覆盖到 C++20；DX11 目标显式 `cxx_std_17`。**DX11 保持 C++17、DX12 保持 C++20 是仓库既有约定**，不要为了统一而改动其中一方。

### 2.2 工具链探测

[`scripts/build-cmake.ps1`](../scripts/build-cmake.ps1) 按以下顺序查找 CMake 与 Ninja：

| 工具 | 查找顺序 |
| --- | --- |
| CMake | `.tools\cmake\bin\cmake.exe` → `PATH` → Visual Studio 内置 `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` → `C:\Program Files\CMake\bin\cmake.exe` |
| Ninja | `.tools\ninja\ninja.exe` → `PATH` → Visual Studio 内置 `CommonExtensions\Microsoft\CMake\Ninja` |
| MSVC 环境 | `vswhere.exe` 查 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` → `vcvars64.bat`（x64）/ `vcvars32.bat`（Win32）；失败时回退扫描 `C:\Program Files\Microsoft Visual Studio\<版本>\<Community\|Professional\|Enterprise\|BuildTools>\VC\Auxiliary\Build\` |

**最低版本**：工程本身要求 CMake ≥ 3.20（`cmake_minimum_required(VERSION 3.20)`）；但 [`CMakePresets.json`](../CMakePresets.json) 声明 `"version": 6`，该 preset schema 需要 **CMake ≥ 3.25**——在 3.20–3.24 上 `cmake --preset` 会直接失败，只有脚本路径（`build-cmake.ps1`，不读 presets）可用。

三项都找不到时脚本打印明确错误并以非零码退出：

- `ERROR: CMake not found.` / `ERROR: Ninja not found.`
- `ERROR: Cannot locate Visual Studio with VC tools (vswhere + fallback scan both failed).`

生成器固定为 `Ninja`（脚本不读 `CMakePresets.json`）。`CMakePresets.json` 是给 IDE / 手工使用准备的等价预设：

```powershell
cmake --preset release          # Ninja，build/x64/release
cmake --build --preset release
```

### 2.3 编译与链接选项

[`cmake/CompilerOptions.cmake`](../cmake/CompilerOptions.cmake) 提供的全局基线（仅 MSVC）：

| 类别 | 选项 |
| --- | --- |
| 运行时库 | `/MT`（Release）、`/MTd`（Debug） |
| 警告/异常/字符集 | `/W3`、`/EHsc`、`/utf-8` |
| Debug 专属 | `/Od`、`/RTC1`、`/GS` |
| 仅 Release | `/O2`、`/GL`、`/LTCG`、`/INCREMENTAL:NO` |
| 所有非 Debug 配置 | `/Gy`（函数级链接）、`/GS-`（关闭缓冲安全检查） |
| 公共 | `/MP`；链接 `/DEBUG`（非 Debug 追加 `/OPT:REF /OPT:ICF`）；Ninja 生成器追加 `/NOLOGO` |
| 预处理 | `_DEBUG`/`NDEBUG`、`UNICODE`、`_UNICODE`、`_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES{,_COUNT}=1`、`_WINDOWS` |

静态 CRT（`/MT`）意味着**部署 DLL 不需要额外安装 VC++ 运行库**。

另有一条针对中文版 MSVC 的兼容处理：当 `CMAKE_CL_SHOWINCLUDES_PREFIX` 被识别为乱码前缀时改写为 `注意: 包含文件:  `，否则 Ninja 会丢失全部头文件依赖。

### 2.4 CMake 开关

| 开关 | 默认 | 作用 |
| --- | --- | --- |
| `BUILD_TESTING` | ON（由 `include(CTest)` 提供） | 是否注册 CTest 与构建测试可执行文件 |
| `DX12_RELEASE_TIMING` | OFF | 在非 Debug 构建里启用**诊断日志（写文件/写线程）与崩溃自诊断**；计时探针与 F11 剖面 HUD 不受此开关控制 |
| `DX12_AUDIT_TESTS` | OFF | 构建 DX12 审计可执行文件（需 `BUILD_TESTING=ON`） |
| `DX11_AUDIT_ASAN_LOCKS` | OFF | 用 AddressSanitizer 插桩锁清理回归（仅 MSVC x64） |
| `DX11_AUDIT_ASAN_DECOMPILER` | OFF | 用 AddressSanitizer 插桩反编译器越界回归（仅 MSVC x64） |
| `DX11_VERSION_TAG` | 空（缓存变量） | 覆盖 DX11 版本资源的 tag 来源 |

`DX12_RELEASE_TIMING` 的三个后果：定义 `DX12_RELEASE_TIMING`、把 `Core/Diag/DX12CrashReport.cpp` 加入目标、以及让 `DX12DiagLog::Open()` 真正打开日志文件并起写线程。

**计时与 HUD 不受该开关控制**：命名桶 profiler 与屏幕 HUD 编译进**每一个**构建（普通 Release 在 HUD 打开时才采样，是有意的运行时门控），只有"日志输出"是 TIMING 专属（见 [`DX12Diag.h`](../src/DirectX12/Core/Diag/DX12Diag.h) 开头注释与 `DX12DiagLog::Open()`）。因此普通 Release 下 F11/F12 剖面面板可用，但不会产生 `d3d12_log.txt`。

### 2.5 DX11 版本资源

[`cmake/GenerateDX11Version.cmake`](../cmake/GenerateDX11Version.cmake) 在构建前生成 `dx11-version.generated.h`，写入 `VERSION_MAJOR/MINOR/REVISION/BUILD`、`DX11_VERSION_IS_RELEASE`、`VER_COMPANYNAME_STR`、`VER_COMMENTS_STR`：

1. 版本来源优先级：`DX11_VERSION_TAG` 缓存变量 → Git `describe --tags --match "v[0-9]*"` → 回退到 `PROJECT_VERSION`（当前 `1.3.16`）。
2. 版本号必须匹配 `v?MAJOR.MINOR.PATCH[.BUILD]`，否则 CMake 直接 `FATAL_ERROR`；各段需落在 Windows 16 位字段内。
3. `build` 段在存在 tag 时叠加"tag 到 HEAD 的提交数"，因此同一 tag 之后的每个提交都会得到不同的 build 号。
4. `DX11_VERSION_IS_RELEASE` 只在 **HEAD 恰好等于该 tag 且工作区干净**时为 1。因此 tag 之后的任何提交、以及工作区不干净的构建，都会写 `PrivateBuild`（`DirectX11.rc` 的 `PrivateBuild` 值与 `VER_SOURCE_FLAGS = VS_FF_PRIVATEBUILD`）——日常非 tag 构建出现该字段属正常。
5. 若 `DX11_VERSION_TAG` 指向的 tag 不是 HEAD 的祖先，CMake 直接 `FATAL_ERROR: DX11 version tag is not an ancestor of HEAD`，构建失败。
6. `VER_COMPANYNAME_STR` 形如 `Built by <builder> @ <commit>`；`VER_COMMENTS_STR` 只保留可公开的 `https://host/path` 形式的远端地址，凭据、query、fragment 一律丢弃。
7. 生成文件写入采用"内容不变则不重写"的临时文件 + 原子改名，避免 Ninja 反复重编。

消费方：[`src/DirectX11/DX11Version.h`](../src/DirectX11/DX11Version.h) 与 [`src/DirectX11/DirectX11.rc`](../src/DirectX11/DirectX11.rc)。DX12 目标不使用该机制。

### 2.6 构建目录

`build-cmake.ps1` 使用的构建目录：

```
build/cmake-<platform>-<config>[-timing]      # 例如 build/cmake-x64-release、build/cmake-x64-release-timing
```

`-ReleaseTiming` 只影响构建目录后缀与 `DX12_RELEASE_TIMING`，因此普通 Release 与 TIMING 的 CMake 缓存互不覆盖。

---

## 3. 构建脚本

### 3.1 主脚本

[`scripts/build-cmake.ps1`](../scripts/build-cmake.ps1) 的参数：

| 参数 | 取值 | 默认 | 说明 |
| --- | --- | --- | --- |
| `-Configuration` | `Debug` / `Release` | `Debug` | 构建配置 |
| `-Platform` | `x64` / `Win32` | `x64` | 目标平台（`Win32` 走 `vcvars32.bat`） |
| `-GameId` | 字符串 | 空 | 指定 `build_config.json` 中的部署目标；为空时用文件顶层的 `GameId` |
| `-ReleasePackage` | 开关 | 关 | 构建两个 API 与 D3DCompiler，**跳过全部清理、部署与启动动作** |
| `-ReleaseTiming` | 开关 | 关 | 追加 `-DDX12_RELEASE_TIMING=ON`，并使用独立的 `-timing` 构建目录 |

执行流程：

1. **清理**（`-ReleasePackage` 除外）：调用 [`scripts/clean_build_result.ps1`](../scripts/clean_build_result.ps1)。
2. **工具探测**：CMake、Ninja、vcvars 三者都必须找到，否则退出码非零。
3. **陈旧缓存检查**：读取构建目录 `CMakeCache.txt` 的 `CMAKE_HOME_DIRECTORY`，与当前源码根不一致时删除该构建目录后重配。
4. **配置**：生成临时 `.bat`，`call vcvars` 后执行
   `cmake -S <root> -B <buildDir> -G Ninja -DCMAKE_MAKE_PROGRAM=<ninja> -DCMAKE_BUILD_TYPE=<cfg> -DDX12_RELEASE_TIMING=<ON|OFF>`。
5. **配置重试**：配置失败时删除整个构建目录并重试一次，用于规避实时杀毒/文件系统竞争导致的 `rules.ninja` 瞬时不可见；仍失败则以该退出码终止。
6. **构建**：`cmake --build <buildDir> --target <targets>`。目标集合为
   - 默认：`DirectX12`
   - `-ReleasePackage`：`DirectX11 DirectX12 D3DCompiler_46`
7. **收集产物**：从**构建树**复制 `src/DirectX11/d3d11.dll`、`src/DirectX12/d3d12.dll`、`src/D3DCompiler_46/d3dcompiler_47.dll`、`src/DirectXGI/dxgi.dll` 到 `x64/<Config>`（Win32 时为 `x32/<Config>`）。源路径 `<buildDir>/src/<Target>/<dll>` 与生成脚本里的 `TARGET_FILE` 一致；**只复制存在的文件**，缺失不报错。
8. **部署与启动**：仅 `-Platform x64` 且非 `-ReleasePackage` 时执行，见第 4 节。

> `dxgi` 目标不在任何脚本的目标集合里，因此 `dxgi.dll` 在上述流程中不会被构建，也就不会被复制到 `x64/<Config>`。需要该产物时须手工 `cmake --build <buildDir> --target dxgi`。

### 3.2 包装脚本

| 脚本 | 作用 |
| --- | --- |
| [`scripts/build_release_x64.ps1`](../scripts/build_release_x64.ps1) | `Release` + `x64` 的日常构建，转调 `build-cmake.ps1` |
| [`scripts/build_release.ps1`](../scripts/build_release.ps1) | 默认 x64 Release 的兼容入口，转调上一个脚本 |
| [`scripts/build_release_timing_x64.ps1`](../scripts/build_release_timing_x64.ps1) | `Release` + `DX12_RELEASE_TIMING=ON`，用于在优化构建里读热路径分阶段计时 |
| [`scripts/build_release_dx11_x64.ps1`](../scripts/build_release_dx11_x64.ps1) | **只**构建 DX11 Release DLL，不部署、不启动游戏；默认构建目录 `build\dx11-release-x64`，显式传 `-DBUILD_TESTING=OFF`。产物留在构建树内，**不复制**到 `x64\Release` |

`build_release_timing_x64.ps1` 与普通 Release 一样会覆盖已部署的 `d3d12.dll`，且计时插桩本身有开销：只能用它读分阶段数据，不能拿它测帧率；测完要回到 `build_release_x64.ps1` 重建干净的 Release。

`build_release_dx11_x64.ps1` 只构建 `DirectX11` 目标，产物留在其自带构建目录（`build\dx11-release-x64\src\DirectX11\d3d11.dll`），**不会**复制到 `x64\Release`，也不会部署到游戏目录。脚本末尾按 `Test-Path` 自检的正是这个路径——与生成脚本里的 `TARGET_FILE = src\DirectX11\d3d11.dll` 一致，文件缺失才抛 `Build output was not found: ...`。

TIMING 的 PDB 同理：PDB 也在构建树内（`TARGET_PDB = src\DirectX12\d3d12.pdb`），`build-cmake.ps1` 从 `$buildDir\src\DirectX12\d3d12.pdb` 取，与实际一致。

### 3.3 清理

[`scripts/clean_build_result.ps1`](../scripts/clean_build_result.ps1) 删除仓库内的：`build`、`CMakeFiles`、`CMakeCache.txt`、`cmake_install.cmake`、`build-debug-current.log`、`build-last.log`，以及 `x64`（除非传 `-KeepOutput`）。

脚本带路径护栏：任何不在项目根之下的目标路径直接抛错拒绝删除；`-WhatIf` 只打印不删除。

> 日常开发构建默认会执行这一步，因此每次都是**全量重建**（`build` 与 `x64` 都被清掉），不是增量构建。

### 3.4 失败路径

| 现象 | 原因与处理 |
| --- | --- |
| `ERROR: CMake not found.` / `ERROR: Ninja not found.` | 三级探测都没命中。安装到 `.tools`、加进 `PATH`，或安装带 C++ 工作负载的 Visual Studio |
| `ERROR: Cannot locate Visual Studio with VC tools` | `vswhere` 与目录扫描都失败。需安装 Visual Studio 2022/18 的"使用 C++ 的桌面开发" |
| `Stale CMake cache detected` | 构建目录来自另一份源码树，脚本自动删除该目录后重配 |
| 配置失败后自动重试一次仍失败 | 脚本以配置阶段的退出码终止，不会继续构建 |
| 构建阶段失败 | 脚本打印 `FAILED: CMake <cfg> <platform>` 并以非零码退出；包装脚本据此 `exit $LASTEXITCODE` |
| `build_config.json` 缺失或无法解析 | 脚本打印警告并**跳过**部署与启动，构建产物仍然可用 |

---

## 4. 开发部署

### 4.1 部署目标表

[`build_config.json`](../build_config.json) 顶层 `GameId` 指定默认目标，`Games` 数组是候选表。下面是 `GameId "004"` 条目的**节选**：实际文件含 `001`–`004` 四个条目（`001`/`002` 配 `TargetExe`，`003`/`004` 配复制目录），照抄时需按自己的环境补全其余条目。

```json
{
  "GameId": "004",
  "Games": [
    {
      "GameId": "004",
      "TargetExe": "",
      "WorkingDirectory": "",
      "LaunchArguments": "",
      "CopyToTargetDirectoryPath": "D:\\Dev\\ssmt4\\src-tauri\\resources\\DX12\\",
      "ExtraCopyToTargetDirectoryPaths": ["D:\\MMTCacheFolder\\3Dmigoto\\ZZMIDX12\\"],
      "CopyToTargetDirectory": true,
      "LaunchGame": false,
      "Notice": "Copies d3d12.dll to the local DX12 resource directory and the ZZMIDX12 test directory after build."
    }
  ]
}
```

字段语义与默认值：

| 字段 | 缺失时的默认 | 说明 |
| --- | --- | --- |
| `TargetExe` | 空 | 目标可执行文件绝对路径；为空表示不启动游戏 |
| `WorkingDirectory` | `Split-Path -Parent TargetExe` | 启动工作目录 |
| `LaunchArguments` | 空 | 传给 `Start-Process` 的参数 |
| `CopyToTargetDirectory` | `true` | 是否复制 `d3d12.dll` |
| `CopyToTargetDirectoryPath` | 空 | 主复制目录；为空且有 `TargetExe` 时取其所在目录 |
| `ExtraCopyToTargetDirectoryPaths` | 空 | 额外复制目录数组 |
| `LaunchGame` | `true` | 是否在部署后启动游戏 |
| `Notice` | — | 仅供人阅读的备注，脚本不解析 |

### 4.2 部署动作顺序

仅在 `-Platform x64` 且非 `-ReleasePackage` 时执行：

1. 解析 `build_config.json`；缺失、无法解析、无 `GameId`、目标 ID 不存在时打印警告并跳过全部部署动作。
2. `TargetExe` 与 `CopyToTargetDirectoryPath` 都没有时跳过。
3. `TargetExe` 不存在 → 关闭 `LaunchGame`；`WorkingDirectory` 不存在 → 关闭 `LaunchGame`。
4. `CopyToTargetDirectory=false` 且 `LaunchGame=false` → 整体跳过。
5. 若 `TargetExe` 存在同名进程，先 `Stop-Process -Force` 结束它并等待 1 秒，避免文件被占用。
6. 对每个目标目录：
   1. **轮转旧日志**（见 [6.2](#62-构建脚本轮转)）；
   2. 删除旧 `ShaderDumpDX12` 目录；
   3. 复制 `d3d12.dll`（失败即抛错终止脚本）；
   4. TIMING 构建额外复制 `d3d12.pdb`（缺失只警告）；
   5. 复制 DXC 后端 `dxcompiler.dll` + `dxil.dll`（缺失只警告，并提示 `[CustomShaderX]` 元素会被跳过；`compile_backend_unavailable` 只是脚本的控制台提示串，**运行时日志里并不存在**该 token）
7. `LaunchGame=true` 时 `Start-Process` 目标程序。

### 4.3 开关一览

| 开关 | 位置 | 默认 | 效果 |
| --- | --- | --- | --- |
| `-ReleasePackage` | `build-cmake.ps1` | 关 | 构建 `DirectX11`+`DirectX12`+`D3DCompiler_46`；跳过清理、部署与启动 |
| `-ReleaseTiming` | `build-cmake.ps1` | 关 | `DX12_RELEASE_TIMING=ON`；独立构建目录；部署额外带 PDB |
| `-GameId` | `build-cmake.ps1` / 包装脚本 | 空（用文件顶层 `GameId`） | 选择部署目标 |
| `CopyToTargetDirectory` | `build_config.json` | `true` | 是否复制 DLL |
| `ExtraCopyToTargetDirectoryPaths` | `build_config.json` | 空 | 额外复制目录 |
| `LaunchGame` | `build_config.json` | `true` | 是否启动游戏 |

### 4.4 DX11 的部署

DX11 DLL 由使用者部署到游戏的加载目录，并搭配对应的 `d3dx.ini`。双 DX 发布构建的 DLL 在 `x64/Release/d3d11.dll`。仅构建 DX11 时，从默认构建目录 `build/dx11-release-x64/src/DirectX11/d3d11.dll` 取用。配置模板见 [`Dependencies/d3dx.ini`](../Dependencies/d3dx.ini)。

DX11 与 DX12 的 `d3dx.ini` 查找规则不同：

- DX11：固定读取 **DLL 所在目录**的 `d3dx.ini`（`src/DirectX11/IniHandler.cpp` 中的 `INI_FILENAME`，见 [`LoadConfigFile()`](../src/DirectX11/IniHandler.cpp)）。
- DX12：先找**进程工作目录**的 `d3dx.ini`，找不到再找 DLL 所在目录，最后回退到相对名 `d3dx.ini`（[`MigotoConfigLocator::FindDefaultConfigPath`](../src/DirectX12/Config/DX12ConfigLoader.h)）。

---

## 5. 发布

发布脚本是 [`scripts/publish_release.ps1`](../scripts/publish_release.ps1)，完整流程、版本递增规则、GitHub Release 草稿与上传细节见 [Release-Package.md](Release-Package.md)。此处只列开关与产物。

### 5.1 开关

| 开关 | 说明 |
| --- | --- |
| `-Version X.Y.Z` | 显式指定版本；省略时自动把发布仓库上最新的 `vX.Y.Z` 的 patch 加一；首次发布必须显式传 `-Version` |
| `-Token` | GitHub Token；优先级为 `-Token` → `GITHUB_TOKEN` → `GH_TOKEN` → Windows Git Credential Manager |
| `-SkipBuild` | 跳过本地构建，复用 `x64\Release` 中已有产物 |
| `-PackageOnly` | 只本地打包，不创建 Release；**需要**同时给 `-Version` |

### 5.2 前置检查

| 检查 | 不通过时 |
| --- | --- |
| 当前分支必须是 `main` | 直接退出 |
| 本地 `main` 不得落后 `origin/main` | 直接退出 |
| 工作区有未提交改动 | **只警告**，并明确提示改动会被打进这次发布 |
| 发布仓库上同名 tag 已存在 | 直接退出（除 `-PackageOnly`） |
| `CMakeLists.txt` 的 `project(VERSION)` 与发布 tag 不一致 | 只警告，并提示 DLL 内部版本仍是 CMake 的值 |

### 5.3 发布包内容

发布 zip 为**扁平布局**（`ProjectBunny-PACKAGE-vX.Y.Z.zip`），内容：

`d3d11.dll`、`d3dcompiler_47.dll`、`d3d12.dll`、`dxcompiler.dll`、`dxil.dll`、`LICENSE.txt`、`d3dx.ini`（取自 [`config/d3dx.ini`](../config/d3dx.ini)），以及存在时的 `dxc-LICENSE.TXT`。

同时生成 `build/Manifest.json`，包含 `version`、`configuration`（`Release`）、`architecture`（`x64`）、每个文件的 `sha256` 与 `source_commit`。打包时会对 `.dll`/`.exe` 校验 PE 头与 AMD64 machine 字段；校验和不冒充上游签名格式。

> `dxgi.dll` **不在**发布包内。`dxcompiler.dll`/`dxil.dll` 缺失是硬错误（不是降级发布），因为使用者没有别的途径取得该运行时依赖。

---

## 6. 日志与轮转

### 6.1 日志文件与位置

| 日志 | 产生者 | 位置 | 何时存在 |
| --- | --- | --- | --- |
| `d3d11_log.txt` | DX11 运行时 | `d3d11.dll` 所在目录 | 依 `[Logging]` 配置；`log_level = disabled/warning/info/debug`（大小写不敏感），缺失、为空或非法时才回退到旧的 `calls`/`debug` 组合。文件以 `"w"` 模式打开，**每次启动都会覆盖**，DX11 侧没有轮转 |
| `d3d12_log.txt` | DX12 运行时 | `d3d12.dll` 所在目录 | **仅 TIMING 构建**写日志文件；普通 Release 不写文件，聚合/写线程只在 HUD 打开时按需启动 |
| `d3d12_log_<tick>.txt` | DX12 运行时 | 同上 | TIMING 构建启动时把上一份 `d3d12_log.txt` 复制留存 |
| `d3d12_log.jsonl` | — | 部署目录 | 仓库内没有生成该文件的代码路径；只被部署脚本轮转（见[待确认事项](#11-待确认事项)） |
| `log.jsonl` | DX12 帧分析 | `FrameAnalysis-<日期-时间>/log.jsonl` | F8 抓帧时生成 |
| `d3d12_crash_<stamp>.dmp` / `.txt` | DX12 崩溃自诊断 | `d3d12.dll` 所在目录 | TIMING 构建崩溃时尽力生成，并非每次崩溃都有 |
| `dxgi_log.txt` | DXGI 包装器 | 进程工作目录（相对路径写入） | `[Logging] calls` 默认为 1，因此**默认就会生成**；其 `d3dx.ini` 取自**宿主 EXE 所在目录** |
| `D3DCompiler_46_log.txt` | D3DCompiler 包装器 | 进程工作目录（相对路径写入） | `[Logging] calls` 默认为 0，需显式开启；文件名由 `COMPILER_DLL_VERSION`（`"46"`）拼出，与输出名 `d3dcompiler_47.dll` 不同 |

### 6.2 构建脚本轮转

部署前，脚本对每个目标目录中的 `d3d12_log.jsonl`、`d3d12_log.txt`、`d3d12_hook.log` 做一次轮转：

```text
d3d12_log.txt      ->  d3d12_log.before_20261118-101112.txt
d3d12_log.jsonl    ->  d3d12_log.before_20261118-101112.jsonl
d3d12_hook.log     ->  d3d12_hook.before_20261118-101112.log
```

时间戳格式为 `yyyyMMdd-HHmmss`。轮转失败时保留原文件并打印警告，不中断部署。

随后按 `d3d12_log.before_*.jsonl`、`d3d12_log.before_*.txt`、`d3d12_hook.before_*.log` 三个模式分别清理：每种保留**最新 10 个**，更旧的删除（删除失败只警告）。

> 这是"构建前保护证据"的轮转：构建不会销毁正在分析的旧日志。旧的轮转文件仍会累积到 10 份为止，需要长期保留时及时手工另存。

### 6.3 进程内轮转

TIMING 构建启动时（[`DX12DiagLog::Open()`](../src/DirectX12/Core/Diag/DX12Diag.h)）：

1. 取 `d3d12.dll` 所在目录；
2. 若该目录已有 `d3d12_log.txt`，**复制**为 `d3d12_log_<GetTickCount64()>.txt`（复制而非移动，原文件保留）；
3. 以 `"w"` 模式打开新的 `d3d12_log.txt`。

路径构造失败或文件打不开时静默降级为"无日志"，不影响运行。

### 6.4 崩溃与抓帧产物

- 崩溃自诊断仅在 `DX12_RELEASE_TIMING=ON` 时编入。报告写入 `d3d12_crash_<stamp>.txt`，minidump 写入 `d3d12_crash_<stamp>.dmp`，两者都在 `d3d12.dll` 所在目录。
- 崩溃报告的符号化依赖**本地 PDB**（dbghelp 守卫会关闭符号服务器），因此 TIMING 部署必须连 PDB 一起复制；缺 PDB 时报告只记录模块 + 偏移。
- DX11 与 DX12 的帧分析输出目录同名规则：`FrameAnalysis-%Y-%m-%d-%H%M%S`，位于 DLL 所在目录。DX12 在目录内另写 `log.jsonl`。
- DX12 的 F9 着色器抓取写入 `d3d12.dll` 所在目录下的 **`ShaderFixes`**（[`GetShaderFixesDirectory()`](../src/DirectX12/ShaderTools/DX12ShaderDump.cpp)），内含 `<hash>-<stage>.bin`、`<hash>-<stage>.asm.txt`、`ShaderAnalysis.txt`、`PsoResourceSummaryDX12.txt`、`ShaderUsage.txt`、`pso_log.txt`。
- 部署脚本在复制 DLL 前会删除目标目录下的 `ShaderDumpDX12` 目录；当前 DX12 源码里没有生成该目录的代码路径（着色器抓取写的是 `ShaderFixes`），见[待确认事项](#11-待确认事项)。

### 6.5 读取与分析

TIMING 日志很密，不要整文件读：

```powershell
Get-Content .\d3d12_log.txt -Tail 200
Select-String -Path .\d3d12_log.txt -Pattern 'failed|unsupported|warning|error' | Select-Object -Last 50
```

[`scripts/analyze-dx12-log.ps1`](../scripts/analyze-dx12-log.ps1) 汇总帧时间线、PreSkin 缓存命中率、API 墙钟 TOP15、Create original/record 拆分与热路径单次开销（`-Watch` 额外盯三个优化相关桶）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\analyze-dx12-log.ps1 -Log 'D:\MMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt'
```

脚本默认日志路径写死为 `D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt`，与实际常用路径 `D:\MMTCacheFolder\...` 不同，**必须用 `-Log` 显式指定**。该脚本依赖 `rg`（ripgrep）。

---

## 7. 测试

### 7.1 CTest 注册项

顶层 `include(CTest)` 打开 `BUILD_TESTING`。DX11 子工程在 `BUILD_TESTING=ON` 时注册以下测试：

| 测试名 | 命令 | 超时 |
| --- | --- | --- |
| `dx11_legacy_version_regression` | `dx11_legacy_version_regression` | 默认 |
| `dx11_version_generator_regression` | `scripts/test-dx11-version.py --source-root <root> --work-dir build/version-generator-regression` | 默认；**仅当同时找到 Python3 与 Git 时注册** |
| `dx11_upstream_regression` | `dx11_upstream_regression` | 60 秒 |
| `dx11_overlay_text_regression` | `dx11_upstream_regression --overlay-text` | 默认 |
| `dx11_texture_match_regression` | `dx11_upstream_regression --texture-match` | 默认 |
| `dx11_texture_match_hooked_regression` | `dx11_upstream_regression --texture-match-hooked` | 默认 |
| `dx11_proxy_smoke` | `dx11_proxy_smoke <DirectX11.dll 路径> <当前二进制目录>` | 30 秒 |

`dx11_upstream_regression` 的做法是**复用生产源码**：它取 `DirectX11` 目标的 `SOURCES` 属性，剔除 `.def` 与 `DLLMainHook.cpp`，把需要观测私有状态的实现单元换成对应的 `tests/*_audit_regression.cpp`（通过 `OBJECT_DEPENDS` 保证生产文件变更会触发重编），再挂上 117 个 `tests/` 回归源文件。该目标复用 `DirectX11` 的 include 路径、链接库与预处理定义（`$<TARGET_GENEX_EVAL:...>`）。因此它覆盖的是真实解析/全局/资源生命周期，而不是用 mock 替换生产依赖。

配套的宿主可执行文件：

- `dx11_loader_test_host` — 由 [`模拟环境/src/dx11_sim_host.cpp`](../模拟环境/src/dx11_sim_host.cpp) 构建，用于在隔离目录中真实加载 `d3d11.dll`。
- `dx11_proxy_smoke` — 校验代理导出与 Hook 路由。
- `dx11_legacy_version_regression` — 以 `MIGOTO_DX=12` 编译，断言共享代码 [`src/common/version.h`](../src/common/version.h) 的版本标识仍是 `1.3.16` / 产品名 `3Dmigoto`，即 DX11 的生成版本头不会渗进非 DX11 代码路径。

### 7.2 DX12 审计测试

`DX12_AUDIT_TESTS=ON`（且 `BUILD_TESTING=ON`）时额外构建 `dx12_shader_model_audit`，注册为 `dx12_shader_model_audit --audit-item F-201b`，超时 60 秒。它只解析着色器模型容器，不需要 D3D12 设备。

> 日常构建脚本只构建 `DirectX12`（或 `DirectX11 DirectX12 D3DCompiler_46`），**不会**构建这些测试可执行文件。跑测试前需要单独构建测试目标。

### 7.3 Python 脚本

| 脚本 | 作用 |
| --- | --- |
| [`scripts/test-dx11-version.py`](../scripts/test-dx11-version.py) | 在隔离的临时 Git 仓库里驱动真实的 CMake 生成器，验证版本生成规则（tag、dirty、祖先关系、格式校验） |
| [`scripts/test-dx11-version-reader.py`](../scripts/test-dx11-version-reader.py) | 在有界共享冲突下验证版本读取器 |
| [`scripts/test-dx11-pool-flags.py`](../scripts/test-dx11-pool-flags.py) | 针对生产标志传播方法的 CPU 回归（在 x64 MSVC 开发者命令行中运行） |
| [`scripts/test-dx11-resource-properties.py`](../scripts/test-dx11-resource-properties.py) | 用 WARP 真实资源验证生产 getter |
| [`scripts/check-dx11-upstream.py`](../scripts/check-dx11-upstream.py) | 拉取 DX11 上游并报告基线之后的每个提交，输出 `build/upstream/dx11-check.json`；**只报告，不改生产代码，也不推进同步状态** |

`check-dx11-upstream.py` 首次运行会在 `build/upstream/XXMI-Libs-Package` 执行 `git clone --no-checkout`，随后 `fetch` 并用 `merge-base --is-ancestor` 校验基线仍是上游祖先。该脚本需要外网。

### 7.4 怎样跑

```powershell
# 用 CMakePresets 的 x64 Release 配置生成测试目标
cmake --preset release
cmake --build --preset release --target dx11_upstream_regression dx11_proxy_smoke dx11_loader_test_host

# 运行
ctest --test-dir build/x64/release --output-on-failure
```

> 仓库脚本不提供"一键运行 CTest"的入口；上面的目录名来自 `CMakePresets.json`（`build/x64/release`）。仓库内部历史文档（如 [`DX11-Upstream-Sync.md`](DX11-Upstream-Sync.md)）使用的是 `build/dx11-upstream-en` 等自建目录，`usage/dx11/troubleshooting.md` 里写的 `build/dx11-dump-validation` 不由当前任何脚本生成——见[待确认事项](#11-待确认事项)。DLL 级宿主测试另有入口，见 7.5。

### 7.5 模拟环境（DLL 级宿主测试）

[`模拟环境/`](../模拟环境/) 是 DX11 的**真实 DLL 宿主**测试环境：它编译出独立的宿主可执行文件，把候选 `d3d11.dll` 真正加载进另一个进程再观察行为，而不是在同进程里调用内部函数。

| 脚本 | 作用 |
| --- | --- |
| [`模拟环境/scripts/build.ps1`](../模拟环境/scripts/build.ps1) | 构建宿主工具，并记录 `build/host-build-info.json`（宿主源文件与宿主可执行文件的 SHA256） |
| [`模拟环境/scripts/run-tests.ps1`](../模拟环境/scripts/run-tests.ps1) | 针对指定候选 DLL 跑宿主用例；`-CandidateDll` **必填**，可选 `-BaselineDll` 做对比 |
| [`模拟环境/scripts/run-full.ps1`](../模拟环境/scripts/run-full.ps1) | 跑完整回归矩阵（special + business），按 item id（`F-\d{3}` / `P-\d{2}`）选择 |
| [`模拟环境/scripts/self-test.ps1`](../模拟环境/scripts/self-test.ps1) | 环境自检 |
| [`模拟环境/scripts/process.ps1`](../模拟环境/scripts/process.ps1) | 被上面脚本 dot-source 的进程管理公共函数 |
| [`模拟环境/scripts/run-components.ps1`](../模拟环境/scripts/run-components.ps1) | 分量级回归 |
| [`模拟环境/scripts/run-dxgi-faults.ps1`](../模拟环境/scripts/run-dxgi-faults.ps1) | DXGI 故障注入用例 |

使用 x64 PowerShell 7。先运行 `模拟环境/scripts/build.ps1` 生成 `模拟环境/build/host-build-info.json`。如果宿主源码或可执行文件的 SHA256 发生变化，按脚本提示重新构建。

- `run-tests.ps1` 要求 `-CandidateDll` 指向现有 DLL，并使用 `模拟环境/build/tools/` 下的 `dx11_sim_host.exe`、`dx11_existing_smoke.exe`、`dx11_fault_provider.dll`。
- 运行 `run-full.ps1` 前，从仓库根目录执行 `./模拟环境/scripts/build.ps1 -BuildCandidate -BuildBusiness`，生成 `模拟环境/build/candidate/` 和 `business-build-info.json`。需要 DX12 兼容用例时追加 `-BuildDx12Compatibility`。

`模拟环境/fixtures/`（INI 夹具）与 `模拟环境/validation/`（逐项验证记录）是这些用例的数据来源。

> 本轮未执行这些宿主测试；上表只是脚本用途与前置条件的说明。

### 7.6 证据边界

- CTest 覆盖的是**进程内**行为：INI 解析、命令列表、资源生命周期、WARP 上的真实 D3D11 调用、代理加载与导出。
- CTest **不覆盖**：目标游戏内的实际画面与帧率、其他 Hook 链共存、内核驱动与全屏/HDR 兼容性、真实游戏的 Hash 与资源内容。这些必须在目标游戏中验证。
- 产物路径已用 x64 MSVC/Ninja 配置复核（见 1.2）。本轮未编译 DLL、运行 CTest 或启动游戏。

---

## 8. 文档站构建与发布

### 8.1 站点构成

- 站点在 [`usage/`](../usage/)，使用 VitePress 2（`vitepress 2.0.0-alpha.21`），依赖由 **Bun** 管理（`packageManager: bun@1.4.3`）。
- 站点使用**自定义主题**，其源码在 `usage/.vitepress/theme/`：`index.ts` 通过 `import DefaultTheme from 'vitepress/theme'` 与 `extends: DefaultTheme` **继承 VitePress 的 DefaultTheme**，再在默认 `Layout` 的 `home-hero-before` / `doc-before` 插槽插入自有的 `DocumentationHome.vue` 与 `DocumentationBreadcrumb.vue`，并附带一份 `style.css`。
- 页面：`usage/index.md`、`usage/dx11/*`、`usage/dx12/*`、`usage/guide/index.md`。`usage/README.md` 是维护说明，被 `srcExclude` 排除，不发布为站点页面。
- 导航与侧边栏在 [`usage/.vitepress/config.ts`](../usage/.vitepress/config.ts)；新增页面后需在那里补导航/侧边栏条目。

### 8.2 本地构建

```powershell
# 仓库根目录
.\scripts\build-docs.ps1 -BasePath /ProjectBunny/

# 或在 usage/ 目录直接用 Bun
bun install --frozen-lockfile
bun run docs:build
bun run docs:preview
```

[`scripts/build-docs.ps1`](../scripts/build-docs.ps1)：

- 必须有 `bun`，否则抛 `Bun is required to build the documentation.`；
- `-BasePath` 必须是绝对 URL 路径（形如 `/ProjectBunny/`），否则抛错；
- 内部把 `VITEPRESS_BASE` 设为 `<BasePath>/`，执行 `bun install --frozen-lockfile` 与 `bun run docs:build`，退出时恢复原环境变量；
- 产物在 `usage/.vitepress/dist`。

### 8.3 CI

[`.github/workflows/pages.yml`](../.github/workflows/pages.yml)：

| 项 | 值 |
| --- | --- |
| 触发 | **只有 `workflow_dispatch`**；commit、push、PR 都不触发 |
| 运行器 | `ubuntu-latest`，单 job 构建 + 单 job 部署，各 10 分钟超时 |
| 工具链 | `actions/setup-node@v4`（Node 22）+ `oven-sh/setup-bun@v2`（版本取自 `usage/package.json`） |
| 子路径 | `actions/configure-pages@v5` 读取 Pages 设置的 `base_path` |
| 构建 | `shell: pwsh` 执行 `./scripts/build-docs.ps1 -BasePath '${{ steps.pages.outputs.base_path }}/'` |
| 上传 | `actions/upload-pages-artifact@v3`，路径 `usage/.vitepress/dist` |
| 部署 | `actions/deploy-pages@v5`，环境 `github-pages`，`concurrency` 组 `pages`（`cancel-in-progress: false`） |

> **CI 只构建文档站，不构建任何 DLL。** 仓库内没有编译 DLL 的 CI workflow；DLL 的构建与发布都在本地 Windows 上完成。

### 8.4 发布

```powershell
# 仓库根目录；需已登录的 GitHub CLI
.\scripts\publish-docs.ps1
```

[`scripts/publish-docs.ps1`](../scripts/publish-docs.ps1) 只做一件事：`gh workflow run pages.yml --repo <Repository> --ref <Ref>`（默认 `StarBobis/ProjectBunny`、`main`）。它**不提交、不推送、不上传本地文件**；本地未提交的改动不会被发布，必须先提交并推送。

也可以直接在 GitHub Actions 的 **Publish documentation → Run workflow** 手动触发。GitHub Pages 的 Source 需保持 **GitHub Actions**。

---

## 9. 平台矩阵与限制

| 维度 | 支持 | 依据 / 限制 |
| --- | --- | --- |
| 操作系统 | Windows（构建与运行） | 代码依赖 Win32 API、`d3d11.lib`/`dxgi.lib`/`d3dcompiler.lib`、`XINPUT9_1_0.lib`、DirectXTK |
| 架构 | x64（发布与日常部署）、Win32（可选构建） | `FindNektra.cmake` 与 pcre2 都提供 32/64 位库；但发布脚本固定 `Release`/`x64` |
| 编译器 | MSVC（VS 2022/18，需 C++ x64 工具） | 所有编译选项都带 `if(MSVC)` 守卫，非 MSVC 得不到 `/W3`、`/EHsc` 等 |
| 生成器 | Ninja（脚本固定）、Visual Studio 17 2022（预设） | `CMakePresets.json` 提供 MSBuild 预设 |
| 运行时库 | 静态 CRT（`/MT`） | 部署目录无需附带 VC++ 运行库 |
| DX11 | C++17；`MIGOTO_DX=11` | 保持上游 XXMI 兼容面 |
| DX12 | C++20；`MIGOTO_DX=12` | `MIGOTO_DX` 让共享代码按 API 分支 |
| DXGI | 包装器存在，但**不在发布包内** | 发布脚本的 ship 列表没有它 |
| 外部 Loader | `Run.exe` 由 SSMT5 维护提供 | ProjectBunny 不保留其源码、构建目标或发布副本；开发部署只更新 DLL，不覆盖目标目录已有的加载器 |

> **产物目录由脚本决定**：构建产物先留在各自的构建树里（`build/cmake-<platform>-<config>/src/<Target>/`），再由 `build-cmake.ps1` 复制到 `x64/<Config>`（x64）或 `x32/<Config>`（Win32）。两个平台的构建目录与目标目录都不同，不会互相覆盖。

---

## 10. 常见失败路径速查

| 症状 | 优先检查 |
| --- | --- |
| 找不到 CMake/Ninja | `.tools` 目录、`PATH`、VS 是否装了 C++ 工作负载 |
| 找不到 vcvars | VS 是否安装"使用 C++ 的桌面开发"工作负载 |
| 配置阶段莫名失败 | 脚本会自动重试一次；仍失败看是否有杀毒软件占用构建目录 |
| `d3d12.dll` 没被部署 | `build_config.json` 的 `GameId` 是否存在、`CopyToTargetDirectory` 是否为真、目标目录是否存在 |
| 部署后旧日志不见了 | 轮转用的是 `Move-Item` **改名**：原名文件会按设计消失，内容保留在 `d3d12_log.before_<时间戳>.txt`（`.jsonl`/`.hook.log` 同理）；只有轮转本身失败时才原地保留并打印警告 |
| 找不到 `d3d12_log.txt` | 普通 Release 不写日志；需要 TIMING 构建（`build_release_timing_x64.ps1`） |
| 崩溃报告没有函数名 | TIMING 部署缺少 `d3d12.pdb`；符号服务器被 dbghelp 守卫关闭 |
| `[CustomShaderX]` 不绘制 | 目标目录缺 `dxcompiler.dll`/`dxil.dll`；运行时日志查 `no SM6 compiler` 或 `cached compile failure`（源码见 [`DX12CustomShaderCompile.h`](../src/DirectX12/ModRuntime/DX12CustomShaderCompile.h)）。缺 `dxcompiler.dll` 时仍可能由 `d3dcompiler_47` 的 SM6 路径补上 |
| 改了 `d3dx.ini` 没生效 | DX11 读 DLL 目录；DX12 优先读进程工作目录。两处同名文件时以工作目录为准 |
| `ctest` 找不到测试 | 构建脚本不构建测试目标；需先 `cmake --build ... --target dx11_upstream_regression` |
| 文档站链接 404 | 站点子路径由 Pages 的 `base_path` 决定；本地复现用 `build-docs.ps1 -BasePath '<base_path>/'` |

---

## 11. 待确认事项

以下条目仍需通过编译、上游记录或目标游戏验证：

1. **`build/dx11-dump-validation` 的来源**。[`usage/dx11/troubleshooting.md`](../usage/dx11/troubleshooting.md) 建议用该目录跑 CTest，但仓库内没有任何脚本生成它；`build_release_dx11_x64.ps1` 用的是 `build\dx11-release-x64`，`build-cmake.ps1` 用的是 `build\cmake-x64-release`。需要确认是历史目录名还是应改文档。
2. **`dxgi.dll` 的定位**。目标存在、导出面完整，但既不在发布包内，也不被任何构建脚本构建。需确认它是保留的历史兼容件，还是应该进入发布流程。
3. **`scripts/analyze-dx12-log.ps1` 的默认日志路径**。默认值 `D:\SSMTCacheFolder\3Dmigoto\ZZMIDX12\d3d12_log.txt` 与仓库常用的 `D:\MMTCacheFolder\3Dmigoto\ZZMIDX12` 不一致，需确认是笔误还是两套环境。
4. **`d3d12_hook.log`**。构建脚本会轮转它，但仓库内没有产生该文件的代码路径，需确认它属于哪条链路（或已废弃）。
5. **`ShaderDumpDX12`**。部署脚本会删除目标目录下的 `ShaderDumpDX12`，但当前 DX12 源码的着色器抓取写的是 `ShaderFixes`；需确认 `ShaderDumpDX12` 是历史遗留名还是仍有别的组件在写。
6. **Win32 构建的完整度**。脚本、库选择与预设都支持 Win32，但发布链路固定 x64；本轮只做过 x64 的配置验证（未编译），未做过 Win32 配置或构建验证。
7. **DX12 帧分析目录内的 `log.jsonl`** 与 `d3d12_log.jsonl` 是不同产物：前者由 F8 抓帧生成，后者被部署脚本轮转。两者同名不同源，需确认轮转规则是否需要区分。
8. **顶层 `CMakeLists.txt` 的输出目录变量未生效**。`CMAKE_{RUNTIME,LIBRARY,ARCHIVE}_OUTPUT_DIRECTORY_Debug/Release` 用混合大小写后缀，CMake 按大写后缀查表，因此产物留在构建树。是否把后缀改成大写（让产物直接进 `x64/<Config>`）、还是删掉这三组变量，需要确认；本轮只做了配置验证，没有编译。

---

## 12. 相关文档

| 文档 | 内容 |
| --- | --- |
| [Release-Package.md](Release-Package.md) | 双 DX x64 Release 发布流程、版本递增、发布包布局 |
| [`../usage/README.md`](../usage/README.md) | 文档站开发与维护说明、内容核对来源 |
| [`../usage/dx11/`](../usage/dx11/index.md) | DX11 使用者文档（配置、Mod、按键、抓帧、排错） |
| [`../usage/dx12/`](../usage/dx12/index.md) | DX12 使用者文档 |
| [`ini-language-extensions/README.md`](ini-language-extensions/README.md) | INI 语言扩展参考 |
| [`../AGENTS.md`](../AGENTS.md) | 仓库级工程约定（DX11 上游同步、DX12 规范、构建与日志约定） |
| [`DX11-Upstream-Sync.md`](DX11-Upstream-Sync.md) | DX11 上游同步过程与逐提交状态 |
