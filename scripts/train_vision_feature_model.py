from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import time
import zipfile
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from urllib.parse import quote_plus

import numpy as np
import requests
from PIL import Image, ImageOps


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

FEATURES = [
    "red",
    "green",
    "blue",
    "brightness",
    "saturation",
    "contrast",
    "center_brightness",
    "edge_brightness",
    "center_saturation",
    "edge_saturation",
    "center_delta",
    "dark_ratio",
    "bright_ratio",
    "red_dominance",
    "green_dominance",
    "blue_dominance",
    "warm_score",
    "low_saturation_score",
]

SCENES = [
    "white_wall",
    "white_paper",
    "desktop",
    "glare",
    "dark",
    "bright",
    "low_texture",
    "hand_cover",
    "unknown",
]

SCENE_ALIASES = {
    "white_wall": ["white_wall", "blank_wall", "wall", "baiqiang", "白墙", "墙面"],
    "white_paper": ["white_paper", "blank_paper", "paper_blank", "paper", "baizhi", "白纸"],
    "desktop": ["desktop", "desk", "tabletop", "table", "zhuomian", "桌面", "桌子"],
    "glare": ["glare", "reflect", "reflection", "overexposed", "fan guang", "fanguang", "反光", "眩光"],
    "dark": ["dark", "low_light", "too_dark", "dim", "anguang", "过暗", "暗光"],
    "bright": ["bright", "too_bright", "overbright", "qiangguang", "过亮", "强光"],
    "low_texture": ["low_texture", "flat", "plain", "blank", "pingtan", "平坦", "纯背景"],
    "hand_cover": ["hand", "cover", "occlusion", "shouzhedang", "手遮挡", "遮挡"],
}

FEATURE_WEIGHTS = np.array(
    [5, 5, 5, 4, 6, 5, 4, 3, 4, 3, 7, 5, 4, 6, 6, 6, 5, 5],
    dtype=np.float32,
)

PUBLIC_QUERIES = {
    "plant_leaf": ["plant leaf", "houseplant leaf", "green leaf"],
    "food_fruit": ["apple fruit", "banana fruit", "orange fruit"],
    "paper_book": ["book cover", "notebook paper", "open book"],
    "electronics_screen": ["computer monitor screen", "laptop screen", "mobile phone screen"],
    "metal_key_coin": ["metal key", "coins", "brass key"],
    "fabric_cloth": ["folded cloth", "shirt fabric", "towel textile"],
    "cup_bottle_water": ["water bottle", "drinking cup", "clear bottle"],
    "toy_figure": ["toy figure", "doll toy", "teddy bear toy"],
    "negative": ["blank wall", "wood table surface", "empty desk"],
}

COCO_CATEGORY_TO_CLASS = {
    "apple": "food_fruit",
    "banana": "food_fruit",
    "orange": "food_fruit",
    "book": "paper_book",
    "tv": "electronics_screen",
    "laptop": "electronics_screen",
    "cell phone": "electronics_screen",
    "keyboard": "electronics_screen",
    "mouse": "electronics_screen",
    "bottle": "cup_bottle_water",
    "cup": "cup_bottle_water",
    "wine glass": "cup_bottle_water",
    "teddy bear": "toy_figure",
}

COCO_ANNOTATIONS_URL = "http://images.cocodataset.org/annotations/annotations_trainval2017.zip"

OPEN_IMAGES_CLASS_DESCRIPTIONS_URL = "https://storage.googleapis.com/openimages/2018_04/class-descriptions.csv"
OPEN_IMAGES_VALIDATION_IMAGES_URL = "https://storage.googleapis.com/openimages/2018_04/validation/validation-images-with-rotation.csv"
OPEN_IMAGES_VALIDATION_LABELS_URL = (
    "https://storage.googleapis.com/openimages/2018_04/validation/validation-annotations-human-imagelabels.csv"
)
OPEN_IMAGES_VALIDATION_BBOX_URL = "https://storage.googleapis.com/openimages/2018_04/validation/validation-annotations-bbox.csv"

OPEN_IMAGES_DISPLAY_TO_CLASS = {
    "Plant": "plant_leaf",
    "Houseplant": "plant_leaf",
    "Flower": "plant_leaf",
    "Tree": "plant_leaf",
    "Leaf": "plant_leaf",
    "Fruit": "food_fruit",
    "Apple": "food_fruit",
    "Banana": "food_fruit",
    "Orange": "food_fruit",
    "Strawberry": "food_fruit",
    "Book": "paper_book",
    "Notebook": "paper_book",
    "Computer monitor": "electronics_screen",
    "Television": "electronics_screen",
    "Laptop": "electronics_screen",
    "Mobile phone": "electronics_screen",
    "Tablet computer": "electronics_screen",
    "Key": "metal_key_coin",
    "Coin": "metal_key_coin",
    "Metal": "metal_key_coin",
    "Clothing": "fabric_cloth",
    "Shirt": "fabric_cloth",
    "Textile": "fabric_cloth",
    "Towel": "fabric_cloth",
    "Woven fabric": "fabric_cloth",
    "Bottle": "cup_bottle_water",
    "Cup": "cup_bottle_water",
    "Drink": "cup_bottle_water",
    "Drinkware": "cup_bottle_water",
    "Water bottle": "cup_bottle_water",
    "Toy": "toy_figure",
    "Doll": "toy_figure",
    "Figurine": "toy_figure",
    "Teddy bear": "toy_figure",
    "Stuffed toy": "toy_figure",
    "Wall": "negative",
    "Floor": "negative",
    "Ceiling": "negative",
}

SYNTHETIC_PROTOTYPES = {
    "plant_leaf": (78, 175, 74, 130, 100, 70, 132, 90, 110, 80, 72, 10, 12, 90, 210, 88, 128, 0),
    "food_fruit": (205, 126, 48, 135, 150, 84, 142, 98, 155, 110, 76, 12, 18, 230, 120, 75, 210, 0),
    "paper_book": (190, 178, 150, 176, 35, 52, 184, 148, 38, 32, 48, 3, 45, 145, 140, 112, 150, 162),
    "electronics_screen": (46, 58, 82, 58, 55, 118, 68, 42, 68, 52, 60, 110, 8, 96, 112, 158, 88, 110),
    "metal_key_coin": (188, 190, 180, 178, 22, 98, 180, 145, 24, 20, 40, 5, 64, 132, 134, 118, 142, 196),
    "fabric_cloth": (135, 115, 105, 116, 72, 38, 118, 92, 80, 62, 42, 15, 12, 148, 122, 115, 142, 64),
    "cup_bottle_water": (80, 128, 196, 150, 95, 72, 156, 112, 105, 78, 66, 8, 50, 78, 120, 226, 70, 4),
    "toy_figure": (190, 64, 150, 126, 145, 115, 140, 90, 152, 108, 86, 18, 24, 192, 70, 164, 130, 0),
    "negative": (128, 128, 126, 128, 18, 18, 128, 126, 16, 15, 8, 8, 8, 128, 128, 128, 128, 210),
}

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
MODEL_SCALE = 16
RNG = random.Random(0x57355054)


@dataclass
class Sample:
    path: Path
    klass: str
    source: str
    features: np.ndarray


def infer_scene_from_path(path: Path) -> str | None:
    text = " ".join(part.lower() for part in path.parts)
    for scene, aliases in SCENE_ALIASES.items():
        if any(alias.lower() in text for alias in aliases):
            return scene
    return None


def infer_scene_from_features(features: np.ndarray) -> str:
    brightness = float(features[3])
    saturation = float(features[4])
    contrast = float(features[5])
    center_delta = float(features[10])
    dark_ratio = float(features[11])
    bright_ratio = float(features[12])
    warm_score = float(features[16])

    if dark_ratio >= 72 or brightness <= 58:
        return "dark"
    if bright_ratio >= 70 or brightness >= 214:
        return "bright"
    if bright_ratio >= 24 and center_delta >= 46:
        return "glare"
    if brightness >= 174 and saturation <= 34 and contrast <= 42 and center_delta <= 34:
        return "white_wall"
    if brightness >= 158 and saturation <= 48 and contrast > 42:
        return "white_paper"
    if 82 <= brightness <= 184 and saturation <= 76 and warm_score >= 138:
        return "desktop"
    if contrast <= 26 and center_delta <= 24:
        return "low_texture"
    return "unknown"


def infer_scene(sample: Sample) -> str:
    return infer_scene_from_path(sample.path) or infer_scene_from_features(sample.features)


def clamp_u8(value: float) -> int:
    return int(max(0, min(255, round(float(value)))))


def fit_image(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        image = ImageOps.exif_transpose(image).convert("RGB")
        image = ImageOps.fit(image, (64, 64), method=Image.Resampling.BILINEAR)
        return np.asarray(image, dtype=np.float32)


def extract_features_from_array(arr: np.ndarray) -> np.ndarray:
    red = arr[:, :, 0]
    green = arr[:, :, 1]
    blue = arr[:, :, 2]
    high = arr.max(axis=2)
    low = arr.min(axis=2)
    gray = (red + green + blue) / 3.0
    saturation = high - low

    center = arr[16:48, 16:48, :]
    edge = np.concatenate(
        [
            arr[:16, :, :].reshape(-1, 3),
            arr[48:, :, :].reshape(-1, 3),
            arr[16:48, :16, :].reshape(-1, 3),
            arr[16:48, 48:, :].reshape(-1, 3),
        ],
        axis=0,
    )
    center_gray = center.mean(axis=2)
    edge_gray = edge.mean(axis=1)
    center_sat = center.max(axis=2) - center.min(axis=2)
    edge_sat = edge.max(axis=1) - edge.min(axis=1)

    r_mean = red.mean()
    g_mean = green.mean()
    b_mean = blue.mean()
    brightness = gray.mean()
    sat_mean = saturation.mean()
    contrast = gray.std() * 2.0
    center_brightness = center_gray.mean()
    edge_brightness = edge_gray.mean()
    center_saturation = center_sat.mean()
    edge_saturation = edge_sat.mean()
    center_delta = abs(center_brightness - edge_brightness) + abs(center_saturation - edge_saturation)
    dark_ratio = (gray < 42).mean() * 255.0
    bright_ratio = (gray > 215).mean() * 255.0
    red_dominance = r_mean - max(g_mean, b_mean) + 128.0
    green_dominance = g_mean - max(r_mean, b_mean) + 128.0
    blue_dominance = b_mean - max(r_mean, g_mean) + 128.0
    warm_score = (r_mean + g_mean - 2.0 * b_mean) / 2.0 + 128.0
    low_saturation_score = max(0.0, 96.0 - sat_mean) * 255.0 / 96.0

    return np.array(
        [
            r_mean,
            g_mean,
            b_mean,
            brightness,
            sat_mean,
            contrast,
            center_brightness,
            edge_brightness,
            center_saturation,
            edge_saturation,
            center_delta,
            dark_ratio,
            bright_ratio,
            red_dominance,
            green_dominance,
            blue_dominance,
            warm_score,
            low_saturation_score,
        ],
        dtype=np.float32,
    ).clip(0, 255)


def extract_features(path: Path) -> np.ndarray:
    return extract_features_from_array(fit_image(path))


def image_files(root: Path, klass: str) -> list[tuple[Path, str]]:
    rows: list[tuple[Path, str]] = []
    candidates = [
        (root / "cores3" / klass, "cores3"),
        (root / "generated" / klass, "generated"),
        (root / "public" / klass, "public"),
        (root / klass, "manual"),
    ]
    for directory, source in candidates:
        if not directory.exists():
            continue
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES:
                rows.append((path, source))
    return rows


def safe_filename(text: str, suffix: str) -> str:
    digest = hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]
    return f"{digest}{suffix}"


def download_url(url: str, output: Path, timeout: float) -> bool:
    try:
        response = requests.get(url, timeout=timeout, headers={"User-Agent": "m5-core-s3-vision-sampler/1.0"})
        response.raise_for_status()
        image = Image.open(BytesIO(response.content)).convert("RGB")
        image.thumbnail((640, 640))
        output.parent.mkdir(parents=True, exist_ok=True)
        image.save(output, format="JPEG", quality=88)
        return True
    except Exception:
        return False


def download_url_crop(url: str, output: Path, bbox: tuple[float, float, float, float], timeout: float) -> bool:
    try:
        response = requests.get(url, timeout=timeout, headers={"User-Agent": "m5-core-s3-vision-sampler/1.0"})
        response.raise_for_status()
        image = Image.open(BytesIO(response.content)).convert("RGB")
        width, height = image.size
        x_min, x_max, y_min, y_max = bbox
        left = max(0, int((x_min - 0.06) * width))
        right = min(width, int((x_max + 0.06) * width))
        top = max(0, int((y_min - 0.06) * height))
        bottom = min(height, int((y_max + 0.06) * height))
        if right - left < 24 or bottom - top < 24:
            return False
        crop = image.crop((left, top, right, bottom))
        crop.thumbnail((640, 640))
        output.parent.mkdir(parents=True, exist_ok=True)
        crop.save(output, format="JPEG", quality=88)
        return True
    except Exception:
        return False


def wikimedia_image_urls(query: str, limit: int) -> list[str]:
    api = "https://commons.wikimedia.org/w/api.php"
    params = (
        f"?action=query&generator=search&gsrnamespace=6&gsrlimit={limit}"
        f"&gsrsearch={quote_plus(query)}&prop=imageinfo&iiprop=url|mime&iiurlwidth=640&format=json"
    )
    try:
        response = requests.get(api + params, timeout=20, headers={"User-Agent": "m5-core-s3-vision-sampler/1.0"})
        response.raise_for_status()
        pages = response.json().get("query", {}).get("pages", {})
    except Exception:
        return []
    urls: list[str] = []
    for page in pages.values():
        info = page.get("imageinfo", [{}])[0]
        mime = info.get("mime", "")
        if mime not in {"image/jpeg", "image/png", "image/webp"}:
            continue
        url = info.get("thumburl") or info.get("url")
        if url:
            urls.append(url)
    return urls


def download_public_samples(
    root: Path,
    target_per_class: int,
    max_per_class: int,
    timeout: float,
    classes: list[str] | None = None,
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for klass in classes or ALL_CLASSES:
        out_dir = root / "public" / klass
        existing = [p for p in out_dir.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES] if out_dir.exists() else []
        need = max(0, min(max_per_class, target_per_class) - len(existing))
        downloaded = 0
        if need == 0:
            counts[klass] = len(existing)
            continue
        for query in PUBLIC_QUERIES[klass]:
            for url in wikimedia_image_urls(query, max(need * 3, 20)):
                suffix = ".jpg"
                output = out_dir / safe_filename(url, suffix)
                if output.exists():
                    continue
                if download_url(url, output, timeout):
                    downloaded += 1
                    if downloaded >= need:
                        break
            if downloaded >= need:
                break
            time.sleep(0.4)
        counts[klass] = len(existing) + downloaded
    return counts


def download_file(url: str, output: Path, timeout: float) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.stat().st_size > 0:
        return
    with requests.get(url, timeout=timeout, stream=True, headers={"User-Agent": "m5-core-s3-vision-sampler/1.0"}) as response:
        response.raise_for_status()
        tmp = output.with_suffix(output.suffix + ".tmp")
        with tmp.open("wb") as handle:
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    handle.write(chunk)
        tmp.replace(output)


def download_coco_val_samples(
    root: Path,
    target_per_class: int,
    max_per_class: int,
    timeout: float,
    classes: list[str] | None = None,
) -> dict[str, int]:
    cache_dir = root / "cache"
    archive = cache_dir / "annotations_trainval2017.zip"
    download_file(COCO_ANNOTATIONS_URL, archive, timeout=max(timeout, 60.0))

    with zipfile.ZipFile(archive) as zf:
        with zf.open("annotations/instances_val2017.json") as handle:
            data = json.load(handle)

    category_name = {row["id"]: row["name"] for row in data["categories"]}
    images = {row["id"]: row for row in data["images"]}
    by_class: dict[str, list[int]] = {klass: [] for klass in ALL_CLASSES}
    seen: set[tuple[str, int]] = set()
    for ann in data["annotations"]:
        klass = COCO_CATEGORY_TO_CLASS.get(category_name.get(ann["category_id"], ""))
        if klass is None:
            continue
        key = (klass, ann["image_id"])
        if key in seen:
            continue
        seen.add(key)
        by_class[klass].append(ann["image_id"])

    counts: dict[str, int] = {}
    for klass in classes or ALL_CLASSES:
        out_dir = root / "public" / klass
        existing = [p for p in out_dir.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES] if out_dir.exists() else []
        need = max(0, min(max_per_class, target_per_class) - len(existing))
        downloaded = 0
        candidates = by_class.get(klass, [])
        RNG.shuffle(candidates)
        for image_id in candidates:
            if downloaded >= need:
                break
            image = images.get(image_id)
            if not image:
                continue
            url = image.get("coco_url") or f"http://images.cocodataset.org/val2017/{image['file_name']}"
            output = out_dir / f"coco_{image_id}.jpg"
            if output.exists():
                continue
            if download_url(url, output, timeout):
                downloaded += 1
        counts[klass] = len(existing) + downloaded
    return counts


def read_open_images_label_map(cache_dir: Path, timeout: float) -> dict[str, str]:
    descriptions = cache_dir / "open_images_class_descriptions.csv"
    download_file(OPEN_IMAGES_CLASS_DESCRIPTIONS_URL, descriptions, timeout=max(timeout, 60.0))

    display_to_class = {display.casefold(): klass for display, klass in OPEN_IMAGES_DISPLAY_TO_CLASS.items()}
    label_to_class: dict[str, str] = {}
    with descriptions.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.reader(handle):
            if len(row) < 2 or row[0] == "LabelName":
                continue
            label_name = row[0].strip()
            display_name = row[1].strip()
            klass = display_to_class.get(display_name.casefold())
            if klass:
                label_to_class[label_name] = klass
    return label_to_class


def read_open_images_urls(cache_dir: Path, timeout: float) -> dict[str, str]:
    image_rows = cache_dir / "open_images_validation_images.csv"
    download_file(OPEN_IMAGES_VALIDATION_IMAGES_URL, image_rows, timeout=max(timeout, 60.0))

    urls: dict[str, str] = {}
    with image_rows.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            image_id = row.get("ImageID", "")
            url = row.get("Thumbnail300KURL") or row.get("OriginalURL") or row.get("OriginalLandingURL")
            if image_id and url:
                urls[image_id] = url
    return urls


def read_open_images_candidates(
    cache_dir: Path,
    label_to_class: dict[str, str],
    timeout: float,
) -> dict[str, list[str]]:
    label_rows = cache_dir / "open_images_validation_human_labels.csv"
    download_file(OPEN_IMAGES_VALIDATION_LABELS_URL, label_rows, timeout=max(timeout, 60.0))

    raw_candidates: dict[str, list[str]] = {klass: [] for klass in ALL_CLASSES}
    image_classes: dict[str, set[str]] = {}
    with label_rows.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("Confidence") not in {"1", "1.0"}:
                continue
            image_id = row.get("ImageID", "")
            klass = label_to_class.get(row.get("LabelName", ""))
            if not image_id or not klass:
                continue
            image_classes.setdefault(image_id, set()).add(klass)
            raw_candidates[klass].append(image_id)

    positive_classes = set(CLASSES)
    candidates: dict[str, list[str]] = {klass: [] for klass in ALL_CLASSES}
    for klass, image_ids in raw_candidates.items():
        seen: set[str] = set()
        for image_id in image_ids:
            if image_id in seen:
                continue
            seen.add(image_id)
            classes = image_classes.get(image_id, set())
            if klass == "negative":
                if classes & positive_classes:
                    continue
            elif (classes & positive_classes) - {klass}:
                continue
            candidates[klass].append(image_id)
    return candidates


def read_open_images_bbox_candidates(
    cache_dir: Path,
    label_to_class: dict[str, str],
    timeout: float,
) -> dict[str, list[tuple[str, tuple[float, float, float, float]]]]:
    bbox_rows = cache_dir / "open_images_validation_bbox.csv"
    download_file(OPEN_IMAGES_VALIDATION_BBOX_URL, bbox_rows, timeout=max(timeout, 60.0))

    candidates: dict[str, list[tuple[str, tuple[float, float, float, float]]]] = {klass: [] for klass in CLASSES}
    seen: set[tuple[str, str, str, str, str, str]] = set()
    with bbox_rows.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("Confidence") not in {"1", "1.0"}:
                continue
            if row.get("IsGroupOf") == "1" or row.get("IsDepiction") == "1":
                continue
            klass = label_to_class.get(row.get("LabelName", ""))
            if klass not in CLASSES:
                continue
            image_id = row.get("ImageID", "")
            try:
                x_min = float(row.get("XMin", "0"))
                x_max = float(row.get("XMax", "0"))
                y_min = float(row.get("YMin", "0"))
                y_max = float(row.get("YMax", "0"))
            except ValueError:
                continue
            if not image_id or x_max <= x_min or y_max <= y_min:
                continue
            if (x_max - x_min) * (y_max - y_min) < 0.01:
                continue
            key = (image_id, klass, f"{x_min:.3f}", f"{x_max:.3f}", f"{y_min:.3f}", f"{y_max:.3f}")
            if key in seen:
                continue
            seen.add(key)
            candidates[klass].append((image_id, (x_min, x_max, y_min, y_max)))
    return candidates


def download_open_images_val_samples(
    root: Path,
    target_per_class: int,
    max_per_class: int,
    timeout: float,
    classes: list[str] | None = None,
) -> dict[str, int]:
    cache_dir = root / "cache"
    label_to_class = read_open_images_label_map(cache_dir, timeout)
    image_urls = read_open_images_urls(cache_dir, timeout)
    candidates = read_open_images_candidates(cache_dir, label_to_class, timeout)

    counts: dict[str, int] = {}
    for klass in classes or ALL_CLASSES:
        out_dir = root / "public" / klass
        existing = [p for p in out_dir.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES] if out_dir.exists() else []
        need = max(0, min(max_per_class, target_per_class) - len(existing))
        downloaded = 0
        image_ids = list(candidates.get(klass, []))
        RNG.shuffle(image_ids)
        for image_id in image_ids:
            if downloaded >= need:
                break
            url = image_urls.get(image_id)
            if not url:
                continue
            output = out_dir / f"oi_{image_id}.jpg"
            if output.exists():
                continue
            if download_url(url, output, timeout):
                downloaded += 1
        counts[klass] = len(existing) + downloaded
    return counts


def download_open_images_bbox_samples(
    root: Path,
    target_per_class: int,
    max_per_class: int,
    timeout: float,
    classes: list[str] | None = None,
) -> dict[str, int]:
    cache_dir = root / "cache"
    label_to_class = read_open_images_label_map(cache_dir, timeout)
    image_urls = read_open_images_urls(cache_dir, timeout)
    candidates = read_open_images_bbox_candidates(cache_dir, label_to_class, timeout)

    counts: dict[str, int] = {}
    selected = [klass for klass in classes or CLASSES if klass in CLASSES]
    for klass in selected:
        out_dir = root / "public" / klass
        existing = [p for p in out_dir.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES] if out_dir.exists() else []
        need = max(0, min(max_per_class, target_per_class) - len(existing))
        downloaded = 0
        rows = list(candidates.get(klass, []))
        RNG.shuffle(rows)
        for index, (image_id, bbox) in enumerate(rows):
            if downloaded >= need:
                break
            url = image_urls.get(image_id)
            if not url:
                continue
            output = out_dir / f"oibbox_{image_id}_{index}.jpg"
            if output.exists():
                continue
            if download_url_crop(url, output, bbox, timeout):
                downloaded += 1
        counts[klass] = len(existing) + downloaded
    return counts


def save_generated_image(arr: np.ndarray, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    image = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode="RGB")
    image.save(output, format="JPEG", quality=90)


def hard_negative_array(index: int, size: int = 128) -> np.ndarray:
    rng = random.Random(0x4e474154 + index * 7919)
    palette = [
        (238, 238, 232),
        (224, 226, 222),
        (205, 202, 194),
        (154, 150, 140),
        (116, 108, 96),
        (58, 58, 56),
        (32, 34, 36),
        (188, 172, 142),
    ]
    base = np.array(palette[index % len(palette)], dtype=np.float32)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    gradient = ((xx - size / 2) * rng.uniform(-0.10, 0.10) + (yy - size / 2) * rng.uniform(-0.10, 0.10))
    noise = np.random.default_rng(0xC0A500 + index).normal(0, rng.uniform(0.8, 4.2), (size, size, 1))
    arr = base.reshape(1, 1, 3) + gradient[:, :, None] + noise

    variant = index % 6
    if variant == 1:
        spacing = rng.randint(16, 28)
        line_color = base - rng.uniform(8, 18)
        for y in range(rng.randint(8, 16), size, spacing):
            arr[y:y + 1, :, :] = line_color
    elif variant == 2:
        for _ in range(5):
            x = rng.randint(0, size - 1)
            arr[:, max(0, x - 1):min(size, x + 1), :] -= rng.uniform(5, 14)
    elif variant == 3:
        cx = rng.uniform(size * 0.35, size * 0.65)
        cy = rng.uniform(size * 0.35, size * 0.65)
        dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        arr += np.maximum(0, 1 - dist / (size * 0.75))[:, :, None] * rng.uniform(8, 22)
    elif variant == 4:
        grain = np.sin(xx / rng.uniform(8, 18) + yy / rng.uniform(45, 80)) * rng.uniform(3, 10)
        arr += grain[:, :, None]
    elif variant == 5:
        shadow = np.maximum(0, (xx + yy - size * 0.9) / size) * rng.uniform(12, 30)
        arr -= shadow[:, :, None]

    return arr


def generate_hard_negative_samples(root: Path, count: int) -> dict[str, int]:
    out_dir = root / "generated" / "negative"
    existing = [p for p in out_dir.glob("hardneg_*.jpg") if p.is_file()] if out_dir.exists() else []
    for index in range(len(existing), count):
        output = out_dir / f"hardneg_{index:04d}.jpg"
        save_generated_image(hard_negative_array(index), output)
    total = len([p for p in out_dir.glob("hardneg_*.jpg") if p.is_file()]) if out_dir.exists() else 0
    return {"negative": total}


def synthetic_rows(klass: str, count: int) -> list[np.ndarray]:
    proto = np.array(SYNTHETIC_PROTOTYPES[klass], dtype=np.float32)
    noise = np.array([28, 28, 28, 30, 34, 34, 28, 28, 30, 30, 22, 22, 22, 30, 30, 30, 28, 28], dtype=np.float32)
    rows = []
    for _ in range(count):
        rows.append(np.array([clamp_u8(v + RNG.gauss(0, n)) for v, n in zip(proto, noise)], dtype=np.float32))
    return rows


def load_samples(root: Path) -> list[Sample]:
    samples: list[Sample] = []
    for klass in ALL_CLASSES:
        for path, source in image_files(root, klass):
            try:
                samples.append(Sample(path=path, klass=klass, source=source, features=extract_features(path)))
            except Exception:
                continue
    return samples


def write_manifest(root: Path, samples: list[Sample]) -> None:
    root.mkdir(parents=True, exist_ok=True)
    manifest = root / "manifest.csv"
    with manifest.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["path", "class", "source", "scene", "width", "height", "sha1"])
        writer.writeheader()
        for sample in samples:
            try:
                with Image.open(sample.path) as image:
                    width, height = image.size
                digest = hashlib.sha1(sample.path.read_bytes()).hexdigest()
            except Exception:
                width = height = 0
                digest = ""
            writer.writerow(
                {
                    "path": str(sample.path),
                    "class": sample.klass,
                    "source": sample.source,
                    "scene": infer_scene(sample),
                    "width": width,
                    "height": height,
                    "sha1": digest,
                }
            )


def distance(features: np.ndarray, prototype: np.ndarray, std: np.ndarray) -> float:
    std = np.maximum(std, 8.0)
    total = np.sum(FEATURE_WEIGHTS * np.abs(features - prototype) * MODEL_SCALE / std)
    return float(total * MODEL_SCALE / np.sum(FEATURE_WEIGHTS))


def split_samples(samples: list[Sample]) -> tuple[list[Sample], list[Sample]]:
    train: list[Sample] = []
    val: list[Sample] = []
    for klass in ALL_CLASSES:
        rows = [sample for sample in samples if sample.klass == klass]
        RNG.shuffle(rows)
        cut = max(1, int(len(rows) * 0.8)) if len(rows) > 1 else len(rows)
        train.extend(rows[:cut])
        val.extend(rows[cut:])
    return train, val


def class_matrix(samples: list[Sample], klass: str, min_count: int) -> np.ndarray:
    rows = [sample.features for sample in samples if sample.klass == klass]
    if rows:
        return np.vstack(rows).astype(np.float32)
    else:
        matrix = np.empty((0, len(FEATURES)), dtype=np.float32)
    if matrix.shape[0] < min_count:
        extra = np.vstack(synthetic_rows(klass, min_count - matrix.shape[0])).astype(np.float32)
        matrix = np.vstack([matrix, extra]) if matrix.size else extra
    return matrix


def sample_counts_by_scene(samples: list[Sample]) -> dict[str, dict[str, int]]:
    counts: dict[str, dict[str, int]] = {
        scene: {klass: 0 for klass in ALL_CLASSES}
        for scene in SCENES
    }
    for sample in samples:
        scene = infer_scene(sample)
        if scene not in counts:
            counts[scene] = {klass: 0 for klass in ALL_CLASSES}
        counts[scene][sample.klass] = counts[scene].get(sample.klass, 0) + 1
    return {scene: rows for scene, rows in counts.items() if any(rows.values())}


def update_scene_eval(scene_eval: dict[str, dict[str, object]], sample: Sample, predicted: str) -> None:
    scene = infer_scene(sample)
    row = scene_eval.setdefault(
        scene,
        {
            "total": 0,
            "positive_total": 0,
            "positive_correct": 0,
            "negative_total": 0,
            "negative_false_positive": 0,
            "reject": 0,
            "confusion": {klass: {candidate: 0 for candidate in CLASSES + ["reject"]} for klass in ALL_CLASSES},
        },
    )
    row["total"] = int(row["total"]) + 1
    if predicted == "reject":
        row["reject"] = int(row["reject"]) + 1
    confusion = row["confusion"]
    confusion.setdefault(sample.klass, {candidate: 0 for candidate in CLASSES + ["reject"]})
    confusion[sample.klass][predicted] = confusion[sample.klass].get(predicted, 0) + 1

    if sample.klass == "negative":
        row["negative_total"] = int(row["negative_total"]) + 1
        if predicted != "reject":
            row["negative_false_positive"] = int(row["negative_false_positive"]) + 1
    else:
        row["positive_total"] = int(row["positive_total"]) + 1
        if predicted == sample.klass:
            row["positive_correct"] = int(row["positive_correct"]) + 1


def finalize_scene_eval(scene_eval: dict[str, dict[str, object]]) -> dict[str, dict[str, object]]:
    finalized: dict[str, dict[str, object]] = {}
    for scene, row in scene_eval.items():
        positive_total = int(row["positive_total"])
        negative_total = int(row["negative_total"])
        total = int(row["total"])
        finalized[scene] = {
            "total": total,
            "positive_total": positive_total,
            "positive_accuracy": (int(row["positive_correct"]) / positive_total) if positive_total else 0.0,
            "negative_total": negative_total,
            "negative_false_positive_rate": (
                int(row["negative_false_positive"]) / negative_total
            ) if negative_total else 0.0,
            "reject_rate": (int(row["reject"]) / total) if total else 0.0,
            "confusion": row["confusion"],
        }
    return finalized


def build_model(samples: list[Sample]) -> tuple[dict[str, object], dict[str, object]]:
    train, val = split_samples(samples)
    prototypes = []
    stds = []
    thresholds = []
    train_counts = {klass: len([s for s in samples if s.klass == klass]) for klass in ALL_CLASSES}

    for klass in CLASSES:
        matrix = class_matrix(train, klass, 24)
        proto = matrix.mean(axis=0)
        std = np.maximum(matrix.std(axis=0), 8.0)
        own_dist = np.array([distance(row, proto, std) for row in matrix], dtype=np.float32)
        threshold = float(np.percentile(own_dist, 96) + 35.0)
        prototypes.append(proto)
        stds.append(std)
        thresholds.append(max(40.0, min(240.0, threshold)))

    negative_matrix = class_matrix(train, "negative", 32)
    negative_proto = negative_matrix.mean(axis=0)
    negative_std = np.maximum(negative_matrix.std(axis=0), 8.0)
    negative_dist = np.array([distance(row, negative_proto, negative_std) for row in negative_matrix], dtype=np.float32)
    real_negative_count = len([sample for sample in samples if sample.klass == "negative"])
    if real_negative_count < 10:
        negative_threshold = 0.0
    else:
        negative_threshold = max(35.0, min(220.0, float(np.percentile(negative_dist, 85) + 12.0)))

    proto_arr = np.vstack(prototypes)
    std_arr = np.vstack(stds)
    threshold_arr = np.array(thresholds, dtype=np.float32)

    confusion = {klass: {candidate: 0 for candidate in CLASSES + ["reject"]} for klass in ALL_CLASSES}
    errors: dict[str, list[str]] = {klass: [] for klass in ALL_CLASSES}
    positive_total = 0
    positive_correct = 0
    negative_total = 0
    negative_false_positive = 0
    scene_eval: dict[str, dict[str, object]] = {}

    eval_rows = val if val else samples
    for sample in eval_rows:
        neg_d = distance(sample.features, negative_proto, negative_std)
        distances = np.array([distance(sample.features, proto_arr[i], std_arr[i]) for i in range(len(CLASSES))])
        order = np.argsort(distances)
        best = int(order[0])
        second = int(order[1])
        margin = float(distances[second] - distances[best])
        predicted = "reject"
        negative_ok = negative_threshold == 0 or neg_d > negative_threshold
        if negative_ok and distances[best] <= threshold_arr[best] and margin >= 3.0:
            predicted = CLASSES[best]

        update_scene_eval(scene_eval, sample, predicted)
        confusion.setdefault(sample.klass, {candidate: 0 for candidate in CLASSES + ["reject"]})
        confusion[sample.klass][predicted] = confusion[sample.klass].get(predicted, 0) + 1
        if sample.klass == "negative":
            negative_total += 1
            if predicted != "reject":
                negative_false_positive += 1
                if len(errors[sample.klass]) < 5:
                    errors[sample.klass].append(str(sample.path))
        else:
            positive_total += 1
            if predicted == sample.klass:
                positive_correct += 1
            elif len(errors[sample.klass]) < 5:
                errors[sample.klass].append(str(sample.path))

    positive_accuracy = positive_correct / positive_total if positive_total else 0.0
    negative_false_positive_rate = negative_false_positive / negative_total if negative_total else 0.0

    data_quality = "ok"
    if any(train_counts[klass] < 30 for klass in CLASSES) or train_counts["negative"] < 50:
        data_quality = "weak"
    if positive_accuracy < 0.65 or negative_false_positive_rate > 0.15:
        data_quality = "weak"

    model = {
        "prototypes": proto_arr,
        "stds": std_arr,
        "thresholds": threshold_arr,
        "negative_prototype": negative_proto,
        "negative_std": negative_std,
        "negative_threshold": negative_threshold,
    }
    report = {
        "classes": CLASSES,
        "features": FEATURES,
        "sample_counts": train_counts,
        "scene_sample_counts": sample_counts_by_scene(samples),
        "scene_eval": finalize_scene_eval(scene_eval),
        "data_quality": data_quality,
        "positive_accuracy": positive_accuracy,
        "negative_false_positive_rate": negative_false_positive_rate,
        "confusion": confusion,
        "errors": errors,
    }
    return model, report


def emit_array_2d(name: str, values: np.ndarray, c_type: str = "uint8_t") -> list[str]:
    rows = [f"static const {c_type} {name}[kVisionClassCount][kVisionFeatureCount] = {{"]
    for row in values:
        rows.append("    {" + ", ".join(str(clamp_u8(v)) for v in row) + "},")
    rows.append("};")
    return rows


def emit_header(model: dict[str, object], report: dict[str, object]) -> str:
    prototypes = model["prototypes"]
    stds = model["stds"]
    thresholds = model["thresholds"]
    negative_prototype = model["negative_prototype"]
    negative_std = model["negative_std"]
    negative_threshold = model["negative_threshold"]
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "// Generated by scripts/train_vision_feature_model.py.",
        "// Lightweight prototype-distance classifier for CoreS3 offline vision.",
        f"// Data quality: {report['data_quality']}",
        f"// Positive validation accuracy: {report['positive_accuracy']:.3f}",
        f"// Negative false positive rate: {report['negative_false_positive_rate']:.3f}",
        "static constexpr uint16_t kVisionInputWidth = 64;",
        "static constexpr uint16_t kVisionInputHeight = 64;",
        "static constexpr uint8_t kVisionInputChannels = 3;",
        "static constexpr uint32_t kVisionInputBytes =",
        "    static_cast<uint32_t>(kVisionInputWidth) * kVisionInputHeight * kVisionInputChannels;",
        f"static constexpr uint8_t kVisionFeatureCount = {len(FEATURES)};",
        f"static constexpr uint8_t kVisionClassCount = {len(CLASSES)};",
        f"static constexpr uint8_t kVisionDistanceScale = {MODEL_SCALE};",
        "static constexpr uint16_t kVisionMinDistanceMargin = 3;",
        f"static constexpr bool kVisionModelQualityWeak = {'true' if report['data_quality'] == 'weak' else 'false'};",
        "",
        "static const char* const kVisionClassLabels[kVisionClassCount] = {",
    ]
    lines += [f'    "{name}",' for name in CLASSES]
    lines += [
        "};",
        "",
        "static const char* const kVisionFeatureNames[kVisionFeatureCount] = {",
    ]
    lines += [f'    "{name}",' for name in FEATURES]
    lines += [
        "};",
        "",
        "static const uint8_t kVisionFeatureWeights[kVisionFeatureCount] = {",
        "    " + ", ".join(str(int(v)) for v in FEATURE_WEIGHTS.astype(int)),
        "};",
        "",
    ]
    lines += emit_array_2d("kVisionPrototype", prototypes)
    lines += [""]
    lines += emit_array_2d("kVisionStd", stds)
    lines += [
        "",
        "static const uint16_t kVisionClassThresholds[kVisionClassCount] = {",
        "    " + ", ".join(str(int(round(v))) for v in thresholds),
        "};",
        "",
        "static const uint8_t kVisionNegativePrototype[kVisionFeatureCount] = {",
        "    " + ", ".join(str(clamp_u8(v)) for v in negative_prototype),
        "};",
        "",
        "static const uint8_t kVisionNegativeStd[kVisionFeatureCount] = {",
        "    " + ", ".join(str(clamp_u8(max(8, v))) for v in negative_std),
        "};",
        "",
        f"static constexpr uint16_t kVisionNegativeThreshold = {int(round(negative_threshold))};",
        "",
        "static const uint8_t kVisionModelData[] = {",
        "    0x4d, 0x35, 0x50, 0x52, 0x4f, 0x54, 0x4f, 0x31",
        "};",
        "static constexpr uint32_t kVisionModelDataLen = sizeof(kVisionModelData);",
        "",
    ]
    return "\n".join(lines)


def parse_class_filter(value: str | None) -> list[str] | None:
    if not value:
        return None
    classes = [item.strip() for item in value.split(",") if item.strip()]
    invalid = sorted(set(classes) - set(ALL_CLASSES))
    if invalid:
        raise SystemExit(f"unknown class filter: {', '.join(invalid)}")
    return classes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train the CoreS3 lightweight vision prototype model.")
    parser.add_argument("--samples", type=Path, default=Path(r"C:\tmp\m5_vision_samples"))
    parser.add_argument("--out", type=Path, default=Path("arduino_demos/04_camera_pet_battle/vision_model_data.h"))
    parser.add_argument("--report", type=Path, default=Path(r"C:\tmp\m5_vision_samples\report.json"))
    parser.add_argument("--download-public", action="store_true")
    parser.add_argument("--download-coco-val", action="store_true")
    parser.add_argument("--download-open-images-val", action="store_true")
    parser.add_argument("--download-open-images-bbox", action="store_true")
    parser.add_argument("--generate-hard-negatives", action="store_true")
    parser.add_argument("--hard-negative-count", type=int, default=240)
    parser.add_argument("--classes", help="Comma-separated class filter for download steps.")
    parser.add_argument("--target-per-class", type=int, default=80)
    parser.add_argument("--max-per-class", type=int, default=160)
    parser.add_argument("--download-timeout", type=float, default=18.0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.samples.mkdir(parents=True, exist_ok=True)
    selected_classes = parse_class_filter(args.classes)

    if args.download_public:
        counts = download_public_samples(
            args.samples,
            target_per_class=args.target_per_class,
            max_per_class=args.max_per_class,
            timeout=args.download_timeout,
            classes=selected_classes,
        )
        print("downloaded/available public samples:", counts)
    if args.download_coco_val:
        counts = download_coco_val_samples(
            args.samples,
            target_per_class=args.target_per_class,
            max_per_class=args.max_per_class,
            timeout=args.download_timeout,
            classes=selected_classes,
        )
        print("downloaded/available COCO val samples:", counts)
    if args.download_open_images_val:
        counts = download_open_images_val_samples(
            args.samples,
            target_per_class=args.target_per_class,
            max_per_class=args.max_per_class,
            timeout=args.download_timeout,
            classes=selected_classes,
        )
        print("downloaded/available Open Images val samples:", counts)
    if args.download_open_images_bbox:
        counts = download_open_images_bbox_samples(
            args.samples,
            target_per_class=args.target_per_class,
            max_per_class=args.max_per_class,
            timeout=args.download_timeout,
            classes=selected_classes,
        )
        print("downloaded/available Open Images bbox crops:", counts)
    if args.generate_hard_negatives:
        counts = generate_hard_negative_samples(args.samples, args.hard_negative_count)
        print("generated hard negatives:", counts)

    samples = load_samples(args.samples)
    if not samples:
        print("no image samples found; generating synthetic fallback model")
    write_manifest(args.samples, samples)

    model, report = build_model(samples)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(emit_header(model, report), encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"wrote {args.out}")
    print(f"wrote {args.report}")
    print(f"positive_accuracy={report['positive_accuracy']:.3f}")
    print(f"negative_false_positive_rate={report['negative_false_positive_rate']:.3f}")
    print(f"data_quality={report['data_quality']}")
    for scene in ["white_wall", "white_paper", "desktop", "glare", "dark", "bright"]:
        metrics = report["scene_eval"].get(scene)
        if metrics:
            print(
                f"scene {scene}: total={metrics['total']} "
                f"pos_acc={metrics['positive_accuracy']:.3f} "
                f"neg_fp={metrics['negative_false_positive_rate']:.3f} "
                f"reject={metrics['reject_rate']:.3f}"
            )


if __name__ == "__main__":
    main()
