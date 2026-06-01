#!/usr/bin/env python3
"""Audit local CoreS3 samples before preparing a Hugging Face dataset upload."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from audit_vision_scene_coverage import audit as audit_scene_coverage


CLASSES = [
    "plant_leaf",
    "food_fruit",
    "paper_book",
    "electronics_screen",
    "metal_key_coin",
    "fabric_cloth",
    "cup_bottle_water",
    "toy_figure",
]
ALL_CLASSES = CLASSES + ["negative"]
REQUIRED_MANIFEST_COLUMNS = ["path", "class", "source", "scene", "width", "height", "sha1"]
PUBLISH_MANIFEST_COLUMNS = [
    "path",
    "class",
    "source",
    "scene",
    "width",
    "height",
    "sha1",
    "license_status",
    "privacy_status",
    "include_in_publish",
    "notes",
]
PRIVATE_PATH_MARKERS = [
    re.compile(r"^[A-Za-z]:\\Users\\", re.IGNORECASE),
    re.compile(r"^/Users/"),
    re.compile(r"^/home/"),
]
DRAFT_MARKERS = [
    "publication status: draft",
    "license status: pending",
    "license status: pending-review",
    "fill in before publishing",
    "todo",
]


@dataclass(frozen=True)
class Check:
    name: str
    status: str
    detail: str


def add(checks: list[Check], name: str, status: str, detail: str) -> None:
    checks.append(Check(name, status, detail))


def load_manifest(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    if not path.exists():
        return [], []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        return list(reader), list(reader.fieldnames or [])


def load_report(path: Path) -> dict[str, object]:
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def resolve_sample_path(samples_root: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else samples_root / path


def count_existing_files(samples_root: Path, rows: list[dict[str, str]]) -> tuple[int, int]:
    total = 0
    existing = 0
    for row in rows:
        value = (row.get("path") or "").strip()
        if not value:
            continue
        total += 1
        if resolve_sample_path(samples_root, value).exists():
            existing += 1
    return existing, total


def private_path_rows(rows: list[dict[str, str]]) -> int:
    count = 0
    for row in rows:
        value = (row.get("path") or "").strip()
        if any(pattern.search(value) for pattern in PRIVATE_PATH_MARKERS):
            count += 1
    return count


def absolute_path_rows(rows: list[dict[str, str]]) -> int:
    count = 0
    for row in rows:
        value = (row.get("path") or "").strip()
        if value and Path(value).is_absolute():
            count += 1
    return count


def source_review_status(
    fields: list[str],
    rows: list[dict[str, str]],
    publish_fields: list[str],
    publish_rows: list[dict[str, str]],
) -> tuple[str, str]:
    sources = Counter((row.get("source") or "unknown").strip() or "unknown" for row in rows)
    public_sources = {
        source: count
        for source, count in sources.items()
        if source not in {"cores3", "generated", "synthetic", "manual"}
    }
    if not public_sources:
        return "PASS", "no public supplemental source rows"
    if "license_status" in publish_fields:
        pending = [
            row
            for row in publish_rows
            if (row.get("source") or "").strip() in public_sources
            and (row.get("license_status") or "").strip() not in {"approved", "redistributable"}
        ]
        if not pending and publish_rows:
            return "PASS", f"public source rows={sum(public_sources.values())} license-reviewed"
        return "PENDING", f"public source license review pending rows={len(pending)}"
    if "license" in fields or "license_status" in fields:
        return "PASS", f"public source rows={sum(public_sources.values())} with license column"
    detail = ", ".join(f"{key}={value}" for key, value in sorted(public_sources.items())[:5])
    return "PENDING", f"public source license review needed: {detail}"


def publish_manifest_status(repo_root: Path, manifest_rows: list[dict[str, str]]) -> tuple[list[dict[str, str]], list[str], list[Check]]:
    checks: list[Check] = []
    publish_path = repo_root / "release/oshw/hf-dataset-manifest.csv"
    rows, fields = load_manifest(publish_path)
    add(checks, "publish_manifest_exists", "PASS" if publish_path.exists() else "PENDING", "release/oshw/hf-dataset-manifest.csv")
    missing_columns = [column for column in PUBLISH_MANIFEST_COLUMNS if column not in fields]
    add(
        checks,
        "publish_manifest_columns",
        "FAIL" if missing_columns else "PASS",
        "missing: " + ",".join(missing_columns) if missing_columns else ",".join(fields),
    )
    add(
        checks,
        "publish_manifest_rows",
        "PASS" if rows and len(rows) == len(manifest_rows) else "PENDING",
        f"{len(rows)}/{len(manifest_rows)} rows",
    )
    private_count = private_path_rows(rows)
    absolute_count = absolute_path_rows(rows)
    add(
        checks,
        "publish_manifest_paths",
        "PASS" if rows and private_count == 0 and absolute_count == 0 else "PENDING",
        f"absolute={absolute_count} private={private_count}",
    )
    return rows, fields, checks


def card_status(path: Path) -> tuple[str, str]:
    if not path.exists():
        return "PENDING", "release/oshw/hf-dataset-card.md missing"
    text = path.read_text(encoding="utf-8", errors="ignore").lower()
    for marker in DRAFT_MARKERS:
        if marker in text:
            return "PENDING", f"draft marker: {marker}"
    return "PASS", "release/oshw/hf-dataset-card.md"


def report_quality_status(report: dict[str, object]) -> tuple[str, str]:
    if not report:
        return "PENDING", "report.json missing"
    quality = str(report.get("data_quality", "not recorded"))
    if quality.lower() == "weak":
        return "PENDING", "data_quality=weak"
    return "PASS", f"data_quality={quality}"


def audit(
    samples_root: Path,
    repo_root: Path,
    min_positive: int,
    min_negative: int,
    min_scene_negative: int,
) -> list[Check]:
    checks: list[Check] = []
    manifest_path = samples_root / "manifest.csv"
    report_path = samples_root / "report.json"
    rows, fields = load_manifest(manifest_path)
    report = load_report(report_path)
    publish_rows, publish_fields, publish_checks = publish_manifest_status(repo_root, rows)

    add(checks, "manifest_exists", "PASS" if manifest_path.exists() else "PENDING", str(manifest_path))
    missing_columns = [column for column in REQUIRED_MANIFEST_COLUMNS if column not in fields]
    add(
        checks,
        "manifest_columns",
        "FAIL" if missing_columns else "PASS",
        "missing: " + ",".join(missing_columns) if missing_columns else ",".join(fields),
    )
    add(checks, "manifest_rows", "PASS" if rows else "PENDING", f"{len(rows)} rows")

    class_counts = Counter((row.get("class") or "unknown").strip() or "unknown" for row in rows)
    missing_classes = [klass for klass in ALL_CLASSES if class_counts.get(klass, 0) == 0]
    add(
        checks,
        "class_presence",
        "PENDING" if missing_classes else "PASS",
        "missing: " + ",".join(missing_classes) if missing_classes else "all classes present",
    )
    weak_counts = [
        f"{klass}={class_counts.get(klass, 0)}"
        for klass in CLASSES
        if class_counts.get(klass, 0) < min_positive
    ]
    if class_counts.get("negative", 0) < min_negative:
        weak_counts.append(f"negative={class_counts.get('negative', 0)}")
    add(
        checks,
        "class_min_counts",
        "PENDING" if weak_counts else "PASS",
        "below target: " + ",".join(weak_counts[:8]) if weak_counts else f"positive>={min_positive}, negative>={min_negative}",
    )

    existing, total = count_existing_files(samples_root, rows)
    file_status = "PASS" if total and existing == total else "PENDING"
    add(checks, "sample_files_exist", file_status, f"{existing}/{total} manifest files exist")

    private_count = private_path_rows(rows)
    add(
        checks,
        "private_path_markers",
        "PENDING" if private_count else "PASS",
        f"{private_count} rows with user-home paths" if private_count else "no user-home path markers",
    )
    checks.extend(publish_checks)
    publish_paths_ok = any(check.name == "publish_manifest_paths" and check.status == "PASS" for check in publish_checks)
    absolute_count = absolute_path_rows(rows)
    add(
        checks,
        "relative_publish_paths",
        "PASS" if publish_paths_ok or absolute_count == 0 else "PENDING",
        "sanitized publish manifest available" if publish_paths_ok else f"{absolute_count} absolute paths need sanitized export",
    )

    source_status, source_detail = source_review_status(fields, rows, publish_fields, publish_rows)
    add(checks, "source_license_review", source_status, source_detail)

    quality_status, quality_detail = report_quality_status(report)
    add(checks, "training_report_quality", quality_status, quality_detail)

    scene = audit_scene_coverage(samples_root, min_negative=min_scene_negative)
    add(
        checks,
        "negative_scene_coverage",
        "PASS" if scene["status"] == "READY" else "PENDING",
        f"source={scene['source']} missing={','.join(scene['missing']) or 'none'}",
    )

    status, detail = card_status(repo_root / "release/oshw/hf-dataset-card.md")
    add(checks, "dataset_card_publishable", status, detail)
    return checks


def summarize(checks: list[Check]) -> str:
    if any(check.status == "FAIL" for check in checks):
        return "FAIL"
    if any(check.status == "PENDING" for check in checks):
        return "INCOMPLETE"
    return "READY"


def print_text(checks: list[Check]) -> None:
    print(f"hf dataset publishability: {summarize(checks)}")
    for check in checks:
        print(f"{check.status:7} {check.name}: {check.detail}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--samples-root", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--min-positive", type=int, default=30)
    parser.add_argument("--min-negative", type=int, default=50)
    parser.add_argument("--min-scene-negative", type=int, default=20)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--strict", action="store_true")
    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    checks = audit(
        samples_root=args.samples_root,
        repo_root=args.repo.resolve(),
        min_positive=args.min_positive,
        min_negative=args.min_negative,
        min_scene_negative=args.min_scene_negative,
    )
    status = summarize(checks)
    if args.json:
        print(json.dumps({"status": status, "checks": [check.__dict__ for check in checks]}, ensure_ascii=False, indent=2))
    else:
        print_text(checks)
    return 1 if args.strict and status != "READY" else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
