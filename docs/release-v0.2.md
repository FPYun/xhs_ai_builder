# v0.2 Release Notes

Project name: `xhs_ai_builder`

Release date: 2026-05-29

## Included

- Main CoreS3 sketch moved to `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- Public type split:
  - `pet_model.h`
  - `vision_types.h`
  - `ui_types.h`
  - `battle_protocol.h`
- Local camera capture, subject detection, lightweight 8-class feature recognition, and five-element pet generation.
- Capture failure path for unclear, dark, flat, or low-confidence frames.
- Backpack capture, select, release, release confirmation, growth fields, XP, wins, battle count, and win rate.
- Backpack persistence through ESP32 `Preferences`.
- Wi-Fi AP + UDP local two-board battle transport.
- Role-neutral player-facing MATCH/BATTLE documentation with developer diagnostics kept in serial/protocol docs.
- BATTLE clash/result flow, XP reward rules, and RAM-only rematch friendship bonus planning.
- Mute-capable local sound effect planning; no voice recognition.
- Productization roadmap for App, desktop MVP, cloud hints, SD storage, dataset/training, and community features.
- Local HTTP management API and Safari `/app` phone page for the first phone companion workflow.
- Protocol documentation and interface-change templates that explicitly avoid modifying `BattlePetPacket` without architecture review.
- Desktop-side `scripts/train_vision_feature_model.py` for generating `vision_model_data.h`.

## Current Runtime Boundaries

- The firmware remains local/offline.
- No cloud API is called.
- No API key is stored in the repository.
- No image upload is performed.
- No external network is required.
- No SD card is required.
- No voice recognition is implemented.
- Recognition is lightweight limited-class local recognition, not a general vision large model.
- CoreS3 local firmware does not run YOLO, CLIP, or a visual large model.
- App, cloud recognition, LLM text generation, SD card storage, and community features are future reserved directions, not current runtime requirements.

## Known Limitations

- Recognition quality depends on the current 8-class feature model and heuristic fallback.
- SD card sample/log/model management is documented but not implemented as a runtime dependency.
- The first local App HTTP management API and Safari `/app` page are implemented, but BLE discovery and cloud sync are not.
- Cloud hints remain behind reserved no-op/local extension points.
- Rematch friendship is RAM-only unless a future storage interface change is approved.
- Full dual-board physical validation still requires two connected CoreS3 boards.

## v0.2 Validation Checklist

- Compile the main sketch.
- Flash the same firmware to two CoreS3 boards.
- Confirm IDLE loads and the three touch zones are usable.
- Tap PHOTO to recognize a clear subject and generate a wild pet only on success.
- Confirm blank, flat, dark, or low-confidence frames enter CAPTURE FAILED and cannot be captured.
- Capture a wild pet into the backpack.
- Confirm BAG shows active/stored state, level, XP, element, wins, battle count, and win rate.
- Select a backpack pet and return to IDLE.
- Enter MATCH on both boards.
- Confirm player-facing UI uses neutral link states instead of HOST/CLIENT.
- Confirm BATTLE shows a clash phase before result.
- Confirm battle result updates XP and battle record.
- Confirm sound cues respect the mute setting.
- Confirm no voice-recognition entry point is present.

## Verification Used For Tagging

Compile command:

```powershell
.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_v02
```

Compile result:

```text
Sketch uses 1051377 bytes (33%) of program storage space. Maximum is 3145728 bytes.
Global variables use 64744 bytes (19%) of dynamic memory, leaving 262936 bytes for local variables. Maximum is 327680 bytes.
```
