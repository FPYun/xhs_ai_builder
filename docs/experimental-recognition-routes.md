# Experimental Recognition Routes

This document defines optional recognition experiments for the CoreS3 pet battle
project. None of these routes is required for the current offline Arduino
firmware, and none may change `RecognitionResult`, `PetGenes`,
`BackpackStorage`, or `BattlePetPacket` without a separate interface proposal.

## Current Mainline Boundary

- Main firmware remains `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- The current runtime path is still local/offline: camera frame, lightweight
  preprocessing, subject presence, 8-class recognition, `RecognitionResult`,
  and pet generation.
- Experimental routes must output only `classId`, `confidence`, `presence`, and
  optional local diagnostics that can be mapped into the existing
  `RecognitionResult`.
- No route may become a hard dependency for capture. If the experiment is
  missing, slow, or low confidence, the firmware must fall back to the current
  local recognition and failure gates.

## Edge Impulse / FOMO Branch

Purpose: test a tiny vision model with locality information without bringing
YOLO/CLIP-scale models onto CoreS3.

Recommended branch shape:

- Keep it outside the main Arduino sketch until measured.
- Use exported C++/Arduino inference code only in the experiment branch.
- Start with grayscale `64x64` or `96x96` input.
- Keep current 8 labels plus `negative` as the target taxonomy.
- Use CoreS3 real captures first; public images only fill missing coverage.
- Map model output to existing `ObjectClass` and `RecognitionResult`.
- Use `experiments/edge_impulse_fomo/edge_output_adapter.py` to validate
  exported classification or FOMO-style `bounding_boxes` output before any
  firmware branch consumes it.
- Main firmware accepts `EDGE_HINT <class> <confidence> <presence>` over USB
  serial as a one-shot bench hint. This does not add the Edge Impulse runtime to
  mainline firmware; it only lets the desktop adapter exercise the existing
  `RecognitionResult` gate.

Acceptance metrics before mainline discussion:

- Firmware compile passes with the model enabled.
- Free heap after camera init remains high enough for capture and App HTTP.
- Inference time is logged per frame.
- White wall, white paper, desktop, dark, glare, and far-object scenes mostly
  fail instead of becoming water/metal.
- 8-class validation accuracy is better than the current prototype model on
  CoreS3 real samples.

Source notes:

- Edge Impulse FOMO is documented as an object-detection approach for
  constrained devices and outputs heat-map/centroid style locality rather than
  full YOLO-style boxes:
  https://docs.edgeimpulse.com/studio/projects/learning-blocks/blocks/object-detection/fomo
- Edge Impulse provides Arduino deployment documentation for exported
  inference libraries:
  https://docs.edgeimpulse.com/docs/run-inference/arduino-library

## HuskyLens Optional Module

Purpose: test an external AI camera as an optional recognition coprocessor for
users who accept extra hardware.

Recommended integration boundary:

- Connect over I2C or UART in an experiment branch only.
- Treat HuskyLens output as a hint, not as guaranteed truth.
- Translate learned IDs into the existing 8 `ObjectClass` values by a local
  mapping table.
- Use `experiments/huskylens/huskylens_bridge.py` to validate the mapping and
  fallback behavior before creating a firmware branch.
- Main firmware accepts `HUSKY_HINT <class> <confidence> <presence>` over USB
  serial as a one-shot bench hint. This avoids adding the HuskyLens Arduino
  library until a separate hardware branch proves the module path.
- If the module is absent, returns timeout, or produces an unmapped ID, continue
  with the current CoreS3 local recognition path.
- Do not add bbox, logits, or module metadata to public headers without an
  interface proposal.

Acceptance metrics:

- CoreS3 still boots and captures without the module attached.
- Module-present path logs ID, mapped class, confidence/presence hint, timeout,
  and fallback reason.
- White wall and white paper remain negative unless the external module gives a
  deliberate trained object ID.

Source note:

- DFRobot documents HuskyLens as a no-code AI vision sensor with STEM-oriented
  object/vision functions:
  https://wiki.dfrobot.com/HUSKYLENS_V1.0_SKU_SEN0305_SEN0336

## ESP-IDF + ESP-DL / ESP-WHO Branch

Purpose: evaluate whether an ESP-IDF version can host a stronger model and
camera pipeline than the current Arduino sketch.

Recommended branch shape:

- Create a separate ESP-IDF proof-of-concept; do not mix ESP-IDF camera/model
  framework code into the Arduino mainline. The initial standalone skeleton is
  under `experiments/esp_idf_vision_poc/`.
- First milestone is camera acquisition from the CoreS3 GC0308 path and memory
  stability, not classification accuracy.
- Second milestone is one small model with logged heap, PSRAM use, flash size,
  and per-frame inference time.
- Only after those pass should common recognition mapping be shared back into
  the Arduino project.

Acceptance metrics:

- Clean ESP-IDF build for ESP32-S3 target.
- Camera frame capture and resize/crop path works repeatedly.
- Model binary plus image buffers fit flash/RAM/PSRAM with App-equivalent
  diagnostics.
- Output can still be reduced to existing `RecognitionResult` semantics.

Source notes:

- ESP-DL is Espressif's deep-learning library for ESP32-family targets,
  including ESP32-S3:
  https://docs.espressif.com/projects/esp-dl/en/release-v1.1/esp32s3/introduction.html
- ESP-WHO is Espressif's camera/vision framework; use it as an ESP-IDF PoC
  reference, not an Arduino mainline dependency:
  https://github.com/espressif/esp-who

## Interface Rule

These routes are allowed to create experiment-only adapters. They are not
allowed to persist new fields, change UDP payloads, or change public headers.
If a future experiment needs bbox, logits, skill IDs, remote model IDs, or
sample metadata in shared storage/protocols, write an interface-change proposal
first.
