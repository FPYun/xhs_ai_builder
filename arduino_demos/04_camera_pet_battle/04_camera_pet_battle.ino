#include <M5CoreS3.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <string.h>
#include "battle_protocol.h"
#include "pet_model.h"
#include "ui_types.h"
#include "vision_types.h"
#include "trainer_intro_audio.h"
#include "vision_model_data.h"

static constexpr uint32_t kPhotoCooldownMs = 1800;
static constexpr uint32_t kGrowthIntervalSec = 30;
static constexpr uint16_t kCaptureXp = 12;
static constexpr uint32_t kBattleClashMs = 700;
static constexpr uint32_t kRematchWindowSec = 180;
static constexpr uint16_t kRematchXpStep = 5;
static constexpr uint8_t kSoundVolume = 80;
static constexpr uint8_t kUiSoundVolume = 66;
static constexpr uint8_t kPetSoundVolume = 74;
static constexpr uint8_t kMusicSoundVolume = 88;
static constexpr uint16_t kSoundGapMs = 18;
static constexpr bool kAudioMuted = false;
static constexpr uint8_t kBattleWifiChannel = 6;
static constexpr uint16_t kBattleUdpPort = 42105;
static constexpr uint32_t kBattleScanIntervalMs = 4500;
static constexpr uint32_t kBattleConnectRetryMs = 5000;
static constexpr uint32_t kBattlePeerTimeoutMs = 12000;
static constexpr uint32_t kBattleStatusLogIntervalMs = 2000;
static constexpr char kBattleSsidPrefix[] = "M5PET-";
static const uint8_t kKnownCom8Mac[6] = {0x44, 0x1B, 0xF6, 0xE3, 0x9A, 0xFC};
static const uint8_t kKnownCom7Mac[6] = {0x44, 0x1B, 0xF6, 0xE3, 0x9B, 0x60};
static constexpr uint8_t kMinPresenceScore = 42;
static constexpr uint8_t kMinRecognitionConfidence = 48;

static uint32_t last_shot_ms = 0;
static uint32_t shot_count = 0;
static bool camera_ok = false;
static uint32_t device_id = 0;
static bool comm_ok = false;
static uint32_t last_pet_broadcast_ms = 0;
static uint32_t last_peer_seen_ms = 0;
static uint32_t match_started_ms = 0;
static uint16_t battle_packets_sent = 0;
static uint16_t battle_packets_seen = 0;
static uint16_t battle_send_failures = 0;
static bool battle_wifi_started = false;
static bool battle_udp_started = false;
static uint8_t device_mac[6] = {};
static uint32_t battle_peer_id = 0;
static uint32_t last_battle_scan_ms = 0;
static uint32_t last_battle_connect_ms = 0;
static uint32_t last_battle_status_log_ms = 0;
static bool battle_scan_running = false;
static IPAddress battle_peer_ip;
static WiFiUDP battle_udp;
static char battle_ap_ssid[20] = {};
static char battle_peer_ssid[20] = {};
static uint32_t display_hold_until_ms = 0;
static int8_t vision_input[kVisionInputBytes] = {};
static uint32_t last_vision_preprocess_ms = 0;
static uint32_t last_vision_classify_ms = 0;
static Preferences prefs;

static PetGenes local_pet = {};
static bool has_local_pet = false;
static PetGenes wild_pet = {};
static ImageTraits wild_traits = {};
static RecognitionResult wild_recognition = {};
static bool has_wild_pet = false;
static BackpackStorage backpack = {};
static ScreenMode screen_mode = kScreenIdle;
static uint8_t bag_cursor = 0;
static BattlePetPacket last_opponent_packet = {};
static BattlePetPacket pending_battle_packet = {};
static volatile bool opponent_packet_pending = false;
static bool battle_result_pending = false;
static uint32_t battle_result_due_ms = 0;
static uint32_t battle_sequence = 0;
static uint32_t local_pet_sequence = 0;
static uint32_t last_battle_key = 0;
static uint32_t last_growth_check_ms = 0;
static uint32_t last_friend_peer_id = 0;
static uint32_t last_friend_battle_sec = 0;
static uint8_t friend_rematch_streak = 0;
static BattleLinkRole battle_link_role = kBattleRoleHost;
enum BattleRuntimeState : uint8_t {
    kBattleStateDiscovering = 0,
    kBattleStatePairing,
    kBattleStateReady,
    kBattleStateBattling,
    kBattleStateRetrying,
};
static BattleRuntimeState battle_runtime_state = kBattleStateDiscovering;

static void take_photo(const char* reason);
static void handle_ui_action(uint8_t action);
static void draw_idle_screen(const char* message, bool playSound);
static void draw_bag_screen(const char* message);
static void draw_capture_fail(const ImageTraits& traits, const RecognitionResult& recog);

void handle_external_action(uint8_t action);
void handle_external_button(uint8_t button);

static void draw_status(const char* line1, const char* line2, uint32_t color)
{
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), 44, TFT_BLACK);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(color, TFT_BLACK);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(6, 6);
    CoreS3.Display.print(line1);
    CoreS3.Display.setCursor(6, 24);
    CoreS3.Display.print(line2);
}

static void draw_action_footer(const char* left, const char* middle, const char* right, uint16_t border)
{
    uint16_t bg = CoreS3.Display.color565(18, 22, 30);
    uint16_t divider = CoreS3.Display.color565(70, 74, 84);
    CoreS3.Display.fillRoundRect(6, 184, 308, 50, 8, bg);
    CoreS3.Display.drawRoundRect(6, 184, 308, 50, 8, border);
    CoreS3.Display.drawLine(108, 190, 108, 228, divider);
    CoreS3.Display.drawLine(212, 190, 212, 228, divider);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.drawString(left, 57, 209);
    CoreS3.Display.drawString(middle, 160, 209);
    CoreS3.Display.drawString(right, 263, 209);
    CoreS3.Display.setTextDatum(top_left);
}

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return CoreS3.Display.color565(r, g, b);
}

enum SoundCue : uint8_t {
    kSoundIdle = 0,
    kSoundPhoto,
    kSoundWild,
    kSoundBag,
    kSoundCapture,
    kSoundRelease,
    kSoundSelect,
    kSoundMatch,
    kSoundWin,
    kSoundDraw,
    kSoundLose,
    kSoundWarning,
    kSoundTrainerIntro,
};

struct SoundAssetRoute {
    SoundCue cue;
    const char* sdPath;
    uint8_t volume;
};

static const SoundAssetRoute kSoundAssetRoutes[] = {
    { kSoundIdle, "/audio/ui/idle.raw", kUiSoundVolume },
    { kSoundPhoto, "/audio/ui/photo.raw", kUiSoundVolume },
    { kSoundWild, "/audio/ui/wild.raw", kUiSoundVolume },
    { kSoundBag, "/audio/ui/bag.raw", kUiSoundVolume },
    { kSoundCapture, "/audio/ui/capture.raw", kUiSoundVolume },
    { kSoundRelease, "/audio/ui/release.raw", kUiSoundVolume },
    { kSoundSelect, "/audio/ui/select.raw", kUiSoundVolume },
    { kSoundMatch, "/audio/battle/match.raw", kUiSoundVolume },
    { kSoundWin, "/audio/battle/win.raw", kUiSoundVolume },
    { kSoundDraw, "/audio/battle/draw.raw", kUiSoundVolume },
    { kSoundLose, "/audio/battle/lose.raw", kUiSoundVolume },
    { kSoundWarning, "/audio/ui/warning.raw", kUiSoundVolume },
    { kSoundTrainerIntro, "/audio/music/intro.raw", kMusicSoundVolume },
};

static bool play_external_sound_asset(const char* path, uint8_t volume)
{
    (void)path;
    (void)volume;
    return false;
}

static bool try_play_external_scene_sound(uint8_t cue)
{
    for (size_t i = 0; i < sizeof(kSoundAssetRoutes) / sizeof(kSoundAssetRoutes[0]); ++i) {
        if (kSoundAssetRoutes[i].cue == cue) {
            return play_external_sound_asset(kSoundAssetRoutes[i].sdPath, kSoundAssetRoutes[i].volume);
        }
    }
    return false;
}

static uint8_t sound_volume_for_cue(uint8_t cue)
{
    switch (cue) {
    case kSoundTrainerIntro:
        return kMusicSoundVolume;
    case kSoundWild:
    case kSoundCapture:
    case kSoundMatch:
    case kSoundWin:
    case kSoundLose:
        return kSoundVolume;
    default:
        return kUiSoundVolume;
    }
}

static void play_notes_at_volume(const uint16_t* notes, const uint16_t* durations, size_t count, uint8_t volume)
{
    if (kAudioMuted) {
        return;
    }
    if (count == 0) {
        return;
    }

    if (!CoreS3.Speaker.begin()) {
        return;
    }
    CoreS3.Speaker.setVolume(volume);
    for (size_t i = 0; i < count; ++i) {
        if (notes[i] > 0) {
            CoreS3.Speaker.tone(notes[i], durations[i], -1, true);
        }
        CoreS3.delay(durations[i] + kSoundGapMs);
    }
    CoreS3.Speaker.stop();
    CoreS3.Speaker.end();
}

static void play_notes(const uint16_t* notes, const uint16_t* durations, size_t count)
{
    play_notes_at_volume(notes, durations, count, kUiSoundVolume);
}

static uint16_t element_base_note(ElementType element)
{
    switch (element) {
    case kWood: return 523;  // C5, rising and light.
    case kFire: return 784;  // G5, sharp and bright.
    case kEarth: return 392; // G4, low and steady.
    case kMetal: return 659; // E5, bell-like.
    case kWater: return 494; // B4, smooth and flowing.
    }
    return 523;
}

static uint16_t shifted_note(uint16_t note, int16_t shift)
{
    int32_t value = static_cast<int32_t>(note) + shift;
    return static_cast<uint16_t>(max<int32_t>(70, min<int32_t>(2600, value)));
}

static void play_trainer_intro_sample()
{
    if (kAudioMuted) {
        return;
    }
    if (!CoreS3.Speaker.begin()) {
        return;
    }
    CoreS3.Speaker.setVolume(kMusicSoundVolume);
    CoreS3.Speaker.playRaw(kTrainerIntroPcm, kTrainerIntroPcmLen, kTrainerIntroSampleRate, false, 1, -1, true);
    CoreS3.delay((kTrainerIntroPcmLen * 1000UL) / kTrainerIntroSampleRate + 80);
    CoreS3.Speaker.stop();
    CoreS3.Speaker.end();
}

static bool play_layered_scene_sound(uint8_t cue)
{
    switch (cue) {
    case kSoundIdle: {
        const uint16_t notes[] = { 523, 659, 784, 659, 880, 0, 784 };
        const uint16_t ms[] = { 75, 75, 95, 75, 130, 30, 150 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundPhoto: {
        const uint16_t notes[] = { 1047, 0, 1568, 1976 };
        const uint16_t ms[] = { 45, 25, 50, 80 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundWild: {
        const uint16_t notes[] = { 220, 277, 330, 415, 523, 0, 659 };
        const uint16_t ms[] = { 70, 70, 85, 85, 110, 30, 160 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundBag: {
        const uint16_t notes[] = { 784, 659, 587, 659, 784 };
        const uint16_t ms[] = { 70, 70, 80, 90, 130 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundCapture: {
        const uint16_t notes[] = { 523, 659, 784, 1047, 1319, 1568 };
        const uint16_t ms[] = { 60, 60, 75, 90, 105, 170 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundRelease: {
        const uint16_t notes[] = { 880, 740, 622, 523, 392 };
        const uint16_t ms[] = { 80, 80, 90, 120, 170 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundSelect: {
        const uint16_t notes[] = { 659, 880, 1175 };
        const uint16_t ms[] = { 55, 60, 90 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundMatch: {
        const uint16_t notes[] = { 392, 523, 659, 784, 659, 784, 988 };
        const uint16_t ms[] = { 80, 80, 80, 100, 70, 90, 150 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundWin: {
        const uint16_t notes[] = { 784, 988, 1175, 1568, 1760, 1976 };
        const uint16_t ms[] = { 70, 70, 80, 105, 120, 210 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundDraw: {
        const uint16_t notes[] = { 659, 0, 659, 784, 659 };
        const uint16_t ms[] = { 85, 35, 85, 115, 150 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundLose: {
        const uint16_t notes[] = { 659, 523, 440, 330, 262 };
        const uint16_t ms[] = { 80, 80, 100, 130, 210 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundWarning: {
        const uint16_t notes[] = { 330, 0, 330, 262 };
        const uint16_t ms[] = { 80, 35, 90, 180 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundTrainerIntro:
        play_trainer_intro_sample();
        return true;
    default:
        return false;
    }
}

static void play_scene_sound(uint8_t cue)
{
    if (kAudioMuted) {
        return;
    }
    const uint8_t cueId = cue;
    if (try_play_external_scene_sound(cueId)) {
        return;
    }
    if (play_layered_scene_sound(cueId)) {
        return;
    }

    switch (cue) {
    case kSoundIdle: {
        const uint16_t notes[] = {392, 0, 523, 659, 784};
        const uint16_t ms[] = {55, 22, 55, 55, 120};
        play_notes(notes, ms, 5);
        break;
    }
    case kSoundPhoto: {
        const uint16_t notes[] = {1480, 0, 980, 1840};
        const uint16_t ms[] = {24, 18, 24, 42};
        play_notes(notes, ms, 4);
        break;
    }
    case kSoundWild: {
        const uint16_t notes[] = {262, 330, 392, 0, 659, 784, 988};
        const uint16_t ms[] = {45, 45, 48, 28, 50, 55, 125};
        play_notes(notes, ms, 7);
        break;
    }
    case kSoundBag: {
        const uint16_t notes[] = {523, 659, 0, 659, 784};
        const uint16_t ms[] = {45, 60, 20, 40, 90};
        play_notes(notes, ms, 5);
        break;
    }
    case kSoundCapture: {
        const uint16_t notes[] = {659, 0, 659, 0, 784, 988, 1319};
        const uint16_t ms[] = {48, 22, 48, 28, 58, 70, 150};
        play_notes(notes, ms, 7);
        break;
    }
    case kSoundRelease: {
        const uint16_t notes[] = {784, 659, 523, 392, 0, 330};
        const uint16_t ms[] = {45, 55, 65, 80, 20, 120};
        play_notes(notes, ms, 6);
        break;
    }
    case kSoundSelect: {
        const uint16_t notes[] = {587, 740, 988};
        const uint16_t ms[] = {42, 52, 115};
        play_notes(notes, ms, 3);
        break;
    }
    case kSoundMatch: {
        const uint16_t notes[] = {392, 0, 392, 523, 659, 523};
        const uint16_t ms[] = {70, 28, 70, 62, 92, 120};
        play_notes(notes, ms, 6);
        break;
    }
    case kSoundWin: {
        const uint16_t notes[] = {523, 659, 784, 988, 1319, 1568};
        const uint16_t ms[] = {45, 45, 55, 60, 70, 165};
        play_notes(notes, ms, 6);
        break;
    }
    case kSoundDraw: {
        const uint16_t notes[] = {587, 0, 587, 740, 0, 740};
        const uint16_t ms[] = {55, 20, 55, 70, 20, 95};
        play_notes(notes, ms, 6);
        break;
    }
    case kSoundLose: {
        const uint16_t notes[] = {659, 523, 392, 330, 262};
        const uint16_t ms[] = {55, 65, 75, 85, 150};
        play_notes(notes, ms, 5);
        break;
    }
    case kSoundTrainerIntro: {
        play_trainer_intro_sample();
        break;
    }
    }
}

static bool play_rich_pet_sound(const PetGenes& genes, uint8_t level, uint8_t stage)
{
    const uint16_t stretch = 8 + level * 2 + stage * 10;
    switch (genes.element) {
    case 0: {
        const uint16_t notes[] = { 523, 659, 784, 1047 + stretch, 784 };
        const uint16_t ms[] = { 55, 65, 80, 105, 85 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), kPetSoundVolume);
        return true;
    }
    case 1: {
        const uint16_t notes[] = { 784, 1047, 1319 + stretch, 1047, 1568 };
        const uint16_t ms[] = { 45, 55, 70, 55, 95 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), kPetSoundVolume);
        return true;
    }
    case 2: {
        const uint16_t notes[] = { 196, 247, 294, 247, 220 };
        const uint16_t ms[] = { 80, 85, 115, 95, 150 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), kPetSoundVolume);
        return true;
    }
    case 3: {
        const uint16_t notes[] = { 988, 0, 1319, 0, 1760 + stretch };
        const uint16_t ms[] = { 45, 25, 50, 25, 120 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), kPetSoundVolume);
        return true;
    }
    case 4: {
        const uint16_t notes[] = { 440, 523, 659, 523, 440, 587 + stretch };
        const uint16_t ms[] = { 70, 90, 110, 90, 75, 130 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), kPetSoundVolume);
        return true;
    }
    default:
        return false;
    }
}

static void play_pet_sound(const PetGenes& genes, uint8_t level, uint8_t stage)
{
    if (kAudioMuted) {
        return;
    }
    if (play_rich_pet_sound(genes, level, stage)) {
        return;
    }

    uint16_t base = element_base_note(genes.element);
    int16_t speciesShift = 36 + genes.species * 52;
    int16_t moodShift = genes.mood * 18;
    int16_t levelShift = min<uint8_t>(level, 20) * 4 + stage * 70;
    int16_t bright = (genes.eyeStyle % 2) ? 24 : 0;
    int16_t growl = (genes.bodyScale > 120) ? -45 : 0;

    if (genes.element == kFire) {
        const uint16_t notes[] = {
            shifted_note(base, growl),
            shifted_note(base, speciesShift + 130),
            shifted_note(base, 40),
            shifted_note(base, speciesShift + moodShift + levelShift + 210),
            shifted_note(base, 120),
        };
        const uint16_t ms[] = {32, 38, 28, 72, 55};
        play_notes(notes, ms, 5);
    } else if (genes.element == kEarth) {
        const uint16_t notes[] = {
            shifted_note(base, -110 + growl),
            0,
            shifted_note(base, -55),
            shifted_note(base, speciesShift / 2),
            shifted_note(base, -35 + levelShift / 2),
        };
        const uint16_t ms[] = {75, 24, 80, 96, 120};
        play_notes(notes, ms, 5);
    } else if (genes.element == kMetal) {
        const uint16_t notes[] = {
            shifted_note(base, 0),
            shifted_note(base, 11),
            0,
            shifted_note(base, speciesShift + 98 + bright),
            shifted_note(base, speciesShift + 244 + levelShift),
        };
        const uint16_t ms[] = {30, 32, 18, 58, 145};
        play_notes(notes, ms, 5);
    } else if (genes.element == kWater) {
        const uint16_t notes[] = {
            shifted_note(base, speciesShift + 180 + bright),
            shifted_note(base, speciesShift + 96),
            shifted_note(base, 42),
            0,
            shifted_note(base, -26 + levelShift / 2),
            shifted_note(base, 68),
        };
        const uint16_t ms[] = {48, 58, 70, 18, 105, 55};
        play_notes(notes, ms, 6);
    } else {
        const uint16_t notes[] = {
            shifted_note(base, -20),
            shifted_note(base, speciesShift),
            shifted_note(base, speciesShift + moodShift + 116),
            0,
            shifted_note(base, 72 + levelShift),
            shifted_note(base, speciesShift + 170),
        };
        const uint16_t ms[] = {42, 54, 82, 18, 70, 105};
        play_notes(notes, ms, 6);
    }
}

static void rgb565_to_rgb(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b)
{
    r = ((color >> 11) & 0x1F) * 255 / 31;
    g = ((color >> 5) & 0x3F) * 255 / 63;
    b = (color & 0x1F) * 255 / 31;
}

static int32_t rgb_luma(uint8_t r, uint8_t g, uint8_t b)
{
    return (r * 30 + g * 59 + b * 11) / 100;
}

static int32_t rgb_chroma(uint8_t r, uint8_t g, uint8_t b)
{
    return max(max(r, g), b) - min(min(r, g), b);
}

static bool preprocess_frame_for_vision()
{
    uint32_t start = millis();
    memset(vision_input, 0, sizeof(vision_input));
    last_vision_preprocess_ms = 0;

    if (CoreS3.Camera.fb == nullptr || CoreS3.Camera.fb->len < 2) {
        return false;
    }

    const uint16_t* pixels = reinterpret_cast<const uint16_t*>(CoreS3.Camera.fb->buf);
    const size_t pixelCount = CoreS3.Camera.fb->len / 2;
    int32_t frameW = CoreS3.Camera.fb->width;
    int32_t frameH = CoreS3.Camera.fb->height;
    if (frameW <= 0 || frameH <= 0 || static_cast<size_t>(frameW * frameH) > pixelCount) {
        frameW = CoreS3.Display.width();
        frameH = pixelCount / max<int32_t>(1, frameW);
    }
    if (frameW <= 0 || frameH <= 0) {
        return false;
    }

    for (uint16_t y = 0; y < kVisionInputHeight; ++y) {
        int32_t srcY = min<int32_t>(frameH - 1, (static_cast<int32_t>(y) * frameH) / kVisionInputHeight);
        for (uint16_t x = 0; x < kVisionInputWidth; ++x) {
            int32_t srcX = min<int32_t>(frameW - 1, (static_cast<int32_t>(x) * frameW) / kVisionInputWidth);
            size_t srcIndex = static_cast<size_t>(srcY) * frameW + srcX;
            if (srcIndex >= pixelCount) {
                continue;
            }

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            rgb565_to_rgb(pixels[srcIndex], r, g, b);
            size_t dst = (static_cast<size_t>(y) * kVisionInputWidth + x) * kVisionInputChannels;
            vision_input[dst] = static_cast<int8_t>(static_cast<int16_t>(r) - 128);
            vision_input[dst + 1] = static_cast<int8_t>(static_cast<int16_t>(g) - 128);
            vision_input[dst + 2] = static_cast<int8_t>(static_cast<int16_t>(b) - 128);
        }
    }

    last_vision_preprocess_ms = millis() - start;
    return true;
}

static ImageTraits analyze_frame()
{
    ImageTraits traits = {};
    traits.element = kEarth;

    if (CoreS3.Camera.fb == nullptr || CoreS3.Camera.fb->len < 2) {
        return traits;
    }

    const uint16_t* pixels = reinterpret_cast<const uint16_t*>(CoreS3.Camera.fb->buf);
    const size_t pixel_count = CoreS3.Camera.fb->len / 2;
    int32_t frame_w = CoreS3.Camera.fb->width;
    int32_t frame_h = CoreS3.Camera.fb->height;
    if (frame_w <= 0 || frame_h <= 0 || static_cast<size_t>(frame_w * frame_h) > pixel_count) {
        frame_w = CoreS3.Display.width();
        frame_h = pixel_count / max<int32_t>(1, frame_w);
    }
    const int32_t stride = max<int32_t>(2, frame_w / 80);

    int64_t sum_r = 0;
    int64_t sum_g = 0;
    int64_t sum_b = 0;
    int64_t sum_luma = 0;
    int64_t sum_luma_delta = 0;
    int64_t center_luma_sum = 0;
    int64_t edge_luma_sum = 0;
    int64_t center_chroma_sum = 0;
    int64_t edge_chroma_sum = 0;
    int32_t center_count = 0;
    int32_t edge_count = 0;
    int32_t dark_count = 0;
    int32_t bright_count = 0;
    int32_t sample_count = 0;
    int32_t min_luma = 255;
    int32_t max_luma = 0;
    uint32_t seed = 2166136261UL;
    int64_t weight_sum = 0;
    int32_t votes[5] = {0, 0, 0, 0, 0};

    for (int32_t y = 0; y < frame_h; y += stride) {
        for (int32_t x = 0; x < frame_w; x += stride) {
            size_t i = static_cast<size_t>(y) * frame_w + x;
            if (i >= pixel_count) {
                continue;
            }

            uint16_t c = pixels[i];
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            rgb565_to_rgb(c, r, g, b);
            int32_t chroma = rgb_chroma(r, g, b);
            int32_t luma = rgb_luma(r, g, b);
            int32_t dx = abs(x - frame_w / 2);
            int32_t dy = abs(y - frame_h / 2);
            int32_t weight = 1;
            bool centerSample = false;
            if (dx < frame_w / 3 && dy < frame_h / 3) {
                weight += 2;
                centerSample = true;
            }
            if (dx < frame_w / 6 && dy < frame_h / 6) {
                weight += 2;
            }

            sum_r += r * weight;
            sum_g += g * weight;
            sum_b += b * weight;
            sum_luma += luma * weight;
            min_luma = min(min_luma, luma);
            max_luma = max(max_luma, luma);
            seed ^= (static_cast<uint32_t>(c) + i + static_cast<uint32_t>(weight << 24));
            seed *= 16777619UL;
            weight_sum += weight;
            ++sample_count;
            if (luma < 42) {
                ++dark_count;
            }
            if (luma > 210) {
                ++bright_count;
            }
            if (centerSample) {
                center_luma_sum += luma;
                center_chroma_sum += chroma;
                ++center_count;
            } else {
                edge_luma_sum += luma;
                edge_chroma_sum += chroma;
                ++edge_count;
            }

            if (chroma < 22) {
                if (luma > 158) {
                    votes[kMetal] += weight * 2;
                } else if (luma < 70) {
                    votes[kWater] += weight;
                } else {
                    votes[kEarth] += weight;
                }
                continue;
            }

            if (g > r + 14 && g > b + 10) {
                votes[kWood] += weight * 4;
            }
            if (r > b + 24 && r > g + 12) {
                votes[kFire] += weight * 4;
            }
            if (r > b + 12 && g > b + 8 && abs(r - g) < 78) {
                votes[kEarth] += weight * 4;
            }
            if (b > r + 18 && b > g + 8) {
                votes[kWater] += weight * 4;
            }
            if (luma > 175 && chroma < 48) {
                votes[kMetal] += weight * 2;
            }
        }
    }

    if (weight_sum == 0) {
        return traits;
    }

    traits.r = sum_r / weight_sum;
    traits.g = sum_g / weight_sum;
    traits.b = sum_b / weight_sum;
    traits.brightness = sum_luma / weight_sum;
    traits.contrast = max_luma - min_luma;
    traits.saturation = max(max(traits.r, traits.g), traits.b) - min(min(traits.r, traits.g), traits.b);
    traits.centerBrightness = center_luma_sum / max<int32_t>(1, center_count);
    traits.edgeBrightness = edge_luma_sum / max<int32_t>(1, edge_count);
    traits.centerSaturation = center_chroma_sum / max<int32_t>(1, center_count);
    traits.edgeSaturation = edge_chroma_sum / max<int32_t>(1, edge_count);
    traits.centerDelta = abs(traits.centerBrightness - traits.edgeBrightness) +
                         abs(traits.centerSaturation - traits.edgeSaturation) / 2;
    traits.darkRatio = (dark_count * 100) / max<int32_t>(1, sample_count);
    traits.brightRatio = (bright_count * 100) / max<int32_t>(1, sample_count);
    traits.frameWidth = frame_w;
    traits.frameHeight = frame_h;

    const size_t delta_step = max<size_t>(1, pixel_count / 900);
    size_t delta_count = 0;
    for (size_t i = 0; i < pixel_count; i += delta_step) {
        uint16_t c = pixels[i];
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        rgb565_to_rgb(c, r, g, b);
        int32_t luma = rgb_luma(r, g, b);
        sum_luma_delta += abs(luma - traits.brightness);
        ++delta_count;
    }
    traits.contrast = max(traits.contrast, static_cast<int32_t>(sum_luma_delta / max<size_t>(1, delta_count)));

    int32_t scores[] = {
        votes[kWood] + max<int32_t>(0, traits.g - max(traits.r, traits.b) + 18) * 8,
        votes[kFire] + max<int32_t>(0, traits.r - traits.b + traits.saturation / 2) * 5,
        votes[kEarth] + max<int32_t>(0, min(traits.r, traits.g) - traits.b + 22 - abs(static_cast<int32_t>(traits.r) - traits.g) / 2) * 6,
        votes[kMetal] + ((traits.saturation < 44) ? max<int32_t>(0, traits.brightness - 126) * 4 : 0),
        votes[kWater] + max<int32_t>(0, traits.b - max(traits.r, traits.g) + 18) * 7,
    };

    uint8_t best = 0;
    uint8_t second = 1;
    if (scores[second] > scores[best]) {
        uint8_t tmp = best;
        best = second;
        second = tmp;
    }
    for (uint8_t i = 1; i < 5; ++i) {
        if (scores[i] > scores[best]) {
            second = best;
            best = i;
        } else if (i != best && scores[i] > scores[second]) {
            second = i;
        }
    }

    traits.confidence = scores[best] - scores[second];
    if (traits.confidence < max<int32_t>(140, weight_sum / 9)) {
        uint8_t ranked[5] = {0, 1, 2, 3, 4};
        for (uint8_t i = 0; i < 4; ++i) {
            for (uint8_t j = i + 1; j < 5; ++j) {
                if (scores[ranked[j]] > scores[ranked[i]]) {
                    uint8_t tmp = ranked[i];
                    ranked[i] = ranked[j];
                    ranked[j] = tmp;
                }
            }
        }
        best = ranked[seed % 3];
    }

    traits.element = static_cast<ElementType>(best);
    traits.seed = seed ^ (traits.r << 16) ^ (traits.g << 8) ^ traits.b ^ millis();
    return traits;
}

static uint32_t hash_mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

static uint32_t next_rand(uint32_t* state)
{
    *state = hash_mix(*state + 0x9e3779b9UL);
    return *state;
}

static uint8_t clamp_u8(int32_t value)
{
    return static_cast<uint8_t>(max<int32_t>(0, min<int32_t>(255, value)));
}

static uint16_t tint_color(uint16_t base, int32_t delta)
{
    uint8_t r = ((base >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((base >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (base & 0x1F) * 255 / 31;
    return rgb(clamp_u8(r + delta), clamp_u8(g + delta), clamp_u8(b + delta));
}

static const char* element_name(ElementType element)
{
    static const char* const names[] = {"Wood / Mu", "Fire / Huo", "Earth / Tu", "Metal / Jin", "Water / Shui"};
    return names[element];
}

static const char* species_name_by(ElementType element, uint8_t species)
{
    static const char* const names[5][3] = {
        {"Leaf Deer", "Vine Fox", "Bud Turtle"},
        {"Flame Cat", "Firebird", "Lava Hound"},
        {"Mountain Bear", "Rock Turtle", "Clay Beast"},
        {"Silver Wolf", "Mecha Rabbit", "Bronze Tiger"},
        {"Wave Otter", "Water Dragon", "Bubble Fish"},
    };
    return names[element][species % 3];
}

static const char* species_name(const PetGenes& genes)
{
    return species_name_by(genes.element, genes.species);
}

static const char* mood_name(uint8_t mood)
{
    static const char* const names[] = {"Bright", "Calm", "Mystic", "Brave"};
    return names[mood % 4];
}

static const char* stage_name(uint8_t stage)
{
    static const char* const names[] = {"Cub", "Adept", "Evolved"};
    return names[stage % 3];
}

static uint16_t element_body_color(ElementType element)
{
    switch (element) {
    case kWood: return rgb(75, 178, 88);
    case kFire: return rgb(226, 74, 42);
    case kEarth: return rgb(191, 136, 65);
    case kMetal: return rgb(210, 218, 221);
    case kWater: return rgb(54, 136, 210);
    }
    return TFT_WHITE;
}

static uint16_t element_accent_color(ElementType element)
{
    switch (element) {
    case kWood: return rgb(184, 235, 118);
    case kFire: return rgb(255, 187, 50);
    case kEarth: return rgb(239, 205, 107);
    case kMetal: return rgb(143, 183, 207);
    case kWater: return rgb(116, 222, 246);
    }
    return TFT_WHITE;
}

static const char* object_class_label(uint8_t classId)
{
    switch (classId) {
    case kObjectPlantLeaf: return "plant_leaf";
    case kObjectFoodFruit: return "food_fruit";
    case kObjectPaperBook: return "paper_book";
    case kObjectElectronicsScreen: return "electronics_screen";
    case kObjectMetalKeyCoin: return "metal_key_coin";
    case kObjectFabricCloth: return "fabric_cloth";
    case kObjectCupBottleWater: return "cup_bottle_water";
    case kObjectToyFigure: return "toy_figure";
    default: return "unknown";
    }
}

static const char* material_label_for_class(uint8_t classId)
{
    switch (classId) {
    case kObjectPlantLeaf: return "plant";
    case kObjectFoodFruit: return "organic";
    case kObjectPaperBook: return "paper";
    case kObjectElectronicsScreen: return "electronic";
    case kObjectMetalKeyCoin: return "metal";
    case kObjectFabricCloth: return "fabric";
    case kObjectCupBottleWater: return "water-container";
    case kObjectToyFigure: return "toy";
    default: return "unknown";
    }
}

static ElementType element_hint_for_class(uint8_t classId, const ImageTraits& traits)
{
    switch (classId) {
    case kObjectPlantLeaf:
        return kWood;
    case kObjectFoodFruit:
        return (traits.brightness < 105 || traits.r > traits.g + 36) ? kEarth : kWood;
    case kObjectPaperBook:
        return (traits.brightness > 154 && traits.saturation < 44) ? kWood : kEarth;
    case kObjectElectronicsScreen:
        return (traits.darkRatio > 35) ? kWater : kMetal;
    case kObjectMetalKeyCoin:
        return kMetal;
    case kObjectFabricCloth:
        return (traits.g > traits.r && traits.g > traits.b) ? kWood : kEarth;
    case kObjectCupBottleWater:
        return kWater;
    case kObjectToyFigure:
        return (traits.r > traits.b + 16) ? kFire : kMetal;
    default:
        return traits.element;
    }
}

static uint8_t species_bias_for_class(uint8_t classId, const ImageTraits& traits)
{
    (void)traits;
    switch (classId) {
    case kObjectPlantLeaf: return 0;
    case kObjectFoodFruit: return 1;
    case kObjectPaperBook: return 2;
    case kObjectElectronicsScreen: return 1;
    case kObjectMetalKeyCoin: return 2;
    case kObjectFabricCloth: return 0;
    case kObjectCupBottleWater: return 2;
    case kObjectToyFigure: return 1;
    default: return 0;
    }
}

static SubjectPresence detect_subject_presence(const ImageTraits& traits)
{
    SubjectPresence presence = {};
    presence.reason = "No subject";

    if (traits.frameWidth <= 0 || traits.frameHeight <= 0) {
        presence.reason = "No frame";
        return presence;
    }
    if (traits.brightness < 32 || traits.darkRatio > 82) {
        presence.reason = "Too dark";
        presence.score = clamp_u8(traits.contrast / 2 + traits.centerDelta);
        return presence;
    }
    if (traits.brightRatio > 92 && traits.contrast < 22) {
        presence.reason = "Too bright";
        presence.score = clamp_u8(traits.centerDelta + traits.contrast);
        return presence;
    }
    if (traits.contrast < 18 && traits.centerDelta < 12 && traits.saturation < 42) {
        presence.reason = "Flat scene";
        presence.score = clamp_u8(traits.contrast + traits.centerDelta);
        return presence;
    }
    if (abs(traits.centerBrightness - traits.edgeBrightness) < 6 &&
        abs(traits.centerSaturation - traits.edgeSaturation) < 6 &&
        traits.contrast < 26) {
        presence.reason = "No centered subject";
        presence.score = clamp_u8(traits.contrast + traits.saturation / 3);
        return presence;
    }

    int32_t score = traits.centerDelta * 2 + traits.contrast / 2 + traits.saturation / 3;
    score += abs(traits.centerBrightness - traits.edgeBrightness) / 2;
    if (traits.centerSaturation > traits.edgeSaturation + 8) {
        score += 12;
    }
    if (traits.contrast < 16 && traits.centerDelta < 13) {
        score -= 32;
    }
    if (traits.saturation < 16 && traits.contrast < 28) {
        score -= 18;
    }

    presence.score = clamp_u8(score);
    presence.present = presence.score >= kMinPresenceScore;
    presence.reason = presence.present ? "Subject centered" : "No clear subject";
    return presence;
}

static int16_t normalized_feature(int32_t value)
{
    return static_cast<int16_t>(max<int32_t>(-kVisionFeatureScale,
                                            min<int32_t>(kVisionFeatureScale,
                                                        (value * kVisionFeatureScale) / 128 - kVisionFeatureScale)));
}

static void build_vision_features(const ImageTraits& traits, int16_t* features)
{
    features[0] = normalized_feature(traits.r);
    features[1] = normalized_feature(traits.g);
    features[2] = normalized_feature(traits.b);
    features[3] = normalized_feature(traits.brightness);
    features[4] = normalized_feature(traits.saturation);
    features[5] = normalized_feature(traits.contrast);
    features[6] = normalized_feature(traits.centerDelta);
    features[7] = normalized_feature((traits.darkRatio * 255) / 100);
    features[8] = normalized_feature((traits.brightRatio * 255) / 100);
    features[9] = normalized_feature(traits.centerSaturation - traits.edgeSaturation + 128);
}

static bool run_embedded_vision_model(const int8_t* input, const ImageTraits& traits, RecognitionResult* result)
{
    (void)input;
    if (result == nullptr || kVisionClassCount != 8 || kVisionFeatureCount != 10 || kVisionModelDataLen == 0) {
        return false;
    }

    int16_t features[kVisionFeatureCount] = {};
    build_vision_features(traits, features);

    int32_t logits[kVisionClassCount] = {};
    for (uint8_t c = 0; c < kVisionClassCount; ++c) {
        int32_t score = kVisionBias[c];
        for (uint8_t i = 0; i < kVisionFeatureCount; ++i) {
            score += static_cast<int32_t>(kVisionWeights[c][i]) * features[i];
        }
        logits[c] = score;
    }

    uint8_t best = 0;
    uint8_t second = 1;
    if (logits[second] > logits[best]) {
        uint8_t tmp = best;
        best = second;
        second = tmp;
    }
    for (uint8_t c = 2; c < kVisionClassCount; ++c) {
        if (logits[c] > logits[best]) {
            second = best;
            best = c;
        } else if (logits[c] > logits[second]) {
            second = c;
        }
    }

    uint8_t classId = static_cast<uint8_t>(best + 1);
    int32_t margin = (logits[best] - logits[second]) / 256;
    result->classId = classId;
    result->objectLabel = object_class_label(classId);
    result->materialLabel = material_label_for_class(classId);
    result->elementHint = element_hint_for_class(classId, traits);
    result->speciesBias = species_bias_for_class(classId, traits);
    result->confidence = clamp_u8(36 + margin / 5 + result->presenceScore / 4);
    result->recognized = result->confidence >= kMinRecognitionConfidence;
    result->failureReason = result->recognized ? "" : "Low model confidence";
    return true;
}

static RecognitionResult classify_object_local(const ImageTraits& traits, const SubjectPresence& presence)
{
    RecognitionResult result = {};
    result.objectLabel = object_class_label(kObjectUnknown);
    result.materialLabel = material_label_for_class(kObjectUnknown);
    result.failureReason = presence.reason;
    result.presenceScore = presence.score;
    result.elementHint = traits.element;

    if (!presence.present) {
        return result;
    }
    if (run_embedded_vision_model(vision_input, traits, &result)) {
        result.recognized = result.confidence >= kMinRecognitionConfidence;
        result.failureReason = result.recognized ? "" : "Low model confidence";
        if (result.recognized) {
            return result;
        }
    }

    int32_t redDominance = static_cast<int32_t>(traits.r) - max(traits.g, traits.b);
    int32_t greenDominance = static_cast<int32_t>(traits.g) - max(traits.r, traits.b);
    int32_t blueDominance = static_cast<int32_t>(traits.b) - max(traits.r, traits.g);
    int32_t warm = static_cast<int32_t>(traits.r) + traits.g - traits.b * 2;
    int32_t lowSaturation = max<int32_t>(0, 75 - traits.saturation);
    int32_t highBrightness = max<int32_t>(0, traits.brightness - 132);
    int32_t midBrightness = max<int32_t>(0, 95 - abs(traits.brightness - 125));

    int32_t scores[9] = {};
    scores[kObjectPlantLeaf] = max<int32_t>(0, greenDominance + 32) * 3 + traits.saturation + presence.score;
    scores[kObjectFoodFruit] = max<int32_t>(0, redDominance + 24) * 2 + max<int32_t>(0, warm + 36) + traits.saturation;
    scores[kObjectPaperBook] = highBrightness * 2 + lowSaturation * 2 + max<int32_t>(0, traits.centerDelta - 10);
    scores[kObjectElectronicsScreen] = traits.darkRatio * 3 + traits.contrast + max<int32_t>(0, blueDominance + 22) * 2;
    scores[kObjectMetalKeyCoin] = lowSaturation * 3 + highBrightness * 2 + traits.contrast;
    scores[kObjectFabricCloth] = midBrightness + traits.saturation * 2 + max<int32_t>(0, 80 - traits.contrast);
    scores[kObjectCupBottleWater] = max<int32_t>(0, blueDominance + 34) * 3 + highBrightness + traits.centerDelta;
    scores[kObjectToyFigure] = traits.saturation * 2 + traits.contrast + max<int32_t>(0, abs(redDominance - blueDominance));

    uint8_t best = kObjectPlantLeaf;
    uint8_t second = kObjectFoodFruit;
    if (scores[second] > scores[best]) {
        uint8_t tmp = best;
        best = second;
        second = tmp;
    }
    for (uint8_t i = kObjectPaperBook; i <= kObjectToyFigure; ++i) {
        if (scores[i] > scores[best]) {
            second = best;
            best = i;
        } else if (i != best && scores[i] > scores[second]) {
            second = i;
        }
    }

    int32_t margin = scores[best] - scores[second];
    result.classId = best;
    result.objectLabel = object_class_label(best);
    result.materialLabel = material_label_for_class(best);
    result.elementHint = element_hint_for_class(best, traits);
    result.speciesBias = species_bias_for_class(best, traits);
    result.confidence = clamp_u8(28 + margin / 5 + presence.score / 3);
    result.recognized = result.confidence >= kMinRecognitionConfidence;
    result.failureReason = result.recognized ? "" : "Low class confidence";
    return result;
}

static RecognitionResult recognize_object_local(const ImageTraits& traits)
{
    uint32_t start = millis();
    SubjectPresence presence = detect_subject_presence(traits);
    RecognitionResult result = classify_object_local(traits, presence);
    last_vision_classify_ms = millis() - start;
    return result;
}

static bool fetch_remote_pet_hint(const RecognitionResult& input, PetHint* output)
{
    (void)input;
    if (output != nullptr) {
        *output = {};
    }
    return false;
}

static bool save_pet_snapshot(const PetGenes& genes, const ImageTraits& traits)
{
    (void)genes;
    (void)traits;
    return false;
}

static PetGenes merge_generation_inputs(ImageTraits traits, RecognitionResult recog, PetHint remoteHint)
{
    PetGenes genes = {};
    int32_t centerBrightnessDelta = traits.centerBrightness - traits.edgeBrightness;
    int32_t centerSaturationDelta = traits.centerSaturation - traits.edgeSaturation;
    uint8_t classId = recog.classId;
    genes.element = remoteHint.valid ? remoteHint.preferredElement : recog.elementHint;
    genes.species = remoteHint.valid ? (remoteHint.preferredSpecies % 3) : (recog.speciesBias % 3);
    genes.mood = (traits.brightness > 168) ? 0 : ((traits.saturation < 38) ? 1 : ((traits.contrast > 145) ? 3 : 2));
    genes.bodyScale = clamp_u8(78 + traits.saturation / 5 + traits.contrast / 10 + abs(centerBrightnessDelta) / 4);
    genes.eyeStyle = (traits.brightness / 48 + recog.confidence / 35 + classId) % 4;
    genes.hornStyle = (traits.contrast / 42 + abs(centerBrightnessDelta) / 18 + remoteHint.styleBias + classId) % 4;
    genes.tailStyle = (traits.saturation / 38 + abs(centerSaturationDelta) / 14 + traits.r + traits.b + classId) % 4;
    genes.auraPattern = (traits.centerDelta / 18 + traits.contrast / 45 + traits.g + remoteHint.styleBias + classId) % 4;
    genes.patternDensity = clamp_u8(2 + traits.saturation / 35 + traits.contrast / 50 + traits.centerDelta / 30);
    genes.accentColor = element_accent_color(genes.element);
    genes.seed = traits.seed ^ (static_cast<uint32_t>(recog.confidence) << 24) ^
                 (static_cast<uint32_t>(classId) << 20) ^
                 (static_cast<uint32_t>(traits.brightness & 0xff) << 12) ^
                 (static_cast<uint32_t>(traits.centerDelta & 0xff) << 4) ^
                 (remoteHint.styleBias << 16);
    return genes;
}

static PetGenes derive_pet_genes(ImageTraits traits, const RecognitionResult& recog, uint32_t timeBucket, uint32_t shotCount)
{
    PetHint hint = {};
    fetch_remote_pet_hint(recog, &hint);

    PetGenes genes = merge_generation_inputs(traits, recog, hint);
    uint32_t state = hash_mix(traits.seed ^ (timeBucket * 0x45d9f3bUL) ^
                              (shotCount * 0x119de1f3UL) ^
                              (static_cast<uint32_t>(recog.classId) << 24) ^
                              (static_cast<uint32_t>(traits.saturation & 0xff) << 16) ^
                              (static_cast<uint32_t>(traits.centerDelta & 0xff) << 8));
    int32_t centerBrightnessDelta = traits.centerBrightness - traits.edgeBrightness;
    uint8_t timeStyle = timeBucket % 4;

    genes.species %= 3;
    genes.bodyScale = clamp_u8(genes.bodyScale + centerBrightnessDelta / 12 +
                               static_cast<int32_t>(next_rand(&state) % 13) - 6);
    genes.eyeStyle = (genes.eyeStyle + timeStyle + next_rand(&state)) % 4;
    genes.hornStyle = (genes.hornStyle + traits.contrast / 64 + next_rand(&state)) % 4;
    genes.tailStyle = (genes.tailStyle + shotCount + traits.saturation / 64 + next_rand(&state)) % 4;
    genes.auraPattern = (genes.auraPattern + traits.centerDelta / 30 + timeStyle + next_rand(&state)) % 4;
    genes.patternDensity = clamp_u8(genes.patternDensity + traits.centerDelta / 45 + next_rand(&state) % 3);

    uint16_t baseAccent = element_accent_color(genes.element);
    int32_t colorShift = static_cast<int32_t>((next_rand(&state) % 41)) - 18 +
                         traits.saturation / 12 + (traits.brightness - 128) / 16 -
                         traits.contrast / 28;
    genes.accentColor = tint_color(baseAccent, colorShift);
    genes.seed = state;

    save_pet_snapshot(genes, traits);
    return genes;
}

static int32_t pet_cx(const PetGenes& genes)
{
    return 160 + static_cast<int32_t>((genes.seed >> 8) % 19) - 9;
}

static int32_t pet_cy(const PetGenes& genes)
{
    return 118 + static_cast<int32_t>((genes.seed >> 13) % 13) - 6;
}

static void draw_leaf(int32_t x, int32_t y, int32_t s, uint16_t color)
{
    CoreS3.Display.fillEllipse(x, y, s, s / 2, color);
    CoreS3.Display.drawLine(x - s, y, x + s, y, rgb(35, 90, 35));
}

static void draw_pet_face(int32_t cx, int32_t cy, const PetGenes& genes, uint16_t eyeColor)
{
    int32_t eyeSize = 4 + (genes.eyeStyle % 3);
    int32_t eyeY = cy - 8 + (genes.eyeStyle == 3 ? 2 : 0);
    CoreS3.Display.fillCircle(cx - 18, eyeY, eyeSize, eyeColor);
    CoreS3.Display.fillCircle(cx + 18, eyeY, eyeSize, eyeColor);
    CoreS3.Display.fillCircle(cx - 16, eyeY - 2, 2, TFT_WHITE);
    CoreS3.Display.fillCircle(cx + 20, eyeY - 2, 2, TFT_WHITE);
    if (genes.mood == 3) {
        CoreS3.Display.drawLine(cx - 28, eyeY - 8, cx - 10, eyeY - 12, eyeColor);
        CoreS3.Display.drawLine(cx + 10, eyeY - 12, cx + 28, eyeY - 8, eyeColor);
    }
    CoreS3.Display.drawLine(cx - 7, cy + 12, cx, cy + 17, eyeColor);
    CoreS3.Display.drawLine(cx + 7, cy + 12, cx, cy + 17, eyeColor);
}

static void draw_pet_background(ElementType element, const PetGenes& genes)
{
    uint16_t bg = TFT_BLACK;
    switch (element) {
    case kWood: bg = rgb(8, 43, 28); break;
    case kFire: bg = rgb(56, 15, 8); break;
    case kEarth: bg = rgb(49, 36, 19); break;
    case kMetal: bg = rgb(30, 34, 40); break;
    case kWater: bg = rgb(7, 28, 56); break;
    }
    CoreS3.Display.fillScreen(bg);

    uint16_t aura = genes.accentColor;
    for (int i = 0; i < 8; ++i) {
        int32_t x = (hash_mix(genes.seed + i * 17) % 310) + 5;
        int32_t y = 50 + (hash_mix(genes.seed + i * 23) % 125);
        int32_t s = 5 + ((genes.auraPattern + i) % 5) * 3;
        if (element == kFire) {
            CoreS3.Display.fillTriangle(x, y - s, x - s, y + s, x + s, y + s, aura);
        } else if (element == kWater) {
            CoreS3.Display.drawArc(x, y, s + 10, s + 4, 20, 160, aura);
        } else if (element == kMetal) {
            CoreS3.Display.drawCircle(x, y, s + 4, aura);
        } else if (element == kWood) {
            draw_leaf(x, y, s + 5, aura);
        } else {
            CoreS3.Display.fillRoundRect(x - s, y - s, s * 2, s * 2, 4, aura);
        }
    }
}

static void draw_pet_body(ElementType element, const PetGenes& genes)
{
    int32_t cx = pet_cx(genes);
    int32_t cy = pet_cy(genes);
    int32_t bodyW = 70 + genes.bodyScale / 3;
    int32_t bodyH = 42 + genes.bodyScale / 5;
    uint16_t body = element_body_color(element);
    uint16_t accent = genes.accentColor;
    uint16_t shade = tint_color(body, -35);

    if (element == kWood) {
        if (genes.species == 0) {
            CoreS3.Display.fillEllipse(cx, cy + 34, bodyW - 8, bodyH - 8, body);
            CoreS3.Display.fillCircle(cx, cy - 16, 34 + genes.bodyScale / 14, body);
            CoreS3.Display.fillTriangle(cx - 34, cy - 30, cx - 58, cy - 58, cx - 20, cy - 45, accent);
            CoreS3.Display.fillTriangle(cx + 34, cy - 30, cx + 58, cy - 58, cx + 20, cy - 45, accent);
            CoreS3.Display.fillRoundRect(cx - 42, cy + 56, 16, 32, 6, shade);
            CoreS3.Display.fillRoundRect(cx + 26, cy + 56, 16, 32, 6, shade);
        } else if (genes.species == 1) {
            CoreS3.Display.fillEllipse(cx + 4, cy + 30, bodyW + 18, bodyH - 14, body);
            CoreS3.Display.fillCircle(cx - 28, cy - 16, 29 + genes.bodyScale / 18, body);
            CoreS3.Display.fillTriangle(cx - 50, cy - 34, cx - 76, cy - 70, cx - 34, cy - 52, accent);
            CoreS3.Display.fillTriangle(cx - 16, cy - 38, cx + 8, cy - 74, cx + 2, cy - 42, accent);
            CoreS3.Display.fillEllipse(cx + bodyW / 2 + 26, cy + 22, 44, 15, accent);
            CoreS3.Display.fillEllipse(cx + bodyW / 2 + 44, cy + 10, 22, 10, tint_color(accent, 25));
        } else {
            CoreS3.Display.fillEllipse(cx, cy + 40, bodyW + 14, bodyH - 4, body);
            CoreS3.Display.fillEllipse(cx, cy + 24, bodyW / 2 + 18, bodyH / 2 + 8, accent);
            CoreS3.Display.drawCircle(cx, cy + 24, bodyW / 2 + 18, tint_color(accent, -35));
            CoreS3.Display.fillCircle(cx, cy - 18, 30 + genes.bodyScale / 18, body);
            CoreS3.Display.fillRoundRect(cx - 50, cy + 58, 28, 22, 7, shade);
            CoreS3.Display.fillRoundRect(cx + 22, cy + 58, 28, 22, 7, shade);
        }
    } else if (element == kFire) {
        if (genes.species == 0) {
            CoreS3.Display.fillEllipse(cx, cy + 34, bodyW - 4, bodyH - 10, body);
            CoreS3.Display.fillCircle(cx, cy - 18, 34 + genes.bodyScale / 16, body);
            CoreS3.Display.fillTriangle(cx - 28, cy - 42, cx - 46, cy - 80, cx - 8, cy - 50, accent);
            CoreS3.Display.fillTriangle(cx + 28, cy - 42, cx + 46, cy - 80, cx + 8, cy - 50, accent);
            CoreS3.Display.fillTriangle(cx + bodyW / 2, cy + 28, cx + bodyW / 2 + 44, cy - 10, cx + bodyW / 2 + 24, cy + 62, accent);
        } else if (genes.species == 1) {
            CoreS3.Display.fillEllipse(cx, cy + 16, bodyW - 18, bodyH - 20, body);
            CoreS3.Display.fillCircle(cx, cy - 22, 27 + genes.bodyScale / 22, body);
            CoreS3.Display.fillTriangle(cx - 34, cy + 8, cx - 110, cy - 14, cx - 56, cy + 62, accent);
            CoreS3.Display.fillTriangle(cx + 34, cy + 8, cx + 110, cy - 14, cx + 56, cy + 62, accent);
            CoreS3.Display.fillTriangle(cx, cy - 30, cx + 34, cy - 20, cx + 16, cy - 8, tint_color(accent, 30));
        } else {
            CoreS3.Display.fillEllipse(cx + 2, cy + 36, bodyW + 22, bodyH - 4, body);
            CoreS3.Display.fillCircle(cx - 28, cy - 12, 33 + genes.bodyScale / 18, body);
            CoreS3.Display.fillEllipse(cx + bodyW / 2 + 18, cy + 36, 34, 17, shade);
            CoreS3.Display.fillTriangle(cx + bodyW / 2 + 30, cy + 16, cx + bodyW / 2 + 70, cy - 24, cx + bodyW / 2 + 48, cy + 55, accent);
            CoreS3.Display.fillRoundRect(cx - 46, cy + 58, 24, 26, 6, shade);
            CoreS3.Display.fillRoundRect(cx + 20, cy + 58, 24, 26, 6, shade);
        }
    } else if (element == kEarth) {
        if (genes.species == 0) {
            CoreS3.Display.fillCircle(cx, cy + 24, 56 + genes.bodyScale / 10, body);
            CoreS3.Display.fillCircle(cx, cy - 34, 34 + genes.bodyScale / 18, body);
            CoreS3.Display.fillCircle(cx - 34, cy - 58, 16, shade);
            CoreS3.Display.fillCircle(cx + 34, cy - 58, 16, shade);
            CoreS3.Display.fillRoundRect(cx - 64, cy + 50, 38, 32, 8, shade);
            CoreS3.Display.fillRoundRect(cx + 26, cy + 50, 38, 32, 8, shade);
        } else if (genes.species == 1) {
            CoreS3.Display.fillEllipse(cx, cy + 42, bodyW + 28, bodyH - 10, body);
            CoreS3.Display.fillRoundRect(cx - 62, cy + 8, 124, 50, 10, accent);
            CoreS3.Display.drawRoundRect(cx - 62, cy + 8, 124, 50, 10, tint_color(accent, -35));
            CoreS3.Display.fillCircle(cx + 64, cy + 22, 24, body);
            CoreS3.Display.fillRoundRect(cx - 64, cy + 58, 32, 22, 6, shade);
            CoreS3.Display.fillRoundRect(cx + 22, cy + 58, 32, 22, 6, shade);
        } else {
            CoreS3.Display.fillRoundRect(cx - 44, cy - 34, 88, 104, 14, body);
            CoreS3.Display.fillRoundRect(cx - 30, cy - 62, 60, 38, 12, body);
            CoreS3.Display.fillRoundRect(cx - 82, cy + 0, 40, 28, 7, shade);
            CoreS3.Display.fillRoundRect(cx + 42, cy + 0, 40, 28, 7, shade);
            CoreS3.Display.fillRoundRect(cx - 42, cy + 62, 28, 24, 6, shade);
            CoreS3.Display.fillRoundRect(cx + 14, cy + 62, 28, 24, 6, shade);
        }
    } else if (element == kMetal) {
        if (genes.species == 0) {
            CoreS3.Display.fillTriangle(cx - 70, cy + 48, cx - 8, cy - 22, cx + 78, cy + 44, body);
            CoreS3.Display.fillCircle(cx - 28, cy - 26, 30 + genes.bodyScale / 18, body);
            CoreS3.Display.fillTriangle(cx - 52, cy - 45, cx - 82, cy - 84, cx - 32, cy - 62, accent);
            CoreS3.Display.fillTriangle(cx - 10, cy - 48, cx + 12, cy - 82, cx + 8, cy - 50, accent);
            CoreS3.Display.fillTriangle(cx + 76, cy + 34, cx + 116, cy + 12, cx + 96, cy + 58, accent);
        } else if (genes.species == 1) {
            CoreS3.Display.fillRoundRect(cx - 36, cy - 30, 72, 92, 10, body);
            CoreS3.Display.fillCircle(cx, cy - 44, 28 + genes.bodyScale / 22, body);
            CoreS3.Display.fillRoundRect(cx - 30, cy - 112, 18, 72, 7, accent);
            CoreS3.Display.fillRoundRect(cx + 12, cy - 112, 18, 72, 7, accent);
            CoreS3.Display.drawRoundRect(cx - 24, cy - 12, 48, 38, 6, tint_color(accent, 30));
            CoreS3.Display.fillRoundRect(cx - 52, cy + 54, 24, 32, 6, shade);
            CoreS3.Display.fillRoundRect(cx + 28, cy + 54, 24, 32, 6, shade);
        } else {
            CoreS3.Display.fillEllipse(cx, cy + 34, bodyW + 20, bodyH - 8, body);
            CoreS3.Display.fillCircle(cx - 30, cy - 16, 32 + genes.bodyScale / 18, body);
            CoreS3.Display.fillTriangle(cx - 50, cy - 36, cx - 78, cy - 70, cx - 30, cy - 54, accent);
            CoreS3.Display.fillTriangle(cx + 8, cy - 38, cx + 32, cy - 74, cx + 24, cy - 42, accent);
            CoreS3.Display.fillTriangle(cx + 70, cy + 28, cx + 114, cy + 2, cx + 90, cy + 54, accent);
            CoreS3.Display.fillRoundRect(cx - 52, cy + 58, 24, 24, 5, shade);
            CoreS3.Display.fillRoundRect(cx + 26, cy + 58, 24, 24, 5, shade);
        }
    } else {
        if (genes.species == 0) {
            CoreS3.Display.fillEllipse(cx, cy + 34, bodyW - 4, bodyH - 10, body);
            CoreS3.Display.fillCircle(cx - 18, cy - 16, 30 + genes.bodyScale / 18, body);
            CoreS3.Display.fillEllipse(cx + bodyW / 2 + 22, cy + 32, 34, 17, accent);
            CoreS3.Display.fillTriangle(cx + bodyW / 2 + 44, cy + 32, cx + bodyW / 2 + 84, cy + 8, cx + bodyW / 2 + 84, cy + 58, accent);
            CoreS3.Display.fillEllipse(cx - 42, cy + 54, 20, 12, tint_color(accent, 20));
        } else if (genes.species == 1) {
            CoreS3.Display.fillCircle(cx - 44, cy + 28, 28, body);
            CoreS3.Display.fillCircle(cx - 10, cy + 4, 31, body);
            CoreS3.Display.fillCircle(cx + 28, cy + 22, 29, body);
            CoreS3.Display.fillCircle(cx + 58, cy + 50, 25, body);
            CoreS3.Display.fillCircle(cx - 18, cy - 34, 28 + genes.bodyScale / 20, body);
            CoreS3.Display.fillTriangle(cx + 80, cy + 48, cx + 118, cy + 20, cx + 106, cy + 72, accent);
        } else {
            CoreS3.Display.fillCircle(cx, cy + 22, 50 + genes.bodyScale / 12, body);
            CoreS3.Display.fillEllipse(cx - 50, cy + 20, 24, 45, accent);
            CoreS3.Display.fillEllipse(cx + 50, cy + 20, 24, 45, accent);
            CoreS3.Display.fillTriangle(cx + 50, cy + 18, cx + 104, cy - 8, cx + 104, cy + 54, tint_color(accent, 10));
            CoreS3.Display.fillCircle(cx - 18, cy - 26, 18, tint_color(body, 25));
        }
    }
}

static void draw_pet_features(ElementType element, const PetGenes& genes)
{
    int32_t cx = pet_cx(genes);
    int32_t cy = pet_cy(genes);
    uint16_t accent = genes.accentColor;
    uint16_t dark = rgb(18, 20, 24);
    int32_t spike = 18 + genes.hornStyle * 7;

    if (element == kWood) {
        if (genes.species == 0) {
            CoreS3.Display.drawLine(cx - 22, cy - 48, cx - 50, cy - 80, accent);
            CoreS3.Display.drawLine(cx + 22, cy - 48, cx + 50, cy - 80, accent);
            draw_leaf(cx - 55, cy - 82, 14 + genes.hornStyle * 2, accent);
            draw_leaf(cx + 55, cy - 82, 14 + genes.hornStyle * 2, accent);
            CoreS3.Display.drawLine(cx - 42, cy + 26, cx + 42, cy + 18, tint_color(accent, -30));
            CoreS3.Display.drawLine(cx - 32, cy + 42, cx + 32, cy + 34, tint_color(accent, -30));
        } else if (genes.species == 1) {
            for (int i = 0; i < 3; ++i) {
                CoreS3.Display.drawArc(cx - 56 + i * 18, cy + 60, 26 + i * 5, 14 + i * 3, 190, 330, accent);
            }
            draw_leaf(cx + 70, cy + 16, 17, accent);
            CoreS3.Display.drawLine(cx + 50, cy + 22, cx + 92, cy - 4, tint_color(accent, 30));
            CoreS3.Display.drawLine(cx - 52, cy - 6, cx - 78, cy - 20, dark);
        } else {
            draw_leaf(cx - 30, cy - 54, 18, accent);
            draw_leaf(cx + 28, cy - 54, 18, accent);
            CoreS3.Display.fillCircle(cx, cy + 28, 7, tint_color(accent, 30));
            for (int i = 0; i < 4; ++i) {
                CoreS3.Display.drawArc(cx, cy + 28, 32 + i * 9, 18 + i * 5, 210, 330, tint_color(accent, -25));
            }
        }
        for (int i = 0; i < 3; ++i) {
            int32_t x = cx - 24 + i * 24;
            CoreS3.Display.drawLine(x, cy - 4, x + 3, cy + 46, tint_color(accent, -35));
        }
    } else if (element == kFire) {
        CoreS3.Display.fillTriangle(cx - 18, cy - 48, cx, cy - 86 - spike / 2, cx + 18, cy - 48, accent);
        if (genes.species == 1) {
            CoreS3.Display.fillTriangle(cx - 50, cy + 18, cx - 100, cy - 8, cx - 58, cy + 56, accent);
            CoreS3.Display.fillTriangle(cx + 50, cy + 18, cx + 100, cy - 8, cx + 58, cy + 56, accent);
            CoreS3.Display.drawLine(cx - 86, cy + 6, cx - 48, cy + 30, tint_color(accent, 45));
            CoreS3.Display.drawLine(cx + 86, cy + 6, cx + 48, cy + 30, tint_color(accent, 45));
        } else {
            CoreS3.Display.fillTriangle(cx + 52, cy + 32, cx + 94, cy - 4, cx + 72, cy + 66, accent);
            if (genes.species == 0) {
                CoreS3.Display.drawLine(cx - 34, cy + 20, cx + 34, cy + 28, tint_color(accent, 40));
            } else {
                for (int i = 0; i < 3; ++i) {
                    CoreS3.Display.fillTriangle(cx - 28 + i * 28, cy - 54, cx - 16 + i * 28, cy - 86 - spike, cx - 4 + i * 28, cy - 54, accent);
                }
            }
        }
    } else if (element == kEarth) {
        for (int i = 0; i < 3 + genes.patternDensity / 3; ++i) {
            int32_t x = cx - 42 + i * 28;
            CoreS3.Display.fillRoundRect(x, cy - 58 + (i % 2) * 8, 24, 20, 5, accent);
        }
        if (genes.species == 0) {
            CoreS3.Display.fillRoundRect(cx - 58, cy + 58, 36, 24, 7, accent);
            CoreS3.Display.fillRoundRect(cx + 22, cy + 58, 36, 24, 7, accent);
        } else if (genes.species == 1) {
            for (int i = 0; i < 5; ++i) {
                CoreS3.Display.drawLine(cx - 52 + i * 26, cy + 12, cx - 36 + i * 20, cy + 52, tint_color(accent, -45));
            }
        } else {
            CoreS3.Display.drawRoundRect(cx - 26, cy - 12, 52, 44, 6, tint_color(accent, 20));
            CoreS3.Display.fillCircle(cx - 18, cy + 46, 5, accent);
            CoreS3.Display.fillCircle(cx + 18, cy + 46, 5, accent);
        }
    } else if (element == kMetal) {
        if (genes.species == 0) {
            CoreS3.Display.drawLine(cx - 70, cy + 50, cx + 70, cy + 50, accent);
            CoreS3.Display.drawLine(cx - 64, cy + 28, cx + 44, cy - 18, tint_color(accent, 25));
            CoreS3.Display.fillTriangle(cx - 43, cy - 28, cx - 76, cy - 68, cx - 24, cy - 54, accent);
            CoreS3.Display.fillTriangle(cx + 4, cy - 32, cx + 34, cy - 70, cx + 24, cy - 38, accent);
        } else if (genes.species == 1) {
            CoreS3.Display.drawRoundRect(cx - 34, cy - 28, 68, 48, 7, tint_color(accent, 35));
            CoreS3.Display.drawCircle(cx - 14, cy + 8, 8, accent);
            CoreS3.Display.drawCircle(cx + 14, cy + 8, 8, accent);
            CoreS3.Display.drawLine(cx - 26, cy - 80, cx - 4, cy - 42, tint_color(accent, 45));
            CoreS3.Display.drawLine(cx + 26, cy - 80, cx + 4, cy - 42, tint_color(accent, 45));
        } else {
            CoreS3.Display.drawCircle(cx, cy + 4, 58 + genes.hornStyle * 4, accent);
            for (int i = 0; i < 4; ++i) {
                CoreS3.Display.drawLine(cx - 44 + i * 25, cy + 0, cx - 28 + i * 25, cy + 42, tint_color(accent, -30));
            }
            CoreS3.Display.fillTriangle(cx - 43, cy - 28, cx - 76, cy - 68, cx - 24, cy - 54, accent);
            CoreS3.Display.fillTriangle(cx + 43, cy - 28, cx + 76, cy - 68, cx + 24, cy - 54, accent);
        }
        CoreS3.Display.drawLine(cx - 38, cy - 24, cx - 8, cy - 44, TFT_WHITE);
        CoreS3.Display.drawLine(cx + 8, cy - 44, cx + 38, cy - 24, TFT_WHITE);
        CoreS3.Display.drawCircle(cx, cy + 20, 22 + genes.tailStyle * 3, tint_color(accent, 25));
    } else {
        if (genes.species == 0) {
            CoreS3.Display.fillTriangle(cx - 54, cy + 26, cx - 100, cy - 4, cx - 100, cy + 62, accent);
            CoreS3.Display.fillTriangle(cx + 50, cy + 26, cx + 96, cy - 4, cx + 96, cy + 62, accent);
            CoreS3.Display.drawLine(cx - 42, cy - 8, cx - 70, cy - 22, dark);
        } else if (genes.species == 1) {
            CoreS3.Display.drawArc(cx - 8, cy + 18, 92, 48, 200, 340, accent);
            CoreS3.Display.drawArc(cx - 4, cy + 20, 76, 38, 200, 340, tint_color(accent, 35));
            CoreS3.Display.drawLine(cx + 22, cy - 48, cx + 56, cy - 82, accent);
            CoreS3.Display.fillCircle(cx + 62, cy - 86, 7, accent);
        } else {
            CoreS3.Display.fillTriangle(cx - 54, cy + 26, cx - 96, cy - 4, cx - 96, cy + 62, accent);
            CoreS3.Display.fillTriangle(cx + 50, cy + 26, cx + 96, cy - 4, cx + 96, cy + 62, accent);
            for (int i = 0; i < 5; ++i) {
                CoreS3.Display.drawCircle(cx - 62 + i * 31, cy - 34 + (i % 2) * 8, 8 + i % 3, tint_color(accent, 25));
            }
        }
        for (int i = 0; i < 4 + genes.patternDensity / 4; ++i) {
            CoreS3.Display.drawArc(48 + i * 58, 52 + (i % 2) * 10, 24, 15, 20, 160, accent);
        }
        CoreS3.Display.fillCircle(cx + 74, cy + 64, 7 + genes.tailStyle, tint_color(accent, 35));
        CoreS3.Display.fillTriangle(cx + 74, cy + 48, cx + 64, cy + 64, cx + 84, cy + 64, tint_color(accent, 35));
    }

    for (int i = 0; i < genes.patternDensity; ++i) {
        int32_t x = cx - 42 + (hash_mix(genes.seed + i * 41) % 84);
        int32_t y = cy - 12 + (hash_mix(genes.seed + i * 59) % 58);
        CoreS3.Display.fillCircle(x, y, 2 + (i % 3), tint_color(accent, 25));
    }

    draw_pet_face(cx, cy, genes, dark);
}

static void draw_pet_badge(const ImageTraits& traits, const RecognitionResult& recog, const PetGenes& genes)
{
    char line[96];
    uint16_t accent = genes.accentColor;

    CoreS3.Display.fillRoundRect(8, 8, 304, 42, 8, rgb(12, 12, 16));
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextColor(accent, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 14);
    CoreS3.Display.print(species_name(genes));
    CoreS3.Display.setCursor(16, 32);
    snprintf(line, sizeof(line), "%s  %s", element_name(genes.element), mood_name(genes.mood));
    CoreS3.Display.print(line);

    CoreS3.Display.fillRoundRect(6, 188, 308, 44, 8, rgb(20, 20, 24));
    CoreS3.Display.drawRoundRect(6, 188, 308, 44, 8, rgb(92, 92, 102));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(20, 20, 24));
    CoreS3.Display.setCursor(14, 194);
    snprintf(line, sizeof(line), "%s C:%u P:%u  %lums/%lums",
             recog.objectLabel, recog.confidence, recog.presenceScore,
             static_cast<unsigned long>(last_vision_preprocess_ms),
             static_cast<unsigned long>(last_vision_classify_ms));
    CoreS3.Display.print(line);
    CoreS3.Display.setCursor(14, 212);
    snprintf(line, sizeof(line), "RGB:%u,%u,%u S:%ld Seed:%08lx",
             traits.r, traits.g, traits.b,
             static_cast<long>(traits.saturation),
             static_cast<unsigned long>(genes.seed));
    CoreS3.Display.print(line);
}

static uint16_t pet_power(const PetGenes& genes)
{
    return 38 + genes.bodyScale / 3 + genes.patternDensity * 3 + genes.hornStyle * 7 + genes.species * 4;
}

static uint16_t pet_agility(const PetGenes& genes)
{
    return 35 + max<int32_t>(0, 140 - genes.bodyScale) / 3 + genes.tailStyle * 8 + genes.auraPattern * 4;
}

static uint16_t pet_spirit(const PetGenes& genes)
{
    return 36 + genes.mood * 6 + genes.eyeStyle * 6 + (genes.seed & 0x0f);
}

static uint32_t now_sec()
{
    return millis() / 1000UL;
}

static uint16_t level_xp_need(uint8_t level)
{
    return 40 + static_cast<uint16_t>(level) * 25;
}

static bool valid_bag_index(uint8_t index)
{
    return index < backpack.count && index < kMaxBackpackPets;
}

static uint8_t win_rate_percent(const SavedPet& pet)
{
    if (pet.battles == 0) {
        return 0;
    }
    return static_cast<uint8_t>(min<uint16_t>(100, (static_cast<uint32_t>(pet.wins) * 100UL) / pet.battles));
}

static SavedPet* selected_pet()
{
    if (!valid_bag_index(backpack.selected)) {
        return nullptr;
    }
    return &backpack.pets[backpack.selected];
}

static const SavedPet* selected_pet_const()
{
    if (!valid_bag_index(backpack.selected)) {
        return nullptr;
    }
    return &backpack.pets[backpack.selected];
}

static uint8_t active_pet_level()
{
    const SavedPet* pet = selected_pet_const();
    return pet == nullptr ? 1 : max<uint8_t>(1, pet->level);
}

static uint8_t active_pet_stage()
{
    const SavedPet* pet = selected_pet_const();
    return pet == nullptr ? 0 : pet->stage;
}

static void reset_backpack()
{
    memset(&backpack, 0, sizeof(backpack));
    backpack.magic = kBagMagic;
    backpack.version = kBagVersion;
}

static void save_backpack()
{
    prefs.putBytes("bag", &backpack, sizeof(backpack));
}

static void load_backpack()
{
    prefs.begin("wuxing", false);
    if (prefs.getBytesLength("bag") == sizeof(backpack)) {
        prefs.getBytes("bag", &backpack, sizeof(backpack));
    }
    if (backpack.magic != kBagMagic || backpack.version != kBagVersion || backpack.count > kMaxBackpackPets) {
        reset_backpack();
        save_backpack();
    }
    if (backpack.count == 0) {
        backpack.selected = 0;
        bag_cursor = 0;
    } else if (backpack.selected >= backpack.count) {
        backpack.selected = 0;
        bag_cursor = 0;
        save_backpack();
    } else {
        bag_cursor = backpack.selected;
    }
}

static bool normalize_pet_level(SavedPet& pet)
{
    bool changed = false;
    if (pet.level == 0) {
        pet.level = 1;
        changed = true;
    }
    while (pet.level < 30 && pet.xp >= level_xp_need(pet.level)) {
        pet.xp -= level_xp_need(pet.level);
        ++pet.level;
        changed = true;
    }
    uint8_t nextStage = (pet.level >= 12) ? 2 : ((pet.level >= 5) ? 1 : 0);
    if (nextStage != pet.stage) {
        pet.stage = nextStage;
        pet.genes.bodyScale = clamp_u8(pet.genes.bodyScale + 8);
        pet.genes.patternDensity = clamp_u8(pet.genes.patternDensity + 1);
        pet.genes.hornStyle = (pet.genes.hornStyle + 1) % 4;
        changed = true;
    }
    return changed;
}

static bool apply_growth(SavedPet& pet)
{
    uint32_t now = now_sec();
    if (pet.lastGrowthSec == 0 || pet.lastGrowthSec > now) {
        pet.lastGrowthSec = now;
        return true;
    }
    uint32_t intervals = (now - pet.lastGrowthSec) / kGrowthIntervalSec;
    if (intervals == 0) {
        return false;
    }
    intervals = min<uint32_t>(intervals, 10);
    pet.lastGrowthSec += intervals * kGrowthIntervalSec;
    pet.xp = min<uint16_t>(9999, pet.xp + static_cast<uint16_t>(intervals * 3));
    normalize_pet_level(pet);
    return true;
}

static void refresh_backpack_growth(bool force)
{
    if (!force && millis() - last_growth_check_ms < 5000) {
        return;
    }
    last_growth_check_ms = millis();
    bool changed = false;
    for (uint8_t i = 0; i < backpack.count; ++i) {
        changed |= apply_growth(backpack.pets[i]);
    }
    if (changed) {
        save_backpack();
        if (has_local_pet) {
            const SavedPet* pet = selected_pet_const();
            if (pet != nullptr) {
                local_pet = pet->genes;
            }
        }
    }
}

static BattlePetPacket make_battle_packet(const PetGenes& genes, uint32_t sequence)
{
    BattlePetPacket packet = {};
    packet.magic = kBattleMagic;
    packet.version = kBattleVersion;
    packet.element = genes.element;
    packet.species = genes.species;
    packet.level = active_pet_level();
    packet.mood = genes.mood;
    packet.bodyScale = genes.bodyScale;
    packet.eyeStyle = genes.eyeStyle;
    packet.hornStyle = genes.hornStyle;
    packet.tailStyle = genes.tailStyle;
    packet.auraPattern = genes.auraPattern;
    packet.patternDensity = genes.patternDensity;
    packet.accentColor = genes.accentColor;
    packet.seed = genes.seed;
    packet.deviceId = device_id;
    packet.seq = sequence;
    uint16_t growthBoost = active_pet_level() * 3 + active_pet_stage() * 8;
    packet.power = pet_power(genes) + growthBoost;
    packet.agility = pet_agility(genes) + growthBoost / 2;
    packet.spirit = pet_spirit(genes) + growthBoost / 2;
    return packet;
}

static ElementType packet_element(const BattlePetPacket& packet)
{
    return static_cast<ElementType>(packet.element % 5);
}

static int32_t element_advantage(ElementType attacker, ElementType defender)
{
    if ((attacker == kWood && defender == kEarth) ||
        (attacker == kEarth && defender == kWater) ||
        (attacker == kWater && defender == kFire) ||
        (attacker == kFire && defender == kMetal) ||
        (attacker == kMetal && defender == kWood)) {
        return 28;
    }
    if ((defender == kWood && attacker == kEarth) ||
        (defender == kEarth && attacker == kWater) ||
        (defender == kWater && attacker == kFire) ||
        (defender == kFire && attacker == kMetal) ||
        (defender == kMetal && attacker == kWood)) {
        return -28;
    }
    return 0;
}

static int32_t battle_score(const BattlePetPacket& self, const BattlePetPacket& other)
{
    int32_t luck = static_cast<int32_t>(hash_mix(self.seed ^ other.seed ^ self.deviceId ^ (other.deviceId << 1)) % 17) - 8;
    return self.power * 2 + self.agility + self.spirit + element_advantage(packet_element(self), packet_element(other)) + luck;
}

static uint16_t record_friendship_bonus(const BattlePetPacket& opponent)
{
    uint32_t now = now_sec();
    bool rematch = last_friend_peer_id == opponent.deviceId &&
                   last_friend_battle_sec != 0 &&
                   now >= last_friend_battle_sec &&
                   now - last_friend_battle_sec <= kRematchWindowSec;
    if (rematch) {
        friend_rematch_streak = min<uint8_t>(3, friend_rematch_streak + 1);
    } else {
        friend_rematch_streak = 0;
    }
    last_friend_peer_id = opponent.deviceId;
    last_friend_battle_sec = now;
    return rematch ? static_cast<uint16_t>(friend_rematch_streak * kRematchXpStep) : 0;
}

static uint16_t award_battle_xp(int32_t diff, uint16_t bonusXp)
{
    SavedPet* pet = selected_pet();
    if (pet == nullptr) {
        return 0;
    }
    uint16_t gained = (abs(diff) <= 6) ? 16 : ((diff > 0) ? 35 : 8);
    gained = min<uint16_t>(80, gained + bonusXp);
    pet->xp = min<uint16_t>(9999, pet->xp + gained);
    ++pet->battles;
    if (diff > 6) {
        ++pet->wins;
    }
    normalize_pet_level(*pet);
    local_pet = pet->genes;
    save_backpack();
    return gained;
}

static void draw_peer_waiting(const BattlePetPacket& packet)
{
    screen_mode = kScreenBattle;
    CoreS3.Display.fillScreen(rgb(10, 14, 22));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(10, 14, 22));
    CoreS3.Display.setCursor(16, 22);
    CoreS3.Display.print("Peer pet received");
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(10, 14, 22));
    CoreS3.Display.setCursor(16, 52);
    CoreS3.Display.printf("%s  Lv%u", species_name_by(packet_element(packet), packet.species), packet.level);
    CoreS3.Display.setCursor(16, 78);
    CoreS3.Display.printf("%s", element_name(packet_element(packet)));
    CoreS3.Display.setCursor(16, 122);
    CoreS3.Display.print("Create your pet to battle.");
    draw_action_footer("BAG", "IDLE", "PHOTO", TFT_CYAN);
    display_hold_until_ms = millis() + 3500;
}

static void draw_battle_clash(const BattlePetPacket& opponent)
{
    screen_mode = kScreenBattle;
    battle_runtime_state = kBattleStateBattling;
    BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);

    CoreS3.Display.fillScreen(rgb(9, 10, 15));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(9, 10, 15));
    CoreS3.Display.setCursor(18, 12);
    CoreS3.Display.print("BATTLE START");

    CoreS3.Display.fillRoundRect(10, 48, 145, 86, 8, rgb(24, 28, 36));
    CoreS3.Display.fillRoundRect(165, 48, 145, 86, 8, rgb(24, 28, 36));
    CoreS3.Display.drawRoundRect(10, 48, 145, 86, 8, element_accent_color(packet_element(mine)));
    CoreS3.Display.drawRoundRect(165, 48, 145, 86, 8, element_accent_color(packet_element(opponent)));

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(24, 28, 36));
    CoreS3.Display.setCursor(20, 60);
    CoreS3.Display.printf("YOU  Lv%u", mine.level);
    CoreS3.Display.setCursor(20, 86);
    CoreS3.Display.printf("%s", element_name(packet_element(mine)));
    CoreS3.Display.setCursor(176, 60);
    CoreS3.Display.printf("PEER Lv%u", opponent.level);
    CoreS3.Display.setCursor(176, 86);
    CoreS3.Display.printf("%s", element_name(packet_element(opponent)));

    CoreS3.Display.setTextColor(TFT_YELLOW, rgb(9, 10, 15));
    CoreS3.Display.setCursor(94, 144);
    CoreS3.Display.print("CLASH...");
    draw_action_footer("BAG", "IDLE", "PHOTO", TFT_CYAN);
    display_hold_until_ms = millis() + 2500;
    play_scene_sound(kSoundMatch);
}

static void draw_battle_result(const BattlePetPacket& opponent)
{
    screen_mode = kScreenBattle;
    battle_runtime_state = kBattleStateBattling;
    BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);
    int32_t myScore = battle_score(mine, opponent);
    int32_t peerScore = battle_score(opponent, mine);
    int32_t diff = myScore - peerScore;
    const char* result = (abs(diff) <= 6) ? "DRAW" : ((diff > 0) ? "YOU WIN" : "YOU LOSE");
    uint16_t resultColor = (abs(diff) <= 6) ? TFT_YELLOW : ((diff > 0) ? TFT_GREEN : TFT_RED);
    uint16_t friendBonus = record_friendship_bonus(opponent);
    uint16_t gainedXp = award_battle_xp(diff, friendBonus);
    mine = make_battle_packet(local_pet, local_pet_sequence);

    CoreS3.Display.fillScreen(rgb(9, 10, 15));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(resultColor, rgb(9, 10, 15));
    CoreS3.Display.setCursor(18, 12);
    CoreS3.Display.printf("BATTLE RESULT  %s", result);

    CoreS3.Display.fillRoundRect(10, 46, 145, 104, 8, rgb(24, 28, 36));
    CoreS3.Display.fillRoundRect(165, 46, 145, 104, 8, rgb(24, 28, 36));
    CoreS3.Display.drawRoundRect(10, 46, 145, 104, 8, element_accent_color(packet_element(mine)));
    CoreS3.Display.drawRoundRect(165, 46, 145, 104, 8, element_accent_color(packet_element(opponent)));

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(24, 28, 36));
    CoreS3.Display.setCursor(20, 56);
    CoreS3.Display.print("YOU");
    CoreS3.Display.setCursor(20, 76);
    CoreS3.Display.printf("%s  Lv%u", species_name_by(packet_element(mine), mine.species), mine.level);
    CoreS3.Display.setCursor(20, 96);
    CoreS3.Display.printf("%s", element_name(packet_element(mine)));
    CoreS3.Display.setCursor(20, 120);
    CoreS3.Display.printf("P:%u A:%u S:%u", mine.power, mine.agility, mine.spirit);
    CoreS3.Display.setCursor(20, 138);
    CoreS3.Display.printf("Score:%ld", static_cast<long>(myScore));

    CoreS3.Display.setCursor(176, 56);
    CoreS3.Display.print("PEER");
    CoreS3.Display.setCursor(176, 76);
    CoreS3.Display.printf("%s  Lv%u", species_name_by(packet_element(opponent), opponent.species), opponent.level);
    CoreS3.Display.setCursor(176, 96);
    CoreS3.Display.printf("%s", element_name(packet_element(opponent)));
    CoreS3.Display.setCursor(176, 120);
    CoreS3.Display.printf("P:%u A:%u S:%u", opponent.power, opponent.agility, opponent.spirit);
    CoreS3.Display.setCursor(176, 138);
    CoreS3.Display.printf("Score:%ld", static_cast<long>(peerScore));

    CoreS3.Display.fillRoundRect(10, 154, 300, 26, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(18, 160);
    if (friendBonus > 0) {
        CoreS3.Display.printf("XP+%u  Rematch +%u", gainedXp, friendBonus);
    } else {
        CoreS3.Display.printf("XP+%u  Rematch for bond", gainedXp);
    }
    draw_action_footer("BAG", "IDLE", "PHOTO", resultColor);

    display_hold_until_ms = millis() + 7000;
    play_scene_sound((diff > 6) ? kSoundWin : ((diff < -6) ? kSoundLose : kSoundDraw));
}

static void handle_opponent_packet(const BattlePetPacket& packet)
{
    if (screen_mode != kScreenMatch && screen_mode != kScreenBattle) {
        return;
    }
    if (battle_result_pending) {
        return;
    }
    if (!has_local_pet) {
        draw_peer_waiting(packet);
        return;
    }

    uint32_t battleKey = hash_mix(packet.deviceId ^ (packet.seq << 7) ^ (local_pet_sequence << 17) ^ local_pet.seed);
    if (battleKey == last_battle_key) {
        return;
    }
    last_battle_key = battleKey;
    pending_battle_packet = packet;
    battle_result_pending = true;
    battle_result_due_ms = millis() + kBattleClashMs;
    draw_battle_clash(packet);
}

static void resolve_pending_battle_result()
{
    if (!battle_result_pending) {
        return;
    }
    if (screen_mode != kScreenBattle) {
        battle_result_pending = false;
        return;
    }
    if (static_cast<int32_t>(millis() - battle_result_due_ms) < 0) {
        return;
    }

    BattlePetPacket packet = pending_battle_packet;
    battle_result_pending = false;
    draw_battle_result(packet);
}

static bool battle_ip_valid(const IPAddress& ip)
{
    return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

static uint8_t battle_ap_subnet()
{
    return static_cast<uint8_t>((device_id % 200UL) + 20UL);
}

static bool mac_equals(const uint8_t* left, const uint8_t* right)
{
    return memcmp(left, right, 6) == 0;
}

static uint32_t device_id_from_mac(const uint8_t* mac)
{
    return (static_cast<uint32_t>(mac[2]) << 24) |
           (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) |
           static_cast<uint32_t>(mac[5]);
}

static void build_battle_ssid_from_mac(char* output, size_t outputLen, const uint8_t* mac)
{
    snprintf(output, outputLen, "%s%02X%02X%02X", kBattleSsidPrefix, mac[3], mac[4], mac[5]);
}

static bool parse_hex_nibble(char c, uint8_t& out)
{
    if (c >= '0' && c <= '9') {
        out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    return false;
}

static bool parse_battle_ssid(const String& ssid, uint32_t& peerId)
{
    const size_t prefixLen = strlen(kBattleSsidPrefix);
    if (!ssid.startsWith(kBattleSsidPrefix)) {
        return false;
    }
    size_t suffixLen = ssid.length() - prefixLen;
    if (suffixLen != 6 && suffixLen != 8) {
        return false;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < suffixLen; ++i) {
        uint8_t nibble = 0;
        if (!parse_hex_nibble(ssid[prefixLen + i], nibble)) {
            return false;
        }
        value = (value << 4) | nibble;
    }
    if (value == 0 || value == device_id) {
        return false;
    }
    peerId = value;
    return true;
}

static const char* battle_role_name()
{
    return battle_link_role == kBattleRoleClient ? "CLIENT" : "HOST";
}

static const char* battle_state_name()
{
    switch (battle_runtime_state) {
    case kBattleStatePairing:
        return "PAIRING";
    case kBattleStateReady:
        return "READY";
    case kBattleStateBattling:
        return "BATTLING";
    case kBattleStateRetrying:
        return "RETRYING";
    case kBattleStateDiscovering:
    default:
        return "DISCOVERING";
    }
}

static const char* known_board_label()
{
    if (mac_equals(device_mac, kKnownCom8Mac)) {
        return "COM8";
    }
    if (mac_equals(device_mac, kKnownCom7Mac)) {
        return "COM7";
    }
    return "UNKNOWN";
}

static void log_battle_identity()
{
    Serial.printf("battle mac=%02X:%02X:%02X:%02X:%02X:%02X board=%s role=%s state=%s ap=%s udp=%u\n",
                  device_mac[0], device_mac[1], device_mac[2],
                  device_mac[3], device_mac[4], device_mac[5],
                  known_board_label(),
                  battle_role_name(),
                  battle_state_name(),
                  battle_ap_ssid,
                  kBattleUdpPort);
}

static void log_battle_runtime_status()
{
    if (screen_mode != kScreenMatch && screen_mode != kScreenBattle) {
        return;
    }
    if (millis() - last_battle_status_log_ms < kBattleStatusLogIntervalMs) {
        return;
    }
    last_battle_status_log_ms = millis();

    uint32_t peerAgeMs = last_peer_seen_ms == 0 ? 0 : millis() - last_peer_seen_ms;
    Serial.printf("battle status state=%s role=%s wifi=%d peer=%lu ip=%u.%u.%u.%u tx=%u rx=%u fail=%u peer_age_ms=%lu\n",
                  battle_state_name(),
                  battle_role_name(),
                  static_cast<int>(WiFi.status()),
                  static_cast<unsigned long>(battle_peer_id),
                  battle_peer_ip[0], battle_peer_ip[1], battle_peer_ip[2], battle_peer_ip[3],
                  battle_packets_sent,
                  battle_packets_seen,
                  battle_send_failures,
                  static_cast<unsigned long>(peerAgeMs));
}

static const char* battle_link_status()
{
    static char status[72];
    if (!comm_ok) {
        snprintf(status, sizeof(status), "LINK INIT FAILED");
        return status;
    }

    if (battle_runtime_state == kBattleStateRetrying) {
        snprintf(status, sizeof(status), "RETRYING...");
        return status;
    }
    if (battle_runtime_state == kBattleStateBattling) {
        snprintf(status, sizeof(status), "BATTLING");
        return status;
    }
    if (last_peer_seen_ms != 0 || battle_runtime_state == kBattleStateReady) {
        snprintf(status, sizeof(status), "CONNECTED");
        return status;
    }
    if (battle_runtime_state == kBattleStatePairing) {
        snprintf(status, sizeof(status), "PAIRING...");
        return status;
    }

    snprintf(status, sizeof(status), "MATCHING...");
    return status;
}

static void connect_to_battle_peer(const char* ssid, uint32_t peerId)
{
    if (battle_link_role == kBattleRoleClient && battle_peer_id == peerId && WiFi.status() == WL_CONNECTED) {
        return;
    }

    snprintf(battle_peer_ssid, sizeof(battle_peer_ssid), "%s", ssid);
    battle_peer_id = peerId;
    battle_peer_ip = IPAddress(0, 0, 0, 0);
    battle_link_role = kBattleRoleClient;
    battle_runtime_state = kBattleStatePairing;
    last_battle_connect_ms = millis();
    WiFi.disconnect(false, false);
    WiFi.begin(battle_peer_ssid);
}

static void finish_battle_scan(int networkCount)
{
    uint32_t bestPeerId = 0xffffffffUL;
    char bestSsid[20] = {};

    for (int i = 0; i < networkCount; ++i) {
        uint32_t peerId = 0;
        String ssid = WiFi.SSID(i);
        if (!parse_battle_ssid(ssid, peerId)) {
            continue;
        }
        if (peerId < device_id && peerId < bestPeerId) {
            bestPeerId = peerId;
            snprintf(bestSsid, sizeof(bestSsid), "%s", ssid.c_str());
        }
    }
    WiFi.scanDelete();

    if (bestPeerId != 0xffffffffUL) {
        connect_to_battle_peer(bestSsid, bestPeerId);
    } else if (battle_link_role != kBattleRoleClient || WiFi.status() != WL_CONNECTED) {
        battle_link_role = kBattleRoleHost;
        battle_peer_id = 0;
        battle_runtime_state = kBattleStateDiscovering;
    }
    battle_scan_running = false;
}

static void scan_for_battle_peer()
{
    if (battle_scan_running) {
        int networkCount = WiFi.scanComplete();
        if (networkCount == WIFI_SCAN_RUNNING) {
            return;
        }
        if (networkCount >= 0) {
            finish_battle_scan(networkCount);
        } else {
            WiFi.scanDelete();
            battle_scan_running = false;
        }
        return;
    }

    if (millis() - last_battle_scan_ms < kBattleScanIntervalMs) {
        return;
    }
    last_battle_scan_ms = millis();
    battle_runtime_state = kBattleStateDiscovering;

    int scanState = WiFi.scanNetworks(true, true);
    if (scanState == WIFI_SCAN_RUNNING) {
        battle_scan_running = true;
    } else if (scanState >= 0) {
        finish_battle_scan(scanState);
    } else {
        WiFi.scanDelete();
        battle_scan_running = false;
    }
}

static void maintain_battle_wifi_link()
{
    if (!comm_ok || (screen_mode != kScreenMatch && screen_mode != kScreenBattle)) {
        return;
    }

    if (battle_ip_valid(battle_peer_ip) && millis() - last_peer_seen_ms > kBattlePeerTimeoutMs) {
        battle_peer_ip = IPAddress(0, 0, 0, 0);
        last_peer_seen_ms = 0;
        battle_runtime_state = kBattleStateRetrying;
    }

    if (battle_link_role == kBattleRoleClient) {
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress gateway = WiFi.gatewayIP();
            if (battle_ip_valid(gateway)) {
                battle_peer_ip = gateway;
            }
            return;
        }
        if (battle_peer_ssid[0] != '\0' &&
            (last_battle_connect_ms == 0 || millis() - last_battle_connect_ms > kBattleConnectRetryMs)) {
            last_battle_connect_ms = millis();
            WiFi.disconnect(false, false);
            WiFi.begin(battle_peer_ssid);
        }
        return;
    }

    scan_for_battle_peer();
}

static void accept_pet_packet(const uint8_t* data, int len, const IPAddress& remoteIp)
{
    if (len != static_cast<int>(sizeof(BattlePetPacket))) {
        return;
    }

    BattlePetPacket packet = {};
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != kBattleMagic || packet.version != kBattleVersion || packet.deviceId == device_id) {
        return;
    }
    if (packet.element > kWater || packet.species > 2) {
        return;
    }

    battle_peer_id = packet.deviceId;
    battle_peer_ip = remoteIp;
    last_peer_seen_ms = millis();
    battle_runtime_state = (screen_mode == kScreenBattle) ? kBattleStateBattling : kBattleStateReady;
    last_opponent_packet = packet;
    ++battle_packets_seen;
    opponent_packet_pending = true;
}

static void receive_pet_packets()
{
    if (!battle_udp_started) {
        return;
    }

    int packetSize = battle_udp.parsePacket();
    while (packetSize > 0) {
        uint8_t buffer[sizeof(BattlePetPacket)] = {};
        int len = battle_udp.read(buffer, sizeof(buffer));
        while (battle_udp.available()) {
            battle_udp.read();
        }
        accept_pet_packet(buffer, len, battle_udp.remoteIP());
        packetSize = battle_udp.parsePacket();
    }
}

static bool init_pet_comms()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);

    WiFi.macAddress(device_mac);
    device_id = device_id_from_mac(device_mac);
    if (device_id == 0) {
        device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xffffffffUL);
    }
    build_battle_ssid_from_mac(battle_ap_ssid, sizeof(battle_ap_ssid), device_mac);

    IPAddress apIp(10, 23, battle_ap_subnet(), 1);
    IPAddress mask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, mask);
    battle_wifi_started = WiFi.softAP(battle_ap_ssid, nullptr, kBattleWifiChannel, false, 2);
    battle_udp_started = battle_udp.begin(kBattleUdpPort) == 1;

    battle_link_role = kBattleRoleHost;
    battle_runtime_state = kBattleStateDiscovering;
    battle_peer_id = 0;
    battle_peer_ssid[0] = '\0';
    return battle_wifi_started && battle_udp_started;
}

static void publish_local_pet()
{
    if (!comm_ok || !has_local_pet || !battle_udp_started) {
        return;
    }

    IPAddress targetIp(0, 0, 0, 0);
    if (battle_link_role == kBattleRoleClient && WiFi.status() == WL_CONNECTED) {
        targetIp = WiFi.gatewayIP();
        if (battle_ip_valid(targetIp)) {
            battle_peer_ip = targetIp;
        }
    } else if (battle_ip_valid(battle_peer_ip)) {
        targetIp = battle_peer_ip;
    }

    if (!battle_ip_valid(targetIp)) {
        last_pet_broadcast_ms = millis();
        return;
    }

    BattlePetPacket packet = make_battle_packet(local_pet, local_pet_sequence);
    bool ok = battle_udp.beginPacket(targetIp, kBattleUdpPort) == 1;
    if (ok) {
        ok = battle_udp.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) == sizeof(packet);
    }
    if (ok) {
        ok = battle_udp.endPacket() == 1;
    }

    if (ok) {
        ++battle_packets_sent;
    } else {
        ++battle_send_failures;
    }
    last_pet_broadcast_ms = millis();
}

static void service_pet_comms()
{
    receive_pet_packets();
    maintain_battle_wifi_link();
    log_battle_runtime_status();

    if (opponent_packet_pending) {
        BattlePetPacket packet = last_opponent_packet;
        opponent_packet_pending = false;
        handle_opponent_packet(packet);
    }

    if (comm_ok && has_local_pet && (screen_mode == kScreenMatch || screen_mode == kScreenBattle) && millis() - last_pet_broadcast_ms > 450) {
        publish_local_pet();
    }
}

static void refresh_match_status()
{
    if (screen_mode != kScreenMatch) {
        return;
    }
    static uint32_t last_match_status_ms = 0;
    if (millis() - last_match_status_ms < 700) {
        return;
    }
    last_match_status_ms = millis();

    CoreS3.Display.fillRoundRect(8, 136, 304, 44, 8, rgb(18, 22, 30));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 142);
    if (last_peer_seen_ms == 0) {
        CoreS3.Display.print(battle_link_status());
    } else {
        CoreS3.Display.printf("Peer seen %lus ago. Waiting result.", static_cast<unsigned long>((millis() - last_peer_seen_ms) / 1000));
    }
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 162);
    CoreS3.Display.print(last_peer_seen_ms == 0 ? "Move near another trainer." : "Opponent ready.");
}

static void draw_pet_scene(const PetGenes& genes)
{
    draw_pet_background(genes.element, genes);
    draw_pet_body(genes.element, genes);
    draw_pet_features(genes.element, genes);
}

static void restore_selected_pet()
{
    const SavedPet* pet = selected_pet_const();
    if (pet == nullptr) {
        has_local_pet = false;
        return;
    }
    local_pet = pet->genes;
    has_local_pet = true;
    local_pet_sequence = ++battle_sequence;
    last_battle_key = 0;
}

static void draw_idle_screen(const char* message, bool playSound)
{
    screen_mode = kScreenIdle;
    has_wild_pet = false;
    battle_result_pending = false;
    battle_runtime_state = kBattleStateDiscovering;
    battle_scan_running = false;
    WiFi.scanDelete();

    CoreS3.Display.fillScreen(rgb(8, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_GREEN, rgb(8, 10, 14));
    CoreS3.Display.setCursor(16, 16);
    CoreS3.Display.print("IDLE");

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 14));
    CoreS3.Display.setCursor(16, 48);
    CoreS3.Display.print(message == nullptr ? "Tap PHOTO to meet a wild pet." : message);

    const SavedPet* pet = selected_pet_const();
    CoreS3.Display.fillRoundRect(14, 84, 292, 72, 8, rgb(20, 24, 32));
    CoreS3.Display.drawRoundRect(14, 84, 292, 72, 8, pet == nullptr ? rgb(78, 82, 92) : pet->genes.accentColor);
    CoreS3.Display.setCursor(24, 96);
    if (pet == nullptr) {
        CoreS3.Display.print("No active pet");
        CoreS3.Display.setCursor(24, 122);
        CoreS3.Display.print("Open BAG after capturing one.");
    } else {
        CoreS3.Display.printf("Active: %s", species_name(pet->genes));
        CoreS3.Display.setCursor(24, 122);
        CoreS3.Display.printf("Lv%u  %s  Bag:%u/%u", pet->level, stage_name(pet->stage), backpack.count, kMaxBackpackPets);
    }

    draw_action_footer("BAG", "MATCH", "PHOTO", TFT_GREEN);
    display_hold_until_ms = millis() + 3500;
    if (playSound) {
        play_scene_sound(kSoundIdle);
    }
}

static void play_trainer_intro()
{
    CoreS3.Display.fillScreen(rgb(8, 10, 18));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_YELLOW, rgb(8, 10, 18));
    CoreS3.Display.setCursor(16, 20);
    CoreS3.Display.print("TRAINER START");
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 18));
    CoreS3.Display.setCursor(16, 56);
    CoreS3.Display.print("Adventure theme playing...");
    CoreS3.Display.setCursor(16, 86);
    CoreS3.Display.print("Get ready with your partner.");
    CoreS3.Display.fillRoundRect(16, 144, 288, 32, 8, rgb(20, 24, 36));
    CoreS3.Display.drawRoundRect(16, 144, 288, 32, 8, TFT_YELLOW);
    CoreS3.Display.setCursor(28, 154);
    CoreS3.Display.print("Original 12s trainer melody");
    play_scene_sound(kSoundTrainerIntro);
}

static void set_active_from_backpack(uint8_t index)
{
    if (!valid_bag_index(index)) {
        return;
    }
    backpack.selected = index;
    bag_cursor = index;
    local_pet = backpack.pets[index].genes;
    has_local_pet = true;
    local_pet_sequence = ++battle_sequence;
    last_battle_key = 0;
    save_backpack();
    publish_local_pet();
}

static void draw_match_screen(const char* message)
{
    screen_mode = kScreenMatch;
    has_wild_pet = false;
    battle_result_pending = false;
    refresh_backpack_growth(true);
    match_started_ms = millis();
    last_peer_seen_ms = 0;
    last_battle_scan_ms = millis() - kBattleScanIntervalMs;
    last_battle_connect_ms = 0;
    last_battle_status_log_ms = 0;
    battle_peer_ip = IPAddress(0, 0, 0, 0);
    battle_peer_id = 0;
    battle_peer_ssid[0] = '\0';
    battle_link_role = kBattleRoleHost;
    battle_runtime_state = kBattleStateDiscovering;
    battle_scan_running = false;
    WiFi.scanDelete();
    opponent_packet_pending = false;
    battle_packets_sent = 0;
    battle_packets_seen = 0;
    battle_send_failures = 0;
    last_pet_broadcast_ms = 0;

    const SavedPet* pet = selected_pet_const();
    if (pet == nullptr) {
        CoreS3.Display.fillScreen(rgb(8, 10, 14));
        CoreS3.Display.setFont(&fonts::Font2);
        CoreS3.Display.setTextDatum(top_left);
        CoreS3.Display.setTextColor(TFT_YELLOW, rgb(8, 10, 14));
        CoreS3.Display.setCursor(16, 24);
        CoreS3.Display.print("MATCH WAITING");
        CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 14));
        CoreS3.Display.setCursor(16, 62);
        CoreS3.Display.print("No active pet.");
        CoreS3.Display.setCursor(16, 92);
        CoreS3.Display.print("Open BAG and select one first.");
        draw_action_footer("BAG", "IDLE", "PHOTO", TFT_YELLOW);
        display_hold_until_ms = millis() + 3500;
        play_scene_sound(kSoundDraw);
        return;
    }

    local_pet = pet->genes;
    has_local_pet = true;
    publish_local_pet();

    draw_pet_scene(pet->genes);
    CoreS3.Display.fillRoundRect(8, 8, 304, 56, 8, rgb(12, 12, 16));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(pet->genes.accentColor, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 14);
    CoreS3.Display.print("MATCHING...");
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 34);
    CoreS3.Display.printf("%s  Lv%u", species_name(pet->genes), pet->level);

    CoreS3.Display.fillRoundRect(8, 136, 304, 44, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 142);
    CoreS3.Display.print(message == nullptr ? battle_link_status() : message);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 162);
    CoreS3.Display.print(last_peer_seen_ms == 0 ? "Move near another trainer." : "Opponent ready.");
    draw_action_footer("BAG", "IDLE", "PHOTO", pet->genes.accentColor);
    display_hold_until_ms = millis() + 6000;
    play_scene_sound(kSoundMatch);
    play_pet_sound(pet->genes, pet->level, pet->stage);
}

static void draw_release_confirm_screen()
{
    if (!valid_bag_index(bag_cursor)) {
        draw_bag_screen(nullptr);
        return;
    }

    screen_mode = kScreenReleaseConfirm;
    SavedPet& pet = backpack.pets[bag_cursor];
    draw_pet_scene(pet.genes);

    CoreS3.Display.fillRoundRect(8, 8, 304, 64, 8, rgb(32, 14, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_RED, rgb(32, 14, 14));
    CoreS3.Display.setCursor(16, 14);
    CoreS3.Display.print("CONFIRM RELEASE");
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(32, 14, 14));
    CoreS3.Display.setCursor(16, 36);
    CoreS3.Display.printf("%s  Lv%u", species_name(pet.genes), pet.level);
    CoreS3.Display.setCursor(16, 54);
    CoreS3.Display.print("This will remove it from this device.");

    CoreS3.Display.fillRoundRect(8, 160, 304, 22, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 164);
    CoreS3.Display.print("Left/right cancel. Middle confirms.");
    draw_action_footer("CANCEL", "CONFIRM", "CANCEL", TFT_RED);
    display_hold_until_ms = millis() + 7000;
    play_scene_sound(kSoundWarning);
}

static void release_stored_pet()
{
    if (!valid_bag_index(bag_cursor)) {
        draw_bag_screen(nullptr);
        return;
    }

    uint8_t removed = bag_cursor;
    bool removedActive = removed == backpack.selected;
    for (uint8_t i = removed; i + 1 < backpack.count; ++i) {
        backpack.pets[i] = backpack.pets[i + 1];
    }
    memset(&backpack.pets[kMaxBackpackPets - 1], 0, sizeof(backpack.pets[kMaxBackpackPets - 1]));
    --backpack.count;

    if (backpack.count == 0) {
        backpack.selected = 0;
        bag_cursor = 0;
        has_local_pet = false;
        save_backpack();
        play_scene_sound(kSoundRelease);
        draw_idle_screen("Released. Bag is empty.", false);
        return;
    }

    if (removedActive) {
        backpack.selected = min<uint8_t>(removed, backpack.count - 1);
    } else if (backpack.selected > removed) {
        --backpack.selected;
    }
    bag_cursor = min<uint8_t>(removed, backpack.count - 1);
    restore_selected_pet();
    save_backpack();
    publish_local_pet();
    play_scene_sound(kSoundRelease);
    draw_bag_screen("Released current pet");
}

static void draw_bag_screen(const char* message)
{
    screen_mode = kScreenBag;
    battle_result_pending = false;
    refresh_backpack_growth(true);

    if (backpack.count == 0) {
        CoreS3.Display.fillScreen(rgb(8, 10, 14));
        CoreS3.Display.setFont(&fonts::Font2);
        CoreS3.Display.setTextDatum(top_left);
        CoreS3.Display.setTextColor(TFT_CYAN, rgb(8, 10, 14));
        CoreS3.Display.setCursor(16, 28);
        CoreS3.Display.print("Backpack empty");
        CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 14));
        CoreS3.Display.setCursor(16, 64);
        CoreS3.Display.print("Tap PHOTO to meet a wild pet.");
        CoreS3.Display.setCursor(16, 96);
        CoreS3.Display.print("Right = photo, left/mid = idle.");
        draw_action_footer("IDLE", "IDLE", "PHOTO", TFT_CYAN);
        display_hold_until_ms = millis() + 2500;
        play_scene_sound(kSoundBag);
        return;
    }

    if (bag_cursor >= backpack.count) {
        bag_cursor = 0;
    }
    SavedPet& pet = backpack.pets[bag_cursor];
    draw_pet_scene(pet.genes);

    CoreS3.Display.fillRoundRect(8, 8, 304, 56, 8, rgb(12, 12, 16));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(pet.genes.accentColor, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 14);
    CoreS3.Display.printf("BAG %u/%u  %s", bag_cursor + 1, backpack.count,
                          (bag_cursor == backpack.selected) ? "ACTIVE" : "STORED");
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 34);
    CoreS3.Display.printf("%s  Lv%u  %s", species_name(pet.genes), pet.level, stage_name(pet.stage));
    CoreS3.Display.setCursor(180, 34);
    CoreS3.Display.printf("XP %u/%u", pet.xp, level_xp_need(pet.level));

    CoreS3.Display.fillRoundRect(8, 160, 304, 22, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 164);
    if (message != nullptr) {
        CoreS3.Display.print(message);
    } else {
        CoreS3.Display.printf("%s  %s  W:%u/%u %u%%", element_name(pet.genes.element),
                              mood_name(pet.genes.mood), pet.wins, pet.battles,
                              win_rate_percent(pet));
    }
    draw_action_footer("RELEASE", "SELECT", "NEXT", pet.genes.accentColor);
    display_hold_until_ms = millis() + 4500;
    play_scene_sound(kSoundBag);
    play_pet_sound(pet.genes, pet.level, pet.stage);
}

static void draw_wild_pet(const ImageTraits& traits, const RecognitionResult& recog)
{
    if (!recog.recognized || recog.confidence < kMinRecognitionConfidence || recog.classId == kObjectUnknown) {
        RecognitionResult fail = recog;
        fail.recognized = false;
        if (fail.failureReason == nullptr || fail.failureReason[0] == '\0') {
            fail.failureReason = "Recognition failed";
        }
        draw_capture_fail(traits, fail);
        return;
    }

    wild_pet = derive_pet_genes(traits, recog, millis() / 30000UL, shot_count);
    wild_traits = traits;
    wild_recognition = recog;
    has_wild_pet = true;
    screen_mode = kScreenWild;
    draw_pet_scene(wild_pet);
    draw_pet_badge(traits, recog, wild_pet);

    CoreS3.Display.fillRoundRect(8, 160, 304, 22, 8, rgb(20, 20, 24));
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextColor(TFT_YELLOW, rgb(20, 20, 24));
    CoreS3.Display.setCursor(14, 164);
    CoreS3.Display.printf("Wild %s  Bag:%u/%u", species_name(wild_pet), backpack.count, kMaxBackpackPets);
    draw_action_footer("CAPTURE", "IDLE", "RELEASE", wild_pet.accentColor);
    display_hold_until_ms = millis() + 6500;
    play_scene_sound(kSoundWild);
    play_pet_sound(wild_pet, 1, 0);
}

static void draw_capture_fail(const ImageTraits& traits, const RecognitionResult& recog)
{
    has_wild_pet = false;
    wild_pet = {};
    screen_mode = kScreenCaptureFail;
    wild_traits = traits;
    wild_recognition = recog;

    CoreS3.Display.fillScreen(rgb(10, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_RED, rgb(10, 10, 14));
    CoreS3.Display.setCursor(16, 18);
    CoreS3.Display.print("CAPTURE FAILED");

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(10, 10, 14));
    CoreS3.Display.setCursor(16, 52);
    CoreS3.Display.printf("%s", recog.failureReason == nullptr ? "No clear object" : recog.failureReason);
    CoreS3.Display.setCursor(16, 78);
    CoreS3.Display.printf("Presence:%u  Confidence:%u", recog.presenceScore, recog.confidence);
    CoreS3.Display.setCursor(16, 104);
    CoreS3.Display.printf("RGB:%u,%u,%u  S:%ld  D:%ld",
                          traits.r, traits.g, traits.b,
                          static_cast<long>(traits.saturation),
                          static_cast<long>(traits.centerDelta));
    CoreS3.Display.setCursor(16, 130);
    CoreS3.Display.printf("Vision:%lums + %lums  Heap:%lu",
                          static_cast<unsigned long>(last_vision_preprocess_ms),
                          static_cast<unsigned long>(last_vision_classify_ms),
                          static_cast<unsigned long>(ESP.getFreeHeap()));
    draw_action_footer("BAG", "IDLE", "RETRY", TFT_RED);
    display_hold_until_ms = millis() + 4500;
    play_scene_sound(kSoundWarning);
}

static void capture_wild_pet()
{
    if (!has_wild_pet) {
        return;
    }
    if (!wild_recognition.recognized ||
        wild_recognition.confidence < kMinRecognitionConfidence ||
        wild_recognition.classId == kObjectUnknown) {
        RecognitionResult fail = wild_recognition;
        fail.recognized = false;
        if (fail.failureReason == nullptr || fail.failureReason[0] == '\0') {
            fail.failureReason = "Recognition failed";
        }
        draw_capture_fail(wild_traits, fail);
        return;
    }
    if (backpack.count >= kMaxBackpackPets) {
        RecognitionResult fail = wild_recognition;
        fail.recognized = false;
        fail.failureReason = "Backpack full. Open BAG to release one.";
        draw_capture_fail(wild_traits, fail);
        return;
    }

    SavedPet& pet = backpack.pets[backpack.count];
    memset(&pet, 0, sizeof(pet));
    pet.genes = wild_pet;
    pet.level = 1;
    pet.stage = 0;
    pet.xp = kCaptureXp;
    normalize_pet_level(pet);
    pet.capturedAtSec = now_sec();
    pet.lastGrowthSec = pet.capturedAtSec;
    uint8_t index = backpack.count;
    ++backpack.count;
    has_wild_pet = false;
    set_active_from_backpack(index);
    play_scene_sound(kSoundCapture);
    draw_idle_screen("Captured. BAG to view, MATCH to fight.", false);
}

static void release_wild_pet()
{
    has_wild_pet = false;
    play_scene_sound(kSoundRelease);
    draw_idle_screen("Released. Tap PHOTO for another encounter.", false);
}

static uint8_t action_for_button(uint8_t button)
{
    uint8_t slot = min<uint8_t>(button, kButtonRight);

    if (screen_mode == kScreenWild) {
        if (slot == kButtonLeft) {
            return kActionCapturePet;
        }
        if (slot == kButtonMiddle) {
            return kActionBackToIdle;
        }
        return kActionReleasePet;
    }

    if (screen_mode == kScreenCaptureFail) {
        if (slot == kButtonLeft) {
            return kActionOpenBag;
        }
        if (slot == kButtonMiddle) {
            return kActionBackToIdle;
        }
        return kActionPhoto;
    }

    if (screen_mode == kScreenBag) {
        if (backpack.count == 0) {
            return (slot == kButtonRight) ? kActionPhoto : kActionBackToIdle;
        }
        if (slot == kButtonLeft) {
            return kActionReleaseStoredPet;
        }
        if (slot == kButtonMiddle) {
            return kActionSelectPet;
        }
        return kActionNextPet;
    }

    if (screen_mode == kScreenReleaseConfirm) {
        if (slot == kButtonMiddle) {
            return kActionConfirmReleaseStoredPet;
        }
        return kActionOpenBag;
    }

    if (screen_mode == kScreenMatch || screen_mode == kScreenBattle) {
        if (slot == kButtonLeft) {
            return kActionOpenBag;
        }
        if (slot == kButtonMiddle) {
            return kActionBackToIdle;
        }
        return kActionPhoto;
    }

    if (slot == kButtonLeft) {
        return kActionOpenBag;
    }
    if (slot == kButtonMiddle) {
        return kActionMatchBattle;
    }
    return kActionPhoto;
}

static uint8_t action_for_touch(int32_t x)
{
    int32_t w = max<int32_t>(1, CoreS3.Display.width());
    if (x < w / 3) {
        return action_for_button(kButtonLeft);
    }
    if (x < (w * 2) / 3) {
        return action_for_button(kButtonMiddle);
    }
    return action_for_button(kButtonRight);
}

static void handle_ui_action(uint8_t action)
{
    switch (action) {
    case kActionPhoto:
        take_photo("Photo trigger");
        break;
    case kActionOpenBag:
        has_wild_pet = false;
        draw_bag_screen(nullptr);
        break;
    case kActionBackToIdle:
        draw_idle_screen("Idle. Tap PHOTO, BAG, or MATCH.", true);
        break;
    case kActionMatchBattle:
        draw_match_screen(nullptr);
        break;
    case kActionPrevPet:
        if (backpack.count > 0) {
            bag_cursor = (bag_cursor + backpack.count - 1) % backpack.count;
            draw_bag_screen(nullptr);
        }
        break;
    case kActionNextPet:
        if (backpack.count > 0) {
            bag_cursor = (bag_cursor + 1) % backpack.count;
            draw_bag_screen(nullptr);
        }
        break;
    case kActionSelectPet:
        if (valid_bag_index(bag_cursor)) {
            set_active_from_backpack(bag_cursor);
            play_scene_sound(kSoundSelect);
            draw_idle_screen("Selected. Tap MATCH to wait for peers.", false);
        }
        break;
    case kActionReleaseStoredPet:
        draw_release_confirm_screen();
        break;
    case kActionConfirmReleaseStoredPet:
        release_stored_pet();
        break;
    case kActionCapturePet:
        capture_wild_pet();
        break;
    case kActionReleasePet:
        release_wild_pet();
        break;
    default:
        break;
    }
}

static void handle_touch(int32_t x)
{
    handle_ui_action(action_for_touch(x));
}

void handle_external_action(uint8_t action)
{
    handle_ui_action(action);
}

void handle_external_button(uint8_t button)
{
    handle_ui_action(action_for_button(button));
}

static void take_photo(const char* reason)
{
    if (millis() - last_shot_ms < kPhotoCooldownMs) {
        return;
    }
    if (!camera_ok) {
        draw_status("Camera init failed", "Press reset and retry", TFT_RED);
        return;
    }

    draw_status("Capturing...", reason, TFT_YELLOW);
    play_scene_sound(kSoundPhoto);

    if (CoreS3.Camera.get()) {
        draw_status("Analyzing image...", "Detecting subject and class", TFT_YELLOW);
        bool preprocessed = preprocess_frame_for_vision();
        ImageTraits traits = analyze_frame();
        RecognitionResult recog = {};
        if (preprocessed) {
            recog = recognize_object_local(traits);
        } else {
            recog.failureReason = "Preprocess failed";
            recog.objectLabel = object_class_label(kObjectUnknown);
            recog.materialLabel = material_label_for_class(kObjectUnknown);
            recog.elementHint = traits.element;
        }
        Serial.printf("vision pre=%lums cls=%lums presence=%u recognized=%u conf=%u class=%s heap=%lu reason=%s\n",
                      static_cast<unsigned long>(last_vision_preprocess_ms),
                      static_cast<unsigned long>(last_vision_classify_ms),
                      recog.presenceScore, recog.recognized ? 1 : 0,
                      recog.confidence, recog.objectLabel,
                      static_cast<unsigned long>(ESP.getFreeHeap()),
                      recog.failureReason == nullptr ? "" : recog.failureReason);
        if (recog.recognized) {
            draw_wild_pet(traits, recog);
        } else {
            draw_capture_fail(traits, recog);
        }
        CoreS3.Camera.free();
        ++shot_count;
    } else {
        ImageTraits traits = {};
        RecognitionResult recog = {};
        recog.failureReason = "Camera capture failed";
        recog.objectLabel = object_class_label(kObjectUnknown);
        recog.materialLabel = material_label_for_class(kObjectUnknown);
        draw_capture_fail(traits, recog);
    }

    last_shot_ms = millis();
}

void setup()
{
    Serial.begin(115200);
    auto cfg = M5.config();
    cfg.internal_mic = false;
    cfg.internal_spk = true;
    CoreS3.begin(cfg);

    CoreS3.Display.setRotation(1);
    CoreS3.Display.fillScreen(TFT_BLACK);
    comm_ok = init_pet_comms();
    log_battle_identity();
    draw_status("Wuxing pet demo", comm_ok ? "Battle link ready" : "Battle link init failed", comm_ok ? TFT_CYAN : TFT_YELLOW);
    CoreS3.delay(500);
    draw_status("Wuxing pet demo", "Init camera...", TFT_CYAN);

    camera_ok = CoreS3.Camera.begin();
    if (!camera_ok) {
        draw_status("Camera init failed", "Check CoreS3 camera", TFT_RED);
    }

    CoreS3.Speaker.setVolume(kSoundVolume);
    if (kAudioMuted) {
        CoreS3.Speaker.stop();
        CoreS3.Speaker.end();
    }
    load_backpack();
    refresh_backpack_growth(true);
    restore_selected_pet();
    if (has_local_pet) {
        publish_local_pet();
    }
    play_trainer_intro();
    draw_idle_screen("Ready. Tap BAG / MATCH / PHOTO.", false);
}

void loop()
{
    CoreS3.update();
    refresh_backpack_growth(false);

    if (CoreS3.Touch.getCount() && CoreS3.Touch.getDetail(0).wasClicked()) {
        auto detail = CoreS3.Touch.getDetail(0);
        handle_touch(detail.x);
        return;
    }

    service_pet_comms();
    resolve_pending_battle_result();
    refresh_match_status();

    static uint32_t last_ui_ms = 0;
    if (screen_mode == kScreenIdle && millis() > display_hold_until_ms && millis() - last_ui_ms > 600) {
        last_ui_ms = millis();
        char line2[64];
        const SavedPet* pet = selected_pet_const();
        if (pet == nullptr) {
            snprintf(line2, sizeof(line2), "Bag:%u/%u  Mid:MATCH", backpack.count, kMaxBackpackPets);
        } else {
            snprintf(line2, sizeof(line2), "Active Lv%u  Mid:MATCH", pet->level);
        }
        draw_status("IDLE  Left:BAG  Right:PHOTO", line2, TFT_GREEN);
    }
}
