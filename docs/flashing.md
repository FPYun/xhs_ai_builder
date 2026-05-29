# Build and Flash

## Prerequisites

- M5Stack CoreS3 connected with a data-capable USB-C cable.
- Arduino CLI from Arduino IDE.
- M5Stack ESP32 board support installed.
- Python available at `E:\Anaconda\python.exe` for helper scripts and direct flash.

## Detect Port

```powershell
E:\Anaconda\python.exe -c "import serial.tools.list_ports; print('\n'.join(f'{p.device} | {p.description} | {p.hwid}' for p in serial.tools.list_ports.comports()))"
```

Typical ports used during development:

- `COM8`: known test board, MAC `44:1B:F6:E3:9A:FC`.
- `COM7`: known test board, MAC `44:1B:F6:E3:9B:60`.

## Compile v0.2

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_v02
```

The compiled firmware is self-contained for the current demo. After flashing, capture, backpack, muteable sound effects, and Wi-Fi AP + UDP battle do not require SD card storage, cloud APIs, or an external network.

## Arduino CLI Upload

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_v02 `
  -Upload `
  -Port COM7
```

Flash both battle boards. COM/MAC are recorded for hardware traceability only;
MATCH role is negotiated dynamically by firmware.

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_flash `
  -Upload `
  -Port COM8
```

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_flash `
  -Upload `
  -Port COM7
```

Record each flash result:

- COM port.
- MAC from the boot serial line.
- Whether write succeeded.
- Whether hash verification succeeded.
- Whether hard reset completed.

## Direct esptool Flash

Use this path if Arduino CLI upload is unstable:

```powershell
E:\Anaconda\python.exe -m esptool `
  --chip esp32s3 `
  -p COM7 `
  -b 115200 `
  --before default-reset `
  --after hard-reset `
  write-flash `
  --flash-mode dio `
  --flash-freq 80m `
  --flash-size 16MB `
  0x0000 C:\tmp\m5_arduino_build_v02\04_camera_pet_battle\04_camera_pet_battle.ino.bootloader.bin `
  0x8000 C:\tmp\m5_arduino_build_v02\04_camera_pet_battle\04_camera_pet_battle.ino.partitions.bin `
  0xe000 C:\tmp\m5_arduino_data\packages\m5stack\hardware\esp32\2.1.4\tools\partitions\boot_app0.bin `
  0x10000 C:\tmp\m5_arduino_build_v02\04_camera_pet_battle\04_camera_pet_battle.ino.bin
```

Replace `COM7` with the actual port.

## Battle Link Verification

After flashing both boards:

- COM8 / `44:1B:F6:E3:9A:FC` should print `board=COM8` on serial.
- COM7 / `44:1B:F6:E3:9B:60` should print `board=COM7` on serial.
- Serial `role=HOST` or `role=CLIENT` is an internal dynamic role and may vary
  with the peer IDs.
- MATCH UI should use neutral link text instead of exposing HOST/CLIENT.
- With an active pet selected on both boards, TX and RX counters should change.
- Receiving a peer `BattlePetPacket` should enter the BATTLE result screen.
- The middle MATCH button should return to IDLE.

## Runtime Boundary After Flash

- The firmware does not need external network access after flashing.
- The firmware does not need an SD card for capture, backpack, or battle.
- There is no voice recognition setup step.
- Cloud, App, SD card storage, and larger model recognition are reserved future
  directions only.

## Download Mode

If flashing fails:

1. Hold RESET for about 2 seconds.
2. Release when the green LED turns on.
3. Run the port detection command again.
4. Flash again with the detected COM port.
