#!/usr/bin/env python3
"""Export a sanitized Hugging Face dataset manifest draft.

This script does not copy images. It rewrites absolute local sample paths into
stable relative publish paths so the release review can proceed without leaking
operator directories.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


OUTPUT_COLUMNS = [
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
PUBLIC_SOURCES = {"public", "coco", "open_images", "open_images_bbox", "open_images_val"}


def sanitize_segment(value: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    cleaned = cleaned.strip("._")
    return cleaned or fallback


def publish_path(row: dict[str, str]) -> str:
    klass = sanitize_segment(row.get("class", ""), "unknown")
    source = sanitize_segment(row.get("source", ""), "unknown")
    sha1 = sanitize_segment(row.get("sha1", ""), "sample")
    suffix = Path(row.get("path", "")).suffix.lower() or ".jpg"
    return f"data/{source}/{klass}/{sha1[:16]}{suffix}"


def row_license_status(source: str, default_public_license: str) -> str:
    normalized = source.strip().lower()
    if normalized in PUBLIC_SOURCES:
        return default_public_license
    if normalized in {"cores3", "manual"}:
        return "operator-review"
    if normalized in {"generated", "synthetic"}:
        return "generated-review"
    return "pending-review"


def include_flag(license_status: str, privacy_status: str) -> str:
    return "true" if license_status == "approved" and privacy_status == "approved" else "false"


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def export_manifest(
    samples_root: Path,
    output: Path,
    default_public_license: str,
    privacy_status: str,
) -> int:
    source_manifest = samples_root / "manifest.csv"
    rows = load_rows(source_manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=OUTPUT_COLUMNS)
        writer.writeheader()
        for row in rows:
            license_status = row_license_status(row.get("source", ""), default_public_license)
            writer.writerow(
                {
                    "path": publish_path(row),
                    "class": row.get("class", ""),
                    "source": row.get("source", ""),
                    "scene": row.get("scene", ""),
                    "width": row.get("width", ""),
                    "height": row.get("height", ""),
                    "sha1": row.get("sha1", ""),
                    "license_status": license_status,
                    "privacy_status": privacy_status,
                    "include_in_publish": include_flag(license_status, privacy_status),
                    "notes": "draft metadata only; image files not copied",
                }
            )
    return len(rows)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples-root", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--out", type=Path, default=Path("release/oshw/hf-dataset-manifest.csv"))
    parser.add_argument("--public-license-status", default="pending-review")
    parser.add_argument("--privacy-status", default="pending-review")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    count = export_manifest(
        samples_root=args.samples_root,
        output=args.out,
        default_public_license=args.public_license_status,
        privacy_status=args.privacy_status,
    )
    print(f"wrote {args.out}")
    print(f"rows={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
