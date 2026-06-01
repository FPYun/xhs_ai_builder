# Device Acceptance Checklist

This checklist is the physical validation gate for the CoreS3 pet battle demo.
Static checks and Arduino compilation prove code structure only; this document
captures what still must be confirmed on real devices before the player-flow
goal can be treated as complete.

## Preflight

- Compile and flash the same `04_camera_pet_battle.ino` build to two CoreS3
  boards.
- Confirm both boards boot to the player-facing idle page, not a diagnostic
  screen.
- Confirm no SD card is required: boot one board without SD and verify capture,
  backpack, `/app`, and MATCH still open.
- Optional SD audio test: copy `sd_card_payload/` contents to the SD card root
  on one board. The expected root paths are `/audio/ui/`,
  `/audio/battle/`, and `/audio/music/`.
- Connect a phone or laptop to the board AP and open `/app`.
- Open serial monitor at the project baud rate for `STATUS`, `ACT ...`, and
  `SOUND ...` checks.

## Single-Board Player Flow

- `IDLE -> PHOTO -> WILD/CAPTURE_FAIL`: tap right, confirm the camera path
  either shows a wild pet with CAPTURE/IDLE/RELEASE or a failure screen with
  BAG/IDLE/RETRY.
- `WILD -> CAPTURE -> BAG`: capture a recognized pet, reboot, and confirm the
  pet remains in Preferences and appears in BAG.
- `BAG -> SELECT -> IDLE -> MATCH`: select a pet in BAG, confirm it becomes the
  active partner, then enter MATCH from the idle middle area.
- `BAG -> RELEASE -> CONFIRM/Cancel`: release must enter the confirmation page;
  middle confirms deletion, left or right cancels back to BAG.
- `CAPTURE_FAIL -> RETRY`: right action must take another photo attempt instead
  of leaving the user on a dead end.
- Confirm every screen keeps the three touch zones readable and no text covers
  the footer buttons.

## Two-Board Battle Flow

- Prepare both boards with an active pet.
- Enter MATCH on both boards and confirm the neutral player states progress
  through finding, pairing, and ready without showing HOST, CLIENT, UDP, TX, RX,
  or packet counters.
- Confirm the MATCH middle zone exits to idle while scanning or waiting.
- Confirm MATCH displays recent opponent, latest result, friendship stage, and
  rematch XP preview after at least one battle.
- Confirm `MATCH -> BATTLE ENTRY -> CLASH -> RESULT -> EXIT`:
  - BATTLE ENTRY shows local and opponent pet entry.
  - CLASH shows the three-round process and round-advance feedback.
  - RESULT shows win/draw/loss, score delta, battle ID, XP, growth, and
    friendship/rematch reward.
  - EXIT shows cleanup actions for BAG, IDLE, PHOTO, and the next rematch hint.
- Confirm both boards show compatible result direction: one win should mirror
  one loss, and draw should be draw on both.

## SD Audio Payload

- With no SD card, confirm scene sounds still play or remain muted according to
  the firmware mute state.
- With SD card inserted, confirm `/api/v1/status` reports `sdCardPresent=true`.
- Trigger the complete cue list from serial:
  `SOUND idle|photo|wild|bag|capture|release|select|match|clash|friend|win|draw|lose|warning|intro|level|exit|cancel`.
- Trigger the same common cues from the `/app` soundboard.
- Toggle mute and confirm the same actions respect `kAudioMuted`; no cue should
  bypass `play_scene_sound`.
- Remove or rename one SD `.raw` file and confirm the firmware falls back to
  built-in sound instead of blocking the UI.
- Optional serial upload path: run
  `E:\Anaconda\python.exe .\scripts\upload-sd-audio.py --port COM7 --payload .\sd_card_payload`,
  then confirm the final `SDINFO` output lists the uploaded `/audio/.../*.raw`
  files.

## SD Sample Export

- With no SD card, `GET /api/v1/samples` must return `ok:true` and
  `sdCardPresent:false`.
- With SD card inserted and after at least one PHOTO attempt, confirm
  `/samples/manifest.csv` exists.
- `GET /api/v1/samples` must report class counts, scene counts, and recent
  sample rows without blocking the device UI.
- `GET /api/v1/samples/manifest` must download the manifest CSV.
- `GET /api/v1/samples/file?path=/samples/...` must serve only `.csv` or
  `.ppm` files under `/samples/`; attempts outside `/samples/` must fail.

## App and Serial Control

- `/app` home must show current screen label, current partner, backpack count,
  next actions, friendship/rematch summary, and mute state.
- `/app` capture, bag, battle, and friends pages must mirror the same next
  actions available on the device screen; the capture page also shows the
  sample-debug panel.
- Serial `STATUS` must report screen, active pet growth, friendship, recent
  battle result, button labels, and current sampling mode.
- Serial `SAMPLE on negative white_wall`, `SAMPLE label plant_leaf`,
  `SAMPLE scene dark`, `SAMPLE status`, and `SAMPLE off` must update or report
  the same RAM-only sampling state shown in `/app`, without changing recognition
  gates or stored pets.
- Serial `ACT friend` must add the current or most recent opponent to the
  local persisted friend list and show on-device feedback.
- Serial guarded actions must reject invalid state transitions with a readable
  error instead of changing screen unexpectedly.

## Pass Criteria

- All listed touch flows are completed on at least one board.
- The full two-board battle flow is completed at least three times: win, loss,
  and draw or near-draw.
- The same recent opponent can be added as a local friend, rematched, and shown
  with increasing friendship/rematch feedback.
- App and serial control paths match the guarded device actions and do not
  bypass release confirmation.
- SD audio is optional: both no-SD and SD-card runs remain playable.
- SD sample export is optional and read-only: no SD card still leaves capture,
  backpack, battle, and phone control usable.
- No voice-recognition prompt, cloud API key, mandatory router, or mandatory SD
  card dependency appears during validation.
