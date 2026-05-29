# v0.1 Release Notes

Project name: `xhs_ai_builder`

Release date: 2026-05-29

## Included

- CoreS3 local offline pet game sketch.
- ESP-SR offline command word recognition path.
- Camera-triggered local pet generation.
- Five-element visual pet system.
- Backpack capture/select/release flow.
- Release confirmation flow.
- Growth and battle stat records.
- Wi-Fi AP + UDP battle transport.
- Startup and scene sound effects.
- Build, flash, and official firmware helper scripts.
- Official firmware manifest and downloaded reference binaries.

## Known Limitations

- Pet recognition is still lightweight local trait extraction, not real object classification.
- Network, SD card, and large model recognition interfaces are reserved but not active.
- Two-board battle requires both boards to enter MATCH and stay close enough for Wi-Fi AP connection.
- The startup audio is embedded in a generated header and increases sketch source size.
- The repository is a development snapshot; memory, storage, and UX still need product-level tightening.

## v0.1 Test Checklist

- Compile main sketch with ESP-SR.
- Flash the same firmware to two CoreS3 boards.
- Confirm idle screen loads after startup sound.
- Say `pai zhao` to generate a wild pet.
- Capture a wild pet into the backpack.
- Select a pet from the backpack.
- Enter MATCH on both boards.
- Confirm one board shows HOST and the other shows CLIENT.
- Confirm RX increases on both boards and a battle result appears.

