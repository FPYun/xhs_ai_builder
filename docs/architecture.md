# Architecture

## Main Sketch

`arduino_demos/04_voice_camera_snap/04_voice_camera_snap.ino` contains the v0.1 system:

- Camera capture and lightweight image trait extraction.
- Offline ESP-SR command word recognition.
- Five-element pet generation.
- Pet rendering on the CoreS3 screen.
- Backpack persistence with `Preferences`.
- Growth and battle stat calculation.
- Wi-Fi AP + UDP battle transport.
- Scene and pet sound effects.

## Pet Generation

The generated pet is represented by `PetGenes`:

- `element`
- `species`
- `mood`
- `bodyScale`
- `eyeStyle`
- `hornStyle`
- `tailStyle`
- `auraPattern`
- `patternDensity`
- `accentColor`
- `seed`

The main local inputs are:

- Average image color.
- Brightness.
- Saturation.
- Contrast.
- Time bucket.
- Shot count.
- Random seed.

The design goal is stable element selection for a similar object, with visible same-element variation between captures.

## Extension Interfaces

The sketch keeps these interfaces for later upgrades:

- `RecognitionResult recognize_object_local(const ImageTraits& traits)`
- `bool fetch_remote_pet_hint(const RecognitionResult& input, PetHint* output)`
- `bool save_pet_snapshot(const PetGenes& genes, const ImageTraits& traits)`
- `PetGenes merge_generation_inputs(ImageTraits traits, RecognitionResult recog, PetHint remoteHint)`

In v0.1 they remain local/no-op. Later network APIs, SD card history, or model-based recognition should be integrated behind these interfaces so the drawing and gameplay layers do not need major rewrites.

## Battle Transport

The v0.1 battle module replaced ESP-NOW with Wi-Fi SoftAP + UDP:

- Every board starts a SoftAP with SSID `M5PET-xxxxxxxx`.
- Every board listens on UDP port `42105`.
- During MATCH, boards scan for peer APs.
- The larger device ID joins the smaller device ID AP.
- The host learns the client IP from the first UDP packet.
- Both boards exchange `BattlePetPacket` structures.

This design is easier to debug than ESP-NOW because the screen can show role, connection state, IP, TX count, RX count, and send failures.

## Persistent Data

Backpack data is stored with ESP32 `Preferences`.

Each saved pet includes:

- `PetGenes`
- Level and stage.
- XP.
- Battle count and win count.
- Capture timestamp.
- Last growth timestamp.

No SD card is required in v0.1.

