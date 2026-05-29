#pragma once

#include <Arduino.h>

enum ScreenMode : uint8_t {
    kScreenIdle = 0,
    kScreenWild,
    kScreenCaptureFail,
    kScreenBag,
    kScreenReleaseConfirm,
    kScreenMatch,
    kScreenBattle,
};

enum UiAction : uint8_t {
    kActionNone = 0,
    kActionPhoto,
    kActionOpenBag,
    kActionBackToIdle,
    kActionPrevPet,
    kActionNextPet,
    kActionSelectPet,
    kActionCapturePet,
    kActionReleasePet,
    kActionReleaseStoredPet,
    kActionConfirmReleaseStoredPet,
    kActionMatchBattle,
};

enum ButtonSlot : uint8_t {
    kButtonLeft = 0,
    kButtonMiddle,
    kButtonRight,
};
