# HuskyLens Optional Module Experiment

Purpose: test HuskyLens as an optional recognition coprocessor. The CoreS3
project must still run locally without this module.

This is an optional experiment path. The main firmware remains local/offline and
can consume a one-shot serial hint for bench testing without adding the
HuskyLens Arduino library as a dependency.

## Wiring To Verify

- Power: confirm voltage and current budget before connecting.
- Data path: choose I2C or UART for the experiment and document the setting.
- Ground: shared ground between CoreS3 and module.
- Mount: keep the external camera aligned with the object area used by CoreS3.

## Mapping Contract

- HuskyLens learned IDs map through `id-map.csv`.
- Unknown IDs must not generate pets.
- Timeout or module missing must fall back to the existing CoreS3 local
  recognition path.
- Do not store bbox, raw module IDs, or confidence metadata in public storage
  until an interface-change proposal is accepted.

## Desktop Bridge

`huskylens_bridge.py` is a no-dependency desktop mapping harness. It validates
`id-map.csv`, maps a learned ID to the existing 8-class contract, and reports
whether local fallback must be used.

```powershell
python .\experiments\huskylens\huskylens_bridge.py --id 1 --confidence 90 --presence 90 --force-disabled
```

The serial hint line is documented in `serial-hint-protocol.md`. Production
firmware accepts it as a RAM-only one-shot hint for the next capture, then still
uses the current CoreS3 camera frame presence/background gates before any pet can
be generated.

## Acceptance Gate

- Boot without HuskyLens attached.
- Boot with HuskyLens attached.
- Log timeout, mapped class, confidence hint, and fallback reason.
- Confirm white wall, white paper, and desktop remain negative unless a trained
  object ID is deliberately returned.
- Run `python .\experiments\huskylens\huskylens_bridge.py --self-test` before
  using the ID map in an experiment branch.
