# Usage Guide

This guide describes the v0.1 user flow on M5Stack CoreS3.

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

## Voice Commands

When ESP-SR is enabled, the sketch uses offline command word recognition:

| Spoken command | Meaning |
| --- | --- |
| `pai zhao` | Take photo |
| `bei bao` | Open backpack |
| `fan hui` | Return to idle |
| `dui zhan` | Enter match battle |

If ESP-SR is not available, the sketch falls back to loudness-triggered photo capture.

## Pet Flow

1. Start from IDLE.
2. Say `pai zhao` or tap PHOTO.
3. The camera captures local color/brightness/saturation/contrast traits.
4. A wild pet is generated from the five elements:
   - Wood: leaf/deer/vine forms.
   - Fire: flame/cat/bird/lava forms.
   - Earth: bear/turtle/stone forms.
   - Metal: wolf/mecha/armor forms.
   - Water: otter/dragon/fish forms.
5. Capture stores the pet into the backpack if there is space.
6. Backpack capacity is 6 pets.

## Backpack Flow

- RELEASE opens a confirmation screen before deleting a stored pet.
- SELECT marks the current backpack pet as the active battle pet and returns to IDLE.
- NEXT cycles through stored pets.

Pet records contain:

- Genes: element, species, body scale, eyes, horns, tail, aura, pattern density, accent color, seed.
- Growth state: level, stage, XP, battles, wins, captured time, last growth time.

## Battle Flow

1. Burn the same v0.1 sketch to two CoreS3 boards.
2. On each board, choose or capture a pet.
3. On each board, enter MATCH from the idle middle area or say `dui zhan`.
4. Both boards create a Wi-Fi AP named `M5PET-xxxxxxxx`.
5. The board with the larger device ID automatically joins the smaller ID board.
6. Pet packets are exchanged through UDP port `42105`.
7. Once each board sees the other pet, battle result is calculated locally.

MATCH status line:

- `HOST`: waiting as the AP owner.
- `CLIENT`: connected or connecting to the peer AP.
- `TX`: successful UDP pet packets sent.
- `RX`: peer pet packets received.
- `F`: UDP send failures.

The result uses pet stats, growth level, five-element advantage, and deterministic seed-based luck.

