# Extended INI Language

This is ProjectBunny's DX11 INI reference, adapted from [XXMI-Libs-Package](https://github.com/SpectrumQT/XXMI-Libs-Package/tree/9e75c1404964da42ab6e5028442169ef4bbe0d20/docs/ini-language-extensions). ProjectBunny keeps its CMake build and DX11 C++17 implementation; DX12 has its own interface and standard.

The upstream documentation and local adaptations are tracked in [Synchronization Notes](SYNC-NOTES.md); actual build and regression results are in the [DX11 synchronization record](../DX11-Upstream-Sync.md). The examples explain syntax; they have not all been executed as complete configurations. Regression checks and WARP tests do not establish compatibility with every target game. Game validation remains incomplete.

The DX11 extensions include:

* [Commands](commands/README.md) — new commands
* [Command Lists](command-lists/README.md) — extended recursion and callback support
* [Runtime Parameters](parameters/README.md) — new values provided by the runtime
* [Expressions](expressions/README.md) — new operators and functions
* [Variables](variables/README.md) — extended variable declarations and read-only mode
* [Resources](resources/README.md) — resource metadata and buffer region support
* [Pools](pools/README.md) — indexed storage and resource management
* [Input Layouts](input-layouts/README.md) — vertex input layout overrides
* [Input Control](input/README.md) — mod/tool binding scopes and repeated HOLD events
* [DX11 Logging](logging.md) — verbosity and warning display
