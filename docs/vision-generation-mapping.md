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

## Compatible Visual Variants

`PetGenes::species` remains the protocol and storage identity and is still limited
to `0..2`. The richer pet library is implemented as an internal visual variant:

`pet_visual_variant = hash(seed, mood, eyeStyle, hornStyle, tailStyle, auraPattern, patternDensity) % 3`

This variant is not stored as a new field and is not sent in `BattlePetPacket`.
Local screens and App JSON can show `variant_pet_name`, while remote battle
packets continue to expose only the base species index/name.

| Element | Species | Variant 0 | Variant 1 | Variant 2 |
| --- | --- | --- | --- | --- |
| Wood | Leaf Deer | Leaf Deer | Sprout Deer | Antler Deer |
| Wood | Vine Fox | Vine Fox | Moss Fox | Tendril Fox |
| Wood | Bud Turtle | Bud Turtle | Seed Turtle | Canopy Turtle |
| Fire | Flame Cat | Flame Cat | Ember Cat | Cinder Cat |
| Fire | Firebird | Firebird | Spark Bird | Halo Bird |
| Fire | Lava Hound | Lava Hound | Coal Hound | Torch Hound |
| Earth | Mountain Bear | Mountain Bear | Pebble Bear | Mossy Bear |
| Earth | Rock Turtle | Rock Turtle | Ridge Turtle | Boulder Turtle |
| Earth | Clay Beast | Clay Beast | Mud Beast | Totem Beast |
| Metal | Silver Wolf | Silver Wolf | Chrome Wolf | Mirror Wolf |
| Metal | Mecha Rabbit | Mecha Rabbit | Gear Rabbit | Bolt Rabbit |
| Metal | Bronze Tiger | Bronze Tiger | Ring Tiger | Prism Tiger |
| Water | Wave Otter | Wave Otter | Ripple Otter | Pearl Otter |
| Water | Water Dragon | Water Dragon | Stream Dragon | Rain Dragon |
| Water | Bubble Fish | Bubble Fish | Drop Fish | Glide Fish |

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

Time window and `shot_count` contribute only to secondary appearance variation,
including visual variants and small body/detail differences. They no longer
change the recognized class, element set, or base species bias.

## Sample-Trained Prototype Model

The current desktop training route is:

`sample folders -> manifest.csv -> 64x64 feature extraction -> class prototypes -> vision_model_data.h -> firmware compile/upload`

Sample images stay outside the repository under `C:\tmp\m5_vision_samples`.
The trainer writes only compact constants into `vision_model_data.h`; it does
not embed image files, API keys, cloud dependencies, or new public types.

Firmware feature order matches `scripts/train_vision_feature_model.py`:

1. red
2. green
3. blue
4. brightness
5. saturation
6. contrast
7. center brightness
8. edge brightness
9. center saturation
10. edge saturation
11. center/edge brightness delta
12. dark ratio
13. bright ratio
14. red dominance
15. green dominance
16. blue dominance
17. warm score
18. low saturation score

`run_embedded_vision_model` uses weighted prototype distance, the second-best
distance margin, subject presence, and the existing minimum confidence gate.
If the model is marked weak by the training report, firmware applies stricter
distance, margin, and confidence checks before accepting a model result. The
rule classifier remains a local fallback and still must pass the same confidence
gate plus class-specific evidence checks.

The fallback path is intentionally conservative while the prototype model is
weak. Generic low-saturation indoor frames must not be enough to classify as
`metal_key_coin`; metal now needs additional evidence such as contrast,
highlight ratio, high brightness, or clear center/edge difference. Serial logs
include `src=model`, `src=rule`, `src=negative`, or `src=presence` so field
testing can separate prototype errors from fallback-rule errors.

The element mapper is also conservative about Metal. Dual-element classes no
longer default to Metal:

- `electronics_screen` returns Water unless the frame has strong metal-like
  visual evidence.
- `toy_figure` returns Fire unless the frame has strong metal-like visual
  evidence.
- `metal_key_coin` remains the only always-Metal object class.

This keeps the public `ObjectClass -> allowed element set` mapping unchanged
while reducing accidental Metal pets from gray indoor lighting.

Blank low-saturation frames are rejected before classification. Plain walls,
empty desktops, and blank paper should hit `Blank scene` unless there is a
clear centered object or strong text/edge contrast. Water is also evidence-gated:
`cup_bottle_water` and the Water branch of `electronics_screen` require blue
dominance, dark-screen evidence, or clear highlight/center structure instead of
accepting neutral gray/white frames.

For field tuning, serial recognition logs include the raw trait summary:
`rgb`, brightness, saturation, contrast, center delta, dark ratio, and bright
ratio. Use those values to adjust blank-scene thresholds from real CoreS3
captures rather than guessing from desktop samples.

The 2026-05-31 desktop training run wrote
`C:\tmp\m5_vision_samples\report.json` and produced a weak model because public
sample coverage is sparse and there are no real `negative` samples yet:

| Class | Sample count |
| --- | ---: |
| `plant_leaf` | 6 |
| `food_fruit` | 20 |
| `paper_book` | 20 |
| `electronics_screen` | 20 |
| `metal_key_coin` | 6 |
| `fabric_cloth` | 9 |
| `cup_bottle_water` | 20 |
| `toy_figure` | 20 |
| `negative` | 0 |

Validation accuracy from this noisy public-only pass is `0.038`, so the model
is written for integration testing rather than accepted accuracy improvement.
The next useful training step is to collect CoreS3 images and negative samples,
then retrain without changing public headers.

The 2026-05-31 online-sample expansion used COCO validation images plus Open
Images validation metadata to fill every class to at least 80 samples:

| Class | Sample count |
| --- | ---: |
| `plant_leaf` | 80 |
| `food_fruit` | 84 |
| `paper_book` | 80 |
| `electronics_screen` | 80 |
| `metal_key_coin` | 80 |
| `fabric_cloth` | 80 |
| `cup_bottle_water` | 80 |
| `toy_figure` | 80 |
| `negative` | 80 |

The exported report still marks the model weak because validation accuracy is
`0.070` and the negative false-positive rate is `0.188`. Public web images
improve coverage, but they do not match the CoreS3 camera distribution closely
enough to replace real device samples.

The later 2026-05-31 enrichment pass added a reusable Open Images validation
downloader and a bbox-crop downloader to `scripts/train_vision_feature_model.py`.
The bbox path stores cropped target objects as public training images so they
better match the CoreS3 "centered object" capture flow than full scene images.
The final sample counts used for export were:

| Class | Sample count |
| --- | ---: |
| `plant_leaf` | 220 |
| `food_fruit` | 220 |
| `paper_book` | 220 |
| `electronics_screen` | 220 |
| `metal_key_coin` | 211 |
| `fabric_cloth` | 220 |
| `cup_bottle_water` | 220 |
| `toy_figure` | 220 |
| `negative` | 160 |

The exported report still marks this model weak: validation accuracy is `0.071`
and the negative false-positive rate is `0.188`. This model is compiled and
uploaded as a stricter weak-model baseline, not as accepted accuracy closure.
The next accuracy step must be CoreS3-shot samples under the actual desk, wall,
paper, bottle, screen, toy, fabric, fruit, key/coin, and leaf lighting
conditions.

The 2026-06-01 enrichment pass added generated hard negatives for white wall,
white paper, tabletop, low-texture, too-bright, and too-dark scenes. It also
continued Open Images bbox crop expansion. Final export counts:

| Class | Sample count |
| --- | ---: |
| `plant_leaf` | 260 |
| `food_fruit` | 260 |
| `paper_book` | 246 |
| `electronics_screen` | 260 |
| `metal_key_coin` | 211 |
| `fabric_cloth` | 260 |
| `cup_bottle_water` | 260 |
| `toy_figure` | 260 |
| `negative` | 400 |

Final report remains weak: validation accuracy is `0.089`, negative
false-positive rate is `0.150`, and `kVisionModelQualityWeak` remains enabled.
This pass is mainly useful for making blank/flat scenes less attractive to the
classifier while preserving strict weak-model gates.

Pet library display was also enriched without changing public interfaces. The
firmware still stores and transmits only `species 0..2`; the visual variant is
derived from existing `PetGenes`. Variant names and drawing signatures now make
the 45 compatible templates easier to distinguish:

- Wood variants emphasize sprouts, vine loops, leaf marks, and vertical growth
  lines.
- Fire variants emphasize flame crowns, small wings, and tail flame clusters.
- Earth variants emphasize stone brows, mountain ridges, shell plates, and
  low-center markings.
- Metal variants emphasize rivets, gear rings, angular ears, and mirror
  highlights.
- Water variants emphasize fin crests, wave bands, droplet tails, and bubble
  chains.

## Capture Assist And Sample Loop

The 2026-06-01 capture assist path keeps the public recognition interfaces
unchanged, but changes the photo entry behavior:

`take_photo -> 3-frame burst -> per-frame quality/presence/proximity check -> choose best candidate -> existing RecognitionResult gate`

Each burst frame records:

- `ImageTraits` brightness, saturation, contrast, center/edge difference, dark
  ratio, and bright ratio.
- `SubjectPresence` score and reason.
- recognition confidence, class, source, distance, and margin.
- best-effort LTR-553 proximity value when the sensor is available.
- a compact SD thumbnail plus `manifest.csv` row when an SD card is present.

The background similarity penalty is applied before accepting a frame. Low
saturation, low contrast, and weak center/edge difference now count as a flat
background even if the prototype model or fallback rule finds a nearby class.
This specifically targets white wall, blank paper, and empty desktop failures.

Quality hints are display-only diagnostics such as `Too dark`, `Flat background`,
`Center object`, `Move closer`, and `Add contrast`. They do not change
`ObjectClass`, `PetGenes`, backpack storage, or battle packets.

Serial sample lines start with `sample ...`; the final chosen frame logs
`vision burst=... quality=... prox=... hint=...`. These logs are the input for
the next CoreS3-shot training pass. The recommended field loop is:

1. Put an SD card in the CoreS3.
2. Capture negatives first: wall, blank paper, desktop, dark, bright, glare,
   hand cover, and distant object.
3. Capture positives by class, at least 50 usable shots each.
4. Copy `/samples/manifest.csv` and thumbnails into `C:\tmp\m5_vision_samples`.
5. Retrain with `scripts/train_vision_feature_model.py` and keep
   `kVisionModelQualityWeak=true` until validation and real-device tests improve.

## Failure Gate

Recognition fails when:

- preprocessing fails
- no frame is available
- the frame is too dark
- the scene is too bright and low contrast
- the scene is flat or lacks a centered subject
- burst selection finds only invalid frames
- background similarity penalty rejects a candidate
- model or fallback confidence is below `kMinRecognitionConfidence`

Failure path:

`draw_capture_fail` sets `has_wild_pet = false`, clears `wild_pet`, stores diagnostics, and enters `kScreenCaptureFail`.

Capture path:

`capture_wild_pet` rejects any stale or low-confidence `wild_recognition` before writing to the backpack.

## Growth And Battle Handoff

- Capture success gives the generated `PetGenes` to `SavedPet`, so backpack,
  growth, and battle all consume the same recognized-class result.
- Local capture, backpack, and App status surfaces use the visual variant name.
  Remote battle displays remain compatible by using the base species carried in
  `BattlePetPacket`.
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
- When real samples exist, retrain `vision_model_data.h` with the same
  18-feature schema first. Expand public structs only if those samples prove
  that features such as bounding box or texture density are required.

## Public Interface Change

No public interface change is required for the current task.

Do not modify `pet_model.h`, `vision_types.h`, `ui_types.h`, `battle_protocol.h`, or `BattlePetPacket` for this stage. The compatible visual variant is derived from existing `PetGenes` fields. If later work needs persistent species IDs beyond `0..2`, bounding boxes, class logits, or sample metadata, submit an interface change proposal to the architecture module before editing shared headers.
