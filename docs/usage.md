# Usage Guide

This guide describes the v0.1 user flow on M5Stack CoreS3.

Current boundaries:

- No voice recognition is implemented.
- Sound effects are local and may be muted.
- Recognition is lightweight local 8-class recognition, not a general vision large model.
- Backpack records are stored in ESP32 `Preferences`.
- No SD card, router, cloud service, or external network is required.

## Controls

The CoreS3 screen is divided into three touch zones:

| Area | Idle | Backpack | Battle |
| --- | --- | --- | --- |
| Left | BAG | RELEASE | BAG |
| Middle | MATCH | SELECT | IDLE |
| Right | PHOTO | NEXT | PHOTO |

Reserved external entry points:

- `handle_external_action(uint8_t action)`
- `handle_external_button(uint8_t button)`

These are kept for later physical buttons, other MCU input, or serial commands.

There is no voice recognition control in v0.1. All current interaction is touch
or reserved external action calls.

## Audio and Runtime Boundary

- Sound effects are local firmware cues and can be muted through the firmware
  audio mute flag.
- Capture, backpack, and battle run locally on CoreS3.
- No network, cloud API, or SD card is required for normal use.
- Recognition is a lightweight 8-class local feature recognizer, not a general
  vision model.

## Pet Flow

1. Start from IDLE.
2. Tap PHOTO.
3. The camera captures a frame and down-samples it to a 64x64 vision input buffer.
4. The local recognizer checks whether there is a clear centered subject.
5. If no subject or low confidence is detected, the screen enters CAPTURE FAILED and no pet can be captured.
6. If recognition succeeds, a wild pet is generated from the recognized class:
   - Wood: leaf/deer/vine forms.
   - Fire: flame/cat/bird/lava forms.
   - Earth: bear/turtle/stone forms.
   - Metal: wolf/mecha/armor forms.
   - Water: otter/dragon/fish forms.
7. Capture stores the pet into the backpack if there is space.
8. Backpack capacity is 6 pets.

The first local recognizer uses 8 heuristic classes:

- `plant_leaf`
- `food_fruit`
- `paper_book`
- `electronics_screen`
- `metal_key_coin`
- `fabric_cloth`
- `cup_bottle_water`
- `toy_figure`

## Backpack Flow

- RELEASE opens a confirmation screen before deleting a stored pet.
- SELECT marks the current backpack pet as the active battle pet and returns to IDLE.
- NEXT cycles through stored pets.
- The BAG screen shows selected status, level, stage, XP, element, mood, wins,
  battle count, and win rate.

Pet records contain:

- Genes: element, species, body scale, eyes, horns, tail, aura, pattern density, accent color, seed.
- Growth state: level, stage, XP, battles, wins, captured time, last growth time.

## Battle Flow

1. Burn the same v0.1 sketch to two CoreS3 boards.
2. On each board, choose or capture a pet.
3. On each board, enter MATCH from the idle middle area.
4. Both boards use local Wi-Fi AP discovery named `M5PET-xxxxxxxx`.
5. The devices automatically pair with each other; the player does not need to
   choose a host or client role.
6. Pet packets are exchanged locally through UDP port `42105`.
7. Once each board sees the other pet, the BATTLE screen first shows a short
   `CLASH...` phase.
8. The result is calculated locally and shows both scores, XP gain, and any
   rematch friendship bonus.

MATCH player status:

- `FINDING`: searching for the other board.
- `CONNECTED`: link is established.
- `READY`: both pets are visible and battle can resolve.

The result uses pet stats, growth level, five-element advantage, and deterministic seed-based luck.

Repeated battles against the same recent peer give a local friendship bonus:
+5, +10, then +15 XP. This friendship state is RAM-only in v0.1 and does not
change `SavedPet` or `BattlePetPacket`.

## Current Runtime Boundaries

- No voice recognition is active.
- Sound cues exist, but firmware can keep them muted through `kAudioMuted`.
- The recognizer is a lightweight local 8-class model plus fallback, not a
  general vision model.
- Backpack data uses ESP32 `Preferences`.
- No SD card is required.
- No cloud API or outside network is required for the current flow.

## Productization Notes

Future App or desktop tools may expose backpack cards, pet details, friendship, battle records, capture triggering, recognition results, and log/sample export. These are planned in `docs/app-cloud-roadmap.md`; they are not required to use the current offline firmware.
