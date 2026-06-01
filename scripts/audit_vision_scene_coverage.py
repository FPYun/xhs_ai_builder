#!/usr/bin/env python3
"""Audit CoreS3 sample coverage for required negative capture scenes."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path


REQUIRED_SCENES = ["white_wall", "white_paper", "desktop", "glare", "dark"]


def load_manifest_counts(path: Path) -> dict[str, Counter[str]]:
    counts: dict[str, Counter[str]] = {}
    if not path.exists():
        return counts
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if "scene" not in (reader.fieldnames or []):
            return counts
        for row in reader:
            scene = (row.get("scene") or "").strip() or "unknown"
            klass = (row.get("class") or "unknown").strip() or "unknown"
            counts.setdefault(scene, Counter())[klass] += 1
    return counts


def load_report_counts(path: Path) -> dict[str, Counter[str]]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        report = json.load(handle)
    raw = report.get("scene_sample_counts")
    if not isinstance(raw, dict):
        return {}
    counts: dict[str, Counter[str]] = {}
    for scene, rows in raw.items():
        if isinstance(rows, dict):
            counts[str(scene)] = Counter({str(key): int(value) for key, value in rows.items()})
    return counts


def choose_counts(samples_root: Path) -> tuple[str, dict[str, Counter[str]]]:
    manifest_counts = load_manifest_counts(samples_root / "manifest.csv")
    if manifest_counts:
        return "manifest", manifest_counts
    report_counts = load_report_counts(samples_root / "report.json")
    if report_counts:
        return "report", report_counts
    return "missing", {}


def audit(samples_root: Path, min_negative: int) -> dict[str, object]:
    source, counts = choose_counts(samples_root)
    rows = []
    missing = []
    for scene in REQUIRED_SCENES:
        class_counts = counts.get(scene, Counter())
        total = sum(class_counts.values())
        negative = class_counts.get("negative", 0)
        ok = negative >= min_negative
        if not ok:
            missing.append(scene)
        rows.append(
            {
                "scene": scene,
                "total": total,
                "negative": negative,
                "status": "PASS" if ok else "PENDING",
            }
        )
    return {
        "status": "READY" if not missing else "INCOMPLETE",
        "source": source,
        "min_negative_per_scene": min_negative,
        "rows": rows,
        "missing": missing,
    }


def print_text(result: dict[str, object]) -> None:
    print(f"vision scene coverage: {result['status']} source={result['source']}")
    print(f"required negative samples per scene: {result['min_negative_per_scene']}")
    for row in result["rows"]:
        print(
            f"{row['status']:7} {row['scene']}: "
            f"negative={row['negative']} total={row['total']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples-root", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--min-negative", type=int, default=20)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    result = audit(args.samples_root, args.min_negative)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print_text(result)
    return 1 if args.strict and result["status"] != "READY" else 0


if __name__ == "__main__":
    raise SystemExit(main())
