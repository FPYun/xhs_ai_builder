# ESP-IDF PoC Build Notes

This folder is a standalone ESP-IDF project skeleton for an ESP32-S3 vision
experiment. It is intentionally separate from
`arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.

## Build

Run from this directory after ESP-IDF is installed and exported:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM7 flash monitor
```

The current `main/main.c` is only a contract smoke test. It logs heap values and
checks that a future model output can be reduced to:

- existing class label
- confidence
- presence
- fallback reason

## Next Bring-Up Steps

1. Add CoreS3 GC0308 camera capture in this ESP-IDF project.
2. Add the same resize/crop feature order used by the Arduino sketch.
3. Add ESP-DL or ESP-WHO only after camera capture is stable.
4. Log model size, PSRAM use, free heap, and inference time.
5. Compare against the current Arduino feature model before discussing a
   mainline migration.

## Boundary

- Do not include this project from the Arduino sketch.
- Do not copy ESP-IDF camera/model code into production firmware before
  acceptance logs pass.
- Do not change `PetGenes`, `RecognitionResult`, backpack storage, or
  `BattlePetPacket` from this PoC.
