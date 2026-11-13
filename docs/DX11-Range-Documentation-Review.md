# DX11 range and pool documentation review

This review covers documentation only. No production code, tests, CMake or synchronization ledger was changed by this documentation task.

## Per-commit evidence

The complete local upstream diffs were read from `build/upstream/XXMI-Libs-Package` and saved under `build/upstream/range-docs/`.

| Commit | Retained final effect and adaptation |
| --- | --- |
| 595de8fadcc76c47445f4ccb5b86cd7fc25a5d31 | New slot-indexing reference, dynamic slots, inclusive physical ranges, inherited bounds, option/dump restrictions. Final range behavior coordinated with the implementation owner rather than preserving the first ref-only description. |
| 5a852ccac15325dd31e3c4ddf7a5505ed3a6340d | All eleven pool options, variable pools, Static indexing, metadata and per-object example. Removed fixed memory/timing estimates. Mixed state and filtered reset retain local behavior. |
| 7800bd060b118dee3991566821fc3d92473299e3 | Whole-pool operations and content/spatial hash reference. Proxy root capture, cycle rejection and detach documented. Spatial offsets corrected to actual byte API; hash collisions and CPU tracking requirements explicit. |
| 83bcd3f2a5bfda09399656a5ca1e5b14b2d9f4c6 | Store warning retained, without claiming every readback waits for all queued work or assigning an unmeasured FPS cost. |
| 893e3072bf5b7841151d6007c6bb252bed23a768 | Examples index and five new retained examples. All introduced variables declared; hashes are legal illustrative hex. External shaders/assets are named or consumer hooks explicitly empty. Slot capture restores its mask slot too. Readback has an actual per-frame interval guard. |
| 278c4e63b7f82bfe4c024693aaf3bef996d35bc9 | Accessory-mesh and material-borrowing pages/links excluded from final tree. |

## Production cross-checks

* Input's missing-key default remains on; existing logging docs retain absent/empty/invalid legacy fallback. The older historical synchronization note remains identified as historical and is superseded by its later default-on entry.
* Binary literal documentation retains 64-digit parser versus float32 exact-integer limits. Pointer/content/spatial hashes are not unique object identifiers.
* Current `GetPoolElementLastFrame` returns 0 for a legal missing-template lookup, -1 for invalid/unwritten entries without a timestamp; the existing metadata description is retained accordingly. Frame 0 is not generally an invalid sentinel.
* `GetResourceSpatialHash` passes offsets directly as bytes to `TryGetSpatialHash`; coordinates at consecutive float32 locations therefore use 0/4/8. `HashRegion` uses byte offsets relative to supported pipeline binding regions and truncates a size at the available snapshot end. Custom hashes need a captured CPU snapshot; `[Rendering] track_region_hashes` is the actual configuration key.
* Store indexes the absolute backing buffer in float32 elements, independent of CB/SRV view offsets. Each command performs its own copy/Map; staging allocation reuse does not combine reads.
* Region byte offsets are absolute. CB references require 256-byte aligned offset/length and at most 64 KiB; a legal reference window may extend physical storage, while copies must stay physically bounded. Compact range syntax does not prove CB copy grouping or per-slot API behavior.
* Range implementation owner confirmed dynamic slots, finite ordered inclusive bounds, physical pool Ring wrapping, bound inheritance restrictions, ordinary options/default directions, skipped unless_null fetch behavior and explicit-only dump bounds. At documentation delivery this owner had not reported its final integrated build; documentation checking is independent of that validation.

## Actual validation

`python build/upstream/check-ini-docs.py` (no arguments, from repository root) passed: **36 files, 79 relative links, zero errors**. The checker checks exact-case local paths, local anchors and Markdown fence pairing and writes `build/upstream/ini-docs-check.json`. It also normalizes local `.md/#` links and line endings within the owned tree.

This is not an INI parser/game execution test, an external-link check, or a performance measurement. Complete new examples have not been executed as target-game configurations. External shader compatibility, game draw hashes/layouts, timing and visual behavior remain integration work. No new C++ build or CTest execution was started by this documentation task.

## Independent production review after initial documentation freeze

Read-only source review traced range acceptance through `AcceptParsedTarget`, the explicit copy/dump opt-in callers, per-invocation 128-entry ComPtr binding snapshots, exclusive cache leases, exception-prefix publication, physical Ring selection, and CB unless-null window preservation. No C++ build or injected exception test was run in this review. Dynamic-source failure was traced through `ResolveSlot`/`GetResource` to ordinary null-source assignment: it can clear a destination unless `unless_null` is used. An invalid destination leaves its pipeline binding unchanged. The slot-indexing reference now states this distinction from whole-range validation failure.

One actionable issue was reported to the implementation owner and root: the fast-reference Fetch branch directly calls `SetResource`, bypassing the scalar reference path's `SetHandleInfo`, `source_stride`, explicit metadata overrides and copy-limit responsibilities. Because `SetResource` does not replace the CPU snapshot, a slot previously capturing tracked buffer A then range-referencing B can retain A's hash snapshot. The upstream direct-SetResource fast path does not remove this local tracking invariant. This is a source-derived finding, not an executed reproducer; final fix/test evidence belongs to the owning implementation's integration record.
