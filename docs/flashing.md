# Build and Flash

## Prerequisites

- M5Stack CoreS3 connected with a data-capable USB-C cable.
- Arduino CLI from Arduino IDE.
- M5Stack ESP32 board support installed.
- ESP-SR source available at `C:\tmp\esp-sr-master` for offline command recognition.
- Python available at `E:\Anaconda\python.exe` for helper scripts and direct flash.

## Detect Port

```powershell
E:\Anaconda\python.exe -c "import serial.tools.list_ports; print('\n'.join(f'{p.device} | {p.description} | {p.hwid}' for p in serial.tools.list_ports.comports()))"
```

Typical ports used during development:

- `COM7`
- `COM8`

## Compile v0.1

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_voice_camera_snap\04_voice_camera_snap.ino `
  -EspSr `
  -BuildRoot C:\tmp\m5_arduino_build_v01
```

The ESP-SR build also generates `srmodels.bin` in the selected build path.

## Arduino CLI Upload

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_voice_camera_snap\04_voice_camera_snap.ino `
  -EspSr `
  -BuildRoot C:\tmp\m5_arduino_build_v01 `
  -Upload `
  -Port COM7
```

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
  0x0000 C:\tmp\m5_arduino_build_v01\04_voice_camera_snap\04_voice_camera_snap.ino.bootloader.bin `
  0x8000 C:\tmp\m5_arduino_build_v01\04_voice_camera_snap\04_voice_camera_snap.ino.partitions.bin `
  0xe000 C:\tmp\m5_arduino_data\packages\m5stack\hardware\esp32\2.1.4\tools\partitions\boot_app0.bin `
  0x10000 C:\tmp\m5_arduino_build_v01\04_voice_camera_snap\04_voice_camera_snap.ino.bin `
  0xD10000 C:\tmp\m5_arduino_build_v01\04_voice_camera_snap\srmodels.bin
```

Replace `COM7` with the actual port.

## Download Mode

If flashing fails:

1. Hold RESET for about 2 seconds.
2. Release when the green LED turns on.
3. Run the port detection command again.
4. Flash again with the detected COM port.

