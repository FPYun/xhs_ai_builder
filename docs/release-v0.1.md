# v0.1 Release Notes

Project name: `xhs_ai_builder`

Release date: 2026-05-29

## Included

- CoreS3 local offline pet game sketch.
- Camera-triggered subject detection and local pet generation.
- Five-element visual pet system.
- Backpack capture/select/release flow.
- Release confirmation flow.
- Growth, XP, win rate, and battle stat records.
- Wi-Fi AP + UDP battle transport.
- BATTLE clash/result flow with muted-capable scene sound effects.
- Local RAM-only rematch friendship XP bonus.
- Public type split for pet, vision, UI, and battle protocol boundaries.
- Productization planning docs for App, cloud, SD card, dataset, and community directions.
- Player flow, UI, and reward design notes in `docs/player-flow-ui.md`.
- Build, flash, and official firmware helper scripts.
- Official firmware manifest and downloaded reference binaries.

## Known Limitations

- Pet recognition uses a first-stage trained softmax feature classifier with heuristic fallback, not a CNN yet.
- The current recognizer is lightweight limited-class local recognition, not a general vision large model.
- Voice recognition is not implemented.
- Network, SD card, App, cloud, LLM, and community interfaces are reserved but not active.
- No cloud API, external network, image upload, or SD card is required for the current firmware.
- Two-board battle requires both boards to enter MATCH and stay close enough for Wi-Fi AP connection.
- Rematch friendship state is not persistent in v0.1.
- The startup audio is embedded in a generated header and increases sketch source size.
- The repository is a development snapshot; memory, storage, and UX still need product-level tightening.

## v0.1 Test Checklist

- Compile main sketch.
- Flash the same firmware to two CoreS3 boards.
- Confirm idle screen loads after startup sound.
- Tap PHOTO to recognize a subject and generate a wild pet only on success.
- Confirm blank/flat/dark frames enter CAPTURE FAILED and cannot be captured.
- Capture a wild pet into the backpack.
- Select a pet from the backpack.
- Enter MATCH on both boards.
- Confirm player UI uses neutral MATCH states, not HOST/CLIENT role labels.
- Confirm BATTLE shows a clash phase and then a result appears.
- For engineering diagnosis only, confirm serial logs still report role and packet counters.
- Confirm BAG shows level, XP, element, active/stored status, win count, battle count, and win rate.
