#!/usr/bin/env python3
"""Build a draft Hugging Face dataset card from local CoreS3 sample metadata."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from datetime import date
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
    "negative",
]


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


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


def fmt_percent(value: object) -> str:
    if value is None:
        return "not recorded"
    try:
        return f"{float(value) * 100:.1f}%"
    except (TypeError, ValueError):
        return str(value)


def counter_table(title: str, counter: Counter[str]) -> list[str]:
    lines = [f"## {title}", "", "| Value | Count |", "| --- | ---: |"]
    if not counter:
        lines.append("| not recorded | 0 |")
    else:
        for key, count in sorted(counter.items()):
            lines.append(f"| `{key or 'unknown'}` | {count} |")
    lines.append("")
    return lines


def report_class_counts(report: dict[str, object]) -> Counter[str]:
    counts = report.get("sample_counts")
    if isinstance(counts, dict):
        return Counter({str(key): int(value) for key, value in counts.items()})
    return Counter()


def build_card(
    manifest_rows: list[dict[str, str]],
    manifest_fields: list[str],
    report: dict[str, object],
    license_status: str,
    scene_coverage: dict[str, object],
) -> str:
    class_counts = Counter(row.get("class", "unknown") for row in manifest_rows)
    source_counts = Counter(row.get("source", "unknown") for row in manifest_rows)
    scene_counts = Counter(row.get("scene", "unknown") for row in manifest_rows if "scene" in row)
    report_counts = report_class_counts(report)

    lines: list[str] = [
        "# Dataset Card: CoreS3 Pet Capture Samples",
        "",
        f"Generated: {date.today().isoformat()}",
        "",
        "Publication status: draft - do not upload until license, privacy, and sample provenance are reviewed.",
        f"License status: {license_status}",
        "",
        "## Dataset Summary",
        "",
        "This draft describes local CoreS3 camera samples for the 8-class pet",
        "capture recognizer. The dataset is intended for lightweight feature",
        "training and negative-scene validation, not for cloud runtime inference.",
        "",
        "Images are not stored in this repository. The local manifest points to",
        "sample files under the operator's sample root.",
        "",
        "## Labels",
        "",
    ]
    lines.extend(f"- `{label}`" for label in CLASSES)
    lines.extend(
        [
            "",
            "## Device And Capture Context",
            "",
            "- Board: M5Stack CoreS3",
            "- Camera: CoreS3 onboard camera",
            "- Runtime: local/offline firmware",
            "- Firmware export: `/samples/manifest.csv` and optional `/samples/*.ppm` thumbnails",
            "- Public images, if present, are development supplements and must be license-checked before redistribution.",
            "",
            "## Manifest Summary",
            "",
            f"- Rows: {len(manifest_rows)}",
            f"- Columns: {', '.join(manifest_fields) if manifest_fields else 'manifest not found'}",
            "",
        ]
    )
    lines.extend(counter_table("Manifest Class Counts", class_counts))
    lines.extend(counter_table("Manifest Source Counts", source_counts))
    lines.extend(counter_table("Manifest Scene Counts", scene_counts))

    lines.extend(
        [
            "## Training Report Summary",
            "",
            f"- Data quality: `{report.get('data_quality', 'not recorded')}`",
            f"- Positive accuracy: {fmt_percent(report.get('positive_accuracy'))}",
            f"- Negative false-positive rate: {fmt_percent(report.get('negative_false_positive_rate'))}",
            "",
        ]
    )
    lines.extend(counter_table("Training Sample Counts", report_counts))

    scene_eval = report.get("scene_eval")
    lines.extend(["## Scene Evaluation", ""])
    if isinstance(scene_eval, dict) and scene_eval:
        lines.extend(["| Scene | Samples | Accuracy | False positive rate |", "| --- | ---: | ---: | ---: |"])
        for scene, row in sorted(scene_eval.items()):
            if isinstance(row, dict):
                lines.append(
                    f"| `{scene}` | {row.get('samples', 0)} | "
                    f"{fmt_percent(row.get('accuracy'))} | {fmt_percent(row.get('false_positive_rate'))} |"
                )
    else:
        lines.append("No scene evaluation is recorded in the current report.")
    lines.extend(
        [
            "",
            "## Required Negative Scene Coverage",
            "",
            f"Coverage status: `{scene_coverage['status']}` from `{scene_coverage['source']}`.",
            "",
            "| Scene | Negative samples | Total samples | Status |",
            "| --- | ---: | ---: | --- |",
        ]
    )
    for row in scene_coverage["rows"]:
        lines.append(f"| `{row['scene']}` | {row['negative']} | {row['total']} | {row['status']} |")
    lines.extend(
        [
            "",
            "## Privacy And License Review",
            "",
            "- Remove private photos, people, screens, documents, and location-identifying backgrounds before publication.",
            "- Verify every public supplemental source license before uploading images or derived thumbnails.",
            "- Keep this card in draft status until the release owner records a publishable license.",
            "",
            "## Known Limitations",
            "",
            "- CoreS3 camera resolution and lighting strongly affect the feature model.",
            "- Public web samples do not fully match the embedded camera domain.",
            "- Negative scenes such as white wall, white paper, desktop, glare, and dark light need real CoreS3 coverage.",
            "",
            "## Rebuild Command",
            "",
            "```powershell",
            "python .\\scripts\\build-hf-dataset-card.py --samples-root C:\\tmp\\m5_vision_samples",
            "```",
            "",
        ]
    )
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_root_from_script())
    parser.add_argument("--samples-root", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--out", type=Path, default=Path("release/oshw/hf-dataset-card.md"))
    parser.add_argument("--license-status", default="pending-review")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    repo = args.repo.resolve()
    manifest, fields = load_manifest(args.samples_root / "manifest.csv")
    report = load_report(args.samples_root / "report.json")
    scene_coverage = audit_scene_coverage(args.samples_root, min_negative=20)
    output = args.out if args.out.is_absolute() else repo / args.out
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_card(manifest, fields, report, args.license_status, scene_coverage), encoding="utf-8")
    try:
        display_output = output.relative_to(repo)
    except ValueError:
        display_output = output
    print(f"wrote {display_output}")
    print(f"manifest_rows={len(manifest)} data_quality={report.get('data_quality', 'not recorded')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
