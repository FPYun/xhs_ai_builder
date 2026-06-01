#!/usr/bin/env python3
"""Validate the optional SD audio, skin, and action payload package."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass
from pathlib import Path


MAX_AUDIO_BYTES = 32768
SAMPLE_RATE = "22050"
FORMAT = "u8_mono_raw"
SKIN_ELEMENTS = {"any", "wood", "fire", "earth", "metal", "water"}
ACTION_SCREENS = {"idle", "wild", "bag", "battle"}
ACTION_KEYS = {"bob", "sparkle", "tilt"}
PALETTE_KEYS = {"body", "accent"}


@dataclass(frozen=True)
class Check:
    name: str
    status: str
    detail: str


def read_csv_rows(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    if not path.exists():
        return [], []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        return list(reader), list(reader.fieldnames or [])


def sd_path(root: Path, value: str) -> Path:
    return root / value.strip().lstrip("/")


def add(checks: list[Check], name: str, ok: bool, detail: str) -> None:
    checks.append(Check(name, "PASS" if ok else "FAIL", detail))


def validate_root_manifest(root: Path, checks: list[Check]) -> None:
    rows, fields = read_csv_rows(root / "manifest.csv")
    required = ["path", "size_bytes", "format", "sample_rate_hz", "purpose"]
    add(checks, "root_manifest_columns", fields == required, ",".join(fields) or "missing")
    add(checks, "root_manifest_rows", bool(rows), f"{len(rows)} rows")

    seen: set[str] = set()
    for row in rows:
        path = row.get("path", "")
        seen.add(path)
        file_path = sd_path(root, path)
        exists = file_path.exists() and file_path.is_file()
        add(checks, f"file_exists:{path}", exists, path)
        if not exists:
            continue
        actual_size = file_path.stat().st_size
        expected_size = int(row.get("size_bytes") or -1)
        add(checks, f"size:{path}", actual_size == expected_size, f"expected={expected_size} actual={actual_size}")
        if path.endswith(".raw"):
            add(checks, f"audio_format:{path}", row.get("format") == FORMAT, row.get("format", ""))
            add(checks, f"audio_rate:{path}", row.get("sample_rate_hz") == SAMPLE_RATE, row.get("sample_rate_hz", ""))
            add(checks, f"audio_size_limit:{path}", 0 < actual_size <= MAX_AUDIO_BYTES, f"{actual_size} bytes")
        elif path.endswith(".csv"):
            add(checks, f"csv_format:{path}", row.get("format") == "csv", row.get("format", ""))

    for required_path in ["/skins/manifest.csv", "/actions/manifest.csv"]:
        add(checks, f"required_addon_manifest:{required_path}", required_path in seen, required_path)


def validate_palette(path: Path, checks: list[Check]) -> None:
    rows, fields = read_csv_rows(path)
    add(checks, f"palette_columns:{path.name}", fields == ["key", "r", "g", "b"], ",".join(fields) or "missing")
    keys = {row.get("key", "") for row in rows}
    add(checks, f"palette_keys:{path.name}", PALETTE_KEYS <= keys, ",".join(sorted(keys)))
    for row in rows:
        for channel in ["r", "g", "b"]:
            try:
                value = int(row.get(channel, ""))
            except ValueError:
                value = -1
            add(checks, f"palette_rgb:{path.name}:{row.get('key')}:{channel}", 0 <= value <= 255, str(value))


def validate_skin_manifest(root: Path, checks: list[Check]) -> None:
    rows, fields = read_csv_rows(root / "skins/manifest.csv")
    add(checks, "skin_manifest_columns", fields == ["path", "type", "version", "element", "species", "variant", "description"], ",".join(fields) or "missing")
    add(checks, "skin_manifest_rows", bool(rows), f"{len(rows)} rows")
    for row in rows:
        path = row.get("path", "")
        add(checks, f"skin_type:{path}", row.get("type") == "palette", row.get("type", ""))
        add(checks, f"skin_version:{path}", row.get("version") == "1", row.get("version", ""))
        add(checks, f"skin_element:{path}", row.get("element") in SKIN_ELEMENTS, row.get("element", ""))
        file_path = sd_path(root, path)
        add(checks, f"skin_file:{path}", file_path.exists(), path)
        if file_path.exists():
            validate_palette(file_path, checks)


def validate_action_profile(path: Path, checks: list[Check]) -> None:
    rows, fields = read_csv_rows(path)
    add(checks, f"action_columns:{path.name}", fields == ["key", "value"], ",".join(fields) or "missing")
    values: dict[str, int] = {}
    for row in rows:
        key = row.get("key", "")
        try:
            values[key] = int(row.get("value", ""))
        except ValueError:
            values[key] = 999
    add(checks, f"action_keys:{path.name}", ACTION_KEYS <= set(values), ",".join(sorted(values)))
    add(checks, f"action_bob:{path.name}", -12 <= values.get("bob", 999) <= 12, str(values.get("bob")))
    add(checks, f"action_sparkle:{path.name}", 0 <= values.get("sparkle", 999) <= 8, str(values.get("sparkle")))
    add(checks, f"action_tilt:{path.name}", -3 <= values.get("tilt", 999) <= 3, str(values.get("tilt")))


def validate_action_manifest(root: Path, checks: list[Check]) -> None:
    rows, fields = read_csv_rows(root / "actions/manifest.csv")
    add(checks, "action_manifest_columns", fields == ["path", "type", "version", "screen", "element", "description"], ",".join(fields) or "missing")
    add(checks, "action_manifest_rows", bool(rows), f"{len(rows)} rows")
    for row in rows:
        path = row.get("path", "")
        add(checks, f"action_type:{path}", row.get("type") == "profile", row.get("type", ""))
        add(checks, f"action_version:{path}", row.get("version") == "1", row.get("version", ""))
        add(checks, f"action_screen:{path}", row.get("screen") in ACTION_SCREENS, row.get("screen", ""))
        file_path = sd_path(root, path)
        add(checks, f"action_file:{path}", file_path.exists(), path)
        if file_path.exists():
            validate_action_profile(file_path, checks)


def validate(root: Path) -> list[Check]:
    checks: list[Check] = []
    validate_root_manifest(root, checks)
    validate_skin_manifest(root, checks)
    validate_action_manifest(root, checks)
    return checks


def summarize(checks: list[Check]) -> str:
    return "FAIL" if any(check.status == "FAIL" for check in checks) else "PASS"


def print_text(checks: list[Check]) -> None:
    print(f"sd payload validation: {summarize(checks)}")
    for check in checks:
        print(f"{check.status:4} {check.name}: {check.detail}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, default=Path("sd_card_payload"))
    parser.add_argument("--json", action="store_true")
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    checks = validate(args.payload)
    if args.json:
        print(json.dumps({"status": summarize(checks), "checks": [check.__dict__ for check in checks]}, ensure_ascii=False, indent=2))
    else:
        print_text(checks)
    return 0 if summarize(checks) == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
