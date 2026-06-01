# Open Source Release Package

This is the release checklist for publishing the CoreS3 pet battle project to
OSHWHub, OSHWHLab, Hackaday, Arduino community posts, or similar open-source
hardware platforms.

## Current Package Contents

- Firmware: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino` plus
  the four public headers and model/audio headers in the same folder.
- Flashing guide: `docs/flashing.md`.
- Local App API: `docs/app-http-api.md`.
- SD file boundary: `docs/sd-card-file-boundary.md`.
- SD audio payload: `sd_card_payload/` and `sd_card_payload/manifest.csv`.
- Acceptance checklist: `docs/device-acceptance.md`.
- Release skeleton: `release/oshw/`.
- Experiment skeletons: `experiments/`.

## Required Before Public Posting

- Run `python .\scripts\check-open-release-readiness.py` and resolve every
  `FAIL`; keep every unresolved `PENDING` item explicitly marked in the release
  notes.
- Generate or refresh the draft dataset card with
  `python .\scripts\build-hf-dataset-card.py --samples-root C:\tmp\m5_vision_samples`.
- Generate the sanitized dataset manifest draft with
  `python .\scripts\export_hf_dataset_manifest.py --samples-root C:\tmp\m5_vision_samples --out .\release\oshw\hf-dataset-manifest.csv`.
- Run `python .\scripts\audit_hf_dataset_publishability.py --samples-root C:\tmp\m5_vision_samples`
  before preparing a Hugging Face upload; resolve `FAIL` items and keep
  unresolved `PENDING` items visible in the dataset card.
- Run `python .\scripts\audit_vision_scene_coverage.py --samples-root C:\tmp\m5_vision_samples`
  and keep missing scene coverage marked as `PENDING`.
- Run `python .\scripts\validate-sd-payload.py --payload .\sd_card_payload`
  before copying or publishing the optional SD audio/skin/action package.
- For final publication, run the release readiness check with `--strict`.
- Build and record one clean compile result.
- Flash and record COM port, MAC, hash verification, and hard reset result for
  each available board.
- Keep the dual-board battle result marked hardware-blocked when the second
  ESP32-S3 is not onsite.
- Verify the single-board phone flow: join `M5PET-xxxxxx`, open `/app`, call
  `/api/v1/status`, `/api/v1/storage`, and `/api/v1/samples`.
- Include the SD root layout and audio format: unsigned 8-bit mono PCM,
  22050 Hz, `.raw`, each file under 32768 bytes.
- Keep malformed optional SD payloads out of public archives; the release
  readiness checker reports this as `sd_payload_validation`.

## Do Not Publish

- Cloud API keys, access tokens, or private proxy URLs.
- Private photos, private training samples, or captures containing people unless
  they are intentionally licensed for redistribution.
- Third-party audio, image, or model assets without a compatible license.
- Claims that BLE, router-mode LAN discovery, cloud sync, or dual-board
  verification are complete before they have matching code and hardware logs.

## Future Additions

- Enclosure/STL, wiring photos, and assembly notes.
- Actual public cover image and assembled-device photos.
- Filled verification log from `release/oshw/verification-log-template.csv`.
- Filled Hugging Face dataset card based on
  `release/oshw/hf-dataset-card-template.md`.
- Interface-change proposal if future releases persist new pet fields or extend
  battle synchronization beyond the current `BattlePetPacket`.
