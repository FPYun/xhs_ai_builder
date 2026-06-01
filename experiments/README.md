# Experiments

This folder holds optional recognition and hardware experiments that must stay
outside the production Arduino sketch until they pass their own acceptance
checks.

Rules:

- Do not include these folders from `04_camera_pet_battle.ino`.
- Do not modify `PetGenes`, `RecognitionResult`, `BackpackStorage`, or
  `BattlePetPacket` for an experiment.
- Do not add runtime cloud dependencies or hard-coded API keys.
- Each experiment must prove compile result, heap, latency, class accuracy, and
  negative-scene behavior before any mainline integration proposal.

Current experiment stubs:

- `edge_impulse_fomo/`: TinyML image/FOMO experiment plan.
- `huskylens/`: Optional external AI camera module plan.
- `esp_idf_vision_poc/`: ESP-IDF + ESP-DL/ESP-WHO proof-of-concept plan.
