# Public Interfaces and Protocols

This document records the stable public boundaries for the CoreS3 Wuxing pet
demo. The current firmware includes local Wi-Fi HTTP management for the iPhone
companion App. SD card storage, cloud recognition, BLE discovery, and LLM calls
remain reserved extension points, not implemented runtime requirements.

The current firmware has no voice-recognition protocol. Sound effects are local
UI feedback and may be muted by firmware state.

## Version Policy

- Public enum values, struct fields, field order, and packed UDP layout are
  compatibility-sensitive.
- Modules must not redefine types from the public headers.
- Modules must not change public enum values, struct fields, or
  `BattlePetPacket` layout locally.
- Any required public interface change must first be proposed as an interface
  change request and merged by the system architecture owner.
- Current storage version: `kBagVersion = 1`.
- Current battle packet version: `kBattleVersion = 1`.

## Public Headers

### `pet_model.h`

Owns pet identity and backpack persistence data:

- `kMaxBackpackPets`: maximum saved pets, currently `6`.
- `ElementType`: five elements in wire/storage order:
  - `kWood = 0`
  - `kFire = 1`
  - `kEarth = 2`
  - `kMetal = 3`
  - `kWater = 4`
- `PetGenes`: generated pet appearance and identity fields.
- `SavedPet`: persistent pet state, including `PetGenes`, level, stage, XP,
  battle counters, capture time, and growth time.
- `BackpackStorage`: persisted backpack envelope.
- `kBagMagic`: `0x57354247UL` (`W5BG`).
- `kBagVersion`: `1`.

Boundary rule: gameplay, drawing, storage, and battle modules may consume
`PetGenes` and `SavedPet`, but they must not add private fields to these shared
structures.

### `vision_types.h`

Owns local recognition inputs and outputs:

- `ObjectClass`: local object classes in recognition order:
  - `kObjectUnknown = 0`
  - `kObjectPlantLeaf = 1`
  - `kObjectFoodFruit = 2`
  - `kObjectPaperBook = 3`
  - `kObjectElectronicsScreen = 4`
  - `kObjectMetalKeyCoin = 5`
  - `kObjectFabricCloth = 6`
  - `kObjectCupBottleWater = 7`
  - `kObjectToyFigure = 8`
- `ImageTraits`: image statistics extracted from the camera frame.
- `RecognitionResult`: local recognition result and capture rejection reason.
- `SubjectPresence`: subject-presence decision helper.
- `PetHint`: reserved remote or SD-assisted hint shape.

Boundary rule: camera and recognition code produce `ImageTraits` and
`RecognitionResult`. Pet generation consumes them. Future SD or cloud modules
may fill `PetHint`, but the current firmware keeps those paths local/no-op.

### `ui_types.h`

Owns UI state and input action vocabulary:

- `ScreenMode`: `IDLE`, `WILD`, `CAPTURE_FAIL`, `BAG`, `RELEASE_CONFIRM`,
  `MATCH`, and `BATTLE`.
- `UiAction`: photo, bag navigation, capture/release, select, and match
  commands.
- `ButtonSlot`: left, middle, right input slots.

Boundary rule: touch, future app control, and any external input adapter should
translate inputs into `UiAction` values before entering gameplay handlers.

### `battle_protocol.h`

Owns the board-to-board battle packet contract:

- `BattleLinkRole`: `kBattleRoleHost = 0`, `kBattleRoleClient = 1`.
- `BattlePetPacket`: packed UDP payload.
- `kBattleMagic`: `0x57355054UL` (`W5PT`).
- `kBattleVersion`: `1`.

`BattlePetPacket` fields, in wire order:

```text
uint32_t magic
uint8_t  version
uint8_t  element
uint8_t  species
uint8_t  level
uint8_t  mood
uint8_t  bodyScale
uint8_t  eyeStyle
uint8_t  hornStyle
uint8_t  tailStyle
uint8_t  auraPattern
uint8_t  patternDensity
uint16_t accentColor
uint32_t seed
uint32_t deviceId
uint32_t seq
uint16_t power
uint16_t agility
uint16_t spirit
```

Boundary rule: no module may append, remove, reorder, or reinterpret packet
fields without a coordinated packet version change.

## Battle Transport

Current transport is local Wi-Fi SoftAP plus UDP. It does not require a router.

- AP SSID prefix: `M5PET-`.
- UDP port: `42105`.
- Wi-Fi channel: `6`.
- Peer timeout: `12000` ms.
- Scan interval: `4500` ms.
- Connect retry interval: `5000` ms.

Receive-side packet validation currently rejects packets when:

- The byte length is not exactly `sizeof(BattlePetPacket)`.
- `magic` does not match `kBattleMagic`.
- `version` does not match `kBattleVersion`.
- `deviceId` is the local device ID.
- `element > kWater`.
- `species > 2`.

Invalid inbound packets are ignored. The current protocol has no explicit
negative acknowledgement or remote error packet.

## App HTTP Management API

The implemented iPhone companion App uses a separate local HTTP management API
while the phone is connected to the CoreS3 AP. It does not extend or reinterpret
`BattlePetPacket`.

Implemented endpoint details live in `docs/app-http-api.md`.

Transport stages:

- Current App v0.1: Wi-Fi HTTP for local App access; keep UDP battle packets
  separate from management packets.
- Desktop tooling can still use USB serial later for logs and samples.
- BLE or LAN discovery remain future work after resource and stability review.

Implemented management actions:

- `GET_STATUS`: device state, active screen, mute state, heap, and firmware
  version.
- `GET_BACKPACK_SUMMARY`: stored pet count and compact pet-card fields.
- `GET_PET_DETAIL`: one `SavedPet` snapshot serialized from the public type.
- `CAPTURE_ONCE`: trigger the existing photo flow.
- `GET_LAST_RECOGNITION`: last `ImageTraits` and `RecognitionResult` snapshot.
- `GET_BATTLE_STATUS`: battle role, peer state, TX/RX counters, and result.
- `EXPORT_LOG`: local log export.
- `EXPORT_SAMPLE_INDEX`: sample metadata export for desktop training.

Interface change request:

- Do not modify `BattlePetPacket`.
- Do not redefine `PetGenes`, `SavedPet`, `ImageTraits`, or
  `RecognitionResult`.
- Prefer read-only snapshots of existing public types.
- Any new public packet or command header requires architecture review and a
  version policy before implementation.

## Future Cloud Hint Shape

Cloud or LLM recognition is optional future functionality. It must not be a
runtime dependency for local capture, backpack, or battle flows and must not
hard-code API keys in firmware or the repository.

Potential request inputs:

- Image or thumbnail, only after explicit future user consent.
- `ImageTraits`.
- `RecognitionResult`.
- Device ID.
- Backpack context.

Potential response fields:

- `objectLabel`
- `materialLabel`
- `elementHint`
- `speciesBias`
- `confidence`
- `petName`
- `evolutionHint`
- `flavorText`

Firmware integration should reuse the existing reserved hooks:

- `fetch_remote_pet_hint`
- `merge_generation_inputs`
- `save_pet_snapshot`

## Error and Status Model

There is no shared numeric error-code enum yet. Current status is represented by
local UI strings, recognition failure strings, and counters:

- Recognition uses `RecognitionResult::recognized` and `failureReason`.
- Battle transport tracks TX count, RX count, and send failures on screen.
- Capture failure is represented by `kScreenCaptureFail`.
- Wi-Fi/UDP initialization failure is shown locally at startup.

If cross-module numeric error codes become necessary, add a new public header
through an interface change request instead of overloading existing enums.

## Module Ownership

- Camera and recognition: owns frame capture, preprocessing, local model
  inference, and `RecognitionResult` production.
- Pet generation and drawing: owns conversion from recognition traits to
  `PetGenes` and CoreS3 rendering.
- Backpack and growth: owns `BackpackStorage`, `Preferences` persistence, XP,
  level, stage, capture, release, and selection.
- Battle communication: owns SoftAP/STA setup, peer discovery, UDP send/receive,
  and `BattlePetPacket` conversion.
- UI flow: owns `ScreenMode`, `UiAction` routing, touch mapping, and screen
  transitions.
- Sound: owns scene and pet sound cues, while respecting `kAudioMuted`.
- Reserved extension adapters: SD, app control, cloud recognition, and LLM
  calls must remain optional and must not break offline CoreS3 operation.

## Interface Change Request Template

Use this format before changing a public header:

```text
Interface change request:
- Header:
- Current type or constant:
- Proposed change:
- Reason:
- Compatibility impact:
- Required version bump:
- Migration plan:
- Compile/test command:
```
