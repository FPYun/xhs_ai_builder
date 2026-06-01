# Delivery Notes 2026-06-01

This handoff keeps the CoreS3 firmware local/offline and preserves the public
headers, backpack format, and UDP battle packet.

## Verified Build

```text
.\scripts\compile-demo.ps1 -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino -BuildRoot C:\tmp\m5_arduino_build_full_flash_ready
Sketch uses 1607857 bytes (51%) of program storage space.
Global variables use 69268 bytes (21%) of dynamic memory.
```

## Flash Readiness Inputs

- Firmware sketch: `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`
- On-device app checks: `/app`, `/api/v1/status`, `/api/v1/storage`,
  `/api/v1/sampling`, `/api/v1/samples`
- Serial acceptance checks: `STATUS`, `ACCEPTANCE`, `BAGSTATUS`,
  `SAMPLE status`, `SDINFO`
- Desktop bridge page: `http://127.0.0.1:8790/`
- Phone bridge page: `http://<computer-lan-ip>:8791/`
- Optional SD payload: `sd_card_payload/` and `sd_card_payload.zip`

## Release Checks

```text
python .\scripts\audit_hf_dataset_publishability.py --samples-root C:\tmp\m5_vision_samples
hf dataset publishability: INCOMPLETE
```

```text
python .\scripts\check-open-release-readiness.py
open release readiness: INCOMPLETE
```

Resolved for this handoff:

- Sample `manifest.csv` now includes the required `scene` column.
- `report.json` includes `scene_sample_counts` and `scene_eval`.
- `hf_dataset_publishability` no longer fails on manifest schema.
- `hf-dataset-manifest.csv` now provides sanitized relative publish paths.
- SD payload validation passes.

Still pending before public upload:

- Copy reviewed image files to the sanitized publish paths before upload.
- Review public supplemental sample licenses.
- Improve `data_quality=weak` with real CoreS3 samples.
- Add enough negative samples for `white_paper` and `glare`.
- Keep the Hugging Face dataset card as draft until license/privacy review is complete.
- Add filled hardware verification logs and public device photos.
