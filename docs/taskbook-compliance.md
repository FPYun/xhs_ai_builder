# Taskbook Compliance Checklist

Source taskbooks: `v0.1/module2.txt`, `v0.1/module3.txt`,
`v0.1/module4.txt`, `v0.1/module5.txt`, and `docs/module-tasks.md`.

This checklist records the current repository evidence for each taskbook area.
It is intentionally documentation-only and does not change public headers or
wire/storage layouts.

## Global Constraints

| Requirement | Current evidence |
| --- | --- |
| Do not redefine public types | Public boundaries are documented in `docs/protocol.md`; gameplay code includes the shared headers. |
| Do not privately edit shared header fields, enum values, or UDP packet layout | Current work keeps `pet_model.h`, `vision_types.h`, `ui_types.h`, and `battle_protocol.h` as the source of shared types. |
| Do not modify `BattlePetPacket` | Battle pairing, clash, result, and friendship use the existing packet only. |
| Interface changes require proposal first | Proposals are documented in `docs/battle-link.md` and `docs/player-flow-ui.md`. |
| Do not restore voice recognition | README, architecture, release notes, and roadmap state that no voice recognition is active. |
| Do not introduce new dependencies or cloud API keys | Current firmware remains Arduino/CoreS3 local code; cloud/App/SD features are documented as future optional layers. |
| Keep CoreS3 local/offline | README, usage, architecture, and roadmap all state offline runtime boundaries. |

## Productization and Documentation Module

Evidence:

- README summarizes the local/offline v0.2 scope, controls, build command, and
   roadmap documents.
- `docs/app-cloud-roadmap.md` defines App information architecture, page list,
  local MVP function list, cloud/LLM boundaries, SD card planning, dataset and
  model training, firmware interface suggestions, and GitHub/documentation
  update suggestions.
- `docs/protocol.md` records public headers, compatibility policy, backpack
  storage, battle wire layout, and future management protocol direction.
- `docs/release-v0.2.md` records release scope, known limitations, and manual
   test checklist.

Taskbook acceptance coverage:

- Current no-voice state: documented.
- Sound effects can be muted: documented.
- Current recognition is lightweight limited-class local recognition: documented.
- Current battle transport is Wi-Fi AP + UDP: documented.
- Current backpack persistence uses `Preferences`: documented.
- Current flow does not require SD card: documented.
- Current flow does not require network/cloud: documented.
- Cloud, App, SD card, and large-model work are future reserved directions:
  documented.

## Capture Recognition and Pet Generation Module

Source: `v0.1/module2.txt`.

Evidence:

- `04_camera_pet_battle.ino` performs capture, 64x64 preprocessing,
  `ImageTraits` extraction, subject presence gating, local 8-class recognition,
  and either `draw_wild_pet` or `draw_capture_fail`.
- `element_hint_for_class` fixes each `ObjectClass` to the allowed element set.
- `species_bias_for_class` fixes each `ObjectClass` to a stable species bias;
  time, shot count, and random state no longer override the recognized class.
- `merge_generation_inputs` and `derive_pet_genes` use `ImageTraits` for mood,
  body scale, eye, horn, tail, aura, pattern density, color, and seed details.
- `draw_wild_pet` rejects unknown, unrecognized, and low-confidence results
  before deriving genes.
- `draw_capture_fail` sets `has_wild_pet = false`, clears `wild_pet`, records
  diagnostics, and enters `kScreenCaptureFail`.
- `capture_wild_pet` rejects stale, unknown, unrecognized, or low-confidence
  recognition before writing to the backpack.
- `docs/vision-generation-mapping.md` records the mapping table, generation
  chain, failure gates, growth/battle handoff advice, and public interface
  conclusion.

Taskbook acceptance coverage:

- Same recognized class maps to a stable element set and species bias:
  implemented and documented.
- Time, shot count, and random perturbation only affect appearance details:
  implemented and documented.
- Failed recognition does not generate a random pet: guarded in wild drawing,
  failure screen, and capture entry.
- Capture, backpack, and release flows are preserved by using existing
  `PetGenes`, `SavedPet`, and `BackpackStorage`.
- No module 2 public interface change is required.

Local verification on 2026-05-29:

- Static check found no public type redefinitions in
  `04_camera_pet_battle.ino`.
- `derive_pet_genes` is only called from the recognized wild-pet path.
- Failure paths include `has_wild_pet = false`.
- Firmware logging includes preprocess time, classify time, presence,
  recognized flag, confidence, class label, heap, and failure reason.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_module2`
- Compile result: program storage `1051377` bytes (`33%`), globals `64744`
  bytes (`19%`), leaving `262936` bytes.

## iPhone App Planning Module

Evidence:

- `docs/app-cloud-roadmap.md` defines the App IA: home, backpack, pet detail,
  capture, battle, data export, connection guide, and settings.
- The same document lists MVP functions: view backpack, pet detail, pet card,
  battle record, friendship value, trigger photo, view local recognition,
  inspect battle status, export logs/samples, and connection guide.
- The document explicitly says this is planning, not current firmware runtime.

## Local Connection Tool Module

Evidence:

- `docs/app-cloud-roadmap.md` prioritizes computer-side MVP via USB serial.
- `docs/battle-link.md` recommends initial serial commands: `BTN`, `ACT`, and
  `STATUS`.
- Current firmware exposes `handle_external_action` and `handle_external_button`
  as the first integration seam for buttons, serial, or App adapters.

## Communication Protocol Module

Evidence:

- `docs/protocol.md` documents `BattlePetPacket` field order and compatibility
  boundaries.
- `docs/battle-link.md` documents Wi-Fi AP + UDP runtime states, heartbeat by
  repeated pet packets, timeout behavior, and the fact that ACK/control packets
  are not implemented in the current wire protocol.
- Future App management protocol is documented as separate from the battle
  packet, with independent magic/version/command/payload.

## Cloud Enhancement Module

Evidence:

- `docs/app-cloud-roadmap.md` describes optional cloud enhancement boundaries:
  no hard-coded API key, no specific commercial model binding, no image upload
  without future user consent, offline fallback required.
- The firmware extension points are documented: `fetch_remote_pet_hint`,
  `merge_generation_inputs`, and `save_pet_snapshot`.

## SD Card Module

Evidence:

- `docs/app-cloud-roadmap.md` documents SD card as optional storage only, not a
  runtime requirement.
- Suggested directories are `/pets/`, `/captures/`, `/samples/`, `/logs/`, and
  `/models/`.
- The document states SD card does not improve model inference speed and SD
  write failures must not break capture, backpack, or battle flows.

## Dataset and Training Module

Evidence:

- `docs/app-cloud-roadmap.md` defines 8 sample classes, sample metadata fields,
  desktop training flow, export to `vision_model_data.h`, and evaluation
  candidates: TFLite Micro, Edge Impulse, and ESP-DL.
- `docs/vision-generation-mapping.md` documents the current 8-class local
  recognition mapping and failure gate.
- `scripts/train_vision_feature_model.py` is the current desktop-side model data
  generator for `vision_model_data.h`.

## Player Flow, Backpack Growth, and UI Module

Source: `v0.1/module5.txt`.

Evidence:

- `docs/player-flow-ui.md` provides the full player flow diagram, screen
  information structure, button text/action table, battle phase design, battle
  settlement rules, sound trigger points, local friendship mechanism,
  growth/reward rules, and interface change suggestions.
- `04_camera_pet_battle.ino` implements IDLE, WILD, CAPTURE_FAIL, BAG,
  RELEASE_CONFIRM, MATCH, BATTLE clash, and BATTLE result flows.
- Backpack display includes active/stored state, level, stage, XP, element,
  mood, wins/battles, and win rate.
- Growth sources include capture XP, waiting XP, battle result XP, and local
  rematch friendship bonus.
- Player-facing MATCH/BATTLE text uses neutral states and does not expose
  HOST/CLIENT, UDP, TX/RX, peer IP, or packet-layout concepts. Those diagnostics
  remain in serial logs and developer documentation.

Taskbook acceptance coverage:

- Player can return from PHOTO, capture, BAG, MATCH, BATTLE, and release
  confirmation to the main flow.
- BAG shows growth progress and battle record.
- BATTLE no longer jumps directly from MATCH to result; it includes a clash
  phase.
- `kAudioMuted` remains the sound gate.
- No public interface change is required for the RAM-only friendship mechanism.

Static acceptance checks used for Module 3:

- Inspect `.ino` display writes for player-visible HOST/CLIENT/UDP/TX/RX/F
  strings.
- Confirm `Serial.printf` may retain role and packet-counter diagnostics.
- Confirm no `Say PAIZHAO` or voice-recognition entry text remains.
- Confirm `kAudioMuted` exists and scene sounds are triggered through
  `play_scene_sound`.
- Confirm `docs/player-flow-ui.md` contains the taskbook output sections.

## Hardware Verification

Automated local hardware evidence:

- COM7 was detected as USB VID:PID `303A:1001`, serial `44:1B:F6:E3:9B:60`.
- The current sketch compiled and uploaded to COM7 successfully.
- Flash write hashes were verified by `esptool.py`.
- Boot serial output was observed:
  `battle mac=44:1B:F6:E3:9B:60 board=COM7 role=HOST state=DISCOVERING ap=M5PET-E39B60 udp=42105`.

Remaining manual physical validation:

- Touch each screen transition.
- Capture real camera samples.
- Confirm blank, wall, and too-dark frames enter CAPTURE FAILED and cannot be
  captured.
- Confirm a clear centered object enters the wild pet screen.
- Confirm low-confidence recognition cannot be captured.
- Flash two boards and confirm MATCH, CLASH, result, and exit behavior.
- Confirm serial or future external-button adapters call the reserved entry
  points as intended.
