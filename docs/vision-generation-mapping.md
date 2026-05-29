# Capture Vision To Pet Generation

## Scope

This note documents the current offline CoreS3 capture path:

`PHOTO -> frame -> preprocess_frame_for_vision -> analyze_frame -> detect_subject_presence -> classify_object_local / run_embedded_vision_model -> recognize_object_local -> draw_wild_pet or draw_capture_fail`

Failed recognition must not create a wild pet. Only a recognized result with sufficient confidence can call `derive_pet_genes`.

## Object Class Mapping

| ObjectClass | Label | Element hint | Species bias | Pet template tendency |
| --- | --- | --- | --- | --- |
| `kObjectPlantLeaf` | `plant_leaf` | Wood | 0 | Leaf / antler / vertical-line body |
| `kObjectFoodFruit` | `food_fruit` | Wood or Earth | 1 | Vine / fruit-heavy or stone-backed body |
| `kObjectPaperBook` | `paper_book` | Wood or Earth | 2 | Bud / shell / blocky low-center body |
| `kObjectElectronicsScreen` | `electronics_screen` | Metal or Water | 1 | Ringed armor or flowing screen-glow body |
| `kObjectMetalKeyCoin` | `metal_key_coin` | Metal | 2 | Angular ears / symmetric highlights / ring motif |
| `kObjectFabricCloth` | `fabric_cloth` | Earth or Wood | 0 | Heavy round body or soft leaf-line body |
| `kObjectCupBottleWater` | `cup_bottle_water` | Water | 2 | Fins / water-drop tail / wave motif |
| `kObjectToyFigure` | `toy_figure` | Fire or Metal | 1 | Triangle/flame silhouette or armored toy body |

The element may still use image traits where the requirement allows a two-element class. The species bias is fixed by class to reduce random-feeling generation.

## Generation Chain

`ImageTraits` contributes:

- brightness: mood, eye style, seed, color tint
- saturation: body scale, tail style, pattern density, seed
- contrast: body scale, horn style, aura pattern, pattern density, color tint
- center/edge difference: body scale, horn style, tail style, aura pattern, pattern density, seed
- frame seed: deterministic variation from sampled pixels

`RecognitionResult` contributes:

- classId: fixed species bias, style variants, seed
- confidence: eye style and seed
- elementHint: final element unless a future valid `PetHint` overrides it

Time window and `shot_count` contribute only to secondary appearance variation. They no longer change the class-to-species bias.

## Failure Gate

Recognition fails when:

- preprocessing fails
- no frame is available
- the frame is too dark
- the scene is too bright and low contrast
- the scene is flat or lacks a centered subject
- model or fallback confidence is below `kMinRecognitionConfidence`

Failure path:

`draw_capture_fail` sets `has_wild_pet = false`, clears `wild_pet`, stores diagnostics, and enters `kScreenCaptureFail`.

Capture path:

`capture_wild_pet` rejects any stale or low-confidence `wild_recognition` before writing to the backpack.

## Growth And Battle Handoff

- Capture success gives the generated `PetGenes` to `SavedPet`, so backpack,
  growth, and battle all consume the same recognized-class result.
- Battle stats should continue to derive from `bodyScale`, `hornStyle`,
  `tailStyle`, `auraPattern`, `patternDensity`, `mood`, `species`, and `seed`.
  This lets recognized class choose the broad pet family while image traits tune
  combat flavor through existing genes.
- Growth should adjust existing gene values such as body scale, pattern density,
  and horn style. Do not add rarity, skill, pet ID, or friendship fields for
  module 2 without an architecture-level public interface change.
- UI and battle modules can display `RecognitionResult::objectLabel`,
  confidence, element, and species name. They do not need new public fields for
  the current module 2 handoff.

## Optimization Suggestions

- Keep the current 8-class feature model as the offline fallback; tune thresholds from real CoreS3 samples before changing model structure.
- Add an SD-card sample capture mode later through `save_pet_snapshot`, but keep it disabled by default to preserve offline play.
- Use `PetHint` and `fetch_remote_pet_hint` as the later cloud or Edge Impulse/TFLite Micro integration point; do not add network dependencies in the current firmware.
- When real samples exist, retrain `vision_model_data.h` with the same 10-feature schema first. Expand public structs only if those samples prove that features such as bounding box or texture density are required.

## Public Interface Change

No public interface change is required for the current task.

Do not modify `pet_model.h`, `vision_types.h`, `ui_types.h`, `battle_protocol.h`, or `BattlePetPacket` for this stage. If later recognition needs bounding boxes, class logits, or sample metadata, submit an interface change proposal to the architecture module before editing shared headers.
