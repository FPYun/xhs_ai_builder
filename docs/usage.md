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
| Left | 背包 | 放生 | 背包 |
| Middle | 匹配 | 选择 | 休闲 |
| Right | 拍照 | 下一只 | 拍照 |

External entry points:

- `handle_external_action(uint8_t action)`
- `handle_external_button(uint8_t button)`

USB serial at 115200 baud can drive the same state machine for bench testing:

- `BTN L`, `BTN M`, or `BTN R`: simulate the left, middle, or right footer area.
- `ACT photo|bag|match|idle|next|select|capture|release|confirm_release|cancel|friend`:
  run the same guarded actions used by `/app`.
- `ACT friend`: add the currently seen opponent, or the most recent opponent,
  to the local persisted friend list and show a short on-device notice.
- `SOUND idle|photo|wild|bag|capture|release|select|match|clash|friend|win|draw|lose|warning|intro|level|exit|cancel`:
  test the existing mute-aware scene sound cues through `play_scene_sound`.
- The `/app` settings page also includes an App soundboard panel with every
  supported serial sound cue; it calls `/api/v1/action?type=sound&cue=...` and
  uses the same mute-aware `play_scene_sound` route.
- The `/app` event page filters recent capture, battle, friend, setting,
  system, and action events. Each filter tab includes a count badge before the
  event detail panel is opened.
- `STATUS`: print current screen, button labels, backpack count, active pet,
  growth target, growth-boosted stats, friendship summary, recent battle
  result, and battle phase.
- `ACCEPTANCE`: print a compact bench-check summary for the physical acceptance
  pass: current flow/buttons, active pet/backpack, battle phase/result,
  friendship/rematch, SD, mute, camera, and sampling state.
- `SAMPLE on|off [label] [scene]`: enable or disable SD sample labeling from
  serial, optionally setting the active label and scene in the same command.
- `SAMPLE label <label>` and `SAMPLE scene <scene>`: update the RAM-only sample
  label or scene without using `/app`. Valid labels are the 8 recognition
  classes plus `negative`; valid scenes are `white_wall`, `white_paper`,
  `desktop`, `glare`, `dark`, `bright`, `low_texture`, `hand_cover`, and
  `unknown`.
- `SAMPLE status`: print the current sampling mode, label, and scene.
- `EDGE_HINT <class> <confidence> <presence>` or
  `HUSKY_HINT <class> <confidence> <presence>`: store a RAM-only one-shot
  external recognition hint for the next photo. Valid classes are the existing
  8 labels or `negative`; confidence must be at least `70`, presence at least
  `55`, and the live CoreS3 frame must still pass subject/background gates.
- `HELP`: print the command summary.

There is no voice recognition control in v0.1. All current interaction is touch
or one of these local external action calls.

## Audio and Runtime Boundary

- Sound effects are local firmware cues and can be muted through the firmware
  audio mute flag.
- If a microSD card is present, short unsigned 8-bit mono PCM `.raw` clips at
  22050 Hz can override the built-in cues under `/audio/ui/`,
  `/audio/battle/`, and `/audio/music/`. Files are optional and capped to short
  clips; missing files or no SD card fall back to the built-in synthesized cues.
- Capture, backpack, and battle run locally on CoreS3.
- No network, cloud API, or SD card is required for normal use.
- Recognition is a lightweight 8-class local feature recognizer, not a general
  vision model.

## Pet Flow

1. Start from IDLE.
2. Tap PHOTO.
3. The camera captures a frame and down-samples it to a 64x64 vision input buffer.
4. The local recognizer checks whether there is a clear centered subject.
5. If no subject or low confidence is detected, the screen enters CAPTURE FAILED,
   shows a center framing guide, and no pet can be captured.
6. If recognition succeeds, a wild pet is generated from the recognized class:
   - Wood: leaf/deer/vine forms.
   - Fire: flame/cat/bird/lava forms.
   - Earth: bear/turtle/stone forms.
   - Metal: wolf/mecha/armor forms.
   - Water: otter/dragon/fish forms.
7. Capture stores the pet into the backpack if there is space.
8. Backpack capacity is 6 pets.

The wild pet screen shows the same 6-slot capacity bar and warns when the bag is
already full, before the player attempts capture.
Using the right button on WILD only releases the current wild preview and returns
to IDLE; it does not delete anything from the stored backpack.
If capture fails because the backpack is full, CAPTURE FAILED shows a full-bag
card, the 6-slot capacity bar, and directs the player to BAG to release one pet
before taking another photo. That full-bag failure plays the normal warning cue
followed by the BAG cue through `play_scene_sound`, so the audio also points to
the next step without bypassing mute.
For normal recognition failures, CAPTURE FAILED shows subject and recognition
scores as two small progress bars beside the framing guide so the player can
tell whether to center the subject or improve lighting/background contrast.
The App capture quality card mirrors this with subject and recognition rails,
then offers capture when recognized or retake when the scene still needs work.

After capture succeeds, the success screen shows the new pet's stats, XP
progress bar, 6-slot backpack bar, the active slot number, the capture XP
reward, active-pet confirmation, next evolution/level XP target, and the next
waiting-growth countdown before the player chooses BAG, MATCH, or PHOTO. The
same compact NEXT growth badge used in BAG keeps that target visible.

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

- RELEASE opens a confirmation screen with the target pet avatar, five-element
  badge, irreversible warning, a compact summary strip for slot/status/stage,
  win rate, XP rail, next growth target, and a 6-slot bag bar for the current
  target/active slot state before deleting a stored pet. Left or right cancels
  and returns to BAG with a short cancellation notice plus a cancel cue.
- The built-in `/app` page mirrors the same three-button footer on home,
  backpack, and battle tabs. App `confirm_release` only works after the device
  is already on RELEASE_CONFIRM, so phone controls cannot skip the second
  confirmation step.
- The `/app` home status begins with a current-flow banner using `screenLabel`
  and a mode-colored strip, so states such as idle, capture, backpack, match,
  battle, and release confirmation appear as player-facing game modes.
- The CoreS3 page titles use the same game-state direction: a compact title
  plate with an accent strip and border makes each state name easier to scan.
- App pet cards mirror the device's next evolution/level XP target through
  `growthGoal` as a separate compact badge, so phone-side backpack cards show
  the same growth loop as BAG without crowding the XP/win-rate line.
  The pet stat values shown there are the same growth-boosted battle stats used
  by local battles, not only the raw captured genes.
  The App pet growth wait rail also shows the waiting-growth countdown and
  progress, matching the CoreS3 BAG/IDLE growth meter.
  The App training plan card on the home page shows the next growth target,
  XP progress, waiting countdown, and the three growth sources: capture,
  battle, and passive waiting.
  The App bag overview card sits above the backpack pager and shows capacity,
  active slot, device cursor slot, and remaining empty slots before the selected
  pet card.
- The `/app` battle tab also has an 添加好友 action. It adds the currently seen
  opponent or most recent opponent to the same local persisted friend list used
  by battle rewards, then shows the result on the CoreS3 screen.
- SELECT marks the current backpack pet as the active battle pet and returns to IDLE.
- NEXT cycles through stored pets.
- The BAG screen shows selected status, level, stage, XP, element, mood,
  wins/total battles, win rate, and a 6-slot capacity bar. The active slot is
  highlighted separately from the current cursor slot. A compact NEXT growth
  badge keeps the next evolution or level target visible above the stat panel.
  The 力/速/心 meters use
  the same level/stage growth boost as battle packets. The bottom status line
  shows wins/total battles, win rate, the next evolution/level XP target, and a
  thin waiting-growth progress meter when there is no fresh growth notice.
- `/app` backpack/current-pet cards and USB serial `STATUS` expose the same
  next evolution/level XP target and growth-boosted battle stats for bench
  validation.
- When the backpack is empty, BAG shows a first-capture card with 0/6 capacity,
  a framing guide, and PHOTO as the next step.
- In `/app`, the empty backpack state is also a first-capture card: it shows
  0/6 capacity, a direct PHOTO action, and a shortcut to the capture page.
- The `/app` current-pet card no longer stops at plain empty text. If stored
  pets exist it points to BAG selection; if the bag is empty it points to PHOTO.
- The idle screen shows a first-capture card with 0/6 capacity and a framing
  guide when there is no pet. Once a pet is active, it becomes a compact partner
  card with avatar, level, stage, XP progress, backpack count, win rate, and
  next waiting-growth countdown/progress.
- If a recent opponent exists, the idle partner card shows the opponent short
  ID, last result or next rematch XP preview, and friendship score instead of
  the passive waiting-growth countdown.

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
   pet-entry and three-round clash phase with both pet names, level badges,
   P/E/S progress, center VS impact beams, and one-shot round cues.
8. The result is calculated locally and shows a settlement verdict card, total
   score difference, a mirrored local battle ID that both boards can compare,
   both scores, P/E/S summary chips, XP gain, and any rematch friendship bonus.
   The exit screen shows a compact result badge, a
   NEXT line for BAG/IDLE/PHOTO or the rematch XP opportunity, the
   recent opponent pet name and level when available, friendship state,
   friendship meter, and next rematch XP preview before letting the player
   return to BAG, IDLE, or PHOTO.

MATCH player status uses neutral player wording:

- MATCHING / PAIRING: searching for the other board.
- CONNECTED: link is established.
- READY: both pets are visible with a center VS badge and battle can resolve.

The device uses glyph-safe ASCII for dense small-font labels (`SUBJ`, `REC`,
`BAG`, `WIN`, `GROW`, `P/A/S`, `P1/P2`, `F/C/B`) to avoid CoreS3 missing-glyph
boxes. Burning the main sketch from another module workflow will include these
display fixes because they live in `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`.

If MATCH is opened without an active pet, the device shows a preparation card:
go to BAG to choose a stored pet, or use PHOTO to capture the first pet when the
bag is empty. This warning state can still exit through BAG, IDLE, or PHOTO.
The cue follows the same logic: warning plus BAG when stored pets exist, or
warning plus PHOTO when the bag is empty.

The MATCH status panel shows the current pairing step label and elapsed waiting
time while no opponent is visible. After sync, it switches to the opponent short
ID plus the last-seen age. The right side of the panel shows a three-step sync
meter: 寻, 连, 战. It is a player-facing pairing indicator and does not expose
host, client, UDP, packet counters, or IP details. Advancing to 连 or 战 plays a
one-shot local cue through the normal mute-aware sound path. The status card
keeps a visible middle-exit hint so the player can leave MATCH while scanning
continues.

The MATCH screen also includes a friendship card with friend count, recent
opponent short ID, bond label, friendship score meter, last result, score
difference, a compact 好友/密友 goal badge, and either the last XP reward or the
next rematch XP preview when available. Friendship rails are stage-colored:
cyan for new friends, yellow for 好友, and green for 密友.
MATCH and BATTLE EXIT also show a compact device-side friendship badge with the
current bond label and score color, so new friend, 好友, and 密友 states are
visible without reading the whole line.
The built-in `/app` page mirrors the same friendship stage as compact progress
rails on the battle page, friendship summary, and each friend card. USB serial `STATUS` mirrors the same
friendship state with short opponent IDs, current 好友/密友 progress, next friendship gain,
and the next rematch XP reward when the recent-opponent window is still active. `/app` action
`friend` and USB serial `ACT friend` can add the current or recent opponent
without changing stored pet data, and both paths show the same short
on-device add-friend result.
When no local friend is recorded yet, the `/app` friends tab shows an App friend
empty card with recent opponent, current friendship goal, add-friend, and
rematch actions.
The same tab now starts with an App friend goal card that summarizes current
friendship score, recent opponent, next rematch XP, next friendship gain, and
direct battle/add-friend actions.
It also includes an App friend streak card with a three-step rematch chain,
next XP reward, next friendship gain, and close-friend goal.

The clash view shows both pet avatars with five-element badges and a center VS
badge, immediately shows the first-round power/speed advantage, then continues
through a 力/克/心 round track with round-by-round power/speed, element, and
spirit advantage. Each round also has a compact ME/RIVAL score plate with a
centered advantage rail, so the player can read the current exchange before
settlement. Opponent entry plays the generated opponent pet sound, and
the local clash timer starts after that entry cue so the three-round animation
does not get shortened. Advancing from the first round to the second and final
rounds triggers one-shot clash cues through the normal mute-aware sound path.

The result uses the pet stats and growth level captured at the start of the
encounter, plus five-element advantage and deterministic seed-based luck. The
result page shows those scoring stats, a settlement verdict card with total
score difference and score-delta rail, post-reward XP progress rail,
stage-colored friendship progress rail when a recent peer exists, and a
力/克/心 round summary with 胜/负/平 labels before the exit flow. The built-in
`/app` battle tab mirrors this as a top battle verdict card plus three compact
round chips instead of showing raw differences in one long line.
It also shows an App battle phase card above the rows, with pairing progress,
waiting time, and the active clash round when available.
Before that, the App match readiness card shows the active partner, recent
opponent, latest result, friendship goal, and rematch XP cue for MATCH setup.
The App battle entry card then names the local partner, the synced or waiting
opponent, and the entry status before the VS matchup appears.
The same tab now includes an App battle matchup card with the local partner,
opponent partner, avatars, level/element labels, and a center VS badge.
The App battle round track card then shows the three-round 力速/五行/气势
sequence with the current round highlighted and score chips for each round.
The App result view also includes an App battle reward card that groups XP,
growth result, rematch XP, next friendship gain, and the friendship rail.
After a valid result, the App battle exit card shows the cleanup step and gives
direct actions for rematch, idle, bag review, and adding the recent opponent.
The same battle tab includes App battle replay history cards for recent local
records, showing opponent, skill, round, XP, friendship, and growth chips.

Repeated battles against the same recent peer give a local friendship bonus:
+5, +10, then +15 XP. This friendship state is saved in an independent
`wuxingfr` Preferences namespace and does not change `SavedPet` or
`BattlePetPacket`. First battle against a peer adds a local friend entry; score
60 shows 好友 and score 100 shows 密友. The match/result UI shows new-friend
reminders, bond-upgrade reminders, and the remaining score to the next 好友/密友
threshold. If the next rematch will cross a threshold, the
device shows 再战可成好友 or 再战可成密友, and bond upgrades reuse the level-up cue.

When that preview first appears for the current peer and target rank, MATCH
plays one friendship cue through the normal mute-aware sound path. Actual bond
upgrades still reuse the level-up cue.

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
