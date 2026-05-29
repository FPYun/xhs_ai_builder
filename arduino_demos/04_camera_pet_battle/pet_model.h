#pragma once

#include <Arduino.h>

static constexpr uint8_t kMaxBackpackPets = 6;

enum ElementType : uint8_t {
    kWood = 0,
    kFire,
    kEarth,
    kMetal,
    kWater,
};

struct PetGenes {
    ElementType element;
    uint8_t species;
    uint8_t mood;
    uint8_t bodyScale;
    uint8_t eyeStyle;
    uint8_t hornStyle;
    uint8_t tailStyle;
    uint8_t auraPattern;
    uint8_t patternDensity;
    uint16_t accentColor;
    uint32_t seed;
};

struct SavedPet {
    PetGenes genes;
    uint8_t level;
    uint8_t stage;
    uint16_t xp;
    uint16_t battles;
    uint16_t wins;
    uint32_t capturedAtSec;
    uint32_t lastGrowthSec;
};

struct BackpackStorage {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint8_t selected;
    uint8_t reserved;
    SavedPet pets[kMaxBackpackPets];
};

static constexpr uint32_t kBagMagic = 0x57354247UL; // W5BG
static constexpr uint8_t kBagVersion = 1;
