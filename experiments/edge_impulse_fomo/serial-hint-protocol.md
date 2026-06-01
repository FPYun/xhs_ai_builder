# Edge Impulse / FOMO Serial Hint Protocol

This document defines the experiment boundary for adapting Edge Impulse or FOMO
output to the existing capture pipeline. The production firmware accepts this
line as a RAM-only one-shot bench hint for the next capture.

## Desktop Adapter

Use `edge_output_adapter.py` to normalize exported model output:

```powershell
python .\experiments\edge_impulse_fomo\edge_output_adapter.py --input .\experiments\edge_impulse_fomo\sample-classification.json
```

The adapter accepts either classification JSON or FOMO-style `bounding_boxes`
JSON and reduces it to the existing 8-class contract.

## Proposed Future Serial Line

```text
EDGE_HINT <class> <confidence> <presence>
```

Example:

```text
EDGE_HINT plant_leaf 82 82
```

Rules:

- `<class>` must be one of the existing 8 object class labels.
- `negative` or low-confidence output must fall back to the current local
  recognizer.
- The hint must still pass current `RecognitionResult` gates before pet
  generation is allowed.
- Firmware requires confidence `>= 70`, presence `>= 55`, and a live CoreS3
  subject-presence pass. Hints expire after 10 seconds or after one photo burst.
- Do not persist logits, heatmaps, bbox data, model name, or raw scores without
  an interface-change proposal.
