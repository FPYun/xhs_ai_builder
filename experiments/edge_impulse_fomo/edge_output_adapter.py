#!/usr/bin/env python3
"""Normalize Edge Impulse or FOMO experiment output to the current 8-class contract.

This is a desktop experiment helper. It does not import Edge Impulse libraries
and does not change the production firmware.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class LabelRow:
    label_id: int
    class_name: str
    element_hint: str
    species_bias: str


def clamp_percent(value: float) -> int:
    if value <= 1.0:
        value *= 100.0
    return max(0, min(100, int(round(value))))


def load_label_map(path: Path) -> dict[str, LabelRow]:
    rows: dict[str, LabelRow] = {}
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"id", "class", "element_hint", "species_bias"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"label map missing columns: {', '.join(sorted(missing))}")
        for row in reader:
            class_name = (row.get("class") or "").strip()
            rows[class_name] = LabelRow(
                label_id=int((row.get("id") or "0").strip()),
                class_name=class_name,
                element_hint=(row.get("element_hint") or "").strip(),
                species_bias=(row.get("species_bias") or "").strip(),
            )
    return rows


def nested_result(data: Any) -> Any:
    if isinstance(data, dict) and "result" in data and isinstance(data["result"], dict):
        return data["result"]
    return data


def collect_scores(data: Any) -> dict[str, float]:
    data = nested_result(data)
    scores: dict[str, float] = {}
    if isinstance(data, dict):
        classification = data.get("classification")
        if isinstance(classification, dict):
            for label, value in classification.items():
                if isinstance(value, dict):
                    value = value.get("value", value.get("score", 0))
                scores[str(label)] = float(value)
        elif isinstance(classification, list):
            for item in classification:
                if isinstance(item, dict) and "label" in item:
                    scores[str(item["label"])] = float(item.get("value", item.get("score", 0)))
        for key in ("bounding_boxes", "boxes"):
            boxes = data.get(key)
            if isinstance(boxes, list):
                for box in boxes:
                    if not isinstance(box, dict) or "label" not in box:
                        continue
                    label = str(box["label"])
                    value = float(box.get("value", box.get("score", 0)))
                    scores[label] = max(scores.get(label, 0.0), value)
    elif isinstance(data, list):
        for item in data:
            if isinstance(item, dict) and "label" in item:
                scores[str(item["label"])] = float(item.get("value", item.get("score", 0)))
    return scores


def estimate_presence(data: Any, confidence: int) -> int:
    data = nested_result(data)
    boxes: list[dict[str, Any]] = []
    if isinstance(data, dict):
        for key in ("bounding_boxes", "boxes"):
            value = data.get(key)
            if isinstance(value, list):
                boxes.extend(item for item in value if isinstance(item, dict))
    if not boxes:
        return confidence
    best = max(boxes, key=lambda box: float(box.get("value", box.get("score", 0))))
    width = float(best.get("width", best.get("w", 0)) or 0)
    height = float(best.get("height", best.get("h", 0)) or 0)
    area_score = max(0, min(35, int(round((width * height) / 96.0))))
    return max(0, min(100, confidence + area_score))


def normalize_output(
    data: Any,
    labels: dict[str, LabelRow],
    min_confidence: int,
    min_presence: int,
    negative_threshold: int,
) -> dict[str, Any]:
    scores = collect_scores(data)
    if not scores:
        return {
            "ok": True,
            "mapped": False,
            "classId": 0,
            "objectLabel": "unknown",
            "confidence": 0,
            "presence": 0,
            "failureReason": "no_supported_output",
            "local_fallback_used": True,
        }
    supported = {label: value for label, value in scores.items() if label in labels}
    if not supported:
        return {
            "ok": True,
            "mapped": False,
            "classId": 0,
            "objectLabel": "unknown",
            "confidence": 0,
            "presence": 0,
            "failureReason": "no_supported_label",
            "local_fallback_used": True,
            "rawLabels": sorted(scores.keys()),
        }
    best_label, best_score = max(supported.items(), key=lambda item: item[1])
    confidence = clamp_percent(best_score)
    negative_score = clamp_percent(supported.get("negative", 0.0))
    if best_label == "negative" or negative_score >= negative_threshold:
        return {
            "ok": True,
            "mapped": False,
            "classId": 0,
            "objectLabel": best_label,
            "confidence": confidence,
            "presence": 0,
            "failureReason": "negative_scene",
            "local_fallback_used": True,
        }
    presence = estimate_presence(data, confidence)
    row = labels[best_label]
    if confidence < min_confidence:
        reason = "low_external_confidence"
    elif presence < min_presence:
        reason = "low_external_presence"
    else:
        reason = ""
    mapped = reason == ""
    return {
        "ok": True,
        "mapped": mapped,
        "classId": row.label_id + 1,
        "objectLabel": row.class_name,
        "confidence": confidence,
        "presence": presence,
        "elementHint": row.element_hint,
        "speciesBias": row.species_bias,
        "failureReason": reason,
        "local_fallback_used": not mapped,
        "serial_hint": f"EDGE_HINT {row.class_name} {confidence} {presence}" if mapped else "",
    }


def load_input(args: argparse.Namespace) -> Any:
    if args.json_text:
        return json.loads(args.json_text)
    if args.input:
        with args.input.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    raise ValueError("provide --input or --json")


def run_self_test(label_map: Path) -> None:
    labels = load_label_map(label_map)
    positive = normalize_output(
        {"classification": {"plant_leaf": 0.82, "negative": 0.05}},
        labels,
        min_confidence=58,
        min_presence=42,
        negative_threshold=55,
    )
    if not positive["mapped"] or positive["objectLabel"] != "plant_leaf":
        raise AssertionError("positive plant_leaf output should map")
    negative = normalize_output(
        {"classification": {"plant_leaf": 0.2, "negative": 0.88}},
        labels,
        min_confidence=58,
        min_presence=42,
        negative_threshold=55,
    )
    if negative["failureReason"] != "negative_scene":
        raise AssertionError("negative output should fall back")
    low = normalize_output(
        {"classification": {"toy_figure": 0.21, "negative": 0.02}},
        labels,
        min_confidence=58,
        min_presence=42,
        negative_threshold=55,
    )
    if low["failureReason"] != "low_external_confidence":
        raise AssertionError("low-confidence output should fall back")
    box = normalize_output(
        {"bounding_boxes": [{"label": "cup_bottle_water", "value": 0.74, "width": 64, "height": 64}]},
        labels,
        min_confidence=58,
        min_presence=42,
        negative_threshold=55,
    )
    if not box["mapped"] or box["objectLabel"] != "cup_bottle_water":
        raise AssertionError("FOMO-style box output should map")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label-map", type=Path, default=Path(__file__).with_name("label-map.csv"))
    parser.add_argument("--input", type=Path, help="Edge Impulse JSON output file.")
    parser.add_argument("--json", dest="json_text", help="Inline Edge Impulse JSON output.")
    parser.add_argument("--min-confidence", type=int, default=58)
    parser.add_argument("--min-presence", type=int, default=42)
    parser.add_argument("--negative-threshold", type=int, default=55)
    parser.add_argument("--self-test", action="store_true")
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.self_test:
        run_self_test(args.label_map)
        print("edge output adapter self-test passed")
        return 0
    labels = load_label_map(args.label_map)
    result = normalize_output(
        load_input(args),
        labels,
        min_confidence=args.min_confidence,
        min_presence=args.min_presence,
        negative_threshold=args.negative_threshold,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
