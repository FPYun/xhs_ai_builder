# HuskyLens Serial Hint Protocol

This is an experiment boundary for an external recognition module. The
production firmware accepts this line as a RAM-only one-shot bench hint for the
next capture.

## Goal

Translate a HuskyLens learned object ID into the existing 8-class recognition
contract without changing public headers, backpack storage, or UDP packets.

## Desktop Bridge

Use `huskylens_bridge.py` to validate ID mapping before any firmware branch is
created:

```powershell
python .\experiments\huskylens\huskylens_bridge.py --id 1 --confidence 90 --presence 90 --force-disabled
```

The tool reads `id-map.csv`. Disabled rows intentionally fall back unless
`--force-disabled` is used for bench simulation.

## Proposed Future Serial Line

```text
HUSKY_HINT <class> <confidence> <presence>
```

Example:

```text
HUSKY_HINT plant_leaf 90 90
```

Rules:

- `<class>` must be one of the existing 8 object class labels.
- `confidence` and `presence` are hints only.
- A missing module, timeout, unmapped ID, disabled ID, or low confidence must
  fall back to the current CoreS3 local recognizer.
- The hint must still pass the existing `RecognitionResult` failure gates before
  pet generation is allowed.
- Firmware requires confidence `>= 70`, presence `>= 55`, and a live CoreS3
  subject-presence pass. Hints expire after 10 seconds or after one photo burst.
- Do not persist HuskyLens IDs, bbox, logits, or module metadata without an
  interface-change proposal.
