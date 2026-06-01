# CoreS3 SD Card Payload

Copy the contents of this folder to the SD card root.

Expected SD layout:

```text
/audio/ui/idle.raw
/audio/ui/photo.raw
/audio/ui/wild.raw
/audio/ui/bag.raw
/audio/ui/capture.raw
/audio/ui/release.raw
/audio/ui/select.raw
/audio/ui/warning.raw
/audio/ui/level_up.raw
/audio/ui/cancel.raw
/audio/battle/match.raw
/audio/battle/clash.raw
/audio/battle/friend.raw
/audio/battle/win.raw
/audio/battle/draw.raw
/audio/battle/lose.raw
/audio/battle/exit.raw
/audio/music/intro.raw
/skins/manifest.csv
/skins/palettes/default.csv
/skins/palettes/wood.csv
/skins/palettes/fire.csv
/skins/palettes/earth.csv
/skins/palettes/metal.csv
/skins/palettes/water.csv
/actions/manifest.csv
/actions/idle.csv
/actions/wild.csv
/actions/bag.csv
/actions/battle_clash.csv
/actions/result.csv
```

Format:

- unsigned 8-bit mono PCM
- 22050 Hz
- `.raw` payload only, no WAV/MP3 header
- each file is below the firmware limit of 32768 bytes

Current firmware reads audio files as optional external sound assets. If a file
is missing or too large, the firmware falls back to built-in sound effects.

`/skins/manifest.csv` lists optional palette files. Each palette CSV uses:

```csv
key,r,g,b
body,72,168,86
accent,184,235,118
```

When `/skins/palettes/<element>.csv` exists, the firmware uses `body` and
`accent` to tint local pet drawing for that element. Missing or invalid palette
files fall back to built-in colors.

`/actions/manifest.csv` lists optional pet action profiles. Each action CSV
uses:

```csv
key,value
bob,1
sparkle,0
tilt,0
```

Supported keys are `bob`, `sparkle`, and `tilt`. When an action profile exists,
the firmware applies small local drawing changes on the matching screen. Missing
or invalid action files fall back to built-in static drawing.

`manifest.csv` records the expected SD paths and sizes. It is for packaging and
verification only; the firmware does not require it to play audio.

Validate the payload before copying or publishing:

```powershell
python .\scripts\validate-sd-payload.py --payload .\sd_card_payload
```

The validator checks root manifest sizes, audio format and size limits, skin
manifest version fields, palette RGB rows, action profile version fields, and
firmware-compatible fallback ranges for `bob`, `sparkle`, and `tilt`.

Serial upload option:

```powershell
E:\Anaconda\python.exe .\scripts\upload-sd-audio.py --port COM7 --payload .\sd_card_payload
```

The script uses the firmware `SDPUT` command and then requests `SDINFO`.
