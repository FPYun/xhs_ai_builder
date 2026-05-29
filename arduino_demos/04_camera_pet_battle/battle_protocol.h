#pragma once

#include <Arduino.h>

enum BattleLinkRole : uint8_t {
    kBattleRoleHost = 0,
    kBattleRoleClient,
};

#pragma pack(push, 1)
struct BattlePetPacket {
    uint32_t magic;
    uint8_t version;
    uint8_t element;
    uint8_t species;
    uint8_t level;
    uint8_t mood;
    uint8_t bodyScale;
    uint8_t eyeStyle;
    uint8_t hornStyle;
    uint8_t tailStyle;
    uint8_t auraPattern;
    uint8_t patternDensity;
    uint16_t accentColor;
    uint32_t seed;
    uint32_t deviceId;
    uint32_t seq;
    uint16_t power;
    uint16_t agility;
    uint16_t spirit;
};
#pragma pack(pop)

static constexpr uint32_t kBattleMagic = 0x57355054UL; // W5PT
static constexpr uint8_t kBattleVersion = 1;
