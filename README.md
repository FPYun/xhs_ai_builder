# xhs_ai_builder

CoreS3 local offline pet demo for camera capture, lightweight limited-class recognition, five-element pet generation, backpack management, growth, muteable sound effects, and board-to-board battle flow.

## v0.2 Scope

- Hardware target: M5Stack CoreS3 / ESP32-S3.
- Main sketch: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- Local subject detection, lightweight 8-class trained feature recognition, and five-element pet generation. This is not a general vision large model.
- Backpack flow: capture, select, release with confirmation, up to 6 pets.
- Backpack persistence uses ESP32 `Preferences`.
- Growth flow: level, stage, XP, battle count, wins, and win rate.
- Battle transport: Wi-Fi SoftAP + UDP automatic peer discovery, no router required.
- Battle flow: MATCH, short CLASH phase, result, XP reward, and local rematch friendship bonus.
- Sound system: idle, wild, bag, battle, result, pet/element feedback, startup melody, and a mute switch.
- No voice recognition is implemented in the current firmware.
- Extension interfaces remain no-op/local for later App, cloud, SD card, and model recognition work.
- Operation is by touch or reserved external action interfaces.

## Repository Layout

```text
arduino_demos/
  01_hello_cores3/             Minimal display demo
  02_touch_test/               Touch input demo
  03_wifi_scan/                Wi-Fi scan demo
  04_camera_pet_battle/        Main v0.2 pet system
firmware/official/             Reference official M5Burner firmware binaries and manifest
scripts/                       PowerShell helper scripts for board info, build, flashing, and model-data generation
docs/                          Usage, flashing, architecture, protocol, roadmap, module tasks, and release notes
```

## Quick Build

Compile the main sketch:

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_v02
```

Upload through Arduino CLI if it works on your port:

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_v02 `
  -Upload `
  -Port COM7
```

For the more reliable direct flash path, see [docs/flashing.md](docs/flashing.md).

## Basic Operation

- Idle screen:
  - Left: BAG
  - Middle: MATCH
  - Right: PHOTO
- Tap PHOTO to capture a wild pet.
- If no clear centered object is recognized, capture fails and no pet can be stored.
- In the wild pet screen, capture the pet into the backpack or release it.
- In BAG, release, select and return to idle, or move to the next pet.
- In MATCH, put two boards close together. The screens show neutral finding/connected states while the boards pair locally.
- BATTLE shows a short clash phase, then local result and XP reward.

More details are in [docs/usage.md](docs/usage.md).

## Productization Roadmap

Planning documents:

- [docs/app-cloud-roadmap.md](docs/app-cloud-roadmap.md): App information architecture, page list, local MVP, cloud/LLM, SD card, dataset, community, and firmware interface planning.
- [docs/player-flow-ui.md](docs/player-flow-ui.md): player flow, screen information, battle phases, rewards, sound cues, and local friendship design.
- [docs/module-tasks.md](docs/module-tasks.md): module ownership, public type boundaries, and roadmap task list.
- [docs/protocol.md](docs/protocol.md): public headers, storage/wire compatibility rules, and battle protocol.
- [docs/release-v0.2.md](docs/release-v0.2.md): latest v0.2 release notes and validation checklist.

## Notes

- v0.2 is local/offline. It does not call cloud APIs, does not require network access, does not upload images, and does not require SD card storage.
- There is no voice recognition entry point in the current firmware.
- Sound effects can be muted in firmware through the existing audio mute flag.
- Recognition is a local lightweight 8-class feature recognizer, not a general vision model or cloud AI.
- Backpack persistence uses ESP32 `Preferences`.
- Board-to-board battle uses Wi-Fi SoftAP + UDP; no router is required.
- App, cloud recognition, LLM text generation, SD card storage, and community features are future reserved directions, not current runtime requirements.
- The startup audio header is included as a local embedded asset based on user-authorized audio. Replace it before redistribution if your authorization terms require a different asset.
- The current GitHub release is intended as a working project snapshot, not a polished product firmware.
