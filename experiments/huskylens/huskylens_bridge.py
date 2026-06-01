#!/usr/bin/env python3
"""Map HuskyLens learned IDs to the project's 8-class recognition contract.

This tool is intentionally desktop-only. It does not talk to CoreS3 directly
and does not require the DFRobot HuskyLens Arduino library.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass
from pathlib import Path


VALID_CLASSES = {
    "plant_leaf",
    "food_fruit",
    "paper_book",
    "electronics_screen",
    "metal_key_coin",
    "fabric_cloth",
    "cup_bottle_water",
    "toy_figure",
}


@dataclass(frozen=True)
class HuskyLensMapping:
    huskylens_id: int
    class_name: str
    enabled: bool
    notes: str


def load_id_map(path: Path) -> dict[int, HuskyLensMapping]:
    mappings: dict[int, HuskyLensMapping] = {}
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"huskylens_id", "class", "enabled", "notes"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"id map missing columns: {', '.join(sorted(missing))}")
        for row in reader:
            class_name = (row.get("class") or "").strip()
            if class_name not in VALID_CLASSES:
                raise ValueError(f"unsupported class in id map: {class_name}")
            huskylens_id = int((row.get("huskylens_id") or "0").strip())
            enabled = (row.get("enabled") or "").strip().lower() in {"1", "true", "yes", "on"}
            mappings[huskylens_id] = HuskyLensMapping(
                huskylens_id=huskylens_id,
                class_name=class_name,
                enabled=enabled,
                notes=(row.get("notes") or "").strip(),
            )
    return mappings


def map_event(
    mappings: dict[int, HuskyLensMapping],
    huskylens_id: int,
    confidence: int,
    presence: int,
    timeout_ms: int,
    force_disabled: bool,
) -> dict[str, object]:
    row = mappings.get(huskylens_id)
    confidence = max(0, min(100, confidence))
    presence = max(0, min(100, presence))
    if row is None:
        return {
            "ok": True,
            "module_detected": True,
            "mapped": False,
            "mapped_class": "unknown",
            "confidence_hint": confidence,
            "presence_hint": presence,
            "timeout_ms": timeout_ms,
            "local_fallback_used": True,
            "fallback_reason": "unmapped_id",
        }
    if not row.enabled and not force_disabled:
        return {
            "ok": True,
            "module_detected": True,
            "mapped": False,
            "mapped_class": row.class_name,
            "confidence_hint": confidence,
            "presence_hint": presence,
            "timeout_ms": timeout_ms,
            "local_fallback_used": True,
            "fallback_reason": "mapping_disabled",
            "notes": row.notes,
        }
    if confidence < 58 or presence < 42:
        return {
            "ok": True,
            "module_detected": True,
            "mapped": False,
            "mapped_class": row.class_name,
            "confidence_hint": confidence,
            "presence_hint": presence,
            "timeout_ms": timeout_ms,
            "local_fallback_used": True,
            "fallback_reason": "low_external_confidence",
            "notes": row.notes,
        }
    return {
        "ok": True,
        "module_detected": True,
        "mapped": True,
        "mapped_class": row.class_name,
        "confidence_hint": confidence,
        "presence_hint": presence,
        "timeout_ms": timeout_ms,
        "local_fallback_used": False,
        "fallback_reason": "",
        "serial_hint": f"HUSKY_HINT {row.class_name} {confidence} {presence}",
        "notes": row.notes,
    }


def run_self_test(id_map: Path) -> None:
    mappings = load_id_map(id_map)
    disabled = map_event(mappings, 1, 90, 90, 60, False)
    if disabled["fallback_reason"] != "mapping_disabled":
        raise AssertionError("disabled mapping should fall back")
    forced = map_event(mappings, 1, 90, 90, 60, True)
    if forced["mapped_class"] != "plant_leaf" or not forced["mapped"]:
        raise AssertionError("forced mapping should map ID 1 to plant_leaf")
    low = map_event(mappings, 1, 12, 90, 60, True)
    if low["fallback_reason"] != "low_external_confidence":
        raise AssertionError("low confidence should fall back")
    unknown = map_event(mappings, 99, 90, 90, 60, True)
    if unknown["fallback_reason"] != "unmapped_id":
        raise AssertionError("unknown ID should fall back")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id-map", type=Path, default=Path(__file__).with_name("id-map.csv"))
    parser.add_argument("--id", type=int, help="HuskyLens learned object ID.")
    parser.add_argument("--confidence", type=int, default=0, help="External confidence hint, 0-100.")
    parser.add_argument("--presence", type=int, default=0, help="External presence hint, 0-100.")
    parser.add_argument("--timeout-ms", type=int, default=0, help="Read timeout observed by the experiment harness.")
    parser.add_argument("--force-disabled", action="store_true", help="Allow disabled id-map rows for bench simulation.")
    parser.add_argument("--self-test", action="store_true", help="Run internal mapping checks.")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.self_test:
        run_self_test(args.id_map)
        print("huskylens bridge self-test passed")
        return 0
    if args.id is None:
        parser.error("--id is required unless --self-test is used")
    mappings = load_id_map(args.id_map)
    result = map_event(
        mappings=mappings,
        huskylens_id=args.id,
        confidence=args.confidence,
        presence=args.presence,
        timeout_ms=args.timeout_ms,
        force_disabled=args.force_disabled,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
