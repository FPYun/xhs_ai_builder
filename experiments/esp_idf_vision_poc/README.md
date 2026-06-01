# ESP-IDF + ESP-DL / ESP-WHO Vision PoC

Purpose: evaluate a future ESP-IDF implementation without disturbing the
current Arduino/CoreS3 firmware.

This folder now contains a standalone ESP-IDF project skeleton. The current
`main/main.c` is a contract smoke test only; camera capture, ESP-DL, and
ESP-WHO are not wired yet.

See `README-build.md` for build commands and boundaries.

## Milestones

1. Create an ESP-IDF project targeting ESP32-S3.
2. Bring up CoreS3 camera frame acquisition and repeated capture stability.
3. Add resize/crop preprocessing equivalent to the Arduino firmware.
4. Run one tiny model and log flash, PSRAM, free heap, and inference time.
5. Reduce output to the existing `RecognitionResult` semantics.

## Hard Boundaries

- Do not copy ESP-IDF framework code into `04_camera_pet_battle.ino`.
- Do not claim ESP-DL/ESP-WHO support until it builds and runs on the target.
- Do not change the public headers or UDP packet for this PoC.

## Acceptance Gate

- Clean ESP-IDF build.
- Stable repeated frame capture.
- Inference latency and heap logs.
- Same negative-scene test set as the Arduino firmware.
- A written migration proposal before any shared-code move.
