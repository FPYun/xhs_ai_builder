# Development Log

## v0.1 Snapshot

This snapshot records the current CoreS3 prototype state before publishing to GitHub as `xhs_ai_builder` v0.1.

## Implemented Flow

1. Basic CoreS3 flashing and official firmware recovery path.
2. Local Arduino demos for display, touch, Wi-Fi scan, and camera/voice.
3. Voice/touch photo trigger.
4. Camera feature extraction into local image traits.
5. Five-element pet generation with visible species and gene variation.
6. Offline ESP-SR command word recognition.
7. Backpack capture, selection, release, and release confirmation.
8. Pet growth fields and simple level/stat progression.
9. Scene, pet, battle, and startup sound system.
10. Two-board battle flow.
11. Communication replacement from ESP-NOW to Wi-Fi AP + UDP for easier pairing and debugging.

## Current Operation Notes

- `PHOTO` captures a wild pet.
- `BAG` opens stored pets.
- `MATCH` starts peer discovery and battle.
- Both boards run identical firmware.
- The matching screen exposes role and packet counters to diagnose communication:
  - `HOST` or `CLIENT`
  - UDP port
  - `TX`
  - `RX`
  - `F`

## Reserved Work

- Real image recognition or cloud LLM classification.
- SD card storage for snapshots, history, and audio assets.
- Wi-Fi API integration for remote pet hints.
- More robust physical button input through the existing external action interfaces.
- Better memory budgeting and optional asset loading from SD card.

