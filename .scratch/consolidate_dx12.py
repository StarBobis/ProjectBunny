from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DX12 = ROOT / "src" / "DirectX12"

HEADER_OUTPUTS = [
    "DX12Foundation.hpp",
    "DX12Infrastructure.hpp",
    "DX12RuntimeInterfaces.hpp",
    "DX12FeatureInterfaces.hpp",
    "DX12Integration.hpp",
]

CPP_GROUPS = {
    "DX12ResourceTracking.cpp": [
        "DX12ResourceCollectQueue.cpp",
        "DX12ResourceTracker.cpp",
        "DX12ResourceTrackerHooks.cpp",
        "DX12ResourceTrackerQueries.cpp",
    ],
    "DX12BindingTracking.cpp": [
        "DX12CaptureLedger.cpp",
        "DX12BindingCollectQueue.cpp",
        "DX12BindingTracker.cpp",
        "DX12BindingTrackerFrameExport.cpp",
        "DX12BindingTrackerResourceExport.cpp",
    ],
    "DX12CommandLists.cpp": [
        "DX12CommandListHooks.cpp",
        "DX12CommandListRuntime.cpp",
        "DX12CommandListLifecycle.cpp",
        "DX12CommandListCommandRecorder.cpp",
        "DX12CommandListStateCapture.cpp",
    ],
    "DX12ShaderTools.cpp": [
        "DX12ShaderDump.cpp",
        "DX12ShaderHunt.cpp",
    ],
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


headers = sorted(path.name for path in DX12.glob("*.hpp"))
header_set = set(headers)
sources = {name: read(DX12 / name) for name in headers}

include_pattern = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
dependencies = {
    name: [dep for dep in include_pattern.findall(text) if dep in header_set]
    for name, text in sources.items()
}

# The existing header graph is acyclic. A dependency-first traversal preserves
# the declaration order that the original include graph provided.
ordered: list[str] = []
visiting: set[str] = set()
visited: set[str] = set()


def visit(name: str) -> None:
    if name in visited:
        return
    if name in visiting:
        raise RuntimeError(f"cyclic DirectX12 header dependency at {name}")
    visiting.add(name)
    for dependency in dependencies[name]:
        visit(dependency)
    visiting.remove(name)
    visited.add(name)
    ordered.append(name)


for header in headers:
    visit(header)

line_counts = {name: sources[name].count("\n") + 1 for name in headers}
total_lines = sum(line_counts.values())
target_lines = total_lines / len(HEADER_OUTPUTS)
header_map: dict[str, str] = {}
chunks: list[list[str]] = [[] for _ in HEADER_OUTPUTS]
chunk_index = 0
chunk_lines = 0
for index, header in enumerate(ordered):
    remaining_headers = len(ordered) - index
    remaining_chunks = len(HEADER_OUTPUTS) - chunk_index
    if (
        chunk_index + 1 < len(HEADER_OUTPUTS)
        and chunk_lines >= target_lines
        and remaining_headers >= remaining_chunks
    ):
        chunk_index += 1
        chunk_lines = 0
    chunks[chunk_index].append(header)
    header_map[header] = HEADER_OUTPUTS[chunk_index]
    chunk_lines += line_counts[header]


def strip_internal_header_lines(text: str) -> str:
    lines = []
    for line in text.splitlines():
        if re.match(r'^\s*#pragma\s+once\s*$', line):
            continue
        match = re.match(r'^\s*#include\s+"([^"]+)"\s*$', line)
        if match and match.group(1) in header_set:
            continue
        lines.append(line)
    return "\n".join(lines).strip() + "\n"


for index, (output, chunk) in enumerate(zip(HEADER_OUTPUTS, chunks)):
    parts = ["#pragma once", ""]
    if index:
        parts.extend([f'#include "{HEADER_OUTPUTS[index - 1]}"', ""])
    parts.extend([
        "// Consolidated DirectX 12 interface layer.",
        "// Sections retain their original file names in #line diagnostics.",
        "",
    ])
    for original in chunk:
        parts.extend([
            f"// ---- {original} ----",
            f'#line 1 "{original}"',
            strip_internal_header_lines(sources[original]),
        ])
    (DX12 / output).write_text("\n".join(parts), encoding="utf-8", newline="\n")


def rewrite_header_includes(text: str) -> str:
    seen_aggregates: set[str] = set()
    output: list[str] = []
    for line in text.splitlines():
        match = re.match(r'^(\s*)#include\s+"([^"]+)"\s*$', line)
        if match and match.group(2) in header_map:
            aggregate = header_map[match.group(2)]
            if aggregate in seen_aggregates:
                continue
            seen_aggregates.add(aggregate)
            output.append(f'{match.group(1)}#include "{aggregate}"')
        else:
            output.append(line)
    return "\n".join(output).rstrip() + "\n"


grouped_sources = {source for group in CPP_GROUPS.values() for source in group}
for output, group in CPP_GROUPS.items():
    parts = [
        f"// Consolidated implementation module: {output}",
        "// Original source names are retained in #line diagnostics.",
        "",
    ]
    for original in group:
        parts.extend([
            f"// ---- {original} ----",
            f'#line 1 "{original}"',
            rewrite_header_includes(read(DX12 / original)),
        ])
    (DX12 / output).write_text("\n".join(parts), encoding="utf-8", newline="\n")

for path in DX12.glob("*.cpp"):
    if path.name not in grouped_sources:
        path.write_text(rewrite_header_includes(read(path)), encoding="utf-8", newline="\n")

for name in headers:
    if name not in HEADER_OUTPUTS:
        (DX12 / name).unlink()
for name in grouped_sources:
    if name not in CPP_GROUPS:
        (DX12 / name).unlink()

print(f"Consolidated {len(headers)} headers into {len(HEADER_OUTPUTS)} layers")
print(f"Consolidated {len(grouped_sources)} implementation files into {len(CPP_GROUPS)} modules")
for output, chunk in zip(HEADER_OUTPUTS, chunks):
    print(f"  {output}: {len(chunk)} original headers")
