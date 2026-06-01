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
- `pet_visual_variant` derives an internal `0..2` visual variant from existing
  `PetGenes` fields; it does not add storage or UDP fields.
- `variant_pet_name` expands local pet naming to 45 display templates while
  keeping base species indices compatible.
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
- Visual variants enrich local capture, backpack, and App JSON display without
  changing `PetGenes::species` or `BattlePetPacket`.
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
- CAPTURE_FAIL shows the failure reason, a center framing guide, subject and
  recognition scores for normal failures, and the next touch actions so the
  player can retry without guessing.
- Firmware logging includes preprocess time, classify time, presence,
  recognized flag, confidence, class label, heap, and failure reason.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_module2`
- Compile result: program storage `1051377` bytes (`33%`), globals `64744`
  bytes (`19%`), leaving `262936` bytes.

Local verification after compatible pet-library expansion:

- Static check confirmed `PetGenes::species` remains a base `0..2` value.
- Static check confirmed `BattlePetPacket` still sends
  `packet.species = genes.species`.
- Static check confirmed incoming pet packets still reject `species > 2`.
- Static check confirmed visual variant data is derived at runtime and is not
  added to public headers, backpack storage, or UDP packets.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_pet_library`
- Compile result: program storage `1428985` bytes (`45%`), globals `65968`
  bytes (`20%`), leaving `261712` bytes.

Local verification after lightweight sample-trained prototype integration:

- `scripts/train_vision_feature_model.py` reads real image folders under
  `C:\tmp\m5_vision_samples`, writes `manifest.csv`, exports weighted
  prototype constants to `vision_model_data.h`, and keeps synthetic fallback
  only for missing classes.
- `04_camera_pet_battle.ino` builds the same 18 firmware features as the
  trainer and classifies with weighted prototype distance, second-best margin,
  subject presence, and the existing confidence gate.
- The local rule classifier remains a fallback; fallback success still requires
  the normal recognition confidence gate.
- The generated model is marked weak because the current desktop sample set is
  public/noisy and lacks negative samples. Current report:
  `C:\tmp\m5_vision_samples\report.json`.
- Sample counts in that report are: `plant_leaf=6`, `food_fruit=20`,
  `paper_book=20`, `electronics_screen=20`, `metal_key_coin=6`,
  `fabric_cloth=9`, `cup_bottle_water=20`, `toy_figure=20`, `negative=0`.
- Validation result from this weak data pass: positive accuracy `0.038`,
  negative false positive rate `0.000`.
- Static check confirmed the generated model constants are not added to public
  headers, backpack storage, or UDP packets.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_vision_samples`
- Compile result: program storage `1501617` bytes (`47%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_vision_samples -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after online public-sample expansion:

- COCO validation samples were used to fill compatible classes such as fruit,
  book, screen, bottle/cup, and toy.
- Open Images validation metadata and thumbnail URLs were used to fill
  `plant_leaf`, `metal_key_coin`, `fabric_cloth`, and `negative`.
- `C:\tmp\m5_vision_samples\manifest.csv` now records at least 80 samples for
  every positive class and 80 `negative` samples.
- `scripts/train_vision_feature_model.py` now marks a model weak when sample
  counts are sufficient but validation accuracy is below `0.65` or negative
  false-positive rate is above `0.15`.
- Current report: positive accuracy `0.070`, negative false-positive rate
  `0.188`, data quality `weak`.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_online_samples`
- Compile result: program storage `1513161` bytes (`48%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_online_samples -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after hard blank-gate / trait-log correction:

- Fixed an Arduino preprocessing failure caused by JavaScript `function`
  declarations at the start of lines inside the embedded `/app` raw string.
- Added a stronger neutral-background gate for gray/white low-saturation
  scenes with weak center structure, so walls, desktops, and blank paper are
  rejected earlier as `Blank scene`.
- Tightened Water evidence again: Water now requires stronger blue dominance,
  dark-screen evidence, or stronger highlight/center structure.
- Serial recognition logs now print `rgb`, brightness, saturation, contrast,
  center delta, dark ratio, and bright ratio for real CoreS3 threshold tuning.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_blank_hard_gate`
- Compile result: program storage `1506181` bytes (`47%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_blank_hard_gate -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after capture assist / CoreS3 sample-loop update:

- `take_photo` now captures a 3-frame burst and chooses the best candidate by
  quality, subject presence, recognition confidence, background penalty, and
  best-effort proximity.
- Flat white wall, blank paper, and empty desktop scenes receive an additional
  background similarity penalty before classification can be accepted.
- `draw_capture_fail` remains the only path for invalid, flat-background,
  no-frame, preprocess-failed, or low-confidence burst results.
- SD sample logging is optional. When an SD card is present, burst thumbnails
  and `manifest.csv` rows are written under `/samples`; without SD, serial logs
  still include sample and final burst diagnostics.
- Public headers, `PetGenes::species`, backpack storage, `BattlePetPacket`, and
  UDP layout were not changed.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_capture_assist`
- Compile/upload result: program storage `1536997` bytes (`48%`), globals `67916`
  bytes (`20%`), leaving `259764` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_capture_assist -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after open-platform route scan:

- `docs/open-platform-extension-audit.md` removes already completed work from
  the remaining route: baseline capture, quality hints, SD audio, basic `/app`,
  basic pet state, basic battle, and public-sample training are no longer
  duplicated as new tasks.
- `scripts/train_vision_feature_model.py` now writes `scene` into
  `manifest.csv` and exports `scene_sample_counts` plus `scene_eval` to
  `report.json`.
- Scene statistics cover white wall, white paper, desktop, glare, dark, bright,
  low-texture, hand-cover, and unknown scenes.
- Verification commands:
  `python -m py_compile .\scripts\train_vision_feature_model.py`
  and a temporary generated-negative training run under
  `C:\tmp\m5_vision_scene_stat_test`.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_route_audit`
- Compile result: program storage `1573413` bytes (`50%`), globals `67972`
  bytes (`20%`), leaving `259708` bytes.

Local verification after blank-scene / Water-bias correction:

- Added a `Blank scene` subject gate for low-saturation, low-center-delta,
  low-contrast frames so plain walls and blank paper fail before classifying.
- Added `water_visual_evidence`; Water now requires blue dominance,
  dark-screen evidence, or clear highlight/center structure.
- `electronics_screen` remains within the required Metal/Water set, but the
  Water branch is now evidence-gated.
- `cup_bottle_water` remains the only always-Water object class, but rule
  fallback no longer treats neutral gray/white frames as enough water evidence.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_blank_water_fix`
- Compile result: program storage `1502737` bytes (`47%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_blank_water_fix -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after Metal element-bias correction:

- Reduced the generic image-trait Metal vote from low-saturation bright frames.
  Metal trait votes now require much stronger highlight and center evidence.
- `electronics_screen` still stays within the required Metal/Water set, but it
  now defaults to Water unless strong metal-like visual evidence exists.
- `toy_figure` still stays within the required Fire/Metal set, but it now
  defaults to Fire unless strong metal-like visual evidence exists.
- `metal_key_coin` remains the only always-Metal object class.
- Serial recognition logs include final `elem=...` so field testing can measure
  the actual element distribution without reading the rendered pet manually.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_element_bias_fix`
- Compile result: program storage `1502525` bytes (`47%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_element_bias_fix -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

Local verification after low-confidence / metal-bias correction:

- Raised the internal recognition confidence gate so low-confidence results no
  longer enter the wild-pet screen or capture path.
- Tightened weak-model acceptance: weak prototype data now requires stronger
  distance, margin, and confidence before it can directly classify an object.
- Added fallback class evidence checks. `metal_key_coin` can no longer win only
  because a frame is low-saturation; it also needs contrast, highlight, high
  brightness, or clear center/edge evidence.
- Serial recognition logs now include `src` so field tests can identify whether
  a result came from the prototype model, rule fallback, negative/background
  gate, or presence gate.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_vision_bias_fix`
- Compile result: program storage `1502341` bytes (`47%`), globals `66764`
  bytes (`20%`), leaving `260916` bytes.
- Upload command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_vision_bias_fix -Upload -Port COM7`
- Upload result: `COM7` matched `USB\VID_303A&PID_1001`, ESP32-S3 connected,
  flashed successfully, and all written data hashes were verified.

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
  as the integration seam for buttons, serial, and App adapters.
- USB serial implements `BTN L/M/R`, `ACT <type>`, `STATUS`, and `HELP` for
  bench testing the same guarded state-machine actions used by touch and `/app`.
  `STATUS` also prints active pet growth, friendship, and recent battle result
  summaries for two-board validation.
- USB serial `ACT friend` adds the currently seen opponent or the recent
  opponent to the same local persisted friend list used by battle rewards and
  shows the add result on the device screen.
- USB serial `STATUS` includes the next rematch XP reward for the recent friend
  when the local rematch window is still active.
- USB serial `SOUND <cue>` tests existing scene sound cues through
  `play_scene_sound`, preserving the same `kAudioMuted` gate as normal gameplay.
- `/app` includes an App soundboard panel that calls
  `/api/v1/action?type=sound&cue=...` and routes the chosen cue through
  `play_scene_sound`, preserving the same mute gate and SD fallback path.
- `/api/v1/status` exposes local persisted friendship display fields, including
  the same `friendshipGoal` 好友/密友 goal badge shown on MATCH.
- `/api/v1/action?type=friend` adds the current or recent opponent to the local
  persisted friend list without changing `SavedPet` or `BattlePetPacket`, then
  shows the add result on the device screen.
- `/app` renders an App friend empty card when there are no local friend rows,
  so the user can add the recent opponent or start another match from the
  friends page.
- `/api/v1/status` also exposes `rematchAvailable` and `nextRematchXp`, and the
  built-in `/app` formats friend IDs as short player-facing IDs with the next
  好友/密友 progress target.
- `/api/v1/status` also exposes the current left/middle/right button labels so
  future external buttons, serial bridges, and App controls can mirror the
  device UI without duplicating state-machine logic.
- `/api/v1/status` exposes matching `buttonActions.left/middle/right` action
  IDs, and the built-in `/app` page renders the same dynamic three-button footer
  as the device screen.
- `/app` uses the dynamic three-button footer on home, backpack, and battle tabs;
  `confirm_release` is rejected unless `RELEASE_CONFIRM` is already active, so
  App controls cannot bypass stored-pet release confirmation.
- App and external actions are state-gated to the same flow as touch controls:
  backpack actions require BAG, wild capture/release requires WILD, and stored
  release confirmation requires RELEASE_CONFIRM.
- Rejected App actions return player-facing next-step errors, so unsupported
  actions do not leave the phone page with only a generic HTTP failure.
- App battle status uses player-facing pairing fields (`pairingStep`,
  `pairingStepLabel`, `waitingSec`, `syncAgeSec`) instead of exposing transport
  TX/RX/failure counters.

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

Source: `v0.1/module3.txt`.

Evidence:

- `docs/player-flow-ui.md` provides the full player flow diagram, screen
  information structure, button text/action table, battle phase design, battle
  settlement rules, sound trigger points, local friendship mechanism,
  growth/reward rules, and interface change suggestions.
- `04_camera_pet_battle.ino` implements IDLE, WILD, CAPTURE_FAIL, BAG,
  RELEASE_CONFIRM, MATCH, BATTLE pet-entry, three-round clash, result, and exit
  flows.
- Backpack display includes active/stored state, level, stage, XP, element,
  mood, growth-boosted P/A/S battle stats, wins/total battles, win rate, next
  evolution/level XP target, and a waiting-growth progress meter.
- `/app` pet cards and USB serial `STATUS` expose the same next evolution/level
  XP target and growth-boosted battle stats through existing runtime data,
  without changing `SavedPet`.
- `/app` renders that target as a separate compact growth badge, avoiding a
  crowded single-line pet card.
- `/app` adds an App pet growth wait rail for current-pet and backpack cards,
  exposing the runtime waiting-growth countdown without changing storage.
- `/app` adds an App training plan card under the current partner, showing
  next growth target, XP progress, waiting countdown, and capture/battle/wait
  growth sources.
- Backpack display includes a 6-slot capacity bar that distinguishes occupied,
  active, current cursor, and empty slots.
- Empty backpack display includes a first-capture card with 0/6 capacity,
  framing guide, and PHOTO next step.
- `/app` also renders an App empty BAG first-capture card with 0/6 capacity,
  direct PHOTO action, and capture-page shortcut.
- `/app` adds an App bag overview card above the backpack pager, showing
  capacity, active slot, device cursor slot, and remaining empty slots.
- `/app` renders an App current-pet empty card with a 6-slot visual and BAG or
  PHOTO next step instead of a plain no-pet label.
- The wild pet screen shows the capacity bar and warns when the bag is already
  full before capture is attempted.
- WILD right-button 放走 only clears the current wild preview and returns to
  IDLE with a wild-release notice; stored backpack pets are not deleted.
- Capture failure for a full backpack shows a full-bag card, 6-slot capacity,
  and a BAG-to-release next step.
- `/app` adds an App capture quality card with subject/recognition rails and a
  capture-or-retake action, using existing recognition fields only.
- Full-backpack capture failure plays the warning cue followed by the BAG cue
  through `play_scene_sound`, so the audio matches the release/select next step
  and remains covered by the mute gate.
- Capture failure for normal recognition misses shows subject/recognition
  scores beside the framing guide.
- Capture success shows the new pet's stats, XP progress bar, active slot
  number, active-pet confirmation, 6-slot bag bar, capture XP reward, and next
  evolution/level XP target plus waiting-growth countdown.
- The idle state shows a first-capture card with 0/6 capacity and framing guide
  when empty, then a compact visual partner card with avatar, XP progress,
  five-element badge, backpack count, win rate, and waiting-growth progress once
  a pet is active.
- When a recent opponent exists, IDLE replaces the passive growth line with the
  opponent short ID, last result or rematch XP preview, and a friendship meter.
- Growth sources include capture XP, waiting XP, battle result XP, and local
  rematch friendship bonus.
- IDLE and BAG show the next waiting-growth countdown when no fresh growth event
  is being displayed.
- Friendship feedback includes local new-friend, rematch reward, remaining score
  to the next 好友/密友 threshold, and 好友/密友 upgrade notices without storage or
  protocol changes.
- App action `friend` and USB serial `ACT friend` provide an explicit local
  add-friend path for the current or recent opponent, still without storage or
  protocol changes, and both paths surface a device-side result notice.
- `/app` includes an App friend goal card with current stage/score, recent
  opponent, next rematch XP, next friendship gain, and battle/add-friend actions.
- `/app` includes an App friend streak card with rematch streak, next XP reward,
  next friendship gain, and close-friend goal.
- MATCH without an active pet shows a warning preparation card with BAG/PHOTO
  next steps instead of entering a stuck matching state.
- MATCH without an active pet plays the warning cue followed by BAG when stored
  pets exist, or PHOTO when the bag is empty, all through `play_scene_sound`.
- MATCH includes a friendship card with friend count, recent opponent short ID,
  compact 好友/密友 goal badge, full-width score meter, last result, score
  difference, and either the last XP reward or next rematch XP preview.
- BATTLE exit shows the recent opponent short ID, friendship state, friendship
  meter, result badge, and next rematch XP preview when the rematch window is
  still active.
- RELEASE_CONFIRM shows the target pet avatar, five-element badge, compact
  slot/status/stage summary, win rate, XP rail, next growth target,
  active/stored slot state, and the explicit middle-confirm / side-cancel
  instruction before deleting from `Preferences`.
- Side-cancel from RELEASE_CONFIRM returns to BAG with a visible cancellation
  notice instead of silently leaving the confirmation page.
- Cancelling RELEASE_CONFIRM plays a dedicated mute-aware cancel cue through
  `play_scene_sound`.
- MATCH includes a neutral 寻/连/战 sync meter so players see two-board pairing
  progress without host/client/UDP details.
- MATCH shows the current pairing step label and elapsed waiting time while
  searching, then switches to opponent short ID plus last-seen age after sync,
  so the page does not feel frozen when the second board is not nearby yet.
- MATCH status also shows the middle-exit hint, so the user can leave scanning
  without waiting for communication state changes.
- MATCH sync milestones play one-shot mute-aware cues when the meter advances
  to 连 or 战.
- BATTLE clash includes both pet names and avatars, a center VS badge, a
  力/克/心 round track, five-element badges, immediate first-round advantage,
  and red/green advantage feedback before settlement.
- BATTLE clash plays the generated opponent pet sound through `play_pet_sound`,
  so the remote pet has an audible entry without adding assets or bypassing
  `kAudioMuted`.
- BATTLE clash starts its local result timer after the entry drawing and sound
  cues, so the three-round animation is not shortened by blocking audio.
- BATTLE clash round advances trigger one-shot mute-aware clash cues for the
  second and final rounds.
- BATTLE result and exit show a mirrored local battle ID derived from the two
  exchanged packets, so two boards can confirm they settled the same encounter
  without changing `BattlePetPacket`.
- Player-facing MATCH/BATTLE text uses neutral states and does not expose
  HOST/CLIENT, UDP, TX/RX, peer IP, or packet-layout concepts. Those diagnostics
  remain in serial logs and developer documentation.

Taskbook acceptance coverage:

- Player can return from PHOTO, capture, BAG, MATCH, BATTLE, and release
  confirmation to the main flow.
- RELEASE_CONFIRM shows the target pet avatar, five-element badge, compact
  summary strip, win rate, XP rail, next growth target, 6-slot bag bar, current
  target/active slot state, and irreversible warning before the middle button
  can delete it.
- BAG shows growth progress, growth-boosted battle stats, and battle record.
- BATTLE no longer jumps directly from MATCH to result; it includes named pet
  entry, three clash rounds, per-round center impact visuals, result settlement,
  and exit flow.
- BATTLE result settlement includes 力/克/心 summary chips so the player can
  scan each round's advantage without reading a dense text line; each chip now
  includes 胜/负/平 plus the numeric difference, and `/app` mirrors the result
  as a battle verdict card plus three compact round chips for external review.
- BATTLE result includes a settlement verdict card with result, score
  difference, mirrored local battle ID, and score-delta rail so the final
  decision is visible at a glance.
- `/app` includes an App battle phase card with pairing progress, waiting time,
  and current clash round before the detailed battle rows.
- `/app` includes an App match readiness card before the battle phase card,
  showing active partner, recent opponent, latest result, friendship goal, and
  rematch XP cue for MATCH setup.
- `/app` includes an App battle entry card between phase and matchup, naming
  the local partner, synced or waiting opponent, and entry status before VS.
- `/app` includes an App battle matchup card with local and opponent pet
  avatars plus a center VS badge, mirroring the device-side pet-entry moment.
- `/app` includes an App battle round track card with 力速/五行/气势 current-round
  highlight and score chips for the three-round clash.
- `/app` includes an App battle reward card that groups XP, growth result,
  rematch XP, next friendship gain, and friendship progress.
- `/app` includes an App battle exit card after valid results, with direct
  rematch, idle, bag, and add-friend actions for the cleanup step.
- BATTLE result keeps the scoring-time local pet packet and avatar for the
  result card, so post-reward level-ups do not rewrite the stats that explain
  the current encounter.
- BATTLE result shows the total score difference with a red/green delta meter.
- BATTLE result exposes the same mirrored local battle ID on screen, `/app`, and
  USB serial `STATUS`.
- BATTLE result shows post-reward XP progress and recent-peer friendship
  progress before the exit flow.
- BATTLE result also keeps the 力/克/心 round summary visible during settlement.
- `kAudioMuted` remains the sound gate.
- Optional SD-card `.raw` cue playback uses the existing `SD` and
  `CoreS3.Speaker.playRaw` APIs, is capped to short clips, and falls back to the
  built-in synthesized cue when the card or file is unavailable.
- No public interface change is required for the local persisted friendship
  mechanism; it uses an independent `wuxingfr` Preferences namespace.
- Device small-detail labels use glyph-safe ASCII (`SUBJ`, `REC`, `BAG`,
  `WIN`, `GROW`, `P/A/S`, `P1/P2`, `F/C/B`) so the same sketch can be burned
  from other module workflows without reintroducing CoreS3 small-font fallback
  boxes. This does not change public headers, storage fields, or UDP packets.

Static acceptance checks used for Module 3:

- Run `.\scripts\check-module3-ui.ps1` from the repository root.
- Inspect `.ino` display writes for player-visible HOST/CLIENT/UDP/TX/RX/F
  strings.
- Confirm `Serial.printf` may retain role and packet-counter diagnostics.
- Confirm no `Say PAIZHAO` or voice-recognition entry text remains.
- Confirm `kAudioMuted` exists and scene sounds are triggered through
  `play_scene_sound`.
- Confirm small-font device labels use glyph-safe ASCII and do not rely on
  Chinese glyphs that may be missing from the CoreS3 small font.
- Confirm `docs/player-flow-ui.md` contains the taskbook output sections.

## Hardware Verification

Automated local hardware evidence:

- COM7 was detected as USB VID:PID `303A:1001`, serial `44:1B:F6:E3:9B:60`.
- The current sketch compiled and uploaded to COM7 successfully.
- Flash write hashes were verified by `esptool.py`.
- Boot serial output was observed:
  `battle mac=44:1B:F6:E3:9B:60 board=COM7 role=HOST state=DISCOVERING ap=M5PET-E39B60 udp=42105`.

## Module 2 Dataset Enrichment 2026-05-31

Implemented evidence:

- `scripts/train_vision_feature_model.py` now supports reproducible Open Images
  validation downloads through `--download-open-images-val`.
- The same script now supports Open Images bbox crop downloads through
  `--download-open-images-bbox`, producing object-centered public samples.
- `--classes` can target only shortfall classes during download steps.
- Final sample counts in `C:\tmp\m5_vision_samples\manifest.csv`:
  `plant_leaf=220`, `food_fruit=220`, `paper_book=220`,
  `electronics_screen=220`, `metal_key_coin=211`, `fabric_cloth=220`,
  `cup_bottle_water=220`, `toy_figure=220`, `negative=160`.
- Final training report:
  `positive_accuracy=0.071`, `negative_false_positive_rate=0.188`,
  `data_quality=weak`.
- The weak-model flag remains enabled in `vision_model_data.h`, so firmware uses
  stricter distance and margin gates instead of accepting low-quality model
  matches.
- Compile passed with `C:\tmp\m5_arduino_build_enriched_samples`:
  program `1527469` bytes (`48%`), globals `67916` bytes (`20%`).
- Upload to COM7 passed; `esptool.py` verified flash hashes.

Remaining risk:

- Public COCO/Open Images samples increase coverage but still do not match the
  CoreS3 camera domain closely enough. Wall, white paper, tabletop, and
  low-light rejection must be validated with real device captures.
- Do not loosen weak-model gates based only on public sample count.
- No public interface change was made or needed.

## Module 2 Dataset And Pet Library Enrichment 2026-06-01

Implemented evidence:

- `scripts/train_vision_feature_model.py` now supports generated hard negatives
  through `--generate-hard-negatives --hard-negative-count <n>`.
- Generated hard negatives are stored under `C:\tmp\m5_vision_samples\generated\negative`
  and enter `manifest.csv` with source `generated`.
- The training set was expanded to:
  `plant_leaf=260`, `food_fruit=260`, `paper_book=246`,
  `electronics_screen=260`, `metal_key_coin=211`, `fabric_cloth=260`,
  `cup_bottle_water=260`, `toy_figure=260`, `negative=400`.
- Final training report:
  `positive_accuracy=0.089`, `negative_false_positive_rate=0.150`,
  `data_quality=weak`.
- `kVisionModelQualityWeak` remains enabled; weak-model stricter distance and
  margin gates are still required.
- Pet library names and drawing signatures were expanded inside
  `04_camera_pet_battle.ino`; `PetGenes.species` remains `0..2`, and
  `BattlePetPacket.species` remains unchanged.
- Compile passed with `C:\tmp\m5_arduino_build_pet_dataset_enriched`:
  program `1528809` bytes (`48%`), globals `67916` bytes (`20%`).
- Upload to COM7 passed; upload compile reported program `1531469` bytes
  (`48%`), globals `67916` bytes (`20%`), and `esptool.py` verified flash
  hashes.

Remaining risk:

- Hard negatives should reduce blank/flat-scene attraction, but only CoreS3
  physical tests can confirm white wall, white paper, and tabletop rejection.
- Positive-class accuracy remains weak with public data; collect CoreS3-shot
  positive samples before loosening model gates.
- No public interface change was made or needed.

Remaining manual physical validation:

- Use `docs/device-acceptance.md` as the physical CoreS3 validation gate for
  touch flow, two-board battle, SD audio, `/app`, and serial controls.
- Touch each screen transition.
- Capture real camera samples.
- Confirm blank, wall, and too-dark frames enter CAPTURE FAILED and cannot be
  captured.
- Confirm a clear centered object enters the wild pet screen.
- Confirm low-confidence recognition cannot be captured.
- Flash two boards and confirm MATCH, CLASH, result, and exit behavior.
- Confirm serial or future external-button adapters call the reserved entry
  points as intended.

## Module 2 Sample Export API 2026-06-01

Implemented evidence:

- Firmware sample rows now include a `scene` column in `/samples/manifest.csv`
  for wall/paper/desktop/glare/dark/bright/low-texture rejection analysis.
- Firmware sample rows now also include `labelSource` and `autoLabel`, so
  manually labeled `/app` samples remain auditable against the automatic
  recognition result.
- Added RAM-only `/api/v1/sampling` for App-selected sample label and scene.
- Added `/api/v1/capture/quality` for one-frame pre-shot quality probes without
  recognition, capture, pet generation, or sample-row writes.
- Added read-only local endpoints:
  `/api/v1/samples`, `/api/v1/samples/manifest`, and
  `/api/v1/samples/file?path=/samples/...`.
- `/app` adds a local sample-debug panel on the capture page for sample label
  controls, scene controls, quality meter, class/scene counts, recent rows, and
  manifest export.
- Training script scene statistics remain PC-side only and do not change public
  firmware interfaces.

Boundary:

- No changes were made to `pet_model.h`, `vision_types.h`, `ui_types.h`,
  `battle_protocol.h`, `SavedPet`, `BackpackStorage`, or `BattlePetPacket`.
- The sample export path is local SD/HTTP only; it does not introduce cloud
  runtime dependencies or new third-party libraries.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_sampling_quality`
- Compile result: program storage `1588681` bytes (`50%`), globals `68020`
  bytes (`20%`), leaving `259660` bytes.

Remaining physical validation:

- Mount an SD card, capture white wall, white paper, desktop, dark, glare, and
  positive-class samples, then confirm `/api/v1/samples` counts match
  `/samples/manifest.csv`.
- Download the manifest from `/app` and retrain on the PC before changing model
  thresholds.

## App Encyclopedia Update 2026-06-01

Implemented evidence:

- Added read-only `/api/v1/encyclopedia` with all `45` compatible local visual
  templates: 5 elements, 3 base species, and 3 visual variants.
- `/app` now includes a 图鉴 tab that marks entries as owned from the current
  backpack and shows the highest captured level per template.
- Encyclopedia state is derived at request time from existing `SavedPet.genes`;
  it is not persisted separately.
- Pet JSON now includes a RAM-only `care` object with fullness, energy,
  affection, focus, mood, and hint, all derived from existing pet fields.
- `/app` pet cards display fullness, energy, affection, and the derived care
  hint without adding persistent state.

Boundary:

- No public header, `SavedPet`, `BackpackStorage`, or `BattlePetPacket` field
  was added for the encyclopedia or care state.
- Remote battle display still uses base species data from `BattlePetPacket`;
  the encyclopedia and care state are local App display only.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_dex_sampling`
- Compile result: program storage `1590825` bytes (`50%`), globals `68020`
  bytes (`20%`), leaving `259660` bytes.
- Compile command after RAM-only care state:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_care_dex_sampling`
- Compile result: program storage `1591821` bytes (`50%`), globals `68020`
  bytes (`20%`), leaving `259660` bytes.

## SD Addon Manifest Update 2026-06-01

Implemented evidence:

- Added optional SD addon manifest placeholders:
  `sd_card_payload/skins/manifest.csv` and
  `sd_card_payload/actions/manifest.csv`.
- `/api/v1/storage` now reports `addonManifests` for `/skins/manifest.csv` and
  `/actions/manifest.csv`, alongside the existing audio asset report.
- `docs/sd-card-file-boundary.md` documents these paths as optional extension
  data. Missing manifests must not block offline capture, backpack, battle, or
  `/app`.

Boundary:

- The manifests are discovery/index files only in this step. They do not change
  `PetGenes`, rendering, animation, `SavedPet`, or `BattlePetPacket`.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_sd_addons`
- Compile result: program storage `1592545` bytes (`50%`), globals `68020`
  bytes (`20%`), leaving `259660` bytes.

## Battle Replay Log Update 2026-06-01

Implemented evidence:

- Added a RAM-only battle replay ring buffer in
  `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- Each battle result stores the local-view `battleId`, outcome, score diff,
  power/element/spirit diffs, opponent base species/element/level, XP reward,
  friend bonus, and level/stage growth flags.
- Added RAM-only local battle skill display prototypes: each element has 3
  skill names, derived from seed/battle ID/device ID for local display only.
- `/api/v1/battle` now includes `replayCapacity`, `replayCount`, and `replays`.
- Added `/api/v1/battle/replays` for reading the same replay list without the
  live battle summary.
- `/app` battle page renders the recent replay list below the existing battle
  and friendship status.

Boundary evidence:

- No public header changes were required.
- `BattlePetPacket` fields, version, packet size, species limit, and UDP flow
  are unchanged.
- Replay records are not written to `BackpackStorage`, SD card, or UDP packets.
- Skill names do not affect `battle_score`, packet exchange, or persisted pet
  stats. A scoring skill system would require a separate interface review.
- Cross-board replay synchronization remains a future interface-change topic.

Verification:

- Public header diff check:
  `git diff -- arduino_demos/04_camera_pet_battle/pet_model.h arduino_demos/04_camera_pet_battle/vision_types.h arduino_demos/04_camera_pet_battle/ui_types.h arduino_demos/04_camera_pet_battle/battle_protocol.h`
  returned no diff.
- Compile command:
  `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_battle_skills_replays`
- Compile result: program storage `1596421` bytes (`50%`), globals `68828`
  bytes (`21%`), leaving `258852` bytes.

## Experimental Recognition Routes 2026-06-01

Implemented evidence:

- Added `docs/experimental-recognition-routes.md`.
- Added experiment skeletons under `experiments/`:
  `edge_impulse_fomo`, `huskylens`, and `esp_idf_vision_poc`.
- Documented Edge Impulse/FOMO as an experiment branch that must output only
  `classId`, `confidence`, `presence`, and diagnostics mappable to the current
  `RecognitionResult`.
- Documented HuskyLens as an optional I2C/UART coprocessor route that must
  fall back to local recognition when absent, timed out, or unmapped.
- Documented ESP-IDF + ESP-DL/ESP-WHO as a separate PoC route, not an Arduino
  mainline dependency.
- Expanded `docs/open-source-release-package.md` with OSHWHub/OSHWHLab and
  Hugging Face dataset packaging requirements.
- Added `release/oshw/` with BOM, verification log, demo video shot list, and
  Hugging Face dataset card templates.
- Updated README repository layout and roadmap links for experiments and
  release packaging.

Boundary evidence:

- These are documentation and experiment-boundary changes only.
- No public header, packet, storage, or dependency change is required.
- Any future bbox, logits, external-module metadata, model IDs, or persistent
  sample metadata must go through an interface-change proposal first.

## Sample Distance Hint Update 2026-06-01

Implemented evidence:

- SD sample rows in `/samples/manifest.csv` now append a `distanceHint` column.
- `distanceHint` is derived from subject presence, background-flatness checks,
  the CoreS3 proximity sensor when available, and visual center difference.
- `/api/v1/samples` includes `distanceHint` in recent sample JSON rows.
- `/app` renders the distance hint in the recognition debug panel's recent
  sample list.

Boundary evidence:

- `distanceHint` is a sample-training aid only.
- Recognition, capture gates, pet generation, `RecognitionResult`, public
  headers, backpack storage, and `BattlePetPacket` are unchanged.
- Old sample manifest rows remain readable; missing `distanceHint` becomes an
  empty display value.

## SD Skin Palette Update 2026-06-01

Implemented evidence:

- Added optional SD skin palette loading in
  `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- Firmware reads `/skins/palettes/default.csv` and
  `/skins/palettes/<element>.csv` when SD is present.
- Palette CSV rows use `key,r,g,b`; supported keys are `body` and `accent`.
- `sd_card_payload/skins/palettes/` now contains default, wood, fire, earth,
  metal, and water palette examples.
- `sd_card_payload/manifest.csv`, `sd_card_payload/README.md`,
  `docs/sd-card-file-boundary.md`, `docs/app-http-api.md`, and
  `docs/open-platform-extension-audit.md` document the palette behavior.

Boundary evidence:

- Missing SD card, missing palette files, empty files, invalid rows, or unknown
  keys fall back to built-in colors.
- Palette data affects local drawing colors only.
- No public header, `PetGenes`, backpack storage, `RecognitionResult`, or
  `BattlePetPacket` change is required.

## SD Action Pack Update 2026-06-01

Implemented evidence:

- Added optional SD action profile loading in
  `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.
- Firmware maps current screens to `/actions/idle.csv`, `/actions/wild.csv`,
  `/actions/bag.csv`, `/actions/battle_clash.csv`, and `/actions/result.csv`.
- Action CSV rows use `key,value`; supported keys are `bob`, `sparkle`, and
  `tilt`.
- `sd_card_payload/actions/` now contains profile examples for idle, wild,
  bag, battle clash, and battle result screens.
- `sd_card_payload/manifest.csv`, `sd_card_payload/README.md`,
  `docs/sd-card-file-boundary.md`, `docs/app-http-api.md`, and
  `docs/open-platform-extension-audit.md` document the action behavior.

Boundary evidence:

- Missing SD card, missing action files, empty files, invalid rows, or unknown
  keys fall back to built-in static drawing.
- Action data affects local drawing only.
- No public header, `PetGenes`, backpack storage, `RecognitionResult`, or
  `BattlePetPacket` change is required.

## Capture Burst Debug Update 2026-06-01

Implemented evidence:

- Firmware caches the most recent 3-frame photo burst in RAM for App debugging.
- `/api/v1/status` now includes `recognition.burstCandidates`, with each
  candidate's quality, presence, confidence, best distance, margin, classifier
  source, image traits, timing, and failure reason.
- `/app` capture recognition panel renders the burst rows and marks the selected
  best frame.
- `docs/open-platform-extension-audit.md` no longer lists App burst
  visualization as remaining work.

Boundary evidence:

- Burst debug data is RAM-only and only reflects the latest photo attempt.
- The data is not written to SD sample manifest rows, not stored in
  `BackpackStorage`, and not sent over `BattlePetPacket`.
- No public header, `PetGenes`, `RecognitionResult`, backpack storage, or UDP
  protocol change is required.

## Serial Sampling Control Update 2026-06-01

Implemented evidence:

- USB serial now accepts `SAMPLE on|off [label] [scene]`, `SAMPLE label
  <label>`, `SAMPLE scene <scene>`, and `SAMPLE status`.
- Serial `SAMPLE` uses the same RAM-only `sample_mode_enabled`,
  `sample_mode_label`, and `sample_mode_scene` state as `/api/v1/sampling`.
- Serial `STATUS` now prints the current sampling mode, label, and scene.
- `docs/usage.md`, `docs/device-acceptance.md`, `docs/app-http-api.md`, and
  `docs/open-platform-extension-audit.md` document the serial sampling path.

Boundary evidence:

- Serial sampling controls only the label/scene written to optional SD sample
  rows.
- Recognition, capture gates, pet generation, backpack storage, and
  `BattlePetPacket` remain unchanged.
- No public header or UI button enum change is required.

## HuskyLens Bridge Experiment Update 2026-06-01

Implemented evidence:

- Added `experiments/huskylens/huskylens_bridge.py`, a standard-library desktop
  mapper from HuskyLens learned IDs to the existing 8 class labels.
- Added `experiments/huskylens/serial-hint-protocol.md` to define the
  `HUSKY_HINT <class> <confidence> <presence>` experiment boundary.
- Updated `experiments/huskylens/README.md`,
  `docs/experimental-recognition-routes.md`, and
  `docs/open-platform-extension-audit.md` with the bridge workflow.

Boundary evidence:

- Disabled ID rows, unmapped IDs, and low external confidence fall back to local
  recognition.
- The bridge does not import the HuskyLens Arduino library and does not add any
  project dependency.
- Production firmware consumes `HUSKY_HINT` only as a RAM-only one-shot serial
  hint; no public header, storage, or UDP packet is changed.

## Edge Impulse / FOMO Adapter Update 2026-06-01

Implemented evidence:

- Added `experiments/edge_impulse_fomo/edge_output_adapter.py`, a
  standard-library desktop adapter for Edge Impulse classification JSON and
  FOMO-style `bounding_boxes` JSON.
- Added `sample-classification.json`, `sample-fomo-boxes.json`, and
  `serial-hint-protocol.md` under `experiments/edge_impulse_fomo/`.
- Updated `experiments/edge_impulse_fomo/README.md`,
  `docs/experimental-recognition-routes.md`, and
  `docs/open-platform-extension-audit.md` with the adapter workflow.

Boundary evidence:

- Adapter output is reduced to existing class labels, confidence, presence, and
  fallback reason.
- `negative`, unsupported labels, and low confidence stay on local fallback.
- Production firmware consumes `EDGE_HINT` only as a RAM-only one-shot serial
  hint; no public header, storage, dependency, or UDP packet is changed.

## External Vision Serial Hint Update 2026-06-01

Implemented evidence:

- Firmware serial control accepts `EDGE_HINT <class> <confidence> <presence>`
  and `HUSKY_HINT <class> <confidence> <presence>`.
- Hints are RAM-only, expire after 10 seconds, and are cleared after one photo
  burst.
- Accepted hints only set the candidate class after the live CoreS3 camera frame
  passes the existing subject-presence path; background-like scenes are still
  rejected by the existing capture gate.
- Low confidence, low presence, `negative`, `unknown`, or unmapped labels clear
  the hint and fall back to the local recognizer.

Boundary evidence:

- No public header, `RecognitionResult` field, `PetGenes`, backpack storage,
  dependency, or `BattlePetPacket` layout was changed.
- The firmware does not persist external IDs, bbox, logits, model metadata, or
  HuskyLens module metadata.
- This is an experiment bridge for the existing desktop adapters, not a claim
  that Edge Impulse or HuskyLens runtime is fully integrated.

## ESP-IDF Vision PoC Skeleton Update 2026-06-01

Implemented evidence:

- Added a standalone ESP-IDF project skeleton under
  `experiments/esp_idf_vision_poc/`.
- The skeleton includes top-level `CMakeLists.txt`, `sdkconfig.defaults`,
  `main/CMakeLists.txt`, `main/main.c`, and `README-build.md`.
- `main/main.c` is a contract smoke test that logs heap values and verifies a
  future model output can be reduced to class label, confidence, presence, and
  fallback reason.
- Updated `experiments/esp_idf_vision_poc/README.md`,
  `docs/experimental-recognition-routes.md`, and
  `docs/open-platform-extension-audit.md`.

Boundary evidence:

- Camera capture, ESP-DL, and ESP-WHO are not wired yet.
- The PoC is not included from the Arduino sketch.
- No public header, storage, dependency, or UDP packet is changed.

## Open Release Readiness Check Update 2026-06-01

Implemented evidence:

- Added `scripts/check-open-release-readiness.py`, a standard-library advisory
  checker for OSHWHub/OSHWHLab and Hugging Face dataset publication readiness.
- The checker verifies required firmware/docs/release files, filled
  verification logs, filled dataset card, public hardware photos, local sample
  manifest/report presence, and obvious API key or bearer-token markers.
- `--strict` turns unresolved `PENDING` or `FAIL` items into a non-zero release
  gate.
- Added `scripts/build-hf-dataset-card.py` to generate a Hugging Face dataset
  card draft from the local sample `manifest.csv` and `report.json`.
- The generated dataset card stays non-publishable while license/privacy review
  is still marked as draft or pending; the readiness checker treats these
  markers as `PENDING`.
- Added `scripts/audit_vision_scene_coverage.py` to gate required negative
  scene coverage for white wall, white paper, desktop, glare, and dark samples.
  The release readiness checker now includes that scene coverage result.
- Updated `release/oshw/README.md`, `docs/open-source-release-package.md`, and
  `docs/open-platform-extension-audit.md`.

Boundary evidence:

- The checker does not upload files or call external services.
- It does not include private samples in the repository.
- No firmware, public header, storage, dependency, or UDP packet is changed.

Verification:

- `python .\scripts\audit_vision_scene_coverage.py --samples-root C:\tmp\m5_vision_samples`
  reports `INCOMPLETE` for the current local sample root because required
  negative scenes are not yet present.
- A temporary manifest with 20 negative rows each for `white_wall`,
  `white_paper`, `desktop`, `glare`, and `dark` passes
  `scripts/audit_vision_scene_coverage.py --strict`.
- `python .\scripts\check-open-release-readiness.py` now keeps
  `vision_scene_coverage` as `PENDING` until those scene samples exist.

## SD Payload Validation Update 2026-06-01

Implemented evidence:

- Added `scripts/validate-sd-payload.py`, a standard-library validator for the
  optional SD payload package.
- The validator checks `sd_card_payload/manifest.csv` paths, declared sizes,
  `.raw` audio format metadata, 22050 Hz sample rate, and 32768-byte per-file
  limit.
- It also checks `skins/manifest.csv`, `actions/manifest.csv`, palette CSV
  keys, action profile keys, v1 version fields, and value ranges that match the
  firmware fallback behavior.
- `scripts/check-open-release-readiness.py` now includes `sd_payload_validation`
  so public-release checks fail on malformed SD resource packs.

Boundary evidence:

- This is a packaging and documentation gate only.
- No firmware source, public header, backpack storage, dependency, or UDP packet
  is changed by this update.

Verification:

- `python .\scripts\validate-sd-payload.py --payload .\sd_card_payload`
  reports `sd payload validation: PASS`.
- `python -m py_compile .\scripts\validate-sd-payload.py .\scripts\check-open-release-readiness.py`
  passes.
- `python .\scripts\check-open-release-readiness.py` reports
  `sd_payload_validation: PASS`; overall readiness is controlled by the other
  release gates listed in the current check output.

## HF Dataset Publishability Audit Update 2026-06-01

Implemented evidence:

- Added `scripts/audit_hf_dataset_publishability.py`, a standard-library
  preflight for Hugging Face dataset publication.
- The audit checks local `manifest.csv` existence, required columns, class
  presence, minimum class counts, sample file existence, user-home path markers,
  absolute publish paths, public-source license review, training report quality,
  required negative scene coverage, and dataset card draft markers.
- `scripts/check-open-release-readiness.py` now includes
  `hf_dataset_publishability`, so a malformed or outdated sample manifest becomes
  a release `FAIL` instead of a hidden note.
- Updated `release/oshw/README.md`, `docs/open-source-release-package.md`, and
  `docs/open-platform-extension-audit.md` with the new audit command and current
  dataset release boundary.

Boundary evidence:

- The audit only reads local manifest/report/card metadata.
- It does not upload data, download data, modify samples, add dependencies,
  change firmware, or alter public headers, storage, or UDP packets.

Verification:

- `python .\scripts\audit_hf_dataset_publishability.py --samples-root C:\tmp\m5_vision_samples`
  currently reports `FAIL` because `C:\tmp\m5_vision_samples\manifest.csv` is an
  old-format manifest without the required `scene` column.
- The same audit also reports `PENDING` for absolute manifest paths, public
  source license review, `data_quality=weak`, missing real negative scene
  coverage, and the draft dataset card marker.
- `python .\scripts\check-open-release-readiness.py` now reports
  `hf_dataset_publishability: FAIL` for that manifest schema issue, while
  `sd_payload_validation` still reports `PASS`.

## Delivery Refresh 2026-06-01

Implemented evidence:

- Refreshed `C:\tmp\m5_vision_samples\manifest.csv` and
  `C:\tmp\m5_vision_samples\report.json` with the existing
  `scripts/train_vision_feature_model.py` flow.
- The refreshed manifest now includes the required `scene` column.
- The refreshed report includes `scene_sample_counts` and `scene_eval`.
- Regenerated `release/oshw/hf-dataset-card.md` from the refreshed metadata.
- Added `release/oshw/delivery-notes.md` for the fast handoff state.

Verification:

- `.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_hf_manifest_refresh`
  passes: program storage `1607857` bytes, global variables `69268` bytes.
- `python .\scripts\audit_hf_dataset_publishability.py --samples-root C:\tmp\m5_vision_samples`
  now reports `INCOMPLETE`, not `FAIL`; manifest schema passes.
- `python .\scripts\check-open-release-readiness.py` now reports `INCOMPLETE`,
  with `hf_dataset_publishability` as `PENDING` rather than `FAIL`.

Remaining delivery notes:

- The dataset is still not ready for public Hugging Face upload because paths
  need sanitizing, public sample licenses need review, model quality remains
  weak, `white_paper` and `glare` negative coverage is still short, and the
  dataset card remains draft.
- Public OSHWHub/OSHWHLab posting still needs real hardware photos and a filled
  verification log.

## HF Dataset Manifest Sanitization Update 2026-06-01

Implemented evidence:

- Added `scripts/export_hf_dataset_manifest.py`.
- Generated `release/oshw/hf-dataset-manifest.csv` from
  `C:\tmp\m5_vision_samples\manifest.csv`.
- The generated manifest uses relative `data/<source>/<class>/<sha1>.<ext>`
  publish paths and keeps license/privacy/include review columns explicit.
- `scripts/audit_hf_dataset_publishability.py` now treats the sanitized manifest
  as evidence for `relative_publish_paths`.
- `scripts/check-open-release-readiness.py` now requires
  `release/oshw/hf-dataset-manifest.csv` in the release kit.

Verification:

- `python .\scripts\export_hf_dataset_manifest.py --samples-root C:\tmp\m5_vision_samples --out .\release\oshw\hf-dataset-manifest.csv`
  writes `2417` rows.
- `python .\scripts\check-open-release-readiness.py` reports
  `HF sanitized manifest: PASS` and keeps overall readiness `INCOMPLETE` only
  for review/data/hardware pending items.
