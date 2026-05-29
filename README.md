# xhs_ai_builder

CoreS3 local AI pet demo for offline voice capture, five-element pet generation, backpack management, growth, sound effects, and board-to-board battle flow.

## v0.1 Scope

- Hardware target: M5Stack CoreS3 / ESP32-S3.
- Main sketch: `arduino_demos/04_voice_camera_snap/04_voice_camera_snap.ino`.
- Offline command words with ESP-SR: `pai zhao`, `bei bao`, `fan hui`, `dui zhan`.
- Local camera trait extraction and five-element pet generation.
- Backpack flow: capture, select, release with confirmation, up to 6 pets.
- Growth flow: level, stage, XP, battle count, wins.
- Battle transport: Wi-Fi SoftAP + UDP automatic peer discovery, no router required.
- Sound system: idle, wild, bag, battle, result, pet/element feedback, and startup melody.
- Extension interfaces remain no-op/local for later network, SD card, and model-based recognition.

## Repository Layout

```text
arduino_demos/
  01_hello_cores3/             Minimal display demo
  02_touch_test/               Touch input demo
  03_wifi_scan/                Wi-Fi scan demo
  04_voice_camera_snap/        Main v0.1 pet system
firmware/official/             Reference official M5Burner firmware binaries and manifest
scripts/                       PowerShell helper scripts for board info, build, and flashing
docs/                          Usage, flashing, architecture, and release notes
```

## Quick Build

Compile the main v0.1 sketch with ESP-SR support:

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_voice_camera_snap\04_voice_camera_snap.ino `
  -EspSr `
  -BuildRoot C:\tmp\m5_arduino_build_v01
```

Upload through Arduino CLI if it works on your port:

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_voice_camera_snap\04_voice_camera_snap.ino `
  -EspSr `
  -BuildRoot C:\tmp\m5_arduino_build_v01 `
  -Upload `
  -Port COM7
```

For the more reliable direct flash path, see [docs/flashing.md](docs/flashing.md).

## Basic Operation

- Idle screen:
  - Left: BAG
  - Middle: MATCH
  - Right: PHOTO
- Say `pai zhao` or tap PHOTO to capture a wild pet.
- In the wild pet screen, capture the pet into the backpack or release it.
- In BAG, release, select and return to idle, or move to the next pet.
- In MATCH, put two boards close together. One board hosts `M5PET-xxxxxxxx`; the other joins it and exchanges battle packets over UDP.

More details are in [docs/usage.md](docs/usage.md).

## Notes

- v0.1 is still local/offline. It does not call cloud APIs and does not require SD card storage.
- The startup audio header is included as a local embedded asset based on user-authorized audio. Replace it before redistribution if your authorization terms require a different asset.
- The current GitHub release is intended as a working project snapshot, not a polished product firmware.

