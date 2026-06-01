# Dataset Card: CoreS3 Pet Capture Samples

Generated: 2026-06-01

Publication status: draft - do not upload until license, privacy, and sample provenance are reviewed.
License status: pending-review

## Dataset Summary

This draft describes local CoreS3 camera samples for the 8-class pet
capture recognizer. The dataset is intended for lightweight feature
training and negative-scene validation, not for cloud runtime inference.

Images are not stored in this repository. The local manifest points to
sample files under the operator's sample root.

## Labels

- `plant_leaf`
- `food_fruit`
- `paper_book`
- `electronics_screen`
- `metal_key_coin`
- `fabric_cloth`
- `cup_bottle_water`
- `toy_figure`
- `negative`

## Device And Capture Context

- Board: M5Stack CoreS3
- Camera: CoreS3 onboard camera
- Runtime: local/offline firmware
- Firmware export: `/samples/manifest.csv` and optional `/samples/*.ppm` thumbnails
- Public images, if present, are development supplements and must be license-checked before redistribution.

## Manifest Summary

- Rows: 2417
- Columns: path, class, source, width, height, sha1

## Manifest Class Counts

| Value | Count |
| --- | ---: |
| `cup_bottle_water` | 260 |
| `electronics_screen` | 260 |
| `fabric_cloth` | 260 |
| `food_fruit` | 260 |
| `metal_key_coin` | 211 |
| `negative` | 400 |
| `paper_book` | 246 |
| `plant_leaf` | 260 |
| `toy_figure` | 260 |

## Manifest Source Counts

| Value | Count |
| --- | ---: |
| `generated` | 240 |
| `public` | 2177 |

## Manifest Scene Counts

| Value | Count |
| --- | ---: |
| not recorded | 0 |

## Training Report Summary

- Data quality: `weak`
- Positive accuracy: 8.9%
- Negative false-positive rate: 15.0%

## Training Sample Counts

| Value | Count |
| --- | ---: |
| `cup_bottle_water` | 260 |
| `electronics_screen` | 260 |
| `fabric_cloth` | 260 |
| `food_fruit` | 260 |
| `metal_key_coin` | 211 |
| `negative` | 400 |
| `paper_book` | 246 |
| `plant_leaf` | 260 |
| `toy_figure` | 260 |

## Scene Evaluation

No scene evaluation is recorded in the current report.

## Required Negative Scene Coverage

Coverage status: `INCOMPLETE` from `missing`.

| Scene | Negative samples | Total samples | Status |
| --- | ---: | ---: | --- |
| `white_wall` | 0 | 0 | PENDING |
| `white_paper` | 0 | 0 | PENDING |
| `desktop` | 0 | 0 | PENDING |
| `glare` | 0 | 0 | PENDING |
| `dark` | 0 | 0 | PENDING |

## Privacy And License Review

- Remove private photos, people, screens, documents, and location-identifying backgrounds before publication.
- Verify every public supplemental source license before uploading images or derived thumbnails.
- Keep this card in draft status until the release owner records a publishable license.

## Known Limitations

- CoreS3 camera resolution and lighting strongly affect the feature model.
- Public web samples do not fully match the embedded camera domain.
- Negative scenes such as white wall, white paper, desktop, glare, and dark light need real CoreS3 coverage.

## Rebuild Command

```powershell
python .\scripts\build-hf-dataset-card.py --samples-root C:\tmp\m5_vision_samples
```
