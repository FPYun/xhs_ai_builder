#include <M5CoreS3.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <string.h>
#include "trainer_intro_audio.h"

#if defined(CORES3_ENABLE_ESP_SR)
#include <model_path.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#define CORES3_HAS_ESP_SR 1
#else
#define CORES3_HAS_ESP_SR 0
#endif

static constexpr size_t kSamples = 1024;
static constexpr uint32_t kSampleRate = 16000;
static constexpr uint32_t kCooldownMs = 1800;
static constexpr uint32_t kWarmupMs = 1600;
static constexpr int32_t kMinThreshold = 900;
static constexpr float kSrMinConfidence = 0.48f;
static constexpr uint8_t kMaxBackpackPets = 6;
static constexpr uint32_t kGrowthIntervalSec = 30;
static constexpr uint8_t kSoundVolume = 80;
static constexpr uint8_t kUiSoundVolume = 66;
static constexpr uint8_t kPetSoundVolume = 74;
static constexpr uint8_t kMusicSoundVolume = 88;
static constexpr uint16_t kSoundGapMs = 18;
static constexpr uint8_t kBattleWifiChannel = 6;
static constexpr uint16_t kBattleUdpPort = 42105;
static constexpr uint32_t kBattleScanIntervalMs = 4500;
static constexpr uint32_t kBattleConnectRetryMs = 5000;
static constexpr uint32_t kBattlePeerTimeoutMs = 12000;
static constexpr char kBattleSsidPrefix[] = "M5PET-";

static int16_t mic_buffer[kSamples];
static int32_t voice_threshold = 2200;
static uint32_t last_shot_ms = 0;
static uint32_t last_voice_command_ms = 0;
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
static uint32_t battle_peer_id = 0;
static uint32_t last_battle_scan_ms = 0;
static uint32_t last_battle_connect_ms = 0;
static IPAddress battle_peer_ip;
static WiFiUDP battle_udp;
static char battle_ap_ssid[20] = {};
static char battle_peer_ssid[20] = {};
static uint32_t display_hold_until_ms = 0;
static bool mic_active = false;
static int32_t last_voice_level = 0;
static Preferences prefs;

enum VoiceCommand : uint8_t {
    kVoiceNone = 0,
    kVoicePhoto = 1,
    kVoiceBag = 2,
    kVoiceBack = 3,
    kVoiceBattle = 4,
};

#if CORES3_HAS_ESP_SR
static srmodel_list_t* sr_models = nullptr;
static esp_mn_iface_t* sr_multinet = nullptr;
static model_iface_data_t* sr_model_data = nullptr;
static int sr_chunk_samples = 0;
#endif
static bool sr_ready = false;

enum ElementType : uint8_t {
    kWood = 0,
    kFire,
    kEarth,
    kMetal,
    kWater,
};

struct ImageTraits {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int32_t brightness;
    int32_t saturation;
    int32_t contrast;
    int32_t confidence;
    ElementType element;
    uint32_t seed;
};

struct RecognitionResult {
    const char* objectLabel;
    const char* materialLabel;
    uint8_t confidence;
};

struct PetHint {
    bool valid;
    ElementType preferredElement;
    uint8_t preferredSpecies;
    uint8_t styleBias;
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

enum ScreenMode : uint8_t {
    kScreenIdle = 0,
    kScreenWild,
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

enum BattleLinkRole : uint8_t {
    kBattleRoleHost = 0,
    kBattleRoleClient,
};

enum ButtonSlot : uint8_t {
    kButtonLeft = 0,
    kButtonMiddle,
    kButtonRight,
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
static constexpr uint32_t kBagMagic = 0x57354247UL; // W5BG
static constexpr uint8_t kBagVersion = 1;

static PetGenes local_pet = {};
static bool has_local_pet = false;
static PetGenes wild_pet = {};
static ImageTraits wild_traits = {};
static bool has_wild_pet = false;
static BackpackStorage backpack = {};
static ScreenMode screen_mode = kScreenIdle;
static uint8_t bag_cursor = 0;
static BattlePetPacket last_opponent_packet = {};
static volatile bool opponent_packet_pending = false;
static uint32_t battle_sequence = 0;
static uint32_t local_pet_sequence = 0;
static uint32_t last_battle_key = 0;
static uint32_t last_growth_check_ms = 0;
static BattleLinkRole battle_link_role = kBattleRoleHost;

static void take_photo(const char* reason);
static void handle_ui_action(uint8_t action);
static void draw_idle_screen(const char* message, bool playSound);
static void draw_bag_screen(const char* message);

void handle_external_action(uint8_t action);
void handle_external_button(uint8_t button);

static bool start_mic()
{
    if (mic_active) {
        return true;
    }
    mic_active = CoreS3.Mic.begin();
    return mic_active;
}

static void stop_mic()
{
    while (CoreS3.Mic.isRecording()) {
        CoreS3.delay(1);
    }
    if (mic_active) {
        CoreS3.Mic.end();
        mic_active = false;
    }
}

static int32_t compute_voice_level(const int16_t* samples, size_t count)
{
    if (count == 0) {
        return 0;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += samples[i];
    }
    const int32_t mean = sum / static_cast<int32_t>(count);

    int64_t abs_sum = 0;
    for (size_t i = 0; i < count; ++i) {
        abs_sum += abs(static_cast<int32_t>(samples[i]) - mean);
    }
    return abs_sum / static_cast<int32_t>(count);
}

static int32_t measure_voice_level()
{
    if (!mic_active) {
        return 0;
    }
    if (!CoreS3.Mic.record(mic_buffer, kSamples, kSampleRate)) {
        return 0;
    }

    last_voice_level = compute_voice_level(mic_buffer, kSamples);
    return last_voice_level;
}

static const char* voice_command_label(VoiceCommand command)
{
    switch (command) {
    case kVoicePhoto:
        return "pai zhao";
    case kVoiceBag:
        return "bei bao";
    case kVoiceBack:
        return "fan hui";
    case kVoiceBattle:
        return "dui zhan";
    default:
        return "none";
    }
}

static bool init_offline_command_recognition()
{
#if CORES3_HAS_ESP_SR
    sr_models = esp_srmodel_init("model");
    if (!sr_models) {
        return false;
    }

    char* model_name = esp_srmodel_filter(sr_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!model_name) {
        model_name = esp_srmodel_filter(sr_models, ESP_MN_PREFIX, nullptr);
    }
    if (!model_name) {
        return false;
    }

    sr_multinet = esp_mn_handle_from_name(model_name);
    if (!sr_multinet) {
        return false;
    }

    sr_model_data = sr_multinet->create(model_name, 6000);
    if (!sr_model_data) {
        return false;
    }

    sr_chunk_samples = sr_multinet->get_samp_chunksize(sr_model_data);
    if (sr_chunk_samples <= 0 || static_cast<size_t>(sr_chunk_samples) > kSamples) {
        return false;
    }

    if (esp_mn_commands_alloc(sr_multinet, sr_model_data) != ESP_OK) {
        return false;
    }
    esp_mn_commands_clear();
    esp_mn_commands_add(kVoicePhoto, "pai zhao");
    esp_mn_commands_add(kVoiceBag, "bei bao");
    esp_mn_commands_add(kVoiceBack, "fan hui");
    esp_mn_commands_add(kVoiceBattle, "dui zhan");
    esp_mn_error_t* command_errors = esp_mn_commands_update();
    if (command_errors != nullptr) {
        return false;
    }

    sr_multinet->set_det_threshold(sr_model_data, kSrMinConfidence);
    sr_multinet->clean(sr_model_data);
    return true;
#else
    return false;
#endif
}

static VoiceCommand recognize_voice_command_local()
{
#if CORES3_HAS_ESP_SR
    if (!sr_ready || !mic_active || !sr_multinet || !sr_model_data) {
        return kVoiceNone;
    }
    if (sr_chunk_samples <= 0 || static_cast<size_t>(sr_chunk_samples) > kSamples) {
        return kVoiceNone;
    }
    if (!CoreS3.Mic.record(mic_buffer, sr_chunk_samples, kSampleRate)) {
        return kVoiceNone;
    }

    last_voice_level = compute_voice_level(mic_buffer, sr_chunk_samples);
    const esp_mn_state_t state = sr_multinet->detect(sr_model_data, mic_buffer);
    if (state == ESP_MN_STATE_TIMEOUT) {
        sr_multinet->clean(sr_model_data);
        return kVoiceNone;
    }
    if (state != ESP_MN_STATE_DETECTED) {
        return kVoiceNone;
    }

    esp_mn_results_t* result = sr_multinet->get_results(sr_model_data);
    sr_multinet->clean(sr_model_data);
    if (!result || result->num <= 0 || result->prob[0] < kSrMinConfidence) {
        return kVoiceNone;
    }

    const int command_id = result->command_id[0];
    if (command_id >= kVoicePhoto && command_id <= kVoiceBattle) {
        return static_cast<VoiceCommand>(command_id);
    }
#endif
    return kVoiceNone;
}

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
    if (count == 0) {
        return;
    }

    stop_mic();
    if (!CoreS3.Speaker.begin()) {
        start_mic();
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
    start_mic();
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
    stop_mic();
    if (!CoreS3.Speaker.begin()) {
        start_mic();
        return;
    }
    CoreS3.Speaker.setVolume(kMusicSoundVolume);
    CoreS3.Speaker.playRaw(kTrainerIntroPcm, kTrainerIntroPcmLen, kTrainerIntroSampleRate, false, 1, -1, true);
    CoreS3.delay((kTrainerIntroPcmLen * 1000UL) / kTrainerIntroSampleRate + 80);
    CoreS3.Speaker.stop();
    CoreS3.Speaker.end();
    start_mic();
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
            uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
            uint8_t b = (c & 0x1F) * 255 / 31;
            int32_t max_c = max(max(r, g), b);
            int32_t min_c = min(min(r, g), b);
            int32_t chroma = max_c - min_c;
            int32_t luma = (r * 30 + g * 59 + b * 11) / 100;
            int32_t dx = abs(x - frame_w / 2);
            int32_t dy = abs(y - frame_h / 2);
            int32_t weight = 1;
            if (dx < frame_w / 3 && dy < frame_h / 3) {
                weight += 2;
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

    const size_t delta_step = max<size_t>(1, pixel_count / 900);
    size_t delta_count = 0;
    for (size_t i = 0; i < pixel_count; i += delta_step) {
        uint16_t c = pixels[i];
        uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (c & 0x1F) * 255 / 31;
        int32_t luma = (r * 30 + g * 59 + b * 11) / 100;
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

static RecognitionResult recognize_object_local(const ImageTraits& traits)
{
    RecognitionResult result = {};
    result.confidence = min<int32_t>(99, max<int32_t>(30, traits.confidence / 12));

    if (traits.saturation < 35 && traits.brightness > 150) {
        result.objectLabel = "bright object";
        result.materialLabel = "metal-like";
    } else if (traits.b > max(traits.r, traits.g) + 12) {
        result.objectLabel = "cool object";
        result.materialLabel = "water-like";
    } else if (traits.g > max(traits.r, traits.b) + 10) {
        result.objectLabel = "green object";
        result.materialLabel = "plant-like";
    } else if (traits.r > traits.b + 18 && traits.r > traits.g + 8) {
        result.objectLabel = "warm object";
        result.materialLabel = "flame-like";
    } else if (traits.r > traits.b && traits.g > traits.b) {
        result.objectLabel = "earthy object";
        result.materialLabel = "stone-like";
    } else {
        result.objectLabel = "mixed object";
        result.materialLabel = "unknown";
    }

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
    genes.element = remoteHint.valid ? remoteHint.preferredElement : traits.element;
    genes.species = remoteHint.valid ? (remoteHint.preferredSpecies % 3) : 0;
    genes.mood = (traits.brightness > 168) ? 0 : ((traits.saturation < 38) ? 1 : ((traits.contrast > 150) ? 3 : 2));
    genes.bodyScale = clamp_u8(82 + traits.saturation / 4 + traits.contrast / 12);
    genes.eyeStyle = (traits.brightness / 52 + recog.confidence / 33) % 4;
    genes.hornStyle = (traits.contrast / 48 + remoteHint.styleBias) % 4;
    genes.tailStyle = (traits.saturation / 42 + traits.r + traits.b) % 4;
    genes.auraPattern = (traits.contrast / 38 + traits.g + remoteHint.styleBias) % 4;
    genes.patternDensity = clamp_u8(2 + traits.saturation / 28 + traits.contrast / 55);
    genes.accentColor = element_accent_color(genes.element);
    genes.seed = traits.seed ^ (static_cast<uint32_t>(recog.confidence) << 24) ^ (remoteHint.styleBias << 16);
    return genes;
}

static PetGenes derive_pet_genes(ImageTraits traits, uint32_t timeBucket, uint32_t shotCount)
{
    RecognitionResult recog = recognize_object_local(traits);
    PetHint hint = {};
    fetch_remote_pet_hint(recog, &hint);

    PetGenes genes = merge_generation_inputs(traits, recog, hint);
    uint32_t state = hash_mix(traits.seed ^ (timeBucket * 0x45d9f3bUL) ^ (shotCount * 0x119de1f3UL));

    if (!hint.valid) {
        genes.species = next_rand(&state) % 3;
    }
    genes.bodyScale = clamp_u8(genes.bodyScale + static_cast<int32_t>(next_rand(&state) % 15) - 7);
    genes.eyeStyle = (genes.eyeStyle + next_rand(&state)) % 4;
    genes.hornStyle = (genes.hornStyle + next_rand(&state)) % 4;
    genes.tailStyle = (genes.tailStyle + next_rand(&state)) % 4;
    genes.auraPattern = (genes.auraPattern + next_rand(&state)) % 4;
    genes.patternDensity = clamp_u8(genes.patternDensity + next_rand(&state) % 4);

    uint16_t baseAccent = element_accent_color(genes.element);
    int32_t colorShift = static_cast<int32_t>((next_rand(&state) % 51)) - 20 + traits.saturation / 10;
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
    }

    for (int i = 0; i < genes.patternDensity; ++i) {
        int32_t x = cx - 42 + (hash_mix(genes.seed + i * 41) % 84);
        int32_t y = cy - 12 + (hash_mix(genes.seed + i * 59) % 58);
        CoreS3.Display.fillCircle(x, y, 2 + (i % 3), tint_color(accent, 25));
    }

    draw_pet_face(cx, cy, genes, dark);
}

static void draw_pet_badge(const ImageTraits& traits, const PetGenes& genes)
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
    snprintf(line, sizeof(line), "Seed:%08lx  Var:%u/%u/%u",
             static_cast<unsigned long>(genes.seed), genes.species + 1, genes.tailStyle, genes.auraPattern);
    CoreS3.Display.print(line);
    CoreS3.Display.setCursor(14, 212);
    snprintf(line, sizeof(line), "RGB:%u,%u,%u S:%ld Conf:%ld",
             traits.r, traits.g, traits.b,
             static_cast<long>(traits.saturation),
             static_cast<long>(traits.confidence));
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

static uint16_t award_battle_xp(int32_t diff)
{
    SavedPet* pet = selected_pet();
    if (pet == nullptr) {
        return 0;
    }
    uint16_t gained = (abs(diff) <= 6) ? 16 : ((diff > 0) ? 35 : 8);
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

static void draw_battle_result(const BattlePetPacket& opponent)
{
    screen_mode = kScreenBattle;
    BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);
    int32_t myScore = battle_score(mine, opponent);
    int32_t peerScore = battle_score(opponent, mine);
    int32_t diff = myScore - peerScore;
    const char* result = (abs(diff) <= 6) ? "DRAW" : ((diff > 0) ? "YOU WIN" : "YOU LOSE");
    uint16_t resultColor = (abs(diff) <= 6) ? TFT_YELLOW : ((diff > 0) ? TFT_GREEN : TFT_RED);
    uint16_t gainedXp = award_battle_xp(diff);
    mine = make_battle_packet(local_pet, local_pet_sequence);

    CoreS3.Display.fillScreen(rgb(9, 10, 15));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(resultColor, rgb(9, 10, 15));
    CoreS3.Display.setCursor(18, 12);
    CoreS3.Display.printf("BATTLE LINK  %s", result);

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
    CoreS3.Display.printf("Rule: Wood>Earth>Water>Fire>Metal");
    CoreS3.Display.setCursor(236, 160);
    CoreS3.Display.printf("XP+%u", gainedXp);
    draw_action_footer("BAG", "IDLE", "PHOTO", resultColor);

    display_hold_until_ms = millis() + 7000;
    play_scene_sound((diff > 6) ? kSoundWin : ((diff < -6) ? kSoundLose : kSoundDraw));
}

static void handle_opponent_packet(const BattlePetPacket& packet)
{
    if (screen_mode != kScreenMatch && screen_mode != kScreenBattle) {
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
    if (!ssid.startsWith(kBattleSsidPrefix) || ssid.length() != prefixLen + 8) {
        return false;
    }

    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
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

static const char* battle_link_status()
{
    static char status[72];
    if (!comm_ok) {
        snprintf(status, sizeof(status), "WiFi UDP init failed.");
        return status;
    }

    if (battle_link_role == kBattleRoleClient) {
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            snprintf(status, sizeof(status), "CLIENT connected %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        } else {
            snprintf(status, sizeof(status), "CLIENT connecting to %08lX", static_cast<unsigned long>(battle_peer_id));
        }
        return status;
    }

    if (battle_ip_valid(battle_peer_ip)) {
        snprintf(status, sizeof(status), "HOST peer %u.%u.%u.%u", battle_peer_ip[0], battle_peer_ip[1], battle_peer_ip[2], battle_peer_ip[3]);
    } else {
        snprintf(status, sizeof(status), "HOST AP %s STA:%u", battle_ap_ssid, WiFi.softAPgetStationNum());
    }
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
    last_battle_connect_ms = millis();
    WiFi.disconnect(false, false);
    WiFi.begin(battle_peer_ssid);
}

static void scan_for_battle_peer()
{
    if (millis() - last_battle_scan_ms < kBattleScanIntervalMs) {
        return;
    }
    last_battle_scan_ms = millis();

    int networkCount = WiFi.scanNetworks(false, true);
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
    }
}

static void maintain_battle_wifi_link()
{
    if (!comm_ok || (screen_mode != kScreenMatch && screen_mode != kScreenBattle)) {
        return;
    }

    if (battle_ip_valid(battle_peer_ip) && millis() - last_peer_seen_ms > kBattlePeerTimeoutMs) {
        battle_peer_ip = IPAddress(0, 0, 0, 0);
    }

    if (battle_link_role == kBattleRoleClient) {
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress gateway = WiFi.gatewayIP();
            if (battle_ip_valid(gateway)) {
                battle_peer_ip = gateway;
            }
            return;
        }
        if (battle_peer_ssid[0] != '\0' && millis() - last_battle_connect_ms > kBattleConnectRetryMs) {
            last_battle_connect_ms = millis();
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
    device_id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xffffffffUL);
    snprintf(battle_ap_ssid, sizeof(battle_ap_ssid), "%s%08lX", kBattleSsidPrefix, static_cast<unsigned long>(device_id));

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);

    IPAddress apIp(10, 23, battle_ap_subnet(), 1);
    IPAddress mask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, mask);
    battle_wifi_started = WiFi.softAP(battle_ap_ssid, nullptr, kBattleWifiChannel, false, 2);
    battle_udp_started = battle_udp.begin(kBattleUdpPort) == 1;
    battle_link_role = kBattleRoleHost;
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

    CoreS3.Display.fillRoundRect(8, 160, 304, 42, 8, rgb(18, 22, 30));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 164);
    if (last_peer_seen_ms == 0) {
        CoreS3.Display.print(battle_link_status());
    } else {
        CoreS3.Display.printf("Peer seen %lus ago. Waiting result.", static_cast<unsigned long>((millis() - last_peer_seen_ms) / 1000));
    }
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 184);
    CoreS3.Display.printf("%s UDP:%u TX:%u RX:%u F:%u", battle_role_name(), kBattleUdpPort, battle_packets_sent, battle_packets_seen, battle_send_failures);
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

    CoreS3.Display.fillScreen(rgb(8, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_GREEN, rgb(8, 10, 14));
    CoreS3.Display.setCursor(16, 16);
    CoreS3.Display.print("IDLE");

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 14));
    CoreS3.Display.setCursor(16, 48);
    CoreS3.Display.print(message == nullptr ? "Say PAIZHAO or tap PHOTO to meet wild pet." : message);

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
    refresh_backpack_growth(true);
    match_started_ms = millis();
    last_peer_seen_ms = 0;
    last_battle_scan_ms = 0;
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

    CoreS3.Display.fillRoundRect(8, 160, 304, 22, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 164);
    CoreS3.Display.print(message == nullptr ? battle_link_status() : message);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 184);
    CoreS3.Display.printf("%s UDP:%u TX:%u RX:%u F:%u", battle_role_name(), kBattleUdpPort, battle_packets_sent, battle_packets_seen, battle_send_failures);
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
        CoreS3.Display.print("Say PAIZHAO to meet a wild pet.");
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
        CoreS3.Display.printf("%s  %s  Win:%u/%u", element_name(pet.genes.element),
                              mood_name(pet.genes.mood), pet.wins, pet.battles);
    }
    draw_action_footer("RELEASE", "SELECT", "NEXT", pet.genes.accentColor);
    display_hold_until_ms = millis() + 4500;
    play_scene_sound(kSoundBag);
    play_pet_sound(pet.genes, pet.level, pet.stage);
}

static void draw_wild_pet(const ImageTraits& traits)
{
    wild_pet = derive_pet_genes(traits, millis() / 30000UL, shot_count);
    wild_traits = traits;
    has_wild_pet = true;
    screen_mode = kScreenWild;
    draw_pet_scene(wild_pet);
    draw_pet_badge(traits, wild_pet);

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

static void capture_wild_pet()
{
    if (!has_wild_pet) {
        return;
    }
    if (backpack.count >= kMaxBackpackPets) {
        draw_bag_screen("Bag full");
        return;
    }

    SavedPet& pet = backpack.pets[backpack.count];
    memset(&pet, 0, sizeof(pet));
    pet.genes = wild_pet;
    pet.level = 1;
    pet.stage = 0;
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
    draw_idle_screen("Released. Say PAIZHAO for another encounter.", false);
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
        draw_idle_screen("Idle. Say PAIZHAO or tap PHOTO.", true);
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
    if (!camera_ok) {
        draw_status("Camera init failed", "Press reset and retry", TFT_RED);
        return;
    }

    stop_mic();

    draw_status("Capturing...", reason, TFT_YELLOW);
    play_scene_sound(kSoundPhoto);
    stop_mic();

    if (CoreS3.Camera.get()) {
        draw_status("Analyzing image...", "Wild Wuxing pet appears", TFT_YELLOW);
        ImageTraits traits = analyze_frame();
        draw_wild_pet(traits);
        CoreS3.Camera.free();
        ++shot_count;
    } else {
        draw_status("Capture failed", "Say again or tap screen", TFT_RED);
    }

    start_mic();
    last_shot_ms = millis();
}

static void calibrate_noise()
{
    draw_status("Calibrating mic...", "Keep quiet", TFT_CYAN);

    uint32_t start = millis();
    int32_t max_level = 0;
    int32_t avg_level = 0;
    int32_t count = 0;

    while (millis() - start < kWarmupMs) {
        int32_t level = measure_voice_level();
        if (level > 0) {
            max_level = max(max_level, level);
            avg_level += level;
            ++count;
        }
        CoreS3.delay(10);
    }

    if (count > 0) {
        avg_level /= count;
    }
    voice_threshold = max(kMinThreshold, max(max_level * 2, avg_level * 4));

    char line2[64];
    snprintf(line2, sizeof(line2), "Threshold: %ld", static_cast<long>(voice_threshold));
    draw_status("Ready", line2, TFT_GREEN);
    CoreS3.delay(700);
    draw_status("Say PAIZHAO", "Left/Mid:BAG  Right:PHOTO", TFT_GREEN);
}

void setup()
{
    auto cfg = M5.config();
    cfg.internal_mic = true;
    cfg.internal_spk = true;
    CoreS3.begin(cfg);

    CoreS3.Display.setRotation(1);
    CoreS3.Display.fillScreen(TFT_BLACK);
    comm_ok = init_pet_comms();
    draw_status("Voice camera demo", comm_ok ? "WiFi UDP battle ready" : "WiFi UDP init failed", comm_ok ? TFT_CYAN : TFT_YELLOW);
    CoreS3.delay(500);
    draw_status("Voice camera demo", "Init camera...", TFT_CYAN);

    camera_ok = CoreS3.Camera.begin();
    if (!camera_ok) {
        draw_status("Camera init failed", "Check CoreS3 camera", TFT_RED);
    }

    CoreS3.Speaker.setVolume(kSoundVolume);
    CoreS3.Speaker.end();
    start_mic();
    sr_ready = init_offline_command_recognition();
    draw_status("Offline voice", sr_ready ? "ESP-SR commands ready" : "ESP-SR not ready; fallback", sr_ready ? TFT_GREEN : TFT_YELLOW);
    CoreS3.delay(700);
    load_backpack();
    refresh_backpack_growth(true);
    restore_selected_pet();
    if (has_local_pet) {
        publish_local_pet();
    }
    calibrate_noise();
    play_trainer_intro();
    draw_idle_screen(sr_ready ? "Say: pai zhao / bei bao / fan hui / dui zhan" : "Ready. Say loudly or tap PHOTO.", false);
}

void loop()
{
    CoreS3.update();
    refresh_backpack_growth(false);
    service_pet_comms();
    refresh_match_status();

    if (CoreS3.Touch.getCount() && CoreS3.Touch.getDetail(0).wasClicked()) {
        auto detail = CoreS3.Touch.getDetail(0);
        handle_touch(detail.x);
        return;
    }

    int32_t level = last_voice_level;
    static int trigger_hits = 0;

    if (sr_ready) {
        const VoiceCommand command = recognize_voice_command_local();
        level = last_voice_level;
        if (command != kVoiceNone && millis() - last_voice_command_ms > kCooldownMs) {
            last_voice_command_ms = millis();
            trigger_hits = 0;
            char message[80];
            snprintf(message, sizeof(message), "Voice: %s", voice_command_label(command));
            draw_status("Command recognized", message, TFT_GREEN);
            CoreS3.delay(220);
            switch (command) {
            case kVoicePhoto:
                handle_ui_action(kActionPhoto);
                break;
            case kVoiceBag:
                handle_ui_action(kActionOpenBag);
                break;
            case kVoiceBack:
                handle_ui_action(kActionBackToIdle);
                break;
            case kVoiceBattle:
                handle_ui_action(kActionMatchBattle);
                break;
            default:
                break;
            }
        }
    } else {
        level = measure_voice_level();
        if (level > voice_threshold) {
            ++trigger_hits;
        } else if (trigger_hits > 0) {
            --trigger_hits;
        }

        if (millis() - last_shot_ms > kCooldownMs && trigger_hits >= 2) {
            trigger_hits = 0;
            handle_ui_action(kActionPhoto);
        }
    }

    static uint32_t last_ui_ms = 0;
    if (screen_mode == kScreenIdle && millis() > display_hold_until_ms && millis() - last_ui_ms > 600) {
        last_ui_ms = millis();
        char line2[64];
        const SavedPet* pet = selected_pet_const();
        if (pet == nullptr) {
            snprintf(line2, sizeof(line2), "Mic:%ld  Bag:%u/%u  Mid:MATCH",
                     static_cast<long>(level), backpack.count, kMaxBackpackPets);
        } else {
            snprintf(line2, sizeof(line2), "Mic:%ld  Active Lv%u  Mid:MATCH",
                     static_cast<long>(level), pet->level);
        }
        draw_status("IDLE  Left:BAG  Right:PHOTO", line2, TFT_GREEN);
    }
}
