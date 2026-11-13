# DX11 untracked TextureOverride review

## Reviewed upstream changes

- `48f3b6db03814bac65b0ac990e0a26446c3bd73f`: documents that whole-resource exact candidates must not be reused for a vertex/index buffer region. Descriptor fuzzy candidates remain reusable for both regions.
- `c1378828238f165cec8efbaf78cab985373e7bbd`: removes uncached runtime descriptor/hash fallbacks. Ordinary and fuzzy candidate lookup for a resource absent from `G->mResources` appends no matches. Existing output entries are preserved. This deliberately changes the preceding revision's native/custom/backbuffer/shared-resource fallback behavior.
- `fbd5f9d48c0d8066b9412102c93203050eaffe79` and `e414c5feb37c2bc9457ebea99ba08fbef1da4a5d`: inspected complete combined and remerge diffs; neither contains extra merge-resolution hunks.

## Local adaptation

Only the old public `find_texture_overrides_for_resource_by_hash` and `find_texture_overrides_for_resource_desc` APIs and their uncached call paths were removed. The creation-time descriptor template and explicit region exact `find_texture_overrides_for_resource_by_hash_from_fuzzy_matches` remain. Tracked hash zero and UINT32_MAX retain separate validity state. Candidate reads keep their COM ownership and G-before-Resources locking; dynamic-resolution reevaluation, L3 synchronization/invalidation, and consumer lifetime scopes remain.

The explicit region exact path is independent of ordinary/fuzzy candidates, as upstream c137 leaves it. A live buffer whose tracker was detached can still match a previously warmed L3 region exact hash; its descriptor fuzzy candidates disappear with its handle. This change does not add a new tracked-handle gate to region exact matching.

The CommandList comment adaptation is isolated in `build/upstream/no-handle-port/no-handle.patch`; no real CommandList file was edited by this task. It passes `git apply --check` against the current tree. Searches find no remaining calls or declarations of the two deleted APIs; the distinct region exact helper remains referenced.

## Added production-path checks (not yet executed for this revision)

Integration update: the root agent subsequently built the full production DLL/regression executable and ran the complete CTest suite successfully (3/3), including these new checks. Evidence: `build/upstream/range-batch-final-build.log`, `range-batch-final-ctest.log` and the CTest LastTest log. The CommandList comment patch is integrated. The authoring-time notes below describe the state before that integrated run; target-game/backbuffer/shared-resource coverage limitations still apply.

`test_dx11_texture_override_cache_regression()` now checks native untracked misses, output-vector preservation and profiling miss/scan counts; actual tracker removal via SetPrivateDataInterface and resource destruction; tracked zero/MAX hash behavior; a custom target referring to an untracked native resource; and two real WARP buffer regions with distinct exact hashes and one shared descriptor fuzzy candidate. The region test also pins warm-L3 exact behavior after tracker detach. Existing filter, CheckTO pre/post lifetime handshake, UAV view-window, different-size CopyResource readback, L3 concurrency, reload and profiling regressions are retained.

No build or test was run for this revision by this agent. The prior batch passed CTest, but that does not validate these new changes. Native custom-resource referencing is exercised by the new test source; actual swap-chain backbuffer and OpenSharedResource creation are not separately exercised. Their ordinary/fuzzy lookup follows the same absent-handle branch. Target-game behavior remains unverified.
