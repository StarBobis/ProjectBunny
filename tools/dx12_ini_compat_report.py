#!/usr/bin/env python3
"""Report DX12 INI compatibility coverage for 3DMigoto-style mod INI files."""

from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


COMMAND_SECTIONS = ("commandlist", "builtincommandlist")
SPECIAL_COMMAND_SECTIONS = {"present", "constants"}
TEXTURE_OVERRIDE = "textureoverride"
SHADER_OVERRIDE = "shaderoverride"
SHADER_REGEX = "shaderregex"
RESOURCE = "resource"
KEY = "key"
CUSTOM_SHADER = "customshader"


@dataclass
class Entry:
    line_no: int
    section: str
    raw: str
    key: str
    value: str
    has_equals: bool


def raw_line_is_control_flow(raw: str) -> bool:
    lower = raw.strip().lower()
    return (
        lower == "else"
        or lower == "endif"
        or lower.startswith("if ")
        or lower.startswith("elif ")
        or lower.startswith("else if ")
    )


def read_text(path: Path) -> str:
    for encoding in ("utf-8-sig", "utf-16", "utf-8", "gb18030"):
        try:
            return path.read_text(encoding=encoding)
        except UnicodeError:
            continue
    return path.read_text(encoding="utf-8", errors="replace")


def parse_entries(path: Path) -> list[Entry]:
    section = ""
    entries: list[Entry] = []
    for line_no, raw_line in enumerate(read_text(path).splitlines(), 1):
        raw = raw_line.strip()
        if not raw or raw.startswith(";") or raw.startswith("#"):
            continue
        if raw.startswith("[") and raw.endswith("]"):
            section = raw[1:-1].strip()
            continue
        if raw_line_is_control_flow(raw):
            entries.append(Entry(line_no, section, raw, raw, "", False))
            continue
        if "=" in raw:
            key, value = raw.split("=", 1)
            entries.append(Entry(line_no, section, raw, key.strip(), value.strip(), True))
        else:
            entries.append(Entry(line_no, section, raw, raw, "", False))
    return entries


def section_kind(section: str) -> str:
    lower = section.lower()
    if lower in SPECIAL_COMMAND_SECTIONS or lower.startswith(COMMAND_SECTIONS):
        return "command"
    for prefix, kind in (
        (TEXTURE_OVERRIDE, "texture_override"),
        (SHADER_OVERRIDE, "shader_override"),
        (SHADER_REGEX, "shader_regex"),
        (RESOURCE, "resource"),
        (KEY, "key"),
        (CUSTOM_SHADER, "custom_shader"),
    ):
        if lower.startswith(prefix):
            return kind
    return "other"


def is_control_flow(raw: str) -> bool:
    return raw_line_is_control_flow(raw)


def is_variable_declaration(text: str) -> bool:
    lower = text.lower().strip()
    return (
        lower.startswith("global $")
        or lower.startswith("global persist $")
        or lower.startswith("persist $")
    )


def is_local_variable_declaration(text: str) -> bool:
    return text.lower().strip().startswith("local $")


def is_shader_target(key: str) -> bool:
    return re.match(r"^(vs|ps|cs)-(cb|t|u)\d+$", key) is not None


def is_command_action(key: str, value: str) -> tuple[bool, str]:
    lower = key.lower().strip()
    if lower in {"run", "checktextureoverride", "handling", "draw", "drawindexed", "dispatch", "ib"}:
        return True, f"action:{lower}"
    if re.match(r"^vb\d+$", lower):
        return True, "action:vb"
    if lower in {"vs", "ps", "cs"}:
        return True, "compat:set_shader"
    if is_shader_target(lower):
        return True, "compat:set_shader_binding"
    if is_variable_declaration(lower):
        return True, "compat:variable"
    if lower.startswith("$"):
        return True, "compat:variable"
    if lower.startswith("post $"):
        return True, "compat:post_variable"
    if lower.startswith("local $"):
        return True, "compat:local_variable"
    if lower.startswith("resource"):
        return True, "compat:resource_assignment"
    if re.match(r"^[xyzw]\d+$", lower):
        return True, "compat:shader_constant"
    if lower in {"blend", "cull", "topology", "depth", "stencil"} or re.match(r"^o\d+$", lower):
        return True, "compat:render_state"
    return False, ""


def is_resource_key(key: str) -> bool:
    return key.lower().strip() in {
        "type",
        "filename",
        "file",
        "format",
        "data",
        "stride",
        "array",
        "array_size",
        "byte_width",
        "width",
    }


def is_texture_override_key(key: str, value: str) -> tuple[bool, str]:
    lower = key.lower().strip()
    if lower in {
        "hash",
        "match_vertex_count",
        "match_index_count",
        "match_instance_count",
        "match_first_vertex",
        "match_first_index",
        "match_first_instance",
        "match_priority",
        "cs_hash",
        "match_uav_bytes",
        "override_byte_stride",
        "override_vertex_count",
        "uav_byte_stride",
        "this",
    }:
        return True, f"texture:{lower}"
    if re.match(r"^(match_cs_t|match_srv)\d+_hash$", lower):
        return True, "texture:pre_skin_match"
    ok, label = is_command_action(lower, value)
    if ok:
        return True, label
    return False, ""


def is_key_key(key: str) -> tuple[bool, str]:
    lower = key.lower().strip()
    if lower in {"condition", "key", "type", "run"}:
        return True, f"key:{lower}"
    if lower.startswith("$"):
        return True, "key:variable"
    return False, ""


def classify(entry: Entry) -> tuple[str, str]:
    kind = section_kind(entry.section)
    if not entry.has_equals:
        if kind in {"command", "texture_override", "shader_override", "shader_regex", "custom_shader"} and is_control_flow(entry.raw):
            return "compat", "compat:control_flow"
        if kind == "command" and is_variable_declaration(entry.raw):
            return "compat", "compat:variable"
        if kind == "command" and is_local_variable_declaration(entry.raw):
            return "compat", "compat:local_variable"
        return "unsupported", f"{kind}:no_equals"

    key = entry.key.lower().strip()
    if kind == "resource":
        return ("supported", f"resource:{key}") if is_resource_key(key) else ("unsupported", f"resource:{key}")

    if kind == "texture_override":
        ok, label = is_texture_override_key(entry.key, entry.value)
        return ("supported" if label.startswith(("texture:", "action:")) else "compat", label) if ok else ("unsupported", f"texture:{key}")

    if kind in {"command", "shader_override", "shader_regex"}:
        if kind == "shader_override" and key == "hash":
            return "supported", "shader_override:hash"
        if kind == "shader_regex" and key in {"shader_model", "temps", "filter_index"}:
            return "supported", f"shader_regex:{key}"
        ok, label = is_command_action(entry.key, entry.value)
        return ("supported" if label.startswith("action:") else "compat", label) if ok else ("unsupported", f"{kind}:{key}")

    if kind == "custom_shader":
        ok, label = is_command_action(entry.key, entry.value)
        return (("supported" if label.startswith("action:") else "compat"), label) if ok else ("unsupported", f"custom_shader:{key}")

    if kind == "key":
        ok, label = is_key_key(entry.key)
        return ("supported", label) if ok else ("unsupported", f"key:{key}")

    return "unsupported", f"{kind}:{key}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ini", type=Path, help="Path to a 3DMigoto-style INI file")
    parser.add_argument("--examples", type=int, default=8, help="Examples per unsupported category")
    args = parser.parse_args()

    entries = parse_entries(args.ini)
    totals = Counter()
    labels = Counter()
    examples: dict[str, list[Entry]] = defaultdict(list)

    for entry in entries:
        status, label = classify(entry)
        totals[status] += 1
        labels[label] += 1
        if status == "unsupported" and len(examples[label]) < args.examples:
            examples[label].append(entry)

    total = sum(totals.values())
    print(f"INI: {args.ini}")
    print(f"Entries: {total}")
    for status in ("supported", "compat", "unsupported"):
        count = totals[status]
        pct = (count / total * 100.0) if total else 0.0
        print(f"{status:>11}: {count:4d} ({pct:5.1f}%)")

    print("\nTop categories:")
    for label, count in labels.most_common(30):
        print(f"  {count:4d}  {label}")

    print("\nUnsupported examples:")
    for label, count in labels.most_common():
        if label not in examples:
            continue
        print(f"\n[{label}] {count}")
        for entry in examples[label]:
            print(f"  {entry.line_no}: [{entry.section}] {entry.raw}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
