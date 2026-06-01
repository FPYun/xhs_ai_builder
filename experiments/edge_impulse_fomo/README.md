# Edge Impulse / FOMO Experiment

Purpose: evaluate whether a tiny image classifier or FOMO-style model improves
the current 8-class CoreS3 recognition without running YOLO, CLIP, or a large
model on the device.

This is an optional experiment path. The main firmware remains local/offline and
can consume a one-shot serial hint for bench testing without adding Edge
Impulse as a dependency.

## Inputs

- CoreS3 SD samples under `C:\tmp\m5_vision_samples\cores3\`.
- Optional public samples under `C:\tmp\m5_vision_samples\public\`.
- Label map: see `label-map.csv`.
- Negative scenes: white wall, white paper, desktop, glare, dark, bright,
  far object, hand occlusion.

## Output Contract

Any experiment adapter must reduce model output to:

- `classId`: one of the existing 8 object classes.
- `confidence`: 0..100.
- `presence`: subject-present score, 0..100.
- `failureReason`: local diagnostic string.

Do not add bbox, logits, or model metadata to public headers.

## Desktop Adapter

`edge_output_adapter.py` is a no-dependency desktop adapter for exported Edge
Impulse output. It accepts classification JSON or FOMO-style `bounding_boxes`
JSON and normalizes the result to the output contract above.

```powershell
python .\experiments\edge_impulse_fomo\edge_output_adapter.py --input .\experiments\edge_impulse_fomo\sample-classification.json
python .\experiments\edge_impulse_fomo\edge_output_adapter.py --input .\experiments\edge_impulse_fomo\sample-fomo-boxes.json
```

The serial hint line is documented in `serial-hint-protocol.md`. Production
firmware accepts it as a RAM-only one-shot hint for the next capture, then still
uses the current camera frame presence/background gates before any pet can be
generated.

## Acceptance Gate

- Compile the experiment independently.
- Log model size, free heap, and per-frame inference time.
- Compare against the current prototype model on the same CoreS3 real samples.
- Negative-scene false positives must be lower than the current firmware.
- If these checks fail, keep the current mainline recognizer unchanged.
- Run `python .\experiments\edge_impulse_fomo\edge_output_adapter.py --self-test`
  before using an exported model's labels.

## Notes

FOMO is useful because it can provide coarse locality on constrained devices,
but it still needs a separate RAM/latency check on CoreS3.
