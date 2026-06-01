#!/usr/bin/env python3
"""Check whether the open hardware and dataset release package is publishable.

The default mode is advisory and exits 0 even when required evidence is still
pending. Use --strict for a release gate that exits non-zero on pending/fail
items.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from audit_vision_scene_coverage import audit as audit_scene_coverage


SECRET_PATTERNS = [
    re.compile(r"sk-[A-Za-z0-9_-]{20,}"),
    re.compile(r"api[_-]?key\s*[:=]\s*['\"][^'\"]+['\"]", re.IGNORECASE),
    re.compile(r"authorization\s*:\s*bearer\s+[A-Za-z0-9._-]+", re.IGNORECASE),
]


@dataclass(frozen=True)
class Check:
    name: str
    status: str
    detail: str


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def exists(root: Path, relative: str) -> bool:
    return (root / relative).exists()


def file_size(root: Path, relative: str) -> int:
    path = root / relative
    return path.stat().st_size if path.exists() and path.is_file() else 0


def add_file_check(checks: list[Check], root: Path, relative: str, name: str | None = None) -> None:
    label = name or relative
    if exists(root, relative) and file_size(root, relative) > 0:
        checks.append(Check(label, "PASS", relative))
    else:
        checks.append(Check(label, "FAIL", f"missing or empty: {relative}"))


def count_csv_rows(path: Path) -> int:
    if not path.exists():
        return 0
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return max(0, sum(1 for _ in csv.reader(handle)) - 1)


def contains_secret(path: Path) -> str:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ""
    for pattern in SECRET_PATTERNS:
        match = pattern.search(text)
        if match:
            return match.group(0)[:80]
    return ""


def dataset_card_is_publishable(root: Path, path: Path) -> tuple[bool, str]:
    if not path.exists():
        return False, "release/oshw/hf-dataset-card.md not filled yet"
    text = path.read_text(encoding="utf-8", errors="ignore").lower()
    draft_markers = [
        "fill in before publishing",
        "publication status: draft",
        "license status: pending",
        "license status: pending-review",
        "todo",
    ]
    for marker in draft_markers:
        if marker in text:
            return False, f"{path.relative_to(root)} still contains draft marker: {marker}"
    return True, str(path.relative_to(root))


def scan_for_secret_markers(root: Path) -> Check:
    targets = [
        "README.md",
        "docs",
        "release",
        "experiments",
        "arduino_demos/04_camera_pet_battle",
        "scripts",
    ]
    for target in targets:
        path = root / target
        files = [path] if path.is_file() else path.rglob("*") if path.exists() else []
        for file_path in files:
            if not file_path.is_file() or file_path.suffix.lower() not in {".md", ".txt", ".csv", ".ino", ".h", ".py", ".json"}:
                continue
            marker = contains_secret(file_path)
            if marker:
                return Check("secret_scan", "FAIL", f"{file_path.relative_to(root)} contains possible secret marker: {marker}")
    return Check("secret_scan", "PASS", "no obvious API key or bearer-token marker found")


def validate_sd_payload(root: Path) -> Check:
    validator_path = root / "scripts/validate-sd-payload.py"
    if not validator_path.exists():
        return Check("sd_payload_validation", "FAIL", "scripts/validate-sd-payload.py missing")
    spec = importlib.util.spec_from_file_location("validate_sd_payload", validator_path)
    if spec is None or spec.loader is None:
        return Check("sd_payload_validation", "FAIL", "cannot load SD payload validator")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    checks = module.validate(root / "sd_card_payload")
    status = module.summarize(checks)
    failed = [check.name for check in checks if check.status == "FAIL"]
    detail = "PASS" if status == "PASS" else ", ".join(failed[:5])
    return Check("sd_payload_validation", status, detail)


def check_release_assets(root: Path, samples_root: Path) -> list[Check]:
    checks: list[Check] = []

    required_files = [
        ("README.md", "root readme"),
        ("arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino", "main sketch"),
        ("arduino_demos/04_camera_pet_battle/pet_model.h", "public pet model"),
        ("arduino_demos/04_camera_pet_battle/vision_types.h", "public vision types"),
        ("arduino_demos/04_camera_pet_battle/ui_types.h", "public UI types"),
        ("arduino_demos/04_camera_pet_battle/battle_protocol.h", "public battle protocol"),
        ("docs/flashing.md", "flashing guide"),
        ("docs/app-http-api.md", "app API guide"),
        ("docs/device-acceptance.md", "device acceptance checklist"),
        ("docs/sd-card-file-boundary.md", "SD boundary doc"),
        ("docs/open-source-release-package.md", "release checklist"),
        ("sd_card_payload/manifest.csv", "SD payload manifest"),
        ("release/oshw/README.md", "release kit readme"),
        ("release/oshw/bom.csv", "BOM"),
        ("release/oshw/video-shot-list.md", "video shot list"),
        ("release/oshw/verification-log-template.csv", "verification log template"),
        ("release/oshw/hf-dataset-card-template.md", "HF dataset card template"),
    ]
    for relative, label in required_files:
        add_file_check(checks, root, relative, label)

    verification_log = root / "release/oshw/verification-log.csv"
    if verification_log.exists() and count_csv_rows(verification_log) > 0:
        checks.append(Check("filled_verification_log", "PASS", str(verification_log.relative_to(root))))
    else:
        checks.append(Check("filled_verification_log", "PENDING", "release/oshw/verification-log.csv not filled yet"))

    dataset_card = root / "release/oshw/hf-dataset-card.md"
    dataset_card_ready, dataset_card_detail = dataset_card_is_publishable(root, dataset_card)
    if dataset_card_ready:
        checks.append(Check("filled_hf_dataset_card", "PASS", dataset_card_detail))
    else:
        checks.append(Check("filled_hf_dataset_card", "PENDING", dataset_card_detail))

    photos = list((root / "release/oshw/photos").glob("*")) if (root / "release/oshw/photos").exists() else []
    photo_count = sum(1 for path in photos if path.is_file() and path.suffix.lower() in {".jpg", ".jpeg", ".png", ".webp"})
    checks.append(Check("hardware_photos", "PASS" if photo_count > 0 else "PENDING", f"{photo_count} public photo files"))

    samples_manifest = samples_root / "manifest.csv"
    samples_report = samples_root / "report.json"
    checks.append(Check("hf_sample_manifest", "PASS" if samples_manifest.exists() else "PENDING", str(samples_manifest)))
    checks.append(Check("hf_training_report", "PASS" if samples_report.exists() else "PENDING", str(samples_report)))
    checks.append(validate_sd_payload(root))
    scene_coverage = audit_scene_coverage(samples_root, min_negative=20)
    if scene_coverage["status"] == "READY":
        checks.append(Check("vision_scene_coverage", "PASS", f"source={scene_coverage['source']}"))
    else:
        missing = ", ".join(scene_coverage["missing"]) or "no scene counts"
        checks.append(Check("vision_scene_coverage", "PENDING", f"missing required negative scenes: {missing}"))

    checks.append(scan_for_secret_markers(root))
    return checks


def summarize(checks: list[Check]) -> str:
    if any(check.status == "FAIL" for check in checks):
        return "FAIL"
    if any(check.status == "PENDING" for check in checks):
        return "INCOMPLETE"
    return "READY"


def print_text(checks: list[Check]) -> None:
    print(f"open release readiness: {summarize(checks)}")
    for check in checks:
        print(f"{check.status:7} {check.name}: {check.detail}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_root_from_script())
    parser.add_argument("--samples-root", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    parser.add_argument("--strict", action="store_true", help="Exit non-zero unless all checks are READY.")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    root = args.repo.resolve()
    checks = check_release_assets(root, args.samples_root)
    status = summarize(checks)
    if args.json:
        print(json.dumps({"status": status, "checks": [check.__dict__ for check in checks]}, ensure_ascii=False, indent=2))
    else:
        print_text(checks)
    return 1 if args.strict and status != "READY" else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
