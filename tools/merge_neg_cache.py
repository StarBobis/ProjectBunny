#!/usr/bin/env python3
"""Merge PSO negative cache layers: 2 caches -> 1 unified fast-reject cache."""
import re

path = r'C:\Users\Administrator\Desktop\ProjectBunny\src\DirectX12\ia\DX12IaReplacementExecutor.hpp'

with open(path, 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

# Count original patterns
print(f"Before: s_psoIaNegativeCache mentions: {content.count('s_psoIaNegativeCache')}")
print(f"Before: s_psoPreFilterCache mentions: {content.count('s_psoPreFilterCache')}")

# 1. Replace dual cache declarations with unified
content = content.replace(
    '\t// PSO and IA negative cache for no-match results.\n'
    '\t// The cache persists across frames and is cleared only on mod reload.\n'
    '\tinline static std::unordered_set<UINT64> s_psoIaNegativeCache;\n'
    '\tinline static SRWLOCK s_psoIaNegativeCacheLock = SRWLOCK_INIT;\n'
    '\n'
    '\t// PSO-level pre-filter: keyed by (PSO, targetPlan, iaViewFingerprint).\n'
    '\t// Checked BEFORE IaHash build; populated when full neg cache misses.\n'
    '\tinline static std::unordered_set<UINT64> s_psoPreFilterCache;\n'
    '\tinline static SRWLOCK s_psoPreFilterCacheLock = SRWLOCK_INIT;',
    '\t// Unified PSO+IA fast-reject cache: single lock, single lookup.\n'
    '\t// Key = Mix(PSO, targetPlan fingerprint, IA view fingerprint).\n'
    '\t// Replaces the previous two-tier pre-filter + negative cache.\n'
    '\t// Persists across frames, cleared on mod reload.\n'
    '\tinline static std::unordered_set<UINT64> s_psoDrawFastRejectCache;\n'
    '\tinline static SRWLOCK s_psoDrawFastRejectCacheLock = SRWLOCK_INIT;'
)

# 2. Replace TryFindPsoIaNegativeCache -> TryFindPsoDrawFastReject
content = content.replace(
    's_psoIaNegativeCache.find(key) != s_psoIaNegativeCache.end()',
    's_psoDrawFastRejectCache.find(key) != s_psoDrawFastRejectCache.end()'
)
content = content.replace(
    'TryFindPsoIaNegativeCache',
    'TryFindPsoDrawFastReject'
)

# 3. Replace StorePsoIaNegativeCache -> StorePsoDrawFastReject
content = content.replace(
    's_psoIaNegativeCache.insert(key)',
    's_psoDrawFastRejectCache.insert(key)'
)
content = content.replace(
    'StorePsoIaNegativeCache',
    'StorePsoDrawFastReject'
)

# 4. Replace lock names in these functions (use unified lock)
content = content.replace(
    's_psoIaNegativeCacheLock',
    's_psoDrawFastRejectCacheLock'
)

# 5. Remove MakePsoPreFilterKey, TryFindPsoPreFilterCache, StorePsoPreFilterCache
# These functions are still referenced. We need to handle them carefully.
# Replace TryFindPsoPreFilterCache -> TryFindPsoDrawFastReject
content = content.replace(
    'TryFindPsoPreFilterCache',
    'TryFindPsoDrawFastReject'
)
content = content.replace(
    'StorePsoPreFilterCache',
    'StorePsoDrawFastReject'
)
content = content.replace(
    's_psoPreFilterCache.find(',
    's_psoDrawFastRejectCache.find('
)
content = content.replace(
    's_psoPreFilterCache.insert(',
    's_psoDrawFastRejectCache.insert('
)
content = content.replace(
    's_psoPreFilterCache.clear()',
    's_psoDrawFastRejectCache.clear()'
)
content = content.replace(
    's_psoPreFilterCacheLock',
    's_psoDrawFastRejectCacheLock'
)

# 6. Replace ClearPsoIaNegativeCache with ClearPsoDrawFastRejectCache
content = content.replace(
    'ClearPsoIaNegativeCache',
    'ClearPsoDrawFastRejectCache'
)

print(f"After: s_psoIaNegativeCache mentions: {content.count('s_psoIaNegativeCache')}")
print(f"After: s_psoPreFilterCache mentions: {content.count('s_psoPreFilterCache')}")
print(f"After: s_psoDrawFastRejectCache mentions: {content.count('s_psoDrawFastRejectCache')}")

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
print('Done!')
