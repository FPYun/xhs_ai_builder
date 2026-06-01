# Dataset Card: CoreS3 Pet Capture Samples

## Dataset Summary

CoreS3 camera samples collected for the 8-class local pet generation recognizer.
Images are exported from the device SD card sample mode and paired with
`manifest.csv` rows.

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

## Negative Scenes

- `white_wall`
- `white_paper`
- `desktop`
- `glare`
- `dark`
- `bright`
- `far_object`
- `hand_occlusion`

## Device

- Board: M5Stack CoreS3
- Camera: CoreS3 onboard camera
- Runtime: local/offline firmware
- Export path: `/samples/manifest.csv` and `/samples/*.ppm`

## Splits

Describe train/validation split here. Keep samples from the same physical
object in one split when possible.

## License

Fill in before publishing. Do not publish private photos, people, or
third-party content without permission.

## Known Limitations

- Low-resolution embedded camera domain.
- Lighting and background strongly affect recognition.
- Negative scenes are part of the model behavior, not an afterthought.

## Training Report

Attach `report.json` from `scripts/train_vision_feature_model.py` and record
class accuracy plus negative false-positive rate.
