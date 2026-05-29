#pragma once

#include "pet_model.h"

enum ObjectClass : uint8_t {
    kObjectUnknown = 0,
    kObjectPlantLeaf,
    kObjectFoodFruit,
    kObjectPaperBook,
    kObjectElectronicsScreen,
    kObjectMetalKeyCoin,
    kObjectFabricCloth,
    kObjectCupBottleWater,
    kObjectToyFigure,
};

struct ImageTraits {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int32_t brightness;
    int32_t saturation;
    int32_t contrast;
    int32_t confidence;
    int32_t centerBrightness;
    int32_t edgeBrightness;
    int32_t centerSaturation;
    int32_t edgeSaturation;
    int32_t centerDelta;
    int32_t darkRatio;
    int32_t brightRatio;
    int32_t frameWidth;
    int32_t frameHeight;
    ElementType element;
    uint32_t seed;
};

struct RecognitionResult {
    bool recognized;
    uint8_t classId;
    const char* objectLabel;
    const char* materialLabel;
    const char* failureReason;
    uint8_t confidence;
    uint8_t presenceScore;
    ElementType elementHint;
    uint8_t speciesBias;
};

struct SubjectPresence {
    bool present;
    uint8_t score;
    const char* reason;
};

struct PetHint {
    bool valid;
    ElementType preferredElement;
    uint8_t preferredSpecies;
    uint8_t styleBias;
};
