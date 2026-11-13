# Documentation Synchronization Notes

This new documentation tree uses the upstream final structure at `9e75c1404964da42ab6e5028442169ef4bbe0d20`, with local corrections and later synchronized additions. It does not identify the whole ProjectBunny DX11 backend as an upstream release or replace the per-commit synchronization ledger.

| Upstream commit | Effect retained here |
| --- | --- |
| [bb6dc33c](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/bb6dc33c8beeb15468c84d11548d289793c27d80) | Expression, resource metadata, pool references and examples. |
| [ba1fb5d6](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/ba1fb5d6009c59a183b4f02f8e69bfacc8e016ba) | Exact `README.md` case in topic links. |
| [471bed39](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/471bed39ef1675738d684672424c39d7e5a9d0e0) | Resource metadata lives under `resources/`; links follow that location. |
| [0df206b6](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/0df206b668b0b3ea7cda8fcc116a3baa029c32e2) | Command lists, Store, functions, literals, layout overrides, runtime parameters, variables and regions. |
| [9e75c140](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/9e75c1404964da42ab6e5028442169ef4bbe0d20) | Corrected input-layout code fence and initialization spelling. |
| [6c9c2ac0](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/6c9c2ac032a4002f78f3e39cf2893eb212fd177d) | TIME/FRAME_TIME/FPS examples and cross-namespace callback restrictions, with sampling/float-precision distinctions. |
| [3102185e](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/3102185e7adaa2069397316f8d76df97dc173ff8) | Input reference follows the new `[Input]` options and mod/tool disable scopes. It records the parser's missing-input default of off, template's explicit on, reload behavior and local source-provenance safeguards. |
| [a756fdec](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/a756fdec80100b5d8462a37f34a808f6da2b5f48) | Logging initialization and compatibility: recognized levels take priority, explicit disabled suppresses legacy settings, and an invalid level falls back to legacy calls/debug. |
| [2806c7a5](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/2806c7a507a09a34f0da88a9e58b6f07e6b7e8fb) | DXGI format literals and format/width/height reference, Ring default, random float/zero clarification and resource options. The upstream example's internal `override_color_space` name is corrected to the actual `color_space` INI key. |
| [45a8ff4e](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/45a8ff4e51c08b97fd2c4eaa3d29616e77ac762a), [4e384d87](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/4e384d877be14bfd49f9723002417c131e55942e) | Correct the earlier missing-input default to on; template uses numeric 1. The earlier310 row records its historical default. |
| [015d3394](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/015d33945e12b2d355c2735129083cdcd5a5c1b9) | Rename unknown-setting cleanup to clear_unknown_settings and compare names plus values after Constants; preserve local file ownership and failure handling. |
| [96aee720](https://github.com/SpectrumQT/XXMI-Libs-Package/commit/96aee72048d1ebe5a81aca51e2f5bbdf39c31fa4) | BindFlags/Array/Mips resource getters and D3D11_BIND literals, with corrected local links and physical-resource versus view distinctions. |

Local corrections distinguish the 64-bit binary parser limit from float precision; explain possible hash/random collisions; remove unmeasured timing and memory estimates; describe absolute byte Region offsets, copy bounds and CB reference-window restrictions; retain the local failure and persistence rules; and describe original-shader declaration queries and the game/custom layout-cache distinction. Links, example hashes and Markdown fences are corrected without preserving intermediate upstream mistakes.

Input classification records the actual first source of each section. Direct includes stay ordinary/tool bindings, while actual recursive loads use full Windows-normalized paths and directory boundaries; namespace aliases cannot change their classification. Logging keeps DX11's verbosity independent of DX12's logging interface. Resource color-space options describe format interpretation and DDS/WIC differences without promising pixel conversion.

The current source exposes `cb_mask` and `srv_mask` names without mask evaluation. They are not documented as usable masks. See [Shader Declarations](resources/shader-declarations.md).

## Validation boundaries

The synchronization task checks relative links with exact case and checks Markdown fence pairing. Those checks do not execute INI examples or verify external websites.

The standalone [Binary Flags](examples/binary-flags.md) snippet needs a command-list context. The [Pool-Based Tracker](examples/pool-based-tracker.md) needs actual game shader/resource hashes and a suitable compute output. Neither complete example has been run against a target game as part of this documentation import. Other snippets also require their surrounding declarations, resources or pipeline state; they are not claimed to be independent runnable files.

Local production regressions cover selected parser, pool, copy/reference, WARP resource, layout and shader-declaration behavior. Consult the repository synchronization report for the commands actually run and their results. Full target-game validation remains pending.
