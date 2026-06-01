# OSHWHub / OSHWHLab Release Kit

This folder collects publication material for OSHWHub, OSHWHLab, Hackaday, and
similar open hardware project pages.

The release kit is not complete until every required field below has evidence.

## Required Artifacts

- `bom.csv`: hardware list and source notes.
- `verification-log-template.csv`: build, flash, `/app`, SD card, capture, and
  battle acceptance records.
- `video-shot-list.md`: short public demo script.
- `hf-dataset-card-template.md`: dataset card for exported CoreS3 samples.
- Photos or renders of the assembled kit, to be added later.
- Optional enclosure or stand files, to be added later.

## Publish Rules

- Do not include private photos or unlicensed assets.
- Do not include API keys, cloud endpoints, or private Wi-Fi credentials.
- Mark dual-board battle verification as pending unless two boards were tested.
- State that CoreS3 local/offline capture still works without cloud services.

## Readiness Check

Run the advisory check before preparing a public post:

```powershell
python ..\..\scripts\check-open-release-readiness.py
```

Use `--strict` only for a final publication gate. The strict mode exits
non-zero while photos, filled verification logs, or publishable dataset files
are still pending.

## Dataset Card Draft

Generate a draft Hugging Face dataset card from the local sample manifest and
training report:

```powershell
python ..\..\scripts\build-hf-dataset-card.py --samples-root C:\tmp\m5_vision_samples
```

The generated `hf-dataset-card.md` remains a draft until license and privacy
review are complete. The readiness check keeps draft cards as `PENDING`.

## Scene Coverage Audit

Check whether the local sample set includes the required negative scenes:

```powershell
python ..\..\scripts\audit_vision_scene_coverage.py --samples-root C:\tmp\m5_vision_samples
```

The default gate expects at least 20 negative samples each for `white_wall`,
`white_paper`, `desktop`, `glare`, and `dark`. Use `--strict` only when preparing
the final dataset package.
