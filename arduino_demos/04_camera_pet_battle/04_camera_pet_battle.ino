#include <M5CoreS3.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
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
static constexpr uint32_t kBattleClashMs = 1800;
static constexpr uint32_t kRematchWindowSec = 180;
static constexpr uint16_t kRematchXpStep = 5;
static constexpr uint8_t kSoundVolume = 80;
static constexpr uint8_t kUiSoundVolume = 66;
static constexpr uint8_t kPetSoundVolume = 74;
static constexpr uint8_t kMusicSoundVolume = 88;
static constexpr uint16_t kSoundGapMs = 18;
static constexpr uint32_t kExternalSoundSampleRate = 22050;
static constexpr size_t kExternalSoundMaxBytes = 32768;
static constexpr bool kAudioMuted = false;
static constexpr uint8_t kBattleWifiChannel = 6;
static constexpr uint16_t kBattleUdpPort = 42105;
static constexpr uint32_t kBattleScanIntervalMs = 4500;
static constexpr uint32_t kBattleConnectRetryMs = 5000;
static constexpr uint32_t kBattlePeerTimeoutMs = 12000;
static constexpr uint32_t kBattleStatusLogIntervalMs = 2000;
static constexpr uint16_t kAppHttpPort = 80;
static constexpr uint8_t kAppLogCapacity = 20;
static constexpr uint8_t kLocalFriendSlots = 4;
static constexpr int kSdSpiCsPin = 4;
static constexpr int kSdSpiSckPin = 36;
static constexpr int kSdSpiMisoPin = 35;
static constexpr int kSdSpiMosiPin = 37;
static constexpr uint32_t kSdSpiFrequency = 25000000;
static constexpr uint8_t kSdFileListMaxDepth = 4;
static constexpr uint16_t kSdFileListMaxEntries = 80;
static constexpr char kBattleSsidPrefix[] = "M5PET-";
static const uint8_t kKnownCom8Mac[6] = {0x44, 0x1B, 0xF6, 0xE3, 0x9A, 0xFC};
static const uint8_t kKnownCom7Mac[6] = {0x44, 0x1B, 0xF6, 0xE3, 0x9B, 0x60};
static constexpr uint8_t kMinPresenceScore = 42;
static constexpr uint8_t kMinRecognitionConfidence = 58;
static constexpr uint8_t kWeakModelMinConfidence = 68;
static constexpr int32_t kFallbackMinClassMargin = 36;
static constexpr int32_t kFallbackMinClassScore = 120;
static constexpr uint8_t kCaptureBurstFrames = 3;
static constexpr bool kVisionSampleLoggingEnabled = true;
static constexpr uint8_t kSampleThumbSize = 32;
static constexpr uint16_t kSampleSummaryMaxRows = 240;
static constexpr uint8_t kSampleRecentMaxRows = 8;
static constexpr uint8_t kProximityI2cAddr = 0x23;
static constexpr uint32_t kProximityI2cFreq = 400000;
static constexpr uint16_t kObjectNearProximity = 24;
static constexpr uint32_t kExternalVisionHintTtlMs = 10000;
static constexpr uint8_t kExternalVisionMinConfidence = 70;
static constexpr uint8_t kExternalVisionMinPresence = 55;

static constexpr uint8_t kSdActionIdle = 0;
static constexpr uint8_t kSdActionWild = 1;
static constexpr uint8_t kSdActionBag = 2;
static constexpr uint8_t kSdActionBattle = 3;
static constexpr uint8_t kSdActionResult = 4;
static constexpr uint8_t kSdActionProfileCount = 5;

static constexpr uint8_t kExternalVisionNone = 0;
static constexpr uint8_t kExternalVisionEdge = 1;
static constexpr uint8_t kExternalVisionHusky = 2;

struct SdActionProfile {
    bool loaded;
    bool valid;
    int8_t bob;
    uint8_t sparkle;
    int8_t tilt;
};

static SdActionProfile sd_action_profiles[kSdActionProfileCount] = {};

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
static bool sd_card_present = false;
static uint8_t device_mac[6] = {};
static uint32_t battle_peer_id = 0;
static uint32_t last_battle_scan_ms = 0;
static uint32_t last_battle_connect_ms = 0;
static uint32_t last_battle_status_log_ms = 0;
static bool battle_scan_running = false;
static bool app_http_started = false;
static bool audio_muted = kAudioMuted;
static IPAddress battle_peer_ip;
static WiFiUDP battle_udp;
static WebServer app_http(kAppHttpPort);
static char battle_ap_ssid[20] = {};
static char battle_peer_ssid[20] = {};
static char app_log_lines[kAppLogCapacity][96] = {};
static uint8_t app_log_next = 0;
static uint8_t app_log_count = 0;
static uint32_t display_hold_until_ms = 0;
static int8_t vision_input[kVisionInputBytes] = {};
static uint32_t last_vision_preprocess_ms = 0;
static uint32_t last_vision_classify_ms = 0;
static uint16_t last_vision_best_distance = 0;
static uint16_t last_vision_margin = 0;
static uint16_t last_capture_proximity = 0;
static uint8_t last_capture_burst_index = 0;
static uint8_t last_capture_quality_score = 0;
static bool proximity_sensor_available = false;
static const char* last_vision_source = "none";
static bool sample_mode_enabled = false;
static char sample_mode_label[24] = "negative";
static char sample_mode_scene[24] = "unknown";
static Preferences prefs;
static Preferences friend_prefs;

struct ExternalVisionHint {
    bool valid;
    uint8_t classId;
    uint8_t confidence;
    uint8_t presence;
    uint8_t source;
    uint32_t expiresAtMs;
};

static ExternalVisionHint external_vision_hint = {};

static PetGenes local_pet = {};
static bool has_local_pet = false;
static PetGenes wild_pet = {};
static ImageTraits wild_traits = {};
static RecognitionResult wild_recognition = {};
static bool has_wild_pet = false;
static BackpackStorage backpack = {};

struct CaptureCandidate {
    bool valid;
    bool preprocessed;
    ImageTraits traits;
    RecognitionResult recog;
    SubjectPresence presence;
    uint16_t proximity;
    uint16_t bestDistance;
    uint16_t margin;
    uint32_t preprocessMs;
    uint32_t classifyMs;
    uint8_t burstIndex;
    uint8_t qualityScore;
    const char* source;
};

static CaptureCandidate last_capture_burst[kCaptureBurstFrames] = {};
static uint8_t last_capture_burst_count = 0;
static int8_t last_capture_best_burst_index = -1;

struct LocalFriendRecord {
    uint32_t peerId;
    uint32_t lastBattleSec;
    uint8_t score;
    uint8_t battleCount;
    uint8_t rematchStreak;
};

static LocalFriendRecord local_friends[kLocalFriendSlots] = {};
static uint8_t local_friend_count = 0;
static ScreenMode screen_mode = kScreenIdle;
static uint8_t bag_cursor = 0;
static BattlePetPacket last_opponent_packet = {};
static BattlePetPacket pending_battle_packet = {};
static volatile bool opponent_packet_pending = false;
static bool battle_result_pending = false;
static uint32_t battle_result_due_ms = 0;
static uint32_t battle_clash_started_ms = 0;
static uint8_t battle_clash_audio_phase = 255;
static bool battle_exit_pending = false;
static uint32_t battle_exit_due_ms = 0;
static bool battle_exit_visible = false;
static uint32_t battle_sequence = 0;
static uint32_t local_pet_sequence = 0;
static uint32_t last_battle_key = 0;
static uint32_t last_growth_check_ms = 0;
static uint32_t last_growth_event_sec = 0;
static uint32_t last_growth_sound_sec = 0;
static uint16_t last_growth_xp_gain = 0;
static uint8_t last_growth_pet_index = 0;
static uint8_t last_growth_level = 0;
static uint8_t last_growth_stage = 0;
static bool last_growth_level_up = false;
static bool last_growth_stage_up = false;
static uint32_t last_friend_peer_id = 0;
static uint32_t last_friend_battle_sec = 0;
static uint8_t friend_rematch_streak = 0;
static uint8_t friend_battle_count = 0;
static uint8_t friend_score = 0;
static bool last_friend_added = false;
static bool last_friend_bond_up = false;
static uint32_t last_friendship_hint_sound_peer = 0;
static uint8_t last_friendship_hint_sound_rank = 0;
static char last_friend_notice[32] = {};
static bool app_last_battle_result_valid = false;
static char app_last_battle_outcome[12] = {};
static uint32_t app_last_battle_id = 0;
static int32_t app_last_battle_my_score = 0;
static int32_t app_last_battle_peer_score = 0;
static int32_t app_last_battle_score_diff = 0;
static int32_t app_last_battle_power_diff = 0;
static int32_t app_last_battle_element_swing = 0;
static int32_t app_last_battle_spirit_diff = 0;
static uint16_t app_last_battle_xp = 0;
static uint16_t app_last_battle_friend_bonus = 0;
static char app_last_battle_my_skill[20] = {};
static char app_last_battle_peer_skill[20] = {};
static char app_last_opponent_species[24] = {};
static char app_last_opponent_element[8] = {};
static uint8_t app_last_opponent_level = 0;
static bool app_last_battle_level_up = false;
static bool app_last_battle_stage_up = false;
static constexpr uint8_t kBattleReplayCapacity = 6;
struct BattleReplayRecord {
    bool valid;
    uint32_t timeSec;
    uint32_t battleId;
    char outcome[12];
    int32_t myScore;
    int32_t peerScore;
    int32_t scoreDiff;
    int32_t powerDiff;
    int32_t elementSwing;
    int32_t spiritDiff;
    uint16_t xpGained;
    uint16_t friendBonus;
    char mySkill[20];
    char opponentSkill[20];
    char opponentSpecies[24];
    char opponentElement[8];
    uint8_t opponentLevel;
    bool levelUp;
    bool stageUp;
};
static BattleReplayRecord battle_replays[kBattleReplayCapacity] = {};
static uint8_t battle_replay_next = 0;
static uint8_t battle_replay_count = 0;
static BattleLinkRole battle_link_role = kBattleRoleHost;
enum BattleRuntimeState : uint8_t {
    kBattleStateDiscovering = 0,
    kBattleStatePairing,
    kBattleStateReady,
    kBattleStateBattling,
    kBattleStateRetrying,
};
static BattleRuntimeState battle_runtime_state = kBattleStateDiscovering;
static uint8_t last_match_sync_audio_step = 255;

static void take_photo(const char* reason);
static void append_app_log(const char* message);
static void detect_sd_card();
static void log_sd_storage_report();
static void start_app_http_server();
static void service_app_http();
static void service_serial_control();
static void append_sampling_json(String& out);

void handle_external_action(uint8_t action);
void handle_external_button(uint8_t button);

static void draw_status(const char* line1, const char* line2, uint32_t color)
{
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), 44, TFT_BLACK);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(color, TFT_BLACK);
    CoreS3.Display.setFont(&fonts::efontCN_16);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(6, 6);
    CoreS3.Display.print(line1);
    CoreS3.Display.setCursor(6, 24);
    CoreS3.Display.print(line2);
    CoreS3.Display.setFont(&fonts::Font2);
}

static void draw_game_title(const char* title, uint16_t color, uint16_t bg, int16_t x, int16_t y)
{
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setFont(&fonts::efontCN_16);
    CoreS3.Display.setTextSize(1);
    int16_t plateW = static_cast<int16_t>(CoreS3.Display.textWidth(title) + 24);
    if (plateW < 86) {
        plateW = 86;
    }
    int16_t maxW = static_cast<int16_t>(CoreS3.Display.width() - x - 8);
    if (plateW > maxW) {
        plateW = maxW;
    }
    uint16_t plateBg = CoreS3.Display.color565(18, 22, 30);
    CoreS3.Display.fillRoundRect(x - 8, y - 5, plateW, 26, 7, plateBg);
    CoreS3.Display.fillRoundRect(x - 8, y - 5, 5, 26, 3, color);
    CoreS3.Display.drawRoundRect(x - 8, y - 5, plateW, 26, 7, color);
    CoreS3.Display.setTextColor(color, plateBg);
    CoreS3.Display.setCursor(x, y);
    CoreS3.Display.print(title);
    CoreS3.Display.setTextColor(color, bg);
    CoreS3.Display.setFont(&fonts::Font2);
}

static void draw_meter(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t value, uint16_t maxValue, uint16_t color)
{
    uint16_t bg = rgb(32, 36, 44);
    CoreS3.Display.fillRoundRect(x, y, w, h, 4, bg);
    CoreS3.Display.drawRoundRect(x, y, w, h, 4, rgb(84, 88, 96));
    if (maxValue == 0) {
        return;
    }
    int16_t fillW = static_cast<int16_t>((static_cast<uint32_t>(w - 4) * min<uint16_t>(value, maxValue)) / maxValue);
    if (fillW > 0) {
        CoreS3.Display.fillRoundRect(x + 2, y + 2, fillW, h - 4, 3, color);
    }
}

static void draw_capture_quality_panel(int16_t x, int16_t y, uint8_t subjectScore, uint8_t recognitionScore)
{
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(x, y, 174, 30, 8, bg);
    CoreS3.Display.drawRoundRect(x, y, 174, 30, 8, TFT_RED);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 10, y + 4);
    CoreS3.Display.print("主体");
    CoreS3.Display.setCursor(x + 10, y + 17);
    CoreS3.Display.print("识别");
    CoreS3.Display.setTextColor(TFT_CYAN, bg);
    CoreS3.Display.setCursor(x + 142, y + 4);
    CoreS3.Display.printf("%u", subjectScore);
    CoreS3.Display.setCursor(x + 142, y + 17);
    CoreS3.Display.printf("%u", recognitionScore);
    draw_meter(x + 44, y + 8, 88, 6, subjectScore, 100, TFT_YELLOW);
    draw_meter(x + 44, y + 21, 88, 6, recognitionScore, 100, TFT_RED);
}

static void draw_delta_meter(int16_t x, int16_t y, int16_t w, int16_t h, int32_t diff, int32_t maxAbs)
{
    uint16_t bg = rgb(32, 36, 44);
    CoreS3.Display.fillRoundRect(x, y, w, h, 4, bg);
    CoreS3.Display.drawRoundRect(x, y, w, h, 4, rgb(84, 88, 96));
    if (maxAbs <= 0 || w <= 4 || h <= 4) {
        return;
    }

    int16_t innerW = w - 4;
    int16_t innerH = h - 4;
    int16_t centerX = x + 2 + innerW / 2;
    CoreS3.Display.drawFastVLine(centerX, y + 2, innerH, rgb(170, 176, 186));

    int32_t capped = diff;
    if (capped > maxAbs) {
        capped = maxAbs;
    } else if (capped < -maxAbs) {
        capped = -maxAbs;
    }
    uint32_t magnitude = static_cast<uint32_t>(capped < 0 ? -capped : capped);
    int16_t fillW = static_cast<int16_t>((static_cast<uint32_t>(innerW / 2) * magnitude) / maxAbs);
    if (fillW <= 0) {
        CoreS3.Display.fillCircle(centerX, y + h / 2, 2, TFT_CYAN);
    } else if (capped > 0) {
        CoreS3.Display.fillRect(centerX + 1, y + 2, fillW, innerH, TFT_GREEN);
    } else {
        CoreS3.Display.fillRect(centerX - fillW, y + 2, fillW, innerH, TFT_RED);
    }
}

static void draw_battle_score_plate(int16_t x, int16_t y, int16_t w, const char* label,
                                    int32_t myScore, int32_t peerScore,
                                    int32_t maxAbs, uint16_t accent)
{
    uint16_t bg = rgb(18, 22, 30);
    uint16_t diffColor = myScore > peerScore ? TFT_GREEN : (myScore < peerScore ? TFT_RED : TFT_CYAN);
    CoreS3.Display.fillRoundRect(x, y, w, 44, 8, bg);
    CoreS3.Display.drawRoundRect(x, y, w, 44, 8, accent);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_YELLOW, bg);
    CoreS3.Display.setCursor(x + 8, y + 5);
    CoreS3.Display.print(label);
    CoreS3.Display.setTextColor(diffColor, bg);
    CoreS3.Display.setCursor(x + 8, y + 20);
    CoreS3.Display.printf("ME %ld", static_cast<long>(myScore));
    CoreS3.Display.setCursor(x + w - 76, y + 20);
    CoreS3.Display.printf("RIVAL %ld", static_cast<long>(peerScore));
    draw_delta_meter(x + 8, y + 34, static_cast<int16_t>(w - 16), 7,
                     myScore - peerScore, maxAbs);
}

static void draw_battle_result_card(int16_t x, int16_t y, int16_t w, const char* result,
                                    int32_t diff, uint32_t battleId, uint16_t color)
{
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(x, y, w, 34, 8, bg);
    CoreS3.Display.drawRoundRect(x, y, w, 34, 8, color);
    CoreS3.Display.fillCircle(x + 11, y + 12, 5, color);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(color, bg);
    CoreS3.Display.setCursor(x + 22, y + 5);
    CoreS3.Display.print(result);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 76, y + 5);
    CoreS3.Display.printf("差%+ld", static_cast<long>(diff));
    CoreS3.Display.setTextColor(TFT_CYAN, bg);
    CoreS3.Display.setCursor(x + w - 62, y + 5);
    CoreS3.Display.printf("局%04lX", static_cast<unsigned long>(battleId & 0xffffUL));
    draw_delta_meter(x + 10, y + 24, static_cast<int16_t>(w - 20), 7, diff, 160);
}

static void draw_labeled_meter(int16_t x, int16_t y, const char* label, uint16_t value, uint16_t maxValue, uint16_t color)
{
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x, y);
    CoreS3.Display.print(label);
    CoreS3.Display.setCursor(x + 92, y);
    CoreS3.Display.printf("%u", value);
    draw_meter(x + 34, y + 4, 52, 8, value, maxValue, color);
}

static void draw_match_sync_meter(int16_t x, int16_t y, uint8_t step, uint16_t accent)
{
    uint16_t bg = rgb(18, 22, 30);
    uint16_t idle = rgb(72, 78, 90);
    int16_t xs[3] = { x, static_cast<int16_t>(x + 38), static_cast<int16_t>(x + 76) };
    const char* labels[3] = { "寻", "连", "战" };

    CoreS3.Display.drawLine(xs[0], y, xs[2], y, idle);
    for (uint8_t i = 0; i < 3; ++i) {
        uint16_t color = i <= step ? accent : idle;
        CoreS3.Display.fillCircle(xs[i], y, 5, color);
        CoreS3.Display.drawCircle(xs[i], y, 6, rgb(150, 156, 166));
        CoreS3.Display.setTextColor(color, bg);
        CoreS3.Display.setFont(&fonts::efontCN_16);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.drawString(labels[i], xs[i], y + 17);
    }
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
}

static void draw_battle_round_track(int16_t x, int16_t y, uint8_t phase, int32_t diff, uint16_t accent)
{
    uint16_t bg = rgb(9, 10, 15);
    uint16_t idle = rgb(72, 78, 90);
    const char* labels[3] = { "力", "克", "心" };
    if (phase > 2) {
        phase = 2;
    }

    CoreS3.Display.fillRoundRect(x - 8, y - 12, 112, 34, 6, bg);
    CoreS3.Display.drawLine(x, y, x + 78, y, idle);
    for (uint8_t i = 0; i < 3; ++i) {
        int16_t sx = x + static_cast<int16_t>(i) * 39;
        uint16_t color = idle;
        if (i < phase) {
            color = accent;
        } else if (i == phase) {
            color = diff > 0 ? TFT_GREEN : (diff < 0 ? TFT_RED : TFT_YELLOW);
        }
        CoreS3.Display.fillCircle(sx, y, 6, color);
        CoreS3.Display.drawCircle(sx, y, 7, rgb(150, 156, 166));
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setFont(&fonts::efontCN_16);
        CoreS3.Display.setTextColor(color, bg);
        CoreS3.Display.drawString(labels[i], sx, y + 16);
    }
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
}

static void draw_battle_impact(int16_t cx, int16_t cy, uint8_t phase, int32_t diff, uint16_t accent)
{
    if (phase > 2) {
        phase = 2;
    }
    uint16_t hitColor = diff > 0 ? TFT_GREEN : (diff < 0 ? TFT_RED : TFT_YELLOW);
    uint16_t phaseColor = phase == 0 ? accent : (phase == 1 ? TFT_CYAN : rgb(210, 120, 255));
    int16_t spread = 10 + static_cast<int16_t>(phase) * 5;

    CoreS3.Display.drawCircle(cx, cy, 23 + phase * 2, phaseColor);
    CoreS3.Display.drawCircle(cx, cy, 29 + phase * 2, hitColor);
    CoreS3.Display.drawLine(cx - 48, cy - spread, cx - 18, cy, hitColor);
    CoreS3.Display.drawLine(cx - 48, cy + spread, cx - 18, cy, phaseColor);
    CoreS3.Display.drawLine(cx + 48, cy - spread, cx + 18, cy, phaseColor);
    CoreS3.Display.drawLine(cx + 48, cy + spread, cx + 18, cy, hitColor);
    if (phase >= 1) {
        CoreS3.Display.drawLine(cx - 30, cy - 22, cx + 30, cy + 22, TFT_CYAN);
        CoreS3.Display.drawLine(cx - 30, cy + 22, cx + 30, cy - 22, phaseColor);
    }
    if (phase >= 2) {
        CoreS3.Display.drawLine(cx, cy - 36, cx, cy - 20, hitColor);
        CoreS3.Display.drawLine(cx, cy + 36, cx, cy + 20, hitColor);
        CoreS3.Display.fillCircle(cx - 38, cy, 3, phaseColor);
        CoreS3.Display.fillCircle(cx + 38, cy, 3, phaseColor);
    }

    CoreS3.Display.fillCircle(cx, cy, 16, TFT_YELLOW);
    CoreS3.Display.drawCircle(cx, cy, 18, TFT_WHITE);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(rgb(9, 10, 15), TFT_YELLOW);
    CoreS3.Display.drawString("VS", cx, cy + 1);
    CoreS3.Display.setTextDatum(top_left);
}

static void draw_battle_round_chip(int16_t x, int16_t y, const char* label, int32_t diff)
{
    uint16_t bg = rgb(30, 34, 42);
    uint16_t color = diff > 0 ? TFT_GREEN : (diff < 0 ? TFT_RED : TFT_YELLOW);
    CoreS3.Display.fillRoundRect(x, y, 88, 14, 5, bg);
    CoreS3.Display.drawRoundRect(x, y, 88, 14, 5, color);
    CoreS3.Display.fillCircle(x + 8, y + 7, 3, color);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 15, y + 1);
    const char* verdict = diff > 0 ? "胜" : (diff < 0 ? "负" : "平");
    CoreS3.Display.printf("%s%s%+ld", label, verdict, static_cast<long>(diff));
}

static void draw_battle_round_summary(int16_t x, int16_t y, int32_t powerDiff, int32_t elementSwing, int32_t spiritDiff)
{
    draw_battle_round_chip(x, y, "力", powerDiff);
    draw_battle_round_chip(x + 98, y, "克", elementSwing);
    draw_battle_round_chip(x + 196, y, "心", spiritDiff);
}

static void draw_capture_guide(int16_t x, int16_t y, uint16_t color)
{
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(x, y, 112, 76, 8, bg);
    CoreS3.Display.drawRoundRect(x, y, 112, 76, 8, color);
    CoreS3.Display.drawLine(x + 14, y + 16, x + 34, y + 16, color);
    CoreS3.Display.drawLine(x + 14, y + 16, x + 14, y + 36, color);
    CoreS3.Display.drawLine(x + 98, y + 16, x + 78, y + 16, color);
    CoreS3.Display.drawLine(x + 98, y + 16, x + 98, y + 36, color);
    CoreS3.Display.drawLine(x + 14, y + 60, x + 34, y + 60, color);
    CoreS3.Display.drawLine(x + 14, y + 60, x + 14, y + 40, color);
    CoreS3.Display.drawLine(x + 98, y + 60, x + 78, y + 60, color);
    CoreS3.Display.drawLine(x + 98, y + 60, x + 98, y + 40, color);
    CoreS3.Display.drawCircle(x + 56, y + 38, 13, TFT_CYAN);
    CoreS3.Display.drawFastHLine(x + 43, y + 38, 26, rgb(130, 220, 230));
    CoreS3.Display.drawFastVLine(x + 56, y + 25, 26, rgb(130, 220, 230));
    CoreS3.Display.setFont(&fonts::efontCN_16);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.drawString("置中", x + 56, y + 38);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
}

static void draw_bag_slot_bar(int16_t x, int16_t y, uint8_t count, uint8_t cursor, uint8_t selected, uint16_t accent)
{
    for (uint8_t i = 0; i < kMaxBackpackPets; ++i) {
        int16_t sx = x + static_cast<int16_t>(i) * 18;
        bool filled = i < count;
        uint16_t fill = filled ? (i == selected ? TFT_YELLOW : accent) : rgb(48, 52, 60);
        uint16_t border = (i == cursor) ? TFT_WHITE : rgb(90, 96, 108);
        CoreS3.Display.fillRoundRect(sx, y, 14, 8, 3, fill);
        CoreS3.Display.drawRoundRect(sx, y, 14, 8, 3, border);
        if (filled && i == selected) {
            CoreS3.Display.fillTriangle(sx + 4, y - 2, sx + 10, y - 2, sx + 7, y - 6, TFT_YELLOW);
        }
    }
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
    CoreS3.Display.setFont(&fonts::efontCN_16);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.drawString(left, 57, 209);
    CoreS3.Display.drawString(middle, 160, 209);
    CoreS3.Display.drawString(right, 263, 209);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
}

static void draw_battle_exit_choice_chip(int16_t x, const char* key, const char* hint, uint16_t color)
{
    uint16_t bg = CoreS3.Display.color565(28, 34, 44);
    CoreS3.Display.fillRoundRect(x, 190, 88, 15, 5, bg);
    CoreS3.Display.drawRoundRect(x, 190, 88, 15, 5, color);
    CoreS3.Display.fillCircle(x + 10, 197, 6, color);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(CoreS3.Display.color565(8, 10, 14), color);
    CoreS3.Display.drawString(key, x + 10, 197);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 21, 191);
    CoreS3.Display.print(hint);
}

static void draw_battle_exit_choices(const char* left, const char* middle, const char* right, uint16_t color)
{
    draw_battle_exit_choice_chip(16, "L", left, color);
    draw_battle_exit_choice_chip(116, "M", middle, color);
    draw_battle_exit_choice_chip(216, "R", right, color);
    CoreS3.Display.setFont(&fonts::Font2);
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
    kSoundBattleClash,
    kSoundFriend,
    kSoundWin,
    kSoundDraw,
    kSoundLose,
    kSoundWarning,
    kSoundTrainerIntro,
    kSoundLevelUp,
    kSoundBattleExit,
    kSoundCancel,
};

struct SoundAssetRoute {
    SoundCue cue;
    const char* sdPath;
    uint8_t volume;
};

struct SdAddonManifestRoute {
    const char* type;
    const char* path;
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
    { kSoundBattleClash, "/audio/battle/clash.raw", kSoundVolume },
    { kSoundFriend, "/audio/battle/friend.raw", kUiSoundVolume },
    { kSoundWin, "/audio/battle/win.raw", kUiSoundVolume },
    { kSoundDraw, "/audio/battle/draw.raw", kUiSoundVolume },
    { kSoundLose, "/audio/battle/lose.raw", kUiSoundVolume },
    { kSoundWarning, "/audio/ui/warning.raw", kUiSoundVolume },
    { kSoundTrainerIntro, "/audio/music/intro.raw", kMusicSoundVolume },
    { kSoundLevelUp, "/audio/ui/level_up.raw", kUiSoundVolume },
    { kSoundBattleExit, "/audio/battle/exit.raw", kUiSoundVolume },
    { kSoundCancel, "/audio/ui/cancel.raw", kUiSoundVolume },
};

static const SdAddonManifestRoute kSdAddonManifestRoutes[] = {
    { "skins", "/skins/manifest.csv" },
    { "actions", "/actions/manifest.csv" },
};

static bool play_external_sound_asset(const char* path, uint8_t volume)
{
    if (!sd_card_present || path == nullptr || path[0] == '\0') {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    size_t fileSize = static_cast<size_t>(file.size());
    if (fileSize == 0 || fileSize > kExternalSoundMaxBytes) {
        file.close();
        return false;
    }

    uint8_t* audioBuffer = static_cast<uint8_t*>(malloc(fileSize));
    if (audioBuffer == nullptr) {
        file.close();
        return false;
    }
    size_t bytesRead = file.read(audioBuffer, fileSize);
    file.close();
    if (bytesRead == 0) {
        free(audioBuffer);
        return false;
    }

    if (!CoreS3.Speaker.begin()) {
        free(audioBuffer);
        return false;
    }
    CoreS3.Speaker.setVolume(volume);
    bool played = CoreS3.Speaker.playRaw(audioBuffer, bytesRead, kExternalSoundSampleRate, false, 1, -1, true);
    if (played) {
        CoreS3.delay((bytesRead * 1000UL) / kExternalSoundSampleRate + 80);
    }
    CoreS3.Speaker.stop();
    CoreS3.Speaker.end();
    free(audioBuffer);
    return played;
}

static bool string_ends_with(const String& value, const char* suffix)
{
    if (suffix == nullptr) {
        return false;
    }
    const size_t valueLen = value.length();
    const size_t suffixLen = strlen(suffix);
    return valueLen >= suffixLen && value.endsWith(suffix);
}

static bool sd_audio_path_allowed(const String& path)
{
    if (!path.startsWith("/audio/ui/") &&
        !path.startsWith("/audio/battle/") &&
        !path.startsWith("/audio/music/")) {
        return false;
    }
    if (!string_ends_with(path, ".raw")) {
        return false;
    }
    return path.indexOf("..") < 0;
}

static bool ensure_sd_audio_dirs(const String& path)
{
    if (!sd_card_present) {
        return false;
    }
    SD.mkdir("/audio");
    if (path.startsWith("/audio/ui/")) {
        return SD.mkdir("/audio/ui") || SD.exists("/audio/ui");
    }
    if (path.startsWith("/audio/battle/")) {
        return SD.mkdir("/audio/battle") || SD.exists("/audio/battle");
    }
    if (path.startsWith("/audio/music/")) {
        return SD.mkdir("/audio/music") || SD.exists("/audio/music");
    }
    return false;
}

static bool receive_serial_sd_file(const String& path, uint32_t size)
{
    if (!sd_card_present) {
        Serial.println("serial error: sd missing");
        return false;
    }
    if (!sd_audio_path_allowed(path)) {
        Serial.println("serial error: SDPUT path must be /audio/ui/*.raw, /audio/battle/*.raw, or /audio/music/*.raw");
        return false;
    }
    if (size == 0 || size > kExternalSoundMaxBytes) {
        Serial.printf("serial error: SDPUT size must be 1..%u bytes\n", static_cast<unsigned>(kExternalSoundMaxBytes));
        return false;
    }
    if (!ensure_sd_audio_dirs(path)) {
        Serial.println("serial error: cannot create sd audio directory");
        return false;
    }

    String tmpPath = path + ".part";
    SD.remove(tmpPath.c_str());
    File file = SD.open(tmpPath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("serial error: cannot open sd temp file");
        return false;
    }

    Serial.printf("serial ready: SDPUT %s %lu\n", path.c_str(), static_cast<unsigned long>(size));
    Serial.flush();

    uint8_t buffer[256];
    uint32_t remaining = size;
    uint32_t written = 0;
    const uint32_t deadline = millis() + 15000UL;
    while (remaining > 0) {
        if (millis() > deadline) {
            file.close();
            SD.remove(tmpPath.c_str());
            Serial.printf("serial error: SDPUT timeout written=%lu expected=%lu\n",
                          static_cast<unsigned long>(written),
                          static_cast<unsigned long>(size));
            return false;
        }
        size_t want = min<size_t>(sizeof(buffer), remaining);
        int got = Serial.readBytes(buffer, want);
        if (got <= 0) {
            delay(1);
            continue;
        }
        size_t out = file.write(buffer, static_cast<size_t>(got));
        if (out != static_cast<size_t>(got)) {
            file.close();
            SD.remove(tmpPath.c_str());
            Serial.println("serial error: sd write failed");
            return false;
        }
        written += static_cast<uint32_t>(got);
        remaining -= static_cast<uint32_t>(got);
    }
    file.close();
    SD.remove(path.c_str());
    if (!SD.rename(tmpPath.c_str(), path.c_str())) {
        SD.remove(tmpPath.c_str());
        Serial.println("serial error: sd rename failed");
        return false;
    }
    Serial.printf("serial ok: SDPUT %s bytes=%lu\n", path.c_str(), static_cast<unsigned long>(written));
    append_app_log("serial sdput ok");
    return true;
}

static const char* sd_card_type_name(uint8_t type)
{
    switch (type) {
    case CARD_MMC:
        return "MMC";
    case CARD_SD:
        return "SDSC";
    case CARD_SDHC:
        return "SDHC";
    case CARD_NONE:
    default:
        return "none";
    }
}

static void detect_sd_card()
{
    SPI.begin(kSdSpiSckPin, kSdSpiMisoPin, kSdSpiMosiPin, kSdSpiCsPin);
    sd_card_present = SD.begin(kSdSpiCsPin, SPI, kSdSpiFrequency);
    if (sd_card_present && SD.cardType() == CARD_NONE) {
        sd_card_present = false;
    }
    Serial.printf("sd card=%s\n", sd_card_present ? "present" : "missing");
    append_app_log(sd_card_present ? "sd card present" : "sd card missing");
}

static void log_sd_file_list(File& dir, uint8_t depth, uint16_t& count, bool& truncated)
{
    while (count < kSdFileListMaxEntries) {
        File entry = dir.openNextFile();
        if (!entry) {
            return;
        }
        const bool isDir = entry.isDirectory();
        const char* name = entry.name();
        Serial.printf("sd file path=%s type=%s size=%llu\n",
                      name == nullptr ? "" : name,
                      isDir ? "dir" : "file",
                      static_cast<unsigned long long>(isDir ? 0 : entry.size()));
        ++count;
        if (isDir && depth + 1 < kSdFileListMaxDepth) {
            log_sd_file_list(entry, depth + 1, count, truncated);
        }
        entry.close();
        if (count >= kSdFileListMaxEntries) {
            truncated = true;
            return;
        }
    }
    truncated = true;
}

static void log_sd_audio_assets()
{
    for (size_t i = 0; i < sizeof(kSoundAssetRoutes) / sizeof(kSoundAssetRoutes[0]); ++i) {
        const char* path = kSoundAssetRoutes[i].sdPath;
        File file = SD.open(path, FILE_READ);
        const bool exists = file && !file.isDirectory();
        const uint64_t size = exists ? file.size() : 0;
        if (file) {
            file.close();
        }
        Serial.printf("sd audio path=%s exists=%u size=%llu\n",
                      path,
                      exists ? 1 : 0,
                      static_cast<unsigned long long>(size));
    }
}

static void log_sd_storage_report()
{
    if (!sd_card_present) {
        Serial.println("sd storage present=0 total=0 used=0 free=0");
        return;
    }

    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    const uint64_t freeBytes = total >= used ? total - used : 0;
    Serial.printf("sd storage present=1 type=%s card_size=%llu total=%llu used=%llu free=%llu\n",
                  sd_card_type_name(SD.cardType()),
                  static_cast<unsigned long long>(SD.cardSize()),
                  static_cast<unsigned long long>(total),
                  static_cast<unsigned long long>(used),
                  static_cast<unsigned long long>(freeBytes));

    Serial.println("sd files begin");
    File root = SD.open("/");
    uint16_t count = 0;
    bool truncated = false;
    if (root) {
        log_sd_file_list(root, 0, count, truncated);
        root.close();
    }
    Serial.printf("sd files end count=%u truncated=%u\n", count, truncated ? 1 : 0);

    Serial.println("sd audio begin");
    log_sd_audio_assets();
    Serial.println("sd audio end");
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
    case kSoundBattleClash:
    case kSoundLevelUp:
    case kSoundWin:
    case kSoundLose:
        return kSoundVolume;
    default:
        return kUiSoundVolume;
    }
}

static void play_notes_at_volume(const uint16_t* notes, const uint16_t* durations, size_t count, uint8_t volume)
{
    if (audio_muted) {
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
    if (audio_muted) {
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
    case kSoundBattleClash: {
        const uint16_t notes[] = { 196, 0, 392, 587, 784, 1175, 784 };
        const uint16_t ms[] = { 70, 24, 65, 70, 90, 105, 150 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundFriend: {
        const uint16_t notes[] = { 523, 659, 784, 1047, 784 };
        const uint16_t ms[] = { 55, 60, 70, 110, 150 };
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
    case kSoundLevelUp: {
        const uint16_t notes[] = { 659, 784, 988, 1319, 1568 };
        const uint16_t ms[] = { 60, 65, 80, 105, 190 };
        play_notes_at_volume(notes, ms, sizeof(notes) / sizeof(notes[0]), sound_volume_for_cue(cue));
        return true;
    }
    case kSoundBattleExit: {
        const uint16_t notes[] = { 1047, 784, 659, 523 };
        const uint16_t ms[] = { 70, 80, 100, 170 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    case kSoundCancel: {
        const uint16_t notes[] = { 784, 659, 784 };
        const uint16_t ms[] = { 55, 60, 95 };
        play_notes(notes, ms, sizeof(notes) / sizeof(notes[0]));
        return true;
    }
    default:
        return false;
    }
}

static void play_scene_sound(uint8_t cue)
{
    if (audio_muted) {
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
    case kSoundLevelUp: {
        const uint16_t notes[] = {659, 784, 988, 1319};
        const uint16_t ms[] = {50, 60, 80, 160};
        play_notes(notes, ms, 4);
        break;
    }
    case kSoundBattleExit: {
        const uint16_t notes[] = {988, 784, 587, 494};
        const uint16_t ms[] = {55, 65, 85, 140};
        play_notes(notes, ms, 4);
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
    if (audio_muted) {
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
                if (luma > 190 && centerSample) {
                    votes[kMetal] += weight;
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
            if (luma > 205 && chroma < 34 && centerSample) {
                votes[kMetal] += weight;
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
        votes[kMetal] + ((traits.saturation < 34 && traits.contrast > 38) ? max<int32_t>(0, traits.brightness - 156) * 2 : 0),
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

static const char* element_badge_label(ElementType element)
{
    static const char* const labels[] = {"木", "火", "土", "金", "水"};
    return labels[static_cast<uint8_t>(element) % 5];
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

static uint8_t pet_visual_variant(const PetGenes& genes)
{
    uint32_t state = genes.seed;
    state ^= static_cast<uint32_t>(genes.mood & 0x03) << 3;
    state ^= static_cast<uint32_t>(genes.eyeStyle & 0x03) << 7;
    state ^= static_cast<uint32_t>(genes.hornStyle & 0x03) << 11;
    state ^= static_cast<uint32_t>(genes.tailStyle & 0x03) << 15;
    state ^= static_cast<uint32_t>(genes.auraPattern & 0x03) << 19;
    state ^= static_cast<uint32_t>(genes.patternDensity) * 0x45d9f3bUL;
    return hash_mix(state) % 3;
}

static const char* variant_pet_name_by(ElementType element, uint8_t species, uint8_t variant)
{
    static const char* const names[5][3][3] = {
        {
            {"Leaf Deer", "Sprout Hart", "Antler Fawn"},
            {"Vine Fox", "Moss Kit", "Tendril Vulp"},
            {"Bud Turtle", "Seedback", "Canopy Shell"},
        },
        {
            {"Flame Cat", "Ember Lynx", "Cinder Purr"},
            {"Firebird", "Spark Kestrel", "Halo Finch"},
            {"Lava Hound", "Coal Pup", "Torch Fang"},
        },
        {
            {"Mountain Bear", "Pebble Cub", "Moss Boulder"},
            {"Rock Turtle", "Ridge Shell", "Boulderback"},
            {"Clay Beast", "Mud Golem", "Totem Cub"},
        },
        {
            {"Silver Wolf", "Chrome Lup", "Mirror Fang"},
            {"Mecha Rabbit", "Gear Bun", "Bolt Hare"},
            {"Bronze Tiger", "Ring Lynx", "Prism Stripe"},
        },
        {
            {"Wave Otter", "Ripple Pup", "Pearl Otter"},
            {"Water Dragon", "Stream Wyrm", "Rain Serpent"},
            {"Bubble Fish", "Drop Minnow", "Glide Ray"},
        },
    };
    return names[static_cast<uint8_t>(element) % 5][species % 3][variant % 3];
}

static const char* variant_pet_name(const PetGenes& genes)
{
    return variant_pet_name_by(genes.element, genes.species, pet_visual_variant(genes));
}

static const char* species_name(const PetGenes& genes)
{
    return variant_pet_name(genes);
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

static bool skin_palette_loaded[5] = {};
static bool skin_palette_body_valid[5] = {};
static bool skin_palette_accent_valid[5] = {};
static uint16_t skin_palette_body[5] = {};
static uint16_t skin_palette_accent[5] = {};

static const char* skin_palette_path_for(ElementType element)
{
    switch (element) {
    case kWood: return "/skins/palettes/wood.csv";
    case kFire: return "/skins/palettes/fire.csv";
    case kEarth: return "/skins/palettes/earth.csv";
    case kMetal: return "/skins/palettes/metal.csv";
    case kWater: return "/skins/palettes/water.csv";
    }
    return "/skins/palettes/default.csv";
}

static bool parse_skin_palette_line(const String& rawLine, String& key, uint16_t* color)
{
    if (color == nullptr) {
        return false;
    }
    String line = rawLine;
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith("key,")) {
        return false;
    }
    int firstComma = line.indexOf(',');
    int secondComma = firstComma < 0 ? -1 : line.indexOf(',', firstComma + 1);
    int thirdComma = secondComma < 0 ? -1 : line.indexOf(',', secondComma + 1);
    if (firstComma < 0 || secondComma < 0 || thirdComma < 0) {
        return false;
    }
    key = line.substring(0, firstComma);
    key.trim();
    key.toLowerCase();
    String rText = line.substring(firstComma + 1, secondComma);
    String gText = line.substring(secondComma + 1, thirdComma);
    String bText = line.substring(thirdComma + 1);
    rText.trim();
    gText.trim();
    bText.trim();
    uint8_t r = clamp_u8(strtol(rText.c_str(), nullptr, 0));
    uint8_t g = clamp_u8(strtol(gText.c_str(), nullptr, 0));
    uint8_t b = clamp_u8(strtol(bText.c_str(), nullptr, 0));
    *color = rgb(r, g, b);
    return true;
}

static void load_skin_palette_file(ElementType element, const char* path)
{
    if (!sd_card_present || path == nullptr || path[0] == '\0') {
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return;
    }
    uint8_t index = static_cast<uint8_t>(element) % 5;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        String key;
        uint16_t color = 0;
        if (!parse_skin_palette_line(line, key, &color)) {
            continue;
        }
        if (key == "body") {
            skin_palette_body[index] = color;
            skin_palette_body_valid[index] = true;
        } else if (key == "accent") {
            skin_palette_accent[index] = color;
            skin_palette_accent_valid[index] = true;
        }
    }
    file.close();
}

static void load_skin_palette(ElementType element)
{
    uint8_t index = static_cast<uint8_t>(element) % 5;
    if (skin_palette_loaded[index]) {
        return;
    }
    skin_palette_loaded[index] = true;
    load_skin_palette_file(element, "/skins/palettes/default.csv");
    load_skin_palette_file(element, skin_palette_path_for(element));
}

static uint16_t default_element_body_color(ElementType element)
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

static uint16_t element_body_color(ElementType element)
{
    uint16_t fallback = default_element_body_color(element);
    load_skin_palette(element);
    uint8_t index = static_cast<uint8_t>(element) % 5;
    return skin_palette_body_valid[index] ? skin_palette_body[index] : fallback;
}

static uint16_t default_element_accent_color(ElementType element)
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

static uint16_t element_accent_color(ElementType element)
{
    uint16_t fallback = default_element_accent_color(element);
    load_skin_palette(element);
    uint8_t index = static_cast<uint8_t>(element) % 5;
    return skin_palette_accent_valid[index] ? skin_palette_accent[index] : fallback;
}

static const char* sd_action_profile_path_for(uint8_t profileId)
{
    switch (profileId) {
    case kSdActionWild: return "/actions/wild.csv";
    case kSdActionBag: return "/actions/bag.csv";
    case kSdActionBattle: return "/actions/battle_clash.csv";
    case kSdActionResult: return "/actions/result.csv";
    case kSdActionIdle:
    default: return "/actions/idle.csv";
    }
}

static bool parse_sd_action_line(const String& rawLine, String& key, int16_t* value)
{
    if (value == nullptr) {
        return false;
    }
    String line = rawLine;
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith("key,")) {
        return false;
    }
    int comma = line.indexOf(',');
    if (comma < 0) {
        return false;
    }
    key = line.substring(0, comma);
    key.trim();
    key.toLowerCase();
    String valueText = line.substring(comma + 1);
    valueText.trim();
    *value = static_cast<int16_t>(strtol(valueText.c_str(), nullptr, 0));
    return key.length() > 0;
}

static int8_t clamp_action_i8(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return static_cast<int8_t>(low);
    }
    if (value > high) {
        return static_cast<int8_t>(high);
    }
    return static_cast<int8_t>(value);
}

static uint8_t clamp_action_u8(int32_t value, int32_t high)
{
    if (value < 0) {
        return 0;
    }
    if (value > high) {
        return static_cast<uint8_t>(high);
    }
    return static_cast<uint8_t>(value);
}

static void load_sd_action_profile(uint8_t profileId)
{
    if (profileId >= kSdActionProfileCount) {
        return;
    }
    SdActionProfile& profile = sd_action_profiles[profileId];
    if (profile.loaded) {
        return;
    }
    profile.loaded = true;
    if (!sd_card_present) {
        return;
    }
    File file = SD.open(sd_action_profile_path_for(profileId), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return;
    }
    while (file.available()) {
        String line = file.readStringUntil('\n');
        String key;
        int16_t value = 0;
        if (!parse_sd_action_line(line, key, &value)) {
            continue;
        }
        if (key == "bob") {
            profile.bob = clamp_action_i8(value, -12, 12);
            profile.valid = true;
        } else if (key == "sparkle") {
            profile.sparkle = clamp_action_u8(value, 8);
            profile.valid = true;
        } else if (key == "tilt") {
            profile.tilt = clamp_action_i8(value, -3, 3);
            profile.valid = true;
        }
    }
    file.close();
}

static uint8_t sd_action_profile_id_for_screen()
{
    switch (screen_mode) {
    case kScreenWild: return kSdActionWild;
    case kScreenBag:
    case kScreenReleaseConfirm: return kSdActionBag;
    case kScreenBattle: return battle_result_pending ? kSdActionResult : kSdActionBattle;
    case kScreenIdle:
    case kScreenCaptureFail:
    case kScreenMatch:
    default: return kSdActionIdle;
    }
}

static uint8_t sd_action_profile_for_screen()
{
    uint8_t profileId = sd_action_profile_id_for_screen();
    load_sd_action_profile(profileId);
    return profileId;
}

static void apply_sd_action_profile(PetGenes& genes, uint8_t profileId)
{
    if (profileId >= kSdActionProfileCount) {
        return;
    }
    const SdActionProfile& profile = sd_action_profiles[profileId];
    if (!profile.valid) {
        return;
    }
    genes.bodyScale = clamp_u8(static_cast<int32_t>(genes.bodyScale) + profile.bob);
    genes.patternDensity = min<uint8_t>(16, clamp_u8(static_cast<int32_t>(genes.patternDensity) + profile.sparkle));
    if (profile.tilt != 0) {
        int32_t aura = static_cast<int32_t>(genes.auraPattern) + profile.tilt;
        while (aura < 0) {
            aura += 4;
        }
        genes.auraPattern = static_cast<uint8_t>(aura % 4);
        genes.seed = hash_mix(genes.seed ^ (static_cast<uint32_t>(profile.tilt + 8) * 0x45d9f3bUL));
    }
}

static void draw_element_badge(int16_t x, int16_t y, ElementType element, uint16_t bg)
{
    uint16_t accent = element_accent_color(element);
    uint16_t fill = tint_color(accent, -115);
    CoreS3.Display.fillRoundRect(x, y, 38, 20, 6, fill);
    CoreS3.Display.drawRoundRect(x, y, 38, 20, 6, accent);
    CoreS3.Display.setFont(&fonts::efontCN_16);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(TFT_WHITE, fill);
    CoreS3.Display.drawString(element_badge_label(element), x + 19, y + 10);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
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

static const char* external_vision_source_label(uint8_t source)
{
    switch (source) {
    case kExternalVisionEdge: return "edge";
    case kExternalVisionHusky: return "husky";
    default: return "external";
    }
}

static bool object_class_from_label(const String& label, uint8_t* classId)
{
    if (classId == nullptr) {
        return false;
    }
    String normalized = label;
    normalized.trim();
    normalized.toLowerCase();
    if (normalized == "plant_leaf") {
        *classId = kObjectPlantLeaf;
    } else if (normalized == "food_fruit") {
        *classId = kObjectFoodFruit;
    } else if (normalized == "paper_book") {
        *classId = kObjectPaperBook;
    } else if (normalized == "electronics_screen") {
        *classId = kObjectElectronicsScreen;
    } else if (normalized == "metal_key_coin") {
        *classId = kObjectMetalKeyCoin;
    } else if (normalized == "fabric_cloth") {
        *classId = kObjectFabricCloth;
    } else if (normalized == "cup_bottle_water") {
        *classId = kObjectCupBottleWater;
    } else if (normalized == "toy_figure") {
        *classId = kObjectToyFigure;
    } else {
        *classId = kObjectUnknown;
        return normalized == "negative" || normalized == "unknown";
    }
    return true;
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

static void init_proximity_sensor()
{
    proximity_sensor_available = false;
    if (!CoreS3.In_I2C.scanID(kProximityI2cAddr, kProximityI2cFreq)) {
        Serial.println("proximity sensor=missing");
        return;
    }
    CoreS3.In_I2C.writeRegister8(kProximityI2cAddr, 0x81, 0x03, kProximityI2cFreq);
    CoreS3.In_I2C.writeRegister8(kProximityI2cAddr, 0x82, 0x7f, kProximityI2cFreq);
    CoreS3.In_I2C.writeRegister8(kProximityI2cAddr, 0x83, 0x04, kProximityI2cFreq);
    CoreS3.In_I2C.writeRegister8(kProximityI2cAddr, 0x84, 0x02, kProximityI2cFreq);
    proximity_sensor_available = true;
    Serial.println("proximity sensor=ready");
}

static uint16_t read_capture_proximity()
{
    if (!proximity_sensor_available) {
        return 0;
    }
    uint8_t lo = CoreS3.In_I2C.readRegister8(kProximityI2cAddr, 0x8d, kProximityI2cFreq);
    uint8_t hi = CoreS3.In_I2C.readRegister8(kProximityI2cAddr, 0x8e, kProximityI2cFreq);
    return (static_cast<uint16_t>(hi & 0x07) << 8) | lo;
}

static bool metal_visual_evidence(const ImageTraits& traits)
{
    return traits.saturation <= 38 &&
           traits.contrast >= 46 &&
           traits.centerDelta >= 12 &&
           (traits.brightRatio >= 5 || traits.brightness >= 158);
}

static bool water_visual_evidence(const ImageTraits& traits)
{
    int32_t blueDominance = static_cast<int32_t>(traits.b) - max(traits.r, traits.g);
    return blueDominance > 18 ||
           (traits.darkRatio >= 18 && traits.contrast >= 52) ||
           (traits.brightRatio >= 10 && traits.centerDelta >= 24 && traits.saturation >= 22);
}

static bool paper_visual_evidence(const ImageTraits& traits)
{
    return traits.brightness >= 88 &&
           traits.saturation <= 80 &&
           (traits.centerDelta >= 18 || traits.contrast >= 60);
}

static bool blank_scene_evidence(const ImageTraits& traits)
{
    int32_t centerBrightnessDiff = abs(traits.centerBrightness - traits.edgeBrightness);
    if (traits.saturation < 22 &&
        traits.centerSaturation < 28 &&
        traits.edgeSaturation < 28 &&
        traits.centerDelta < 30 &&
        traits.contrast < 96) {
        return true;
    }
    if (traits.saturation < 14 &&
        traits.centerSaturation < 18 &&
        traits.edgeSaturation < 18 &&
        traits.centerDelta < 42 &&
        centerBrightnessDiff < 32 &&
        traits.contrast < 128) {
        return true;
    }
    if (traits.saturation < 20 &&
        traits.centerSaturation < 24 &&
        traits.edgeSaturation < 24 &&
        traits.centerDelta < 60 &&
        centerBrightnessDiff < 44 &&
        traits.darkRatio < 20 &&
        traits.brightRatio < 28 &&
        traits.contrast < 170) {
        return true;
    }
    return false;
}

static bool background_similarity_penalty(const ImageTraits& traits)
{
    int32_t centerBrightnessDiff = abs(traits.centerBrightness - traits.edgeBrightness);
    if (traits.saturation <= 30 &&
        traits.centerSaturation <= 34 &&
        traits.edgeSaturation <= 34 &&
        traits.centerDelta <= 38 &&
        centerBrightnessDiff <= 26 &&
        traits.contrast <= 118) {
        return true;
    }
    if (traits.saturation <= 54 &&
        traits.centerDelta <= 24 &&
        centerBrightnessDiff <= 18 &&
        traits.contrast <= 54) {
        return true;
    }
    if (traits.brightness >= 178 &&
        traits.saturation <= 42 &&
        traits.centerDelta <= 46 &&
        traits.contrast <= 96 &&
        traits.brightRatio >= 28) {
        return true;
    }
    return false;
}

static const char* capture_quality_hint(const ImageTraits& traits, const SubjectPresence& presence, uint16_t proximity)
{
    if (traits.frameWidth <= 0 || traits.frameHeight <= 0) {
        return "No camera frame";
    }
    if (traits.brightness < 32 || traits.darkRatio > 82) {
        return "Too dark";
    }
    if (traits.brightRatio > 92 && traits.contrast < 28) {
        return "Too bright";
    }
    if (background_similarity_penalty(traits) || blank_scene_evidence(traits)) {
        return "Flat background";
    }
    if (!presence.present) {
        return "Center object";
    }
    if (proximity_sensor_available && proximity < kObjectNearProximity && traits.centerDelta < 44) {
        return "Move closer";
    }
    if (traits.contrast < 34 && traits.centerDelta < 32) {
        return "Add contrast";
    }
    return "Quality OK";
}

static const char* capture_distance_hint(const ImageTraits& traits, const SubjectPresence& presence, uint16_t proximity)
{
    if (!presence.present) {
        return "no_subject";
    }
    if (background_similarity_penalty(traits) || blank_scene_evidence(traits)) {
        return "flat_background";
    }
    if (proximity_sensor_available) {
        if (proximity >= kObjectNearProximity) {
            return "near";
        }
        return traits.centerDelta >= 44 ? "visual_centered" : "too_far";
    }
    if (traits.centerDelta >= 54 && presence.score >= 70) {
        return "visual_near";
    }
    if (traits.centerDelta >= 34 && presence.score >= 48) {
        return "visual_centered";
    }
    return "distance_unknown";
}

static const char* scene_label_for_traits(const ImageTraits& traits)
{
    if (traits.darkRatio >= 72 || traits.brightness <= 58) {
        return "dark";
    }
    if (traits.brightRatio >= 70 || traits.brightness >= 214) {
        return "bright";
    }
    if (traits.brightRatio >= 24 && traits.centerDelta >= 46) {
        return "glare";
    }
    if (traits.brightness >= 174 &&
        traits.saturation <= 34 &&
        traits.contrast <= 42 &&
        traits.centerDelta <= 34) {
        return "white_wall";
    }
    if (traits.brightness >= 158 &&
        traits.saturation <= 48 &&
        traits.contrast > 42) {
        return "white_paper";
    }
    if (traits.brightness >= 82 &&
        traits.brightness <= 184 &&
        traits.saturation <= 76 &&
        traits.r + traits.g >= traits.b * 2 + 20) {
        return "desktop";
    }
    if (traits.contrast <= 26 && traits.centerDelta <= 24) {
        return "low_texture";
    }
    return "unknown";
}

static uint8_t capture_quality_score(const ImageTraits& traits, const SubjectPresence& presence, uint16_t proximity)
{
    int32_t score = presence.score * 2 + traits.centerDelta * 2 + traits.contrast / 2 + traits.saturation / 3;
    score -= abs(traits.brightness - 128) / 2;
    if (background_similarity_penalty(traits)) {
        score -= 120;
    }
    if (traits.brightness < 32 || traits.darkRatio > 82) {
        score -= 120;
    }
    if (traits.brightRatio > 92 && traits.contrast < 28) {
        score -= 110;
    }
    if (proximity_sensor_available && proximity >= kObjectNearProximity) {
        score += 16;
    }
    return static_cast<uint8_t>(max<int32_t>(0, min<int32_t>(255, score / 2)));
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
        return water_visual_evidence(traits) ? kWater : kMetal;
    case kObjectMetalKeyCoin:
        return kMetal;
    case kObjectFabricCloth:
        return (traits.g > traits.r && traits.g > traits.b) ? kWood : kEarth;
    case kObjectCupBottleWater:
        return kWater;
    case kObjectToyFigure:
        return metal_visual_evidence(traits) ? kMetal : kFire;
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
    if (blank_scene_evidence(traits)) {
        presence.reason = "Blank scene";
        presence.score = clamp_u8(traits.contrast / 3 + traits.centerDelta);
        return presence;
    }
    if (background_similarity_penalty(traits)) {
        presence.reason = "Background-like scene";
        presence.score = clamp_u8(traits.contrast / 3 + traits.centerDelta);
        return presence;
    }
    if (traits.saturation < 24 && traits.centerDelta < 22 && traits.contrast < 58) {
        presence.reason = "Blank scene";
        presence.score = clamp_u8(traits.contrast / 2 + traits.centerDelta);
        return presence;
    }
    if (traits.saturation < 18 &&
        traits.centerSaturation < 20 &&
        traits.edgeSaturation < 20 &&
        traits.contrast < 72 &&
        abs(traits.centerBrightness - traits.edgeBrightness) < 18) {
        presence.reason = "Blank scene";
        presence.score = clamp_u8(traits.contrast / 2 + traits.centerDelta);
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

static int16_t raw_vision_feature(int32_t value)
{
    return static_cast<int16_t>(max<int32_t>(0, min<int32_t>(255, value)));
}

static int32_t scaled_ratio_feature(int32_t percent)
{
    return max<int32_t>(0, min<int32_t>(255, (percent * 255) / 100));
}

static void build_vision_features(const ImageTraits& traits, int16_t* features)
{
    int32_t redDominance = static_cast<int32_t>(traits.r) - max(traits.g, traits.b) + 128;
    int32_t greenDominance = static_cast<int32_t>(traits.g) - max(traits.r, traits.b) + 128;
    int32_t blueDominance = static_cast<int32_t>(traits.b) - max(traits.r, traits.g) + 128;
    int32_t warmScore = (static_cast<int32_t>(traits.r) + traits.g - traits.b * 2) / 2 + 128;
    int32_t lowSaturationScore = max<int32_t>(0, 96 - traits.saturation) * 255 / 96;

    features[0] = raw_vision_feature(traits.r);
    features[1] = raw_vision_feature(traits.g);
    features[2] = raw_vision_feature(traits.b);
    features[3] = raw_vision_feature(traits.brightness);
    features[4] = raw_vision_feature(traits.saturation);
    features[5] = raw_vision_feature(traits.contrast);
    features[6] = raw_vision_feature(traits.centerBrightness);
    features[7] = raw_vision_feature(traits.edgeBrightness);
    features[8] = raw_vision_feature(traits.centerSaturation);
    features[9] = raw_vision_feature(traits.edgeSaturation);
    features[10] = raw_vision_feature(traits.centerDelta);
    features[11] = raw_vision_feature(scaled_ratio_feature(traits.darkRatio));
    features[12] = raw_vision_feature(scaled_ratio_feature(traits.brightRatio));
    features[13] = raw_vision_feature(redDominance);
    features[14] = raw_vision_feature(greenDominance);
    features[15] = raw_vision_feature(blueDominance);
    features[16] = raw_vision_feature(warmScore);
    features[17] = raw_vision_feature(lowSaturationScore);
}

static uint16_t weighted_vision_distance(const int16_t* features, const uint8_t* prototype, const uint8_t* stddev)
{
    uint32_t total = 0;
    uint32_t weightTotal = 0;
    for (uint8_t i = 0; i < kVisionFeatureCount; ++i) {
        uint32_t weight = kVisionFeatureWeights[i];
        uint32_t sigma = max<uint32_t>(8, stddev[i]);
        uint32_t delta = abs(static_cast<int32_t>(features[i]) - prototype[i]);
        total += weight * delta * kVisionDistanceScale / sigma;
        weightTotal += weight;
    }
    if (weightTotal == 0) {
        return 0xffff;
    }
    return static_cast<uint16_t>(min<uint32_t>(0xffff, (total * kVisionDistanceScale) / weightTotal));
}

static bool run_embedded_vision_model(const int8_t* input, const ImageTraits& traits, RecognitionResult* result)
{
    (void)input;
    if (result == nullptr || kVisionClassCount != 8 || kVisionFeatureCount != 18 || kVisionModelDataLen == 0) {
        return false;
    }

    int16_t features[kVisionFeatureCount] = {};
    build_vision_features(traits, features);

    uint16_t negativeDistance = weighted_vision_distance(features, kVisionNegativePrototype, kVisionNegativeStd);
    if (negativeDistance <= kVisionNegativeThreshold) {
        last_vision_best_distance = negativeDistance;
        last_vision_margin = 0;
        last_vision_source = "negative";
        result->classId = kObjectUnknown;
        result->objectLabel = object_class_label(kObjectUnknown);
        result->materialLabel = material_label_for_class(kObjectUnknown);
        result->confidence = 0;
        result->recognized = false;
        result->failureReason = "Background-like scene";
        return true;
    }

    uint16_t distances[kVisionClassCount] = {};
    for (uint8_t c = 0; c < kVisionClassCount; ++c) {
        distances[c] = weighted_vision_distance(features, kVisionPrototype[c], kVisionStd[c]);
    }

    uint8_t best = 0;
    uint8_t second = 1;
    if (distances[second] < distances[best]) {
        uint8_t tmp = best;
        best = second;
        second = tmp;
    }
    for (uint8_t c = 2; c < kVisionClassCount; ++c) {
        if (distances[c] < distances[best]) {
            second = best;
            best = c;
        } else if (distances[c] < distances[second]) {
            second = c;
        }
    }

    uint8_t classId = static_cast<uint8_t>(best + 1);
    uint16_t bestDistance = distances[best];
    uint16_t secondDistance = distances[second];
    uint16_t margin = secondDistance > bestDistance ? secondDistance - bestDistance : 0;
    uint16_t threshold = kVisionClassThresholds[best];
    uint16_t inside = threshold > bestDistance ? threshold - bestDistance : 0;
    uint8_t distanceScore = threshold == 0 ? 0 : clamp_u8((inside * 70) / threshold);
    uint8_t marginScore = clamp_u8(margin * 2);

    last_vision_best_distance = bestDistance;
    last_vision_margin = margin;
    last_vision_source = "model";

    result->classId = classId;
    result->objectLabel = object_class_label(classId);
    result->materialLabel = material_label_for_class(classId);
    result->elementHint = element_hint_for_class(classId, traits);
    result->speciesBias = species_bias_for_class(classId, traits);
    result->confidence = clamp_u8(28 + result->presenceScore / 5 + distanceScore / 2 + marginScore);
    bool distanceOk = bestDistance <= threshold;
    bool marginOk = margin >= kVisionMinDistanceMargin || bestDistance < (threshold * 3) / 4;
    uint8_t minConfidence = kMinRecognitionConfidence;
    if (kVisionModelQualityWeak) {
        distanceOk = bestDistance <= threshold / 2;
        marginOk = margin >= 28;
        minConfidence = kWeakModelMinConfidence;
    }
    result->recognized = distanceOk && marginOk && result->confidence >= minConfidence;
    if (result->recognized) {
        result->failureReason = "";
    } else if (!distanceOk) {
        result->failureReason = "Model distance high";
    } else if (!marginOk) {
        result->failureReason = "Model class ambiguous";
    } else {
        result->failureReason = "Low model confidence";
    }
    return true;
}

static void clear_external_vision_hint()
{
    external_vision_hint = {};
}

static bool external_vision_hint_active()
{
    if (!external_vision_hint.valid) {
        return false;
    }
    if (static_cast<int32_t>(external_vision_hint.expiresAtMs - millis()) <= 0) {
        clear_external_vision_hint();
        return false;
    }
    return true;
}

static bool apply_external_vision_hint(const ImageTraits& traits,
                                       const SubjectPresence& presence,
                                       RecognitionResult* result)
{
    if (result == nullptr || !external_vision_hint_active()) {
        return false;
    }
    if (external_vision_hint.classId == kObjectUnknown ||
        external_vision_hint.confidence < kExternalVisionMinConfidence ||
        external_vision_hint.presence < kExternalVisionMinPresence) {
        clear_external_vision_hint();
        return false;
    }
    if (!presence.present || presence.score < kMinPresenceScore) {
        return false;
    }

    uint8_t blendedConfidence = clamp_u8(
        (static_cast<uint16_t>(external_vision_hint.confidence) * 2 + presence.score) / 3);
    result->classId = external_vision_hint.classId;
    result->objectLabel = object_class_label(external_vision_hint.classId);
    result->materialLabel = material_label_for_class(external_vision_hint.classId);
    result->elementHint = element_hint_for_class(external_vision_hint.classId, traits);
    result->speciesBias = species_bias_for_class(external_vision_hint.classId, traits);
    result->presenceScore = presence.score;
    result->confidence = blendedConfidence;
    result->recognized = blendedConfidence >= kMinRecognitionConfidence;
    result->failureReason = result->recognized ? "" : "Low external confidence";

    last_vision_source = external_vision_source_label(external_vision_hint.source);
    last_vision_best_distance = static_cast<uint16_t>(100 - min<uint8_t>(100, external_vision_hint.confidence));
    last_vision_margin = static_cast<uint16_t>(
        result->confidence > kMinRecognitionConfidence ? result->confidence - kMinRecognitionConfidence : 0);
    return true;
}

static bool fallback_class_evidence_ok(uint8_t classId,
                                       const ImageTraits& traits,
                                       int32_t score,
                                       int32_t margin)
{
    if (score < kFallbackMinClassScore || margin < kFallbackMinClassMargin) {
        return false;
    }

    int32_t redDominance = static_cast<int32_t>(traits.r) - max(traits.g, traits.b);
    int32_t greenDominance = static_cast<int32_t>(traits.g) - max(traits.r, traits.b);
    int32_t blueDominance = static_cast<int32_t>(traits.b) - max(traits.r, traits.g);
    int32_t warm = static_cast<int32_t>(traits.r) + traits.g - traits.b * 2;

    switch (classId) {
    case kObjectPlantLeaf:
        return traits.saturation >= 28 && greenDominance > -10;
    case kObjectFoodFruit:
        return traits.saturation >= 28 && (redDominance > -8 || warm > 18);
    case kObjectPaperBook:
        return paper_visual_evidence(traits);
    case kObjectElectronicsScreen:
        return traits.darkRatio >= 8 || traits.contrast >= 64 || blueDominance > 4;
    case kObjectMetalKeyCoin:
        return metal_visual_evidence(traits);
    case kObjectFabricCloth:
        return traits.saturation >= 18 && traits.contrast <= 126;
    case kObjectCupBottleWater:
        return water_visual_evidence(traits) ||
               (traits.centerDelta >= 30 && traits.brightRatio >= 6 && traits.saturation >= 22);
    case kObjectToyFigure:
        return traits.saturation >= 38 && traits.contrast >= 25;
    default:
        return false;
    }
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
        last_vision_source = "presence";
        return result;
    }
    if (apply_external_vision_hint(traits, presence, &result)) {
        return result;
    }
    if (run_embedded_vision_model(vision_input, traits, &result)) {
        if (result.recognized) {
            return result;
        }
        if (result.failureReason != nullptr && strcmp(result.failureReason, "Background-like scene") == 0) {
            return result;
        }
    }

    int32_t redDominance = static_cast<int32_t>(traits.r) - max(traits.g, traits.b);
    int32_t greenDominance = static_cast<int32_t>(traits.g) - max(traits.r, traits.b);
    int32_t blueDominance = static_cast<int32_t>(traits.b) - max(traits.r, traits.g);
    int32_t warm = static_cast<int32_t>(traits.r) + traits.g - traits.b * 2;
    int32_t lowSaturation = max<int32_t>(0, 70 - traits.saturation);
    int32_t highBrightness = max<int32_t>(0, traits.brightness - 140);
    int32_t midBrightness = max<int32_t>(0, 90 - abs(traits.brightness - 126));
    int32_t centerSatLift = max<int32_t>(0, traits.centerSaturation - traits.edgeSaturation + 8);

    int32_t scores[9] = {};
    scores[kObjectPlantLeaf] = max<int32_t>(0, greenDominance + 36) * 4 +
                               traits.saturation + traits.centerDelta + presence.score / 2;
    scores[kObjectFoodFruit] = max<int32_t>(0, redDominance + 28) * 3 +
                               max<int32_t>(0, warm + 42) + traits.saturation + centerSatLift;
    scores[kObjectPaperBook] = max<int32_t>(0, traits.brightness - 112) * 2 +
                               lowSaturation +
                               max<int32_t>(0, traits.centerDelta - 8) * 3 +
                               max<int32_t>(0, traits.contrast - 34);
    scores[kObjectElectronicsScreen] = traits.darkRatio * 4 + traits.contrast * 2 +
                                       max<int32_t>(0, blueDominance + 8) * 2;
    scores[kObjectMetalKeyCoin] = max<int32_t>(0, 42 - traits.saturation) * 2 +
                                  max<int32_t>(0, traits.contrast - 42) * 2 +
                                  max<int32_t>(0, traits.brightRatio - 5) * 9 +
                                  max<int32_t>(0, traits.centerDelta - 12) * 2 +
                                  max<int32_t>(0, traits.brightness - 156);
    scores[kObjectFabricCloth] = midBrightness + traits.saturation * 2 +
                                 max<int32_t>(0, 90 - traits.contrast) + centerSatLift;
    scores[kObjectCupBottleWater] = max<int32_t>(0, blueDominance + 4) * 3 +
                                    max<int32_t>(0, traits.centerDelta - 16) * 2 +
                                    traits.brightRatio * 3 +
                                    max<int32_t>(0, traits.saturation - 22);
    scores[kObjectToyFigure] = traits.saturation * 2 + traits.contrast +
                               max<int32_t>(0, abs(redDominance - blueDominance) - 8) +
                               max<int32_t>(0, warm);

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
    bool evidenceOk = fallback_class_evidence_ok(best, traits, scores[best], margin);
    last_vision_source = "rule";
    last_vision_best_distance = static_cast<uint16_t>(min<int32_t>(0xffff, max<int32_t>(0, scores[best])));
    last_vision_margin = static_cast<uint16_t>(min<int32_t>(0xffff, max<int32_t>(0, margin)));
    result.classId = best;
    result.objectLabel = object_class_label(best);
    result.materialLabel = material_label_for_class(best);
    result.elementHint = element_hint_for_class(best, traits);
    result.speciesBias = species_bias_for_class(best, traits);
    result.confidence = clamp_u8(28 + margin / 5 + presence.score / 3);
    result.recognized = evidenceOk && result.confidence >= kMinRecognitionConfidence;
    if (result.recognized) {
        result.failureReason = "";
    } else if (!evidenceOk) {
        result.failureReason = "Weak class evidence";
    } else {
        result.failureReason = "Low class confidence";
    }
    return result;
}

static RecognitionResult recognize_object_local(const ImageTraits& traits)
{
    uint32_t start = millis();
    last_vision_best_distance = 0;
    last_vision_margin = 0;
    last_vision_source = "none";
    SubjectPresence presence = detect_subject_presence(traits);
    RecognitionResult result = classify_object_local(traits, presence);
    last_vision_classify_ms = millis() - start;
    return result;
}

static bool save_camera_thumbnail_ppm(const char* path)
{
    if (!sd_card_present || path == nullptr || CoreS3.Camera.fb == nullptr || CoreS3.Camera.fb->len < 2) {
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

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }
    file.printf("P6\n%u %u\n255\n", kSampleThumbSize, kSampleThumbSize);
    for (uint8_t y = 0; y < kSampleThumbSize; ++y) {
        int32_t srcY = min<int32_t>(frameH - 1, (static_cast<int32_t>(y) * frameH) / kSampleThumbSize);
        for (uint8_t x = 0; x < kSampleThumbSize; ++x) {
            int32_t srcX = min<int32_t>(frameW - 1, (static_cast<int32_t>(x) * frameW) / kSampleThumbSize);
            size_t srcIndex = static_cast<size_t>(srcY) * frameW + srcX;
            uint8_t rgbBytes[3] = {};
            if (srcIndex < pixelCount) {
                rgb565_to_rgb(pixels[srcIndex], rgbBytes[0], rgbBytes[1], rgbBytes[2]);
            }
            file.write(rgbBytes, sizeof(rgbBytes));
        }
    }
    file.close();
    return true;
}

static void log_capture_sample(uint8_t burstIndex,
                               const ImageTraits& traits,
                               const RecognitionResult& recog,
                               const SubjectPresence& presence,
                               uint16_t proximity)
{
    if (!kVisionSampleLoggingEnabled) {
        return;
    }

    const char* autoLabel = (recog.recognized && recog.classId != kObjectUnknown) ? recog.objectLabel : "negative";
    const char* label = sample_mode_enabled ? sample_mode_label : autoLabel;
    const char* reason = recog.failureReason == nullptr ? "" : recog.failureReason;
    const char* hint = capture_quality_hint(traits, presence, proximity);
    const char* distanceHint = capture_distance_hint(traits, presence, proximity);
    const char* autoScene = scene_label_for_traits(traits);
    const char* scene = (sample_mode_enabled && sample_mode_scene[0] != '\0') ? sample_mode_scene : autoScene;
    char thumbPath[80] = {};
    bool thumbSaved = false;

    if (sd_card_present) {
        SD.mkdir("/samples");
        char dirPath[48];
        snprintf(dirPath, sizeof(dirPath), "/samples/%s", label);
        SD.mkdir(dirPath);
        snprintf(thumbPath, sizeof(thumbPath), "%s/%lu_%lu_%u.ppm",
                 dirPath,
                 static_cast<unsigned long>(now_sec()),
                 static_cast<unsigned long>(shot_count),
                 burstIndex);
        thumbSaved = save_camera_thumbnail_ppm(thumbPath);

        bool newManifest = !SD.exists("/samples/manifest.csv");
        File manifest = SD.open("/samples/manifest.csv", FILE_APPEND);
        if (manifest) {
            if (newManifest) {
                manifest.println("time,shot,burst,label,scene,recognized,confidence,presence,quality,proximity,bri,sat,ctr,centerDelta,dark,bright,reason,thumb,labelSource,autoLabel,distanceHint");
            }
            manifest.printf("%lu,%lu,%u,%s,%s,%u,%u,%u,%u,%u,%ld,%ld,%ld,%ld,%ld,%ld,%s,%s,%s,%s,%s\n",
                            static_cast<unsigned long>(now_sec()),
                            static_cast<unsigned long>(shot_count),
                            burstIndex,
                            label,
                            scene,
                            recog.recognized ? 1 : 0,
                            recog.confidence,
                            presence.score,
                            capture_quality_score(traits, presence, proximity),
                            proximity,
                            static_cast<long>(traits.brightness),
                            static_cast<long>(traits.saturation),
                            static_cast<long>(traits.contrast),
                            static_cast<long>(traits.centerDelta),
                            static_cast<long>(traits.darkRatio),
                            static_cast<long>(traits.brightRatio),
                            reason,
                            thumbSaved ? thumbPath : "",
                            sample_mode_enabled ? "manual" : "auto",
                            autoLabel,
                            distanceHint);
            manifest.close();
        }
    }

    Serial.printf("sample shot=%lu burst=%u label=%s source=%s auto=%s scene=%s rec=%u conf=%u presence=%u q=%u prox=%u hint=%s distance=%s bri=%ld sat=%ld ctr=%ld cd=%ld dark=%ld bright=%ld thumb=%s reason=%s\n",
                  static_cast<unsigned long>(shot_count),
                  burstIndex,
                  label,
                  sample_mode_enabled ? "manual" : "auto",
                  autoLabel,
                  scene,
                  recog.recognized ? 1 : 0,
                  recog.confidence,
                  presence.score,
                  capture_quality_score(traits, presence, proximity),
                  proximity,
                  hint,
                  distanceHint,
                  static_cast<long>(traits.brightness),
                  static_cast<long>(traits.saturation),
                  static_cast<long>(traits.contrast),
                  static_cast<long>(traits.centerDelta),
                  static_cast<long>(traits.darkRatio),
                  static_cast<long>(traits.brightRatio),
                  thumbSaved ? thumbPath : "",
                  reason);
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
    Serial.printf("pet_snapshot species=%s element=%s variant=%u seed=%lu bri=%ld sat=%ld ctr=%ld cd=%ld\n",
                  variant_pet_name(genes),
                  element_name(genes.element),
                  pet_visual_variant(genes),
                  static_cast<unsigned long>(genes.seed),
                  static_cast<long>(traits.brightness),
                  static_cast<long>(traits.saturation),
                  static_cast<long>(traits.contrast),
                  static_cast<long>(traits.centerDelta));
    if (!sd_card_present) {
        return false;
    }
    SD.mkdir("/samples");
    bool newManifest = !SD.exists("/samples/pets.csv");
    File file = SD.open("/samples/pets.csv", FILE_APPEND);
    if (!file) {
        return false;
    }
    if (newManifest) {
        file.println("time,shot,species,element,variant,seed,bri,sat,ctr,centerDelta,body,eye,horn,tail,aura,density");
    }
    file.printf("%lu,%lu,%s,%s,%u,%lu,%ld,%ld,%ld,%ld,%u,%u,%u,%u,%u,%u\n",
                static_cast<unsigned long>(now_sec()),
                static_cast<unsigned long>(shot_count),
                variant_pet_name(genes),
                element_name(genes.element),
                pet_visual_variant(genes),
                static_cast<unsigned long>(genes.seed),
                static_cast<long>(traits.brightness),
                static_cast<long>(traits.saturation),
                static_cast<long>(traits.contrast),
                static_cast<long>(traits.centerDelta),
                genes.bodyScale,
                genes.eyeStyle,
                genes.hornStyle,
                genes.tailStyle,
                genes.auraPattern,
                genes.patternDensity);
    file.close();
    return true;
}

static PetGenes merge_generation_inputs(ImageTraits traits, RecognitionResult recog, PetHint remoteHint)
{
    PetGenes genes = {};
    int32_t centerBrightnessDelta = traits.centerBrightness - traits.edgeBrightness;
    int32_t centerSaturationDelta = traits.centerSaturation - traits.edgeSaturation;
    uint8_t classId = recog.classId;
    genes.element = remoteHint.valid ? remoteHint.preferredElement : recog.elementHint;
    genes.species = remoteHint.valid ? (remoteHint.preferredSpecies % 3) : (recog.speciesBias % 3);
    genes.mood = (traits.brightness > 168) ? 0 : ((traits.saturation < 38) ? 1 : ((traits.contrast + classId * 7 > 145) ? 3 : 2));
    genes.bodyScale = clamp_u8(78 + traits.saturation / 5 + traits.contrast / 10 + abs(centerBrightnessDelta) / 4);
    genes.eyeStyle = (traits.brightness / 48 + traits.centerDelta / 36 + recog.confidence / 35 + classId) % 4;
    genes.hornStyle = (traits.contrast / 42 + abs(centerBrightnessDelta) / 18 + remoteHint.styleBias + classId + genes.species) % 4;
    genes.tailStyle = (traits.saturation / 38 + abs(centerSaturationDelta) / 14 + traits.r + traits.b + classId + genes.species) % 4;
    genes.auraPattern = (traits.centerDelta / 18 + traits.contrast / 45 + traits.g + remoteHint.styleBias + classId + traits.b / 64) % 4;
    genes.patternDensity = clamp_u8(2 + traits.saturation / 35 + traits.contrast / 50 + traits.centerDelta / 30);
    genes.accentColor = element_accent_color(genes.element);
    genes.seed = traits.seed ^ (static_cast<uint32_t>(recog.confidence) << 24) ^
                 (static_cast<uint32_t>(classId) << 20) ^
                 (static_cast<uint32_t>(genes.species & 0x03) << 18) ^
                 (static_cast<uint32_t>(traits.brightness & 0xff) << 12) ^
                 (static_cast<uint32_t>(traits.saturation & 0xff) << 8) ^
                 (static_cast<uint32_t>(traits.centerDelta & 0xff) << 4) ^
                 (static_cast<uint32_t>(abs(centerSaturationDelta) & 0x0f)) ^
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

static void draw_pet_variant_signature(ElementType element, const PetGenes& genes,
                                       int32_t cx, int32_t cy, uint16_t light, uint16_t dark)
{
    uint8_t visualVariant = pet_visual_variant(genes);
    uint16_t shine = tint_color(light, 35);
    uint16_t shadow = tint_color(light, -45);

    if (element == kWood) {
        if (visualVariant == 0) {
            draw_leaf(cx - 18, cy - 72, 10 + genes.hornStyle, light);
            draw_leaf(cx + 18, cy - 72, 10 + genes.hornStyle, light);
        } else if (visualVariant == 1) {
            CoreS3.Display.drawArc(cx - 48, cy + 40, 42, 18, 185, 345, light);
            CoreS3.Display.drawArc(cx + 36, cy + 32, 34, 14, 195, 340, shine);
            CoreS3.Display.fillCircle(cx + 55, cy + 23, 4, shine);
        } else {
            for (int i = 0; i < 4; ++i) {
                int32_t x = cx - 30 + i * 20;
                CoreS3.Display.drawLine(x, cy - 14, x + 4, cy + 46, light);
            }
        }
    } else if (element == kFire) {
        if (visualVariant == 0) {
            CoreS3.Display.fillTriangle(cx - 10, cy - 66, cx, cy - 96, cx + 10, cy - 66, shine);
            CoreS3.Display.fillTriangle(cx - 30, cy - 58, cx - 20, cy - 82, cx - 10, cy - 58, light);
            CoreS3.Display.fillTriangle(cx + 10, cy - 58, cx + 20, cy - 82, cx + 30, cy - 58, light);
        } else if (visualVariant == 1) {
            CoreS3.Display.fillTriangle(cx - 58, cy + 24, cx - 98, cy + 2, cx - 68, cy + 54, shine);
            CoreS3.Display.fillTriangle(cx + 58, cy + 24, cx + 98, cy + 2, cx + 68, cy + 54, shine);
        } else {
            for (int i = 0; i < 3; ++i) {
                int32_t x = cx + 58 + i * 14;
                CoreS3.Display.fillTriangle(x, cy + 30, x + 10, cy + 8, x + 20, cy + 32, light);
            }
        }
    } else if (element == kEarth) {
        if (visualVariant == 0) {
            CoreS3.Display.fillRoundRect(cx - 34, cy - 62, 24, 18, 5, light);
            CoreS3.Display.fillRoundRect(cx + 10, cy - 62, 24, 18, 5, light);
        } else if (visualVariant == 1) {
            for (int i = 0; i < 4; ++i) {
                CoreS3.Display.fillTriangle(cx - 44 + i * 28, cy - 6, cx - 28 + i * 28, cy - 30, cx - 12 + i * 28, cy - 6, light);
            }
        } else {
            CoreS3.Display.drawRoundRect(cx - 46, cy + 2, 92, 48, 9, light);
            CoreS3.Display.drawLine(cx - 34, cy + 17, cx + 34, cy + 17, shadow);
        }
    } else if (element == kMetal) {
        if (visualVariant == 0) {
            for (int i = 0; i < 4; ++i) {
                CoreS3.Display.fillCircle(cx - 36 + i * 24, cy + 38, 4, shine);
            }
        } else if (visualVariant == 1) {
            CoreS3.Display.drawCircle(cx, cy - 18, 20 + genes.hornStyle * 2, light);
            CoreS3.Display.drawCircle(cx, cy - 18, 10 + genes.tailStyle * 2, shine);
        } else {
            CoreS3.Display.drawLine(cx - 46, cy - 28, cx + 46, cy + 28, TFT_WHITE);
            CoreS3.Display.drawLine(cx - 28, cy - 44, cx + 28, cy + 44, shine);
        }
    } else {
        if (visualVariant == 0) {
            CoreS3.Display.fillTriangle(cx - 10, cy - 62, cx, cy - 86, cx + 10, cy - 62, light);
            CoreS3.Display.drawArc(cx - 28, cy - 32, 30, 14, 20, 160, shine);
            CoreS3.Display.drawArc(cx + 28, cy - 32, 30, 14, 20, 160, shine);
        } else if (visualVariant == 1) {
            for (int i = 0; i < 4; ++i) {
                CoreS3.Display.drawArc(cx - 52 + i * 34, cy + 30, 24, 12, 20, 160, light);
            }
        } else {
            for (int i = 0; i < 5; ++i) {
                CoreS3.Display.fillCircle(cx - 48 + i * 24, cy - 34 + (i % 2) * 8, 5 + (i % 3), shine);
            }
        }
    }

    if (genes.species == 2 && visualVariant == 2) {
        CoreS3.Display.drawCircle(cx, cy + 18, 12 + genes.patternDensity, dark);
    }
}

static void draw_pet_avatar(int16_t cx, int16_t cy, const PetGenes& genes, bool mirror)
{
    ElementType element = genes.element;
    uint16_t body = element_body_color(element);
    uint16_t accent = genes.accentColor;
    uint16_t dark = rgb(16, 18, 22);
    uint8_t visualVariant = pet_visual_variant(genes);
    int8_t dir = mirror ? -1 : 1;
    int16_t avatarBodyW = 34 + genes.bodyScale / 12 + (visualVariant == 1 ? 3 : 0);
    int16_t avatarBodyH = 24 + genes.bodyScale / 18 + (visualVariant == 2 ? 3 : 0);

    CoreS3.Display.fillEllipse(cx, cy + 20, 38, 9, rgb(8, 10, 14));
    CoreS3.Display.fillEllipse(cx, cy + 4, avatarBodyW, avatarBodyH, body);
    CoreS3.Display.fillCircle(cx - dir * 18, cy - 16, 19 + genes.bodyScale / 24, body);

    if (element == kFire) {
        CoreS3.Display.fillTriangle(cx - dir * 8, cy - 34, cx + dir * 4, cy - 58, cx + dir * 16, cy - 34, accent);
        CoreS3.Display.fillTriangle(cx + dir * 28, cy + 4, cx + dir * 56, cy - 14, cx + dir * 44, cy + 28, accent);
    } else if (element == kWater) {
        CoreS3.Display.fillEllipse(cx + dir * 32, cy + 4, 20, 11, accent);
        CoreS3.Display.fillTriangle(cx + dir * 45, cy + 4, cx + dir * 67, cy - 9, cx + dir * 67, cy + 17, accent);
    } else if (element == kWood) {
        draw_leaf(cx - dir * 28, cy - 31, 9 + genes.hornStyle, accent);
        draw_leaf(cx - dir * 8, cy - 35, 8 + genes.hornStyle, accent);
    } else if (element == kMetal) {
        CoreS3.Display.drawRoundRect(cx - 26, cy - 12, 42, 28, 5, accent);
        CoreS3.Display.fillTriangle(cx - dir * 32, cy - 24, cx - dir * 48, cy - 44, cx - dir * 20, cy - 34, accent);
    } else {
        CoreS3.Display.fillRoundRect(cx - 26, cy - 20, 52, 18, 5, accent);
        CoreS3.Display.drawLine(cx - 26, cy - 11, cx + 26, cy - 11, tint_color(accent, -35));
    }
    if (visualVariant == 1) {
        CoreS3.Display.fillCircle(cx + dir * 30, cy + 18, 5, tint_color(accent, 35));
    } else if (visualVariant == 2) {
        CoreS3.Display.drawCircle(cx, cy + 3, 18, tint_color(accent, 35));
    }

    int16_t eyeY = cy - 18 + (genes.eyeStyle == 3 ? 2 : 0);
    CoreS3.Display.fillCircle(cx - dir * 24, eyeY, 4, dark);
    CoreS3.Display.fillCircle(cx - dir * 8, eyeY, 4, dark);
    CoreS3.Display.fillCircle(cx - dir * 23, eyeY - 1, 1, TFT_WHITE);
    CoreS3.Display.fillCircle(cx - dir * 7, eyeY - 1, 1, TFT_WHITE);
    if (genes.mood == 3) {
        CoreS3.Display.drawLine(cx - dir * 31, eyeY - 7, cx - dir * 18, eyeY - 10, dark);
    }
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
    uint16_t light = tint_color(accent, 35);
    uint8_t visualVariant = pet_visual_variant(genes);
    bodyW += (visualVariant == 1) ? 6 : ((visualVariant == 2) ? -4 : 0);
    bodyH += (visualVariant == 2) ? 6 : 0;

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

    if (visualVariant == 1) {
        if (element == kWood) {
            draw_leaf(cx - bodyW / 2 + 10, cy + 18, 12, light);
            draw_leaf(cx + bodyW / 2 - 10, cy + 18, 12, light);
        } else if (element == kFire) {
            CoreS3.Display.fillTriangle(cx - bodyW / 2 - 8, cy + 26, cx - bodyW / 2 - 36, cy + 2, cx - bodyW / 2 - 18, cy + 48, light);
            CoreS3.Display.fillTriangle(cx + bodyW / 2 + 8, cy + 26, cx + bodyW / 2 + 36, cy + 2, cx + bodyW / 2 + 18, cy + 48, light);
        } else if (element == kEarth) {
            CoreS3.Display.fillCircle(cx - 30, cy + 48, 9, shade);
            CoreS3.Display.fillCircle(cx, cy + 52, 11, shade);
            CoreS3.Display.fillCircle(cx + 30, cy + 48, 9, shade);
        } else if (element == kMetal) {
            CoreS3.Display.drawCircle(cx - bodyW / 2 + 18, cy + 10, 14, light);
            CoreS3.Display.drawCircle(cx + bodyW / 2 - 18, cy + 10, 14, light);
        } else {
            CoreS3.Display.fillEllipse(cx - bodyW / 2 + 12, cy + 22, 15, 24, light);
            CoreS3.Display.fillEllipse(cx + bodyW / 2 - 12, cy + 22, 15, 24, light);
        }
    } else if (visualVariant == 2) {
        if (element == kWood) {
            CoreS3.Display.drawLine(cx - 46, cy + 46, cx + 46, cy + 28, light);
            CoreS3.Display.drawLine(cx - 38, cy + 58, cx + 38, cy + 42, light);
        } else if (element == kFire) {
            CoreS3.Display.fillTriangle(cx - 10, cy - 62, cx, cy - 92, cx + 10, cy - 62, light);
            CoreS3.Display.fillTriangle(cx + bodyW / 2 + 8, cy + 18, cx + bodyW / 2 + 46, cy - 18, cx + bodyW / 2 + 28, cy + 44, light);
        } else if (element == kEarth) {
            CoreS3.Display.drawRoundRect(cx - 42, cy - 4, 84, 56, 10, light);
            CoreS3.Display.drawLine(cx - 34, cy + 14, cx + 34, cy + 14, tint_color(light, -40));
        } else if (element == kMetal) {
            CoreS3.Display.drawRoundRect(cx - 48, cy - 20, 96, 70, 8, light);
            CoreS3.Display.drawLine(cx - 34, cy - 10, cx + 34, cy + 42, TFT_WHITE);
        } else {
            CoreS3.Display.fillCircle(cx + bodyW / 2 + 22, cy + 34, 10, light);
            CoreS3.Display.fillTriangle(cx + bodyW / 2 + 22, cy + 12, cx + bodyW / 2 + 10, cy + 32, cx + bodyW / 2 + 34, cy + 32, light);
        }
    }
}

static void draw_pet_features(ElementType element, const PetGenes& genes)
{
    int32_t cx = pet_cx(genes);
    int32_t cy = pet_cy(genes);
    uint16_t accent = genes.accentColor;
    uint16_t dark = rgb(18, 20, 24);
    uint16_t light = tint_color(accent, 38);
    int32_t spike = 18 + genes.hornStyle * 7;
    uint8_t visualVariant = pet_visual_variant(genes);

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

    if (visualVariant == 1) {
        if (element == kWood) {
            draw_leaf(cx, cy - 72, 13 + genes.hornStyle, light);
            CoreS3.Display.drawArc(cx - 40, cy + 54, 32, 16, 190, 330, light);
        } else if (element == kFire) {
            CoreS3.Display.fillTriangle(cx - 14, cy - 52, cx, cy - 82 - spike / 2, cx + 14, cy - 52, light);
            CoreS3.Display.fillTriangle(cx + 58, cy + 24, cx + 88, cy - 8, cx + 76, cy + 54, light);
        } else if (element == kEarth) {
            CoreS3.Display.fillRoundRect(cx - 44, cy - 66, 28, 22, 6, light);
            CoreS3.Display.fillRoundRect(cx + 16, cy - 66, 28, 22, 6, light);
        } else if (element == kMetal) {
            CoreS3.Display.drawCircle(cx, cy - 28, 16 + genes.hornStyle * 2, light);
            CoreS3.Display.drawLine(cx - 48, cy - 18, cx + 48, cy - 18, TFT_WHITE);
        } else {
            CoreS3.Display.drawArc(cx, cy + 8, 104, 48, 205, 335, light);
            CoreS3.Display.fillCircle(cx + 58, cy - 58, 6 + genes.tailStyle, light);
        }
    } else if (visualVariant == 2) {
        if (element == kWood) {
            for (int i = 0; i < 4; ++i) {
                draw_leaf(cx - 36 + i * 24, cy - 40 + (i % 2) * 8, 9, light);
            }
        } else if (element == kFire) {
            for (int i = 0; i < 3; ++i) {
                CoreS3.Display.fillTriangle(cx - 26 + i * 26, cy - 50, cx - 14 + i * 26, cy - 76 - spike / 2, cx - 2 + i * 26, cy - 50, light);
            }
        } else if (element == kEarth) {
            CoreS3.Display.drawLine(cx - 48, cy + 42, cx + 48, cy + 20, light);
            CoreS3.Display.drawLine(cx - 38, cy + 56, cx + 38, cy + 38, light);
        } else if (element == kMetal) {
            CoreS3.Display.drawRoundRect(cx - 54, cy - 36, 108, 78, 7, light);
            CoreS3.Display.drawCircle(cx - 22, cy + 6, 7, TFT_WHITE);
            CoreS3.Display.drawCircle(cx + 22, cy + 6, 7, TFT_WHITE);
        } else {
            for (int i = 0; i < 5; ++i) {
                CoreS3.Display.drawArc(cx - 60 + i * 30, cy - 42 + (i % 2) * 8, 18, 10, 20, 160, light);
            }
        }
    }

    draw_pet_variant_signature(element, genes, cx, cy, light, dark);

    for (int i = 0; i < genes.patternDensity; ++i) {
        int32_t x = cx - 42 + (hash_mix(genes.seed + i * 41) % 84);
        int32_t y = cy - 12 + (hash_mix(genes.seed + i * 59) % 58);
        CoreS3.Display.fillCircle(x, y, 2 + (i % 3), tint_color(accent, 25));
    }

    draw_pet_face(cx, cy, genes, dark);
}

static void draw_pet_action_marks(const PetGenes& genes, uint8_t profileId)
{
    if (profileId >= kSdActionProfileCount) {
        return;
    }
    const SdActionProfile& profile = sd_action_profiles[profileId];
    if (!profile.valid) {
        return;
    }
    int32_t cx = pet_cx(genes);
    int32_t cy = pet_cy(genes);
    uint16_t accent = genes.accentColor;
    uint16_t light = tint_color(accent, 42);
    for (uint8_t i = 0; i < profile.sparkle; ++i) {
        int32_t x = cx - 76 + (hash_mix(genes.seed + i * 71) % 152);
        int32_t y = cy - 82 + (hash_mix(genes.seed + i * 83) % 96);
        CoreS3.Display.drawCircle(x, y, 3 + (i % 3), light);
    }
    if (profile.bob != 0) {
        int32_t y = cy + 86 + (profile.bob > 0 ? 2 : -2);
        CoreS3.Display.drawArc(cx, y, 78, 18, 190, 350, tint_color(accent, -35));
    }
    if (profile.tilt != 0) {
        int32_t dx = profile.tilt * 8;
        CoreS3.Display.drawLine(cx - 62, cy + 76, cx - 28 + dx, cy + 66, light);
        CoreS3.Display.drawLine(cx + 28 + dx, cy + 66, cx + 62, cy + 76, light);
    }
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

    CoreS3.Display.fillRoundRect(8, 54, 304, 46, 8, rgb(20, 20, 24));
    CoreS3.Display.drawRoundRect(8, 54, 304, 46, 8, rgb(92, 92, 102));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(20, 20, 24));
    CoreS3.Display.setCursor(16, 62);
    snprintf(line, sizeof(line), "%s  识别:%u  存在:%u",
             recog.objectLabel, recog.confidence, recog.presenceScore);
    CoreS3.Display.print(line);
    CoreS3.Display.setCursor(16, 82);
    snprintf(line, sizeof(line), "元素:%s  色彩:%u,%u,%u  S:%ld",
             element_name(genes.element), traits.r, traits.g, traits.b,
             static_cast<long>(traits.saturation));
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

static uint16_t pet_growth_boost(uint8_t level, uint8_t stage)
{
    return max<uint8_t>(1, level) * 3 + stage * 8;
}

static uint16_t pet_battle_power(const SavedPet& pet)
{
    return pet_power(pet.genes) + pet_growth_boost(pet.level, pet.stage);
}

static uint16_t pet_battle_agility(const SavedPet& pet)
{
    return pet_agility(pet.genes) + pet_growth_boost(pet.level, pet.stage) / 2;
}

static uint16_t pet_battle_spirit(const SavedPet& pet)
{
    return pet_spirit(pet.genes) + pet_growth_boost(pet.level, pet.stage) / 2;
}

static uint32_t now_sec()
{
    return millis() / 1000UL;
}

static uint16_t level_xp_need(uint8_t level)
{
    return 40 + static_cast<uint16_t>(level) * 25;
}

static uint16_t xp_to_level_target(const SavedPet& pet, uint8_t targetLevel)
{
    targetLevel = min<uint8_t>(targetLevel, 30);
    if (pet.level >= targetLevel || pet.level >= 30) {
        return 0;
    }

    uint32_t total = 0;
    uint8_t level = max<uint8_t>(1, pet.level);
    uint16_t need = level_xp_need(level);
    total += need > pet.xp ? need - pet.xp : 0;
    for (uint8_t next = level + 1; next < targetLevel && next < 30; ++next) {
        total += level_xp_need(next);
    }
    return static_cast<uint16_t>(min<uint32_t>(9999, total));
}

static void format_growth_goal(char* out, size_t outSize, const SavedPet& pet)
{
    if (out == nullptr || outSize == 0) {
        return;
    }
    if (pet.level >= 30) {
        snprintf(out, outSize, "满级");
    } else if (pet.level < 5) {
        snprintf(out, outSize, "进化差%uXP", xp_to_level_target(pet, 5));
    } else if (pet.level < 12) {
        snprintf(out, outSize, "进化差%uXP", xp_to_level_target(pet, 12));
    } else {
        snprintf(out, outSize, "升级差%uXP", xp_to_level_target(pet, pet.level + 1));
    }
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

static bool apply_growth(SavedPet& pet, uint16_t* xpGain)
{
    if (xpGain != nullptr) {
        *xpGain = 0;
    }
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
    uint16_t gained = static_cast<uint16_t>(intervals * 3);
    if (xpGain != nullptr) {
        *xpGain = gained;
    }
    pet.xp = min<uint16_t>(9999, pet.xp + gained);
    normalize_pet_level(pet);
    return true;
}

static bool growth_notice_recent(uint32_t windowSec)
{
    uint32_t now = now_sec();
    return last_growth_event_sec != 0 &&
           now >= last_growth_event_sec &&
           now - last_growth_event_sec <= windowSec;
}

static void format_growth_notice(char* out, size_t outSize)
{
    if (out == nullptr || outSize == 0) {
        return;
    }
    const char* growth = last_growth_stage_up ? "进化" : (last_growth_level_up ? "升级" : "成长");
    snprintf(out, outSize, "#%u %s XP+%u Lv%u %s",
             static_cast<unsigned>(last_growth_pet_index + 1),
             growth,
             last_growth_xp_gain,
             last_growth_level,
             stage_name(last_growth_stage));
}

static uint16_t seconds_until_growth(const SavedPet& pet)
{
    uint32_t now = now_sec();
    if (pet.lastGrowthSec == 0 || pet.lastGrowthSec > now) {
        return 0;
    }
    uint32_t elapsed = now - pet.lastGrowthSec;
    uint32_t remainder = elapsed % kGrowthIntervalSec;
    return static_cast<uint16_t>(remainder == 0 ? kGrowthIntervalSec : (kGrowthIntervalSec - remainder));
}

static uint16_t growth_wait_progress(const SavedPet& pet)
{
    uint16_t remaining = seconds_until_growth(pet);
    if (remaining >= kGrowthIntervalSec) {
        return 0;
    }
    return static_cast<uint16_t>(kGrowthIntervalSec - remaining);
}

static void draw_growth_goal_badge(int16_t x, int16_t y, int16_t w, const SavedPet& pet, uint16_t accent)
{
    char goal[24];
    format_growth_goal(goal, sizeof(goal), pet);
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(x, y, w, 24, 7, bg);
    CoreS3.Display.drawRoundRect(x, y, w, 24, 7, accent);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_CYAN, bg);
    CoreS3.Display.setCursor(x + 7, y + 4);
    CoreS3.Display.print("NEXT");
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 42, y + 4);
    CoreS3.Display.print(goal);
    draw_meter(x + 7, y + 18, static_cast<int16_t>(w - 14), 4,
               growth_wait_progress(pet), kGrowthIntervalSec, accent);
}

static void draw_release_confirm_summary(int16_t x, int16_t y, int16_t w,
                                         const SavedPet& pet, uint8_t index,
                                         uint8_t count, bool active)
{
    uint16_t bg = rgb(32, 18, 22);
    CoreS3.Display.fillRoundRect(x, y, w, 24, 7, bg);
    CoreS3.Display.drawRoundRect(x, y, w, 24, 7, pet.genes.accentColor);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 7, y + 4);
    CoreS3.Display.printf("#%u/%u %s", index + 1, count, active ? "出战" : "收藏");
    CoreS3.Display.setTextColor(TFT_CYAN, bg);
    CoreS3.Display.setCursor(x + 74, y + 4);
    CoreS3.Display.print(stage_name(pet.stage));
    CoreS3.Display.setCursor(x + w - 34, y + 4);
    CoreS3.Display.printf("%u%%", win_rate_percent(pet));
    draw_meter(x + 7, y + 18, static_cast<int16_t>(w - 14), 4,
               pet.xp, level_xp_need(pet.level), pet.genes.accentColor);
}

static void refresh_backpack_growth(bool force)
{
    if (!force && millis() - last_growth_check_ms < 5000) {
        return;
    }
    last_growth_check_ms = millis();
    bool changed = false;
    for (uint8_t i = 0; i < backpack.count; ++i) {
        uint8_t oldLevel = backpack.pets[i].level;
        uint8_t oldStage = backpack.pets[i].stage;
        uint16_t gained = 0;
        bool petChanged = apply_growth(backpack.pets[i], &gained);
        changed |= petChanged;
        if (gained > 0) {
            last_growth_event_sec = now_sec();
            last_growth_pet_index = i;
            last_growth_xp_gain = gained;
            last_growth_level = backpack.pets[i].level;
            last_growth_stage = backpack.pets[i].stage;
            last_growth_level_up = backpack.pets[i].level > oldLevel;
            last_growth_stage_up = backpack.pets[i].stage > oldStage;
        }
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
    uint16_t growthBoost = pet_growth_boost(active_pet_level(), active_pet_stage());
    packet.power = pet_power(genes) + growthBoost;
    packet.agility = pet_agility(genes) + growthBoost / 2;
    packet.spirit = pet_spirit(genes) + growthBoost / 2;
    return packet;
}

static ElementType packet_element(const BattlePetPacket& packet)
{
    return static_cast<ElementType>(packet.element % 5);
}

static PetGenes genes_from_packet(const BattlePetPacket& packet)
{
    PetGenes genes = {};
    genes.element = packet_element(packet);
    genes.species = packet.species % 3;
    genes.mood = packet.mood;
    genes.bodyScale = packet.bodyScale;
    genes.eyeStyle = packet.eyeStyle;
    genes.hornStyle = packet.hornStyle;
    genes.tailStyle = packet.tailStyle;
    genes.auraPattern = packet.auraPattern;
    genes.patternDensity = packet.patternDensity;
    genes.accentColor = packet.accentColor;
    genes.seed = packet.seed;
    return genes;
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

static const char* battle_skill_name(ElementType element, uint8_t skill)
{
    static const char* const kNames[5][3] = {
        { "Vine Guard", "Sprout Rush", "Antler Focus" },
        { "Flare Dash", "Spark Horn", "Ember Tail" },
        { "Stone Shell", "Hill Roll", "Root Stance" },
        { "Ring Guard", "Mirror Edge", "Armor Tap" },
        { "Wave Glide", "Drop Guard", "Fin Pulse" },
    };
    return kNames[static_cast<uint8_t>(element) % 5][skill % 3];
}

static uint8_t battle_skill_index_for(const BattlePetPacket& self, const BattlePetPacket& other, uint32_t battleId)
{
    return static_cast<uint8_t>(hash_mix(self.seed ^ (other.seed << 1) ^ battleId ^ self.deviceId) % 3);
}

static int32_t battle_power_round(const BattlePetPacket& packet)
{
    return static_cast<int32_t>(packet.power) * 2 + packet.agility;
}

static int32_t battle_luck(const BattlePetPacket& self, const BattlePetPacket& other)
{
    return static_cast<int32_t>(hash_mix(self.seed ^ other.seed ^ self.deviceId ^ (other.deviceId << 1)) % 17) - 8;
}

static int32_t battle_spirit_round(const BattlePetPacket& self, const BattlePetPacket& other)
{
    return static_cast<int32_t>(self.spirit) + battle_luck(self, other);
}

static uint32_t battle_sync_id(const BattlePetPacket& left, const BattlePetPacket& right)
{
    const BattlePetPacket* first = &left;
    const BattlePetPacket* second = &right;
    if (right.deviceId < left.deviceId ||
        (right.deviceId == left.deviceId && right.seq < left.seq)) {
        first = &right;
        second = &left;
    }

    uint32_t value = 0xB4771E03UL;
    value = hash_mix(value ^ first->deviceId ^ (first->seq << 7) ^ first->seed);
    value = hash_mix(value ^ second->deviceId ^ (second->seq << 11) ^ second->seed);
    value = hash_mix(value ^
                     (static_cast<uint32_t>(first->power) << 24) ^
                     (static_cast<uint32_t>(first->agility) << 16) ^
                     (static_cast<uint32_t>(second->power) << 8) ^
                     second->agility ^
                     (static_cast<uint32_t>(first->spirit + second->spirit) << 3));
    return value;
}

static uint8_t friend_bond_rank(uint8_t score)
{
    if (score >= 100) {
        return 2;
    }
    if (score >= 60) {
        return 1;
    }
    return 0;
}

static const char* friend_bond_name_for(uint32_t peerId, uint8_t score)
{
    if (peerId == 0) {
        return "等待好友";
    }
    switch (friend_bond_rank(score)) {
    case 2: return "密友";
    case 1: return "好友";
    default:
        return "新朋友";
    }
}

static int8_t find_friend_slot(uint32_t peerId)
{
    if (peerId == 0) {
        return -1;
    }
    for (uint8_t i = 0; i < kLocalFriendSlots; ++i) {
        if (local_friends[i].peerId == peerId) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

static uint8_t choose_friend_slot()
{
    for (uint8_t i = 0; i < kLocalFriendSlots; ++i) {
        if (local_friends[i].peerId == 0) {
            return i;
        }
    }

    uint8_t oldest = 0;
    for (uint8_t i = 1; i < kLocalFriendSlots; ++i) {
        if (local_friends[i].lastBattleSec < local_friends[oldest].lastBattleSec) {
            oldest = i;
        }
    }
    return oldest;
}

static LocalFriendRecord* ensure_friend_slot(uint32_t peerId)
{
    if (peerId == 0) {
        return nullptr;
    }
    int8_t existing = find_friend_slot(peerId);
    if (existing >= 0) {
        return &local_friends[existing];
    }

    uint8_t slot = choose_friend_slot();
    bool wasEmpty = local_friends[slot].peerId == 0;
    local_friends[slot] = {};
    local_friends[slot].peerId = peerId;
    if (wasEmpty && local_friend_count < kLocalFriendSlots) {
        ++local_friend_count;
    }
    return &local_friends[slot];
}

static void activate_friend_record(const LocalFriendRecord& record)
{
    last_friend_peer_id = record.peerId;
    last_friend_battle_sec = record.lastBattleSec;
    friend_rematch_streak = record.rematchStreak;
    friend_battle_count = record.battleCount;
    friend_score = record.score;
}

static void clear_active_friend_record()
{
    last_friend_peer_id = 0;
    last_friend_battle_sec = 0;
    friend_rematch_streak = 0;
    friend_battle_count = 0;
    friend_score = 0;
    last_friend_added = false;
    last_friend_bond_up = false;
    last_friend_notice[0] = '\0';
}

static void recalc_local_friend_count()
{
    local_friend_count = 0;
    for (uint8_t i = 0; i < kLocalFriendSlots; ++i) {
        LocalFriendRecord& record = local_friends[i];
        if (record.peerId == 0) {
            record = {};
            continue;
        }
        record.score = min<uint8_t>(100, record.score);
        record.battleCount = min<uint8_t>(99, record.battleCount);
        record.rematchStreak = min<uint8_t>(3, record.rematchStreak);
        ++local_friend_count;
    }
}

static void save_friendship_state()
{
    friend_prefs.putBytes("friends", local_friends, sizeof(local_friends));
    friend_prefs.putUChar("count", local_friend_count);
    friend_prefs.putULong("last_peer", last_friend_peer_id);
}

static void load_friendship_state()
{
    friend_prefs.begin("wuxingfr", false);
    if (friend_prefs.getBytesLength("friends") == sizeof(local_friends)) {
        friend_prefs.getBytes("friends", local_friends, sizeof(local_friends));
    } else {
        memset(local_friends, 0, sizeof(local_friends));
    }
    recalc_local_friend_count();

    int8_t active = find_friend_slot(friend_prefs.getULong("last_peer", 0));
    if (active < 0) {
        uint32_t newestSec = 0;
        for (uint8_t i = 0; i < kLocalFriendSlots; ++i) {
            if (local_friends[i].peerId != 0 && local_friends[i].lastBattleSec >= newestSec) {
                newestSec = local_friends[i].lastBattleSec;
                active = static_cast<int8_t>(i);
            }
        }
    }
    if (active >= 0) {
        activate_friend_record(local_friends[active]);
    } else {
        clear_active_friend_record();
    }
    save_friendship_state();
}

static uint16_t record_friendship_bonus(const BattlePetPacket& opponent)
{
    uint32_t now = now_sec();
    LocalFriendRecord* record = ensure_friend_slot(opponent.deviceId);
    if (record == nullptr) {
        last_friend_added = false;
        last_friend_bond_up = false;
        last_friend_notice[0] = '\0';
        return 0;
    }
    bool newFriend = record->battleCount == 0;
    uint8_t beforeRank = friend_bond_rank(record->score);
    bool rematch = record->lastBattleSec != 0 &&
                   now >= record->lastBattleSec &&
                   now - record->lastBattleSec <= kRematchWindowSec;
    if (rematch) {
        record->rematchStreak = min<uint8_t>(3, record->rematchStreak + 1);
    } else {
        record->rematchStreak = 0;
    }
    record->battleCount = min<uint8_t>(99, record->battleCount + 1);
    uint8_t scoreGain = rematch ? static_cast<uint8_t>(18 + record->rematchStreak * 4) : 14;
    record->score = min<uint8_t>(100, static_cast<uint8_t>(record->score + scoreGain));
    record->lastBattleSec = now;
    activate_friend_record(*record);
    uint8_t afterRank = friend_bond_rank(record->score);
    last_friend_added = newFriend;
    last_friend_bond_up = afterRank > beforeRank;
    if (last_friend_bond_up) {
        snprintf(last_friend_notice, sizeof(last_friend_notice), "羁绊升格:%s",
                 friend_bond_name_for(record->peerId, record->score));
    } else if (last_friend_added) {
        snprintf(last_friend_notice, sizeof(last_friend_notice), "新好友已添加");
    } else if (rematch) {
        snprintf(last_friend_notice, sizeof(last_friend_notice), "连战奖励生效");
    } else {
        snprintf(last_friend_notice, sizeof(last_friend_notice), "友情+%u", scoreGain);
    }
    save_friendship_state();
    return rematch ? static_cast<uint16_t>(record->rematchStreak * kRematchXpStep) : 0;
}

static const char* friend_bond_name()
{
    return friend_bond_name_for(last_friend_peer_id, friend_score);
}

static uint8_t friendship_score()
{
    return last_friend_peer_id == 0 ? 0 : friend_score;
}

static bool friendship_rematch_window_open()
{
    uint32_t now = now_sec();
    return last_friend_peer_id != 0 &&
           last_friend_battle_sec != 0 &&
           now >= last_friend_battle_sec &&
           now - last_friend_battle_sec <= kRematchWindowSec;
}

static uint8_t next_friendship_score_gain()
{
    if (!friendship_rematch_window_open()) {
        return 14;
    }
    uint8_t nextStreak = min<uint8_t>(3, friend_rematch_streak + 1);
    return static_cast<uint8_t>(18 + nextStreak * 4);
}

static uint8_t next_friendship_projected_rank()
{
    if (last_friend_peer_id == 0) {
        return 0;
    }
    uint8_t projectedScore = min<uint8_t>(100, static_cast<uint8_t>(friend_score + next_friendship_score_gain()));
    return friend_bond_rank(projectedScore);
}

static void maybe_play_friendship_hint_sound()
{
    if (screen_mode != kScreenMatch || last_friend_peer_id == 0) {
        return;
    }
    uint8_t currentRank = friend_bond_rank(friend_score);
    uint8_t projectedRank = next_friendship_projected_rank();
    if (projectedRank <= currentRank) {
        return;
    }
    if (last_friendship_hint_sound_peer == last_friend_peer_id &&
        last_friendship_hint_sound_rank == projectedRank) {
        return;
    }
    last_friendship_hint_sound_peer = last_friend_peer_id;
    last_friendship_hint_sound_rank = projectedRank;
    play_scene_sound(kSoundFriend);
}

static const char* friendship_prompt()
{
    static char prompt[32];
    if (last_friend_peer_id == 0) {
        return "首次对战后自动添加好友";
    }
    if (friend_score >= 100) {
        return "已结成密友";
    }
    uint8_t projectedScore = min<uint8_t>(100, static_cast<uint8_t>(friend_score + next_friendship_score_gain()));
    if (friend_score < 60 && projectedScore >= 60) {
        return "再战可成好友";
    }
    if (friend_score < 100 && projectedScore >= 100) {
        return "再战可成密友";
    }
    if (friend_rematch_streak > 0) {
        return "连战奖励生效";
    }
    if (friend_score >= 60) {
        snprintf(prompt, sizeof(prompt), "距密友还%u", static_cast<uint8_t>(100 - friend_score));
        return prompt;
    }
    snprintf(prompt, sizeof(prompt), "距好友还%u", static_cast<uint8_t>(60 - friend_score));
    return prompt;
}

static const char* friendship_goal_badge()
{
    static char badge[20];
    if (last_friend_peer_id == 0) {
        return "待添加";
    }
    if (friend_score >= 100) {
        return "密友";
    }
    if (friend_score >= 60) {
        snprintf(badge, sizeof(badge), "密友差%u", static_cast<uint8_t>(100 - friend_score));
        return badge;
    }
    snprintf(badge, sizeof(badge), "好友差%u", static_cast<uint8_t>(60 - friend_score));
    return badge;
}

static uint16_t friendship_color()
{
    if (last_friend_peer_id == 0) {
        return rgb(78, 82, 92);
    }
    switch (friend_bond_rank(friend_score)) {
    case 2: return TFT_GREEN;
    case 1: return TFT_YELLOW;
    default:
        return TFT_CYAN;
    }
}

static void draw_friendship_badge(int16_t x, int16_t y, int16_t w)
{
    uint16_t color = friendship_color();
    uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(x, y, w, 18, 6, bg);
    CoreS3.Display.drawRoundRect(x, y, w, 18, 6, color);
    CoreS3.Display.fillCircle(x + 9, y + 9, 4, color);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, bg);
    CoreS3.Display.setCursor(x + 17, y + 3);
    CoreS3.Display.print(friend_bond_name());
    if (last_friend_peer_id != 0 && w >= 70) {
        CoreS3.Display.setTextColor(color, bg);
        CoreS3.Display.setCursor(x + w - 26, y + 3);
        CoreS3.Display.printf("%u", friendship_score());
    }
}

static uint32_t current_friend_candidate_id()
{
    if (battle_peer_id != 0 &&
        last_peer_seen_ms != 0 &&
        millis() - last_peer_seen_ms <= kBattlePeerTimeoutMs) {
        return battle_peer_id;
    }
    return last_friend_peer_id;
}

static bool add_current_friend(char* out, size_t outSize)
{
    uint32_t peerId = current_friend_candidate_id();
    if (peerId == 0) {
        if (out != nullptr && outSize > 0) {
            snprintf(out, outSize, "暂无可添加对手");
        }
        return false;
    }

    int8_t existing = find_friend_slot(peerId);
    LocalFriendRecord* record = ensure_friend_slot(peerId);
    if (record == nullptr) {
        if (out != nullptr && outSize > 0) {
            snprintf(out, outSize, "好友添加失败");
        }
        return false;
    }

    bool added = existing < 0;
    if (record->score < 5) {
        record->score = 5;
    }
    activate_friend_record(*record);
    last_friend_added = added;
    last_friend_bond_up = false;
    snprintf(last_friend_notice, sizeof(last_friend_notice), added ? "好友已添加" : "好友已在列表");
    if (out != nullptr && outSize > 0) {
        snprintf(out, outSize, "%s #%06lX",
                 last_friend_notice,
                 static_cast<unsigned long>(peerId & 0xffffffUL));
    }
    save_friendship_state();
    play_scene_sound(added ? kSoundFriend : kSoundSelect);
    return true;
}

static bool friend_rematch_available()
{
    return friendship_rematch_window_open();
}

static uint16_t next_rematch_xp_bonus()
{
    if (!friend_rematch_available()) {
        return 0;
    }
    uint8_t nextStreak = min<uint8_t>(3, friend_rematch_streak + 1);
    return static_cast<uint16_t>(nextStreak * kRematchXpStep);
}

static const char* battle_outcome_label(const char* outcome)
{
    if (outcome == nullptr || outcome[0] == '\0') {
        return "";
    }
    if (strcmp(outcome, "win") == 0) {
        return "获胜";
    }
    if (strcmp(outcome, "loss") == 0) {
        return "惜败";
    }
    if (strcmp(outcome, "draw") == 0) {
        return "平局";
    }
    return outcome;
}

static const char* battle_advantage_label(int32_t swing)
{
    if (swing > 0) {
        return "我方五行优势";
    }
    if (swing < 0) {
        return "对手五行优势";
    }
    return "五行均势";
}

static void record_battle_replay()
{
    if (!app_last_battle_result_valid || kBattleReplayCapacity == 0) {
        return;
    }
    BattleReplayRecord& record = battle_replays[battle_replay_next];
    record.valid = true;
    record.timeSec = millis() / 1000UL;
    record.battleId = app_last_battle_id;
    snprintf(record.outcome, sizeof(record.outcome), "%s", app_last_battle_outcome);
    record.myScore = app_last_battle_my_score;
    record.peerScore = app_last_battle_peer_score;
    record.scoreDiff = app_last_battle_score_diff;
    record.powerDiff = app_last_battle_power_diff;
    record.elementSwing = app_last_battle_element_swing;
    record.spiritDiff = app_last_battle_spirit_diff;
    record.xpGained = app_last_battle_xp;
    record.friendBonus = app_last_battle_friend_bonus;
    snprintf(record.mySkill, sizeof(record.mySkill), "%s", app_last_battle_my_skill);
    snprintf(record.opponentSkill, sizeof(record.opponentSkill), "%s", app_last_battle_peer_skill);
    snprintf(record.opponentSpecies, sizeof(record.opponentSpecies), "%s", app_last_opponent_species);
    snprintf(record.opponentElement, sizeof(record.opponentElement), "%s", app_last_opponent_element);
    record.opponentLevel = app_last_opponent_level;
    record.levelUp = app_last_battle_level_up;
    record.stageUp = app_last_battle_stage_up;
    battle_replay_next = static_cast<uint8_t>((battle_replay_next + 1) % kBattleReplayCapacity);
    if (battle_replay_count < kBattleReplayCapacity) {
        ++battle_replay_count;
    }
}

static uint8_t current_battle_clash_phase()
{
    if (!battle_result_pending || battle_clash_started_ms == 0) {
        return 0;
    }
    uint32_t elapsed = millis() - battle_clash_started_ms;
    return elapsed < (kBattleClashMs / 3) ? 0 : (elapsed < (kBattleClashMs * 2 / 3) ? 1 : 2);
}

static const char* battle_round_title(uint8_t phase)
{
    if (phase == 0) {
        return "第1回合  力量交锋";
    }
    if (phase == 1) {
        return "第2回合  元素相克";
    }
    return "最终回合  气势爆发";
}

static int32_t battle_round_diff_for_phase(uint8_t phase, const BattlePetPacket& mine, const BattlePetPacket& opponent)
{
    if (phase == 0) {
        return battle_power_round(mine) - battle_power_round(opponent);
    }
    if (phase == 1) {
        return element_advantage(packet_element(mine), packet_element(opponent));
    }
    return battle_spirit_round(mine, opponent) - battle_spirit_round(opponent, mine);
}

static void battle_round_scores_for_phase(uint8_t phase, const BattlePetPacket& mine, const BattlePetPacket& opponent,
                                          int32_t* mineScore, int32_t* opponentScore)
{
    if (mineScore == nullptr || opponentScore == nullptr) {
        return;
    }
    if (phase == 0) {
        *mineScore = battle_power_round(mine);
        *opponentScore = battle_power_round(opponent);
        return;
    }
    if (phase == 1) {
        int32_t swing = element_advantage(packet_element(mine), packet_element(opponent));
        *mineScore = max<int32_t>(0, swing);
        *opponentScore = max<int32_t>(0, -swing);
        return;
    }
    *mineScore = battle_spirit_round(mine, opponent);
    *opponentScore = battle_spirit_round(opponent, mine);
}

static uint16_t award_battle_xp(int32_t diff, uint16_t bonusXp)
{
    SavedPet* pet = selected_pet();
    if (pet == nullptr) {
        app_last_battle_level_up = false;
        app_last_battle_stage_up = false;
        return 0;
    }
    uint8_t oldLevel = pet->level;
    uint8_t oldStage = pet->stage;
    uint16_t gained = (abs(diff) <= 6) ? 16 : ((diff > 0) ? 35 : 8);
    gained = min<uint16_t>(80, gained + bonusXp);
    pet->xp = min<uint16_t>(9999, pet->xp + gained);
    ++pet->battles;
    if (diff > 6) {
        ++pet->wins;
    }
    normalize_pet_level(*pet);
    app_last_battle_level_up = pet->level > oldLevel;
    app_last_battle_stage_up = pet->stage > oldStage;
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
    draw_game_title("对手就绪", TFT_CYAN, rgb(10, 14, 22), 16, 22);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(10, 14, 22));
    CoreS3.Display.setCursor(16, 52);
    CoreS3.Display.printf("%s  Lv%u", species_name_by(packet_element(packet), packet.species), packet.level);
    CoreS3.Display.setCursor(16, 78);
    CoreS3.Display.printf("%s", element_name(packet_element(packet)));
    CoreS3.Display.setCursor(16, 122);
    CoreS3.Display.print("请先捕捉或选择伙伴");
    draw_action_footer("背包", "休闲", "拍照", TFT_CYAN);
    display_hold_until_ms = millis() + 3500;
    play_scene_sound(kSoundBattleClash);
}

static void draw_battle_clash(const BattlePetPacket& opponent)
{
    screen_mode = kScreenBattle;
    battle_runtime_state = kBattleStateBattling;
    app_last_battle_result_valid = false;
    app_last_battle_level_up = false;
    app_last_battle_stage_up = false;
    battle_exit_visible = false;
    BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);
    PetGenes opponentGenes = genes_from_packet(opponent);
    int32_t myRoundScore = 0;
    int32_t peerRoundScore = 0;
    battle_round_scores_for_phase(0, mine, opponent, &myRoundScore, &peerRoundScore);
    int32_t powerDiff = myRoundScore - peerRoundScore;
    app_last_battle_id = battle_sync_id(mine, opponent);
    snprintf(app_last_opponent_species, sizeof(app_last_opponent_species), "%s",
             species_name_by(packet_element(opponent), opponent.species));
    snprintf(app_last_opponent_element, sizeof(app_last_opponent_element), "%s",
             element_name(packet_element(opponent)));
    app_last_opponent_level = opponent.level;

    CoreS3.Display.fillScreen(rgb(9, 10, 15));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("宠物出场", TFT_CYAN, rgb(9, 10, 15), 18, 12);
    draw_battle_round_track(208, 24, 0, powerDiff, element_accent_color(packet_element(mine)));

    CoreS3.Display.fillRoundRect(10, 48, 145, 86, 8, rgb(24, 28, 36));
    CoreS3.Display.fillRoundRect(165, 48, 145, 86, 8, rgb(24, 28, 36));
    CoreS3.Display.drawRoundRect(10, 48, 145, 86, 8, element_accent_color(packet_element(mine)));
    CoreS3.Display.drawRoundRect(165, 48, 145, 86, 8, element_accent_color(packet_element(opponent)));
    draw_element_badge(112, 58, packet_element(mine), rgb(24, 28, 36));
    draw_element_badge(267, 58, packet_element(opponent), rgb(24, 28, 36));
    draw_pet_avatar(84, 110, local_pet, false);
    draw_pet_avatar(238, 110, opponentGenes, true);
    draw_battle_impact(160, 100, 0, powerDiff, element_accent_color(packet_element(mine)));

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(24, 28, 36));
    CoreS3.Display.setCursor(20, 60);
    CoreS3.Display.print(species_name(local_pet));
    CoreS3.Display.setCursor(20, 86);
    CoreS3.Display.printf("我方 Lv%u", mine.level);
    CoreS3.Display.setCursor(176, 60);
    CoreS3.Display.printf("%s", species_name_by(packet_element(opponent), opponent.species));
    CoreS3.Display.setCursor(176, 86);
    CoreS3.Display.printf("对手 Lv%u", opponent.level);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(9, 10, 15));
    CoreS3.Display.setCursor(114, 120);
    CoreS3.Display.printf("同步局%04lX", static_cast<unsigned long>(app_last_battle_id & 0xffffUL));

    draw_battle_score_plate(52, 138, 216, battle_round_title(0),
                            myRoundScore, peerRoundScore, 80,
                            element_accent_color(packet_element(mine)));
    draw_action_footer("背包", "休闲", "拍照", TFT_CYAN);
    display_hold_until_ms = millis() + 2500;
    battle_clash_audio_phase = 0;
    play_scene_sound(kSoundBattleClash);
    uint8_t opponentStage = opponent.level >= 12 ? 2 : (opponent.level >= 5 ? 1 : 0);
    play_pet_sound(opponentGenes, opponent.level, opponentStage);
}

static void draw_battle_result(const BattlePetPacket& opponent)
{
    screen_mode = kScreenBattle;
    battle_runtime_state = kBattleStateBattling;
    PetGenes localBattleGenes = local_pet;
    BattlePetPacket mine = make_battle_packet(localBattleGenes, local_pet_sequence);
    int32_t myScore = battle_score(mine, opponent);
    int32_t peerScore = battle_score(opponent, mine);
    int32_t diff = myScore - peerScore;
    int32_t powerDiff = battle_power_round(mine) - battle_power_round(opponent);
    int32_t elementSwing = element_advantage(packet_element(mine), packet_element(opponent));
    int32_t spiritDiff = battle_spirit_round(mine, opponent) - battle_spirit_round(opponent, mine);
    uint32_t battleId = battle_sync_id(mine, opponent);
    uint8_t mySkill = battle_skill_index_for(mine, opponent, battleId);
    uint8_t peerSkill = battle_skill_index_for(opponent, mine, battleId);
    const char* resultCode = (abs(diff) <= 6) ? "draw" : ((diff > 0) ? "win" : "loss");
    const char* result = battle_outcome_label(resultCode);
    uint16_t resultColor = (abs(diff) <= 6) ? TFT_YELLOW : ((diff > 0) ? TFT_GREEN : TFT_RED);
    PetGenes opponentGenes = genes_from_packet(opponent);
    uint16_t friendBonus = record_friendship_bonus(opponent);
    uint16_t gainedXp = award_battle_xp(diff, friendBonus);
    app_last_battle_result_valid = true;
    snprintf(app_last_battle_outcome, sizeof(app_last_battle_outcome), "%s",
             resultCode);
    app_last_battle_id = battleId;
    app_last_battle_my_score = myScore;
    app_last_battle_peer_score = peerScore;
    app_last_battle_score_diff = diff;
    app_last_battle_power_diff = powerDiff;
    app_last_battle_element_swing = elementSwing;
    app_last_battle_spirit_diff = spiritDiff;
    app_last_battle_xp = gainedXp;
    app_last_battle_friend_bonus = friendBonus;
    snprintf(app_last_battle_my_skill, sizeof(app_last_battle_my_skill), "%s",
             battle_skill_name(packet_element(mine), mySkill));
    snprintf(app_last_battle_peer_skill, sizeof(app_last_battle_peer_skill), "%s",
             battle_skill_name(packet_element(opponent), peerSkill));
    snprintf(app_last_opponent_species, sizeof(app_last_opponent_species), "%s",
             species_name_by(packet_element(opponent), opponent.species));
    snprintf(app_last_opponent_element, sizeof(app_last_opponent_element), "%s",
             element_name(packet_element(opponent)));
    app_last_opponent_level = opponent.level;
    record_battle_replay();
    append_app_log(result);
    const SavedPet* updatedPet = selected_pet_const();

    CoreS3.Display.fillScreen(rgb(9, 10, 15));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("战斗结算", resultColor, rgb(9, 10, 15), 18, 12);
    draw_battle_result_card(106, 10, 202, result, diff, battleId, resultColor);

    CoreS3.Display.fillRoundRect(10, 46, 145, 104, 8, rgb(24, 28, 36));
    CoreS3.Display.fillRoundRect(165, 46, 145, 104, 8, rgb(24, 28, 36));
    CoreS3.Display.drawRoundRect(10, 46, 145, 104, 8, element_accent_color(packet_element(mine)));
    CoreS3.Display.drawRoundRect(165, 46, 145, 104, 8, element_accent_color(packet_element(opponent)));
    draw_element_badge(112, 54, packet_element(mine), rgb(24, 28, 36));
    draw_element_badge(267, 54, packet_element(opponent), rgb(24, 28, 36));
    draw_pet_avatar(92, 116, localBattleGenes, false);
    draw_pet_avatar(246, 116, opponentGenes, true);

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(24, 28, 36));
    CoreS3.Display.setCursor(20, 56);
    CoreS3.Display.print("我方");
    CoreS3.Display.setCursor(20, 76);
    CoreS3.Display.printf("Lv%u  %s", mine.level, element_name(packet_element(mine)));
    CoreS3.Display.setCursor(20, 96);
    CoreS3.Display.printf("力:%u 速:%u", mine.power, mine.agility);
    CoreS3.Display.setCursor(20, 116);
    CoreS3.Display.printf("心:%u", mine.spirit);
    CoreS3.Display.setCursor(20, 138);
    CoreS3.Display.printf("分数:%ld", static_cast<long>(myScore));

    CoreS3.Display.setCursor(176, 56);
    CoreS3.Display.print("对手");
    CoreS3.Display.setCursor(176, 76);
    CoreS3.Display.printf("Lv%u  %s", opponent.level, element_name(packet_element(opponent)));
    CoreS3.Display.setCursor(176, 96);
    CoreS3.Display.printf("力:%u 速:%u", opponent.power, opponent.agility);
    CoreS3.Display.setCursor(176, 116);
    CoreS3.Display.printf("心:%u", opponent.spirit);
    CoreS3.Display.setCursor(176, 138);
    CoreS3.Display.printf("分数:%ld", static_cast<long>(peerScore));

    CoreS3.Display.fillRoundRect(10, 150, 300, 34, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(18, 156);
    if (last_friend_bond_up) {
        CoreS3.Display.printf("%s XP+%u 友:%u", last_friend_notice, gainedXp, friendship_score());
    } else if (last_friend_added) {
        CoreS3.Display.printf("新好友 XP+%u 友:%u", gainedXp, friendship_score());
    } else if (app_last_battle_stage_up && updatedPet != nullptr) {
        CoreS3.Display.printf("XP+%u  进化:%s  友情 %u", gainedXp, stage_name(updatedPet->stage), friendship_score());
    } else if (app_last_battle_level_up && updatedPet != nullptr) {
        CoreS3.Display.printf("XP+%u  升级 Lv%u  友情 %u", gainedXp, updatedPet->level, friendship_score());
    } else if (friendBonus > 0) {
        CoreS3.Display.printf("XP+%u  友情 %u/100  +%u", gainedXp, friendship_score(), friendBonus);
    } else {
        CoreS3.Display.printf("XP+%u  友情 %u/100", gainedXp, friendship_score());
    }
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    draw_battle_round_summary(18, 166, powerDiff, elementSwing, spiritDiff);
    if (updatedPet != nullptr) {
        draw_meter(18, 179, last_friend_peer_id == 0 ? 284 : 136, 4,
                   updatedPet->xp, level_xp_need(updatedPet->level), updatedPet->genes.accentColor);
    }
    if (last_friend_peer_id != 0) {
        draw_meter(166, 179, 136, 4, friendship_score(), 100, friendship_color());
    }
    draw_action_footer("背包", "休闲", "拍照", resultColor);

    display_hold_until_ms = millis() + 7000;
    battle_exit_pending = true;
    battle_exit_due_ms = millis() + 4200;
    play_scene_sound((diff > 6) ? kSoundWin : ((diff < -6) ? kSoundLose : kSoundDraw));
    if (friendBonus > 0 || last_friend_added || last_friend_bond_up) {
        play_scene_sound(kSoundFriend);
    }
    if (last_friend_bond_up || app_last_battle_level_up || app_last_battle_stage_up) {
        play_scene_sound(kSoundLevelUp);
    }
}

static void refresh_battle_clash_progress()
{
    if (!battle_result_pending || screen_mode != kScreenBattle) {
        return;
    }

    uint8_t phase = current_battle_clash_phase();
    const char* line1 = battle_round_title(phase);
    BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);
    BattlePetPacket opponent = pending_battle_packet;
    int32_t roundDiff = battle_round_diff_for_phase(phase, mine, opponent);
    draw_battle_round_track(208, 24, phase, roundDiff, element_accent_color(packet_element(mine)));
    draw_battle_impact(160, 100, phase, roundDiff, element_accent_color(packet_element(mine)));
    if (phase != battle_clash_audio_phase) {
        battle_clash_audio_phase = phase;
        play_scene_sound(kSoundBattleClash);
    }
    int32_t myRoundScore = 0;
    int32_t peerRoundScore = 0;
    battle_round_scores_for_phase(phase, mine, opponent, &myRoundScore, &peerRoundScore);
    draw_battle_score_plate(52, 138, 216, line1,
                            myRoundScore, peerRoundScore, 80,
                            element_accent_color(packet_element(mine)));
}

static void draw_battle_exit()
{
    screen_mode = kScreenBattle;
    battle_runtime_state = kBattleStateReady;
    battle_exit_visible = true;
    const SavedPet* pet = selected_pet_const();
    uint16_t resultColor = TFT_CYAN;
    if (app_last_battle_result_valid) {
        resultColor = app_last_battle_score_diff > 6 ? TFT_GREEN :
                      (app_last_battle_score_diff < -6 ? TFT_RED : TFT_YELLOW);
    }

    CoreS3.Display.fillScreen(rgb(8, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("退场整理", resultColor, rgb(8, 10, 14), 18, 14);
    if (app_last_battle_result_valid) {
        CoreS3.Display.fillRoundRect(218, 8, 88, 36, 8, rgb(20, 24, 32));
        CoreS3.Display.drawRoundRect(218, 8, 88, 36, 8, resultColor);
        CoreS3.Display.setTextColor(resultColor, rgb(20, 24, 32));
        CoreS3.Display.setCursor(226, 12);
        CoreS3.Display.printf("%s %+ld", battle_outcome_label(app_last_battle_outcome),
                              static_cast<long>(app_last_battle_score_diff));
        CoreS3.Display.setCursor(226, 29);
        CoreS3.Display.printf("局%04lX", static_cast<unsigned long>(app_last_battle_id & 0xffffUL));
    }

    CoreS3.Display.fillRoundRect(12, 46, 296, 74, 8, rgb(20, 24, 32));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(20, 24, 32));
    CoreS3.Display.setCursor(22, 58);
    if (pet != nullptr) {
        CoreS3.Display.printf("%s  Lv%u  %s", species_name(pet->genes), pet->level, stage_name(pet->stage));
        draw_element_badge(260, 56, pet->genes.element, rgb(20, 24, 32));
        CoreS3.Display.setCursor(22, 80);
        CoreS3.Display.printf("XP %u/%u  胜%u/%u %u%%", pet->xp, level_xp_need(pet->level),
                              pet->wins, pet->battles, win_rate_percent(*pet));
        draw_meter(22, 102, 266, 10, pet->xp, level_xp_need(pet->level), pet->genes.accentColor);
    } else {
        CoreS3.Display.print("暂无当前伙伴");
        CoreS3.Display.setCursor(22, 82);
        CoreS3.Display.print("请到背包选择伙伴");
    }

    if (app_last_battle_result_valid) {
        draw_battle_round_summary(18, 124, app_last_battle_power_diff,
                                  app_last_battle_element_swing,
                                  app_last_battle_spirit_diff);
    }

    int16_t infoY = app_last_battle_result_valid ? 142 : 128;
    int16_t infoH = app_last_battle_result_valid ? 38 : 52;
    int16_t infoLine1Y = static_cast<int16_t>(infoY + 8);
    int16_t infoLine2Y = static_cast<int16_t>(infoY + 24);
    int16_t friendMeterY = static_cast<int16_t>(infoY + (app_last_battle_result_valid ? 32 : 43));
    CoreS3.Display.fillRoundRect(12, infoY, 296, infoH, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(22, infoLine1Y);
    uint16_t nextBonus = next_rematch_xp_bonus();
    if (nextBonus > 0) {
        CoreS3.Display.printf("NEXT 休闲再战 +%uXP", nextBonus);
    } else {
        CoreS3.Display.print("NEXT 背包 / 休闲 / 拍照");
    }
    if (last_friend_peer_id != 0) {
        draw_friendship_badge(218, static_cast<int16_t>(infoLine1Y - 4), 82);
    }
    CoreS3.Display.setCursor(22, infoLine2Y);
    if (last_friend_peer_id != 0) {
        if (app_last_opponent_species[0] != '\0') {
            CoreS3.Display.printf("对手:%s Lv%u  友%u/100",
                                  app_last_opponent_species,
                                  app_last_opponent_level,
                                  friendship_score());
        } else {
            CoreS3.Display.printf("对手#%06lX  友%u/100  %s",
                                  static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL),
                                  friendship_score(), friend_bond_name());
        }
        draw_meter(22, friendMeterY, 266, 6, friendship_score(), 100, friendship_color());
    } else {
        CoreS3.Display.print(friendship_prompt());
    }

    uint16_t footerColor = pet == nullptr ? TFT_CYAN : pet->genes.accentColor;
    draw_action_footer("背包", "休闲", "拍照", footerColor);
    draw_battle_exit_choices("整理", "休闲", "捕捉", footerColor);
    display_hold_until_ms = millis() + 5500;
    play_scene_sound(kSoundBattleExit);
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
    battle_exit_pending = false;
    draw_battle_clash(packet);
    battle_clash_started_ms = millis();
    battle_result_due_ms = battle_clash_started_ms + kBattleClashMs;
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

static void resolve_pending_battle_exit()
{
    if (!battle_exit_pending) {
        return;
    }
    if (screen_mode != kScreenBattle || battle_result_pending) {
        battle_exit_pending = false;
        return;
    }
    if (static_cast<int32_t>(millis() - battle_exit_due_ms) < 0) {
        return;
    }
    battle_exit_pending = false;
    draw_battle_exit();
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
        snprintf(status, sizeof(status), "对战连接未就绪");
        return status;
    }

    if (battle_runtime_state == kBattleStateRetrying) {
        snprintf(status, sizeof(status), "重新寻找训练师...");
        return status;
    }
    if (battle_runtime_state == kBattleStateBattling) {
        snprintf(status, sizeof(status), "对战进行中");
        return status;
    }
    if (last_peer_seen_ms != 0 || battle_runtime_state == kBattleStateReady) {
        snprintf(status, sizeof(status), "同步完成");
        return status;
    }
    if (battle_runtime_state == kBattleStatePairing) {
        snprintf(status, sizeof(status), "羁绊同步中...");
        return status;
    }

    snprintf(status, sizeof(status), "寻找附近训练师...");
    return status;
}

static uint8_t match_sync_step()
{
    if (last_peer_seen_ms != 0 || battle_runtime_state == kBattleStateReady) {
        return 2;
    }
    if (battle_runtime_state == kBattleStatePairing || WiFi.status() == WL_CONNECTED) {
        return 1;
    }
    return 0;
}

static const char* match_sync_step_label(uint8_t step)
{
    switch (min<uint8_t>(step, 2)) {
    case 2: return "准备开战";
    case 1: return "羁绊同步";
    default: return "寻找训练师";
    }
}

static void maybe_play_match_sync_sound(uint8_t step)
{
    if (step == last_match_sync_audio_step) {
        return;
    }
    bool advanced = last_match_sync_audio_step == 255 || step > last_match_sync_audio_step;
    last_match_sync_audio_step = step;
    if (!advanced || step == 0) {
        return;
    }
    play_scene_sound(step >= 2 ? kSoundBattleClash : kSoundSelect);
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
    uint8_t step = match_sync_step();
    if (last_peer_seen_ms == 0) {
        CoreS3.Display.printf("%s  %lu秒", match_sync_step_label(step),
                              static_cast<unsigned long>((millis() - match_started_ms) / 1000));
    } else {
        CoreS3.Display.printf("%s  对手#%06lX  %lu秒前",
                              match_sync_step_label(step),
                              static_cast<unsigned long>(battle_peer_id & 0xffffffUL),
                              static_cast<unsigned long>((millis() - last_peer_seen_ms) / 1000));
    }
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 162);
    CoreS3.Display.print(last_peer_seen_ms == 0 ? "靠近训练机  中键休闲" : "准备开战  中键休闲");
    const SavedPet* pet = selected_pet_const();
    draw_match_sync_meter(208, 156, step, pet == nullptr ? TFT_CYAN : pet->genes.accentColor);
    maybe_play_match_sync_sound(step);
}

static void draw_pet_scene(const PetGenes& genes)
{
    PetGenes displayGenes = genes;
    uint8_t actionProfile = sd_action_profile_for_screen();
    apply_sd_action_profile(displayGenes, actionProfile);
    draw_pet_background(displayGenes.element, displayGenes);
    draw_pet_body(displayGenes.element, displayGenes);
    draw_pet_features(displayGenes.element, displayGenes);
    draw_pet_action_marks(displayGenes, actionProfile);
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
    battle_exit_pending = false;
    battle_exit_visible = false;
    battle_runtime_state = kBattleStateDiscovering;
    battle_scan_running = false;
    WiFi.scanDelete();

    CoreS3.Display.fillScreen(rgb(8, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("休闲状态", TFT_GREEN, rgb(8, 10, 14), 16, 16);

    CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 14));
    CoreS3.Display.setCursor(16, 48);
    CoreS3.Display.print(message == nullptr ? "拍照寻找野生伙伴" : message);

    const SavedPet* pet = selected_pet_const();
    uint16_t cardBg = rgb(20, 24, 32);
    CoreS3.Display.fillRoundRect(14, 80, 292, 94, 8, cardBg);
    CoreS3.Display.drawRoundRect(14, 80, 292, 94, 8, pet == nullptr ? rgb(78, 82, 92) : pet->genes.accentColor);
    CoreS3.Display.setTextColor(TFT_WHITE, cardBg);
    CoreS3.Display.setCursor(24, 94);
    if (pet == nullptr) {
        if (backpack.count == 0) {
            CoreS3.Display.print("空背包 0/6");
            CoreS3.Display.setCursor(24, 120);
            CoreS3.Display.print("右键拍照捕捉伙伴");
            CoreS3.Display.setCursor(24, 146);
            CoreS3.Display.print("中键对战前先有伙伴");
            draw_capture_guide(188, 92, TFT_GREEN);
        } else {
            CoreS3.Display.printf("背包已有%u只", backpack.count);
            CoreS3.Display.setCursor(24, 120);
            CoreS3.Display.print("左键背包选择出战");
            draw_bag_slot_bar(24, 148, backpack.count, 255, backpack.selected, TFT_GREEN);
        }
    } else {
        draw_pet_avatar(258, 138, pet->genes, true);
        CoreS3.Display.print("当前伙伴");
        draw_element_badge(128, 92, pet->genes.element, cardBg);
        CoreS3.Display.setCursor(24, 112);
        CoreS3.Display.printf("%s  Lv%u", species_name(pet->genes), pet->level);
        CoreS3.Display.setCursor(24, 130);
        CoreS3.Display.printf("%s  XP:%u/%u", stage_name(pet->stage), pet->xp, level_xp_need(pet->level));
        draw_meter(24, 148, 150, 9, pet->xp, level_xp_need(pet->level), pet->genes.accentColor);
        CoreS3.Display.setCursor(24, 160);
        if (growth_notice_recent(180)) {
            const char* growth = last_growth_stage_up ? "进化" : (last_growth_level_up ? "升级" : "成长");
            CoreS3.Display.printf("%s XP+%u", growth, last_growth_xp_gain);
        } else if (last_friend_peer_id != 0) {
            uint16_t nextBonus = next_rematch_xp_bonus();
            if (app_last_battle_result_valid && nextBonus > 0) {
                CoreS3.Display.printf("#%06lX %s 再+%uXP",
                                      static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL),
                                      battle_outcome_label(app_last_battle_outcome),
                                      nextBonus);
            } else if (app_last_battle_result_valid) {
                CoreS3.Display.printf("#%06lX %s 友%u",
                                      static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL),
                                      battle_outcome_label(app_last_battle_outcome),
                                      friendship_score());
            } else {
                CoreS3.Display.printf("#%06lX %s 友%u",
                                      static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL),
                                      friend_bond_name(),
                                      friendship_score());
            }
            draw_meter(24, 170, 150, 4, friendship_score(), 100, friendship_color());
        } else {
            CoreS3.Display.printf("包%u/%u 胜率%u%% 成长%us", backpack.count, kMaxBackpackPets,
                                  win_rate_percent(*pet), seconds_until_growth(*pet));
            draw_meter(24, 170, 150, 4, growth_wait_progress(*pet), kGrowthIntervalSec, pet->genes.accentColor);
        }
    }

    draw_action_footer("背包", "对战", "拍照", TFT_GREEN);
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
    draw_game_title("训练师启动", TFT_YELLOW, rgb(8, 10, 18), 16, 20);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(8, 10, 18));
    CoreS3.Display.setCursor(16, 56);
    CoreS3.Display.print("训练师旋律启动中");
    CoreS3.Display.setCursor(16, 86);
    CoreS3.Display.print("带上伙伴准备出发");
    CoreS3.Display.fillRoundRect(16, 144, 288, 32, 8, rgb(20, 24, 36));
    CoreS3.Display.drawRoundRect(16, 144, 288, 32, 8, TFT_YELLOW);
    CoreS3.Display.setCursor(28, 154);
    CoreS3.Display.print("本地原创启动音效");
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
    battle_exit_pending = false;
    battle_exit_visible = false;
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
    last_match_sync_audio_step = 0;

    const SavedPet* pet = selected_pet_const();
    if (pet == nullptr) {
        CoreS3.Display.fillScreen(rgb(8, 10, 14));
        CoreS3.Display.setFont(&fonts::Font2);
        CoreS3.Display.setTextDatum(top_left);
        draw_game_title("匹配准备", TFT_YELLOW, rgb(8, 10, 14), 16, 24);
        CoreS3.Display.fillRoundRect(14, 58, 292, 112, 8, rgb(18, 22, 30));
        CoreS3.Display.drawRoundRect(14, 58, 292, 112, 8, TFT_YELLOW);
        CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
        CoreS3.Display.setCursor(24, 72);
        CoreS3.Display.print("暂无出战伙伴");
        CoreS3.Display.setCursor(24, 98);
        if (backpack.count == 0) {
            CoreS3.Display.print("先拍照捕捉第一只");
            CoreS3.Display.setCursor(24, 122);
            CoreS3.Display.print("右键拍照，左键背包");
            draw_capture_guide(188, 76, TFT_YELLOW);
        } else {
            CoreS3.Display.print("先到背包选择伙伴");
            CoreS3.Display.setCursor(24, 122);
            CoreS3.Display.print("左键背包，中键休闲");
            uint8_t selected = backpack.selected < backpack.count ? backpack.selected : 255;
            draw_bag_slot_bar(24, 146, backpack.count, 255, selected, TFT_YELLOW);
        }
        draw_action_footer("背包", "休闲", "拍照", TFT_YELLOW);
        display_hold_until_ms = millis() + 3500;
        play_match_prepare_sound(backpack.count > 0);
        return;
    }

    local_pet = pet->genes;
    has_local_pet = true;
    local_pet_sequence = ++battle_sequence;
    last_battle_key = 0;
    publish_local_pet();

    draw_pet_scene(pet->genes);
    CoreS3.Display.fillRoundRect(8, 8, 304, 56, 8, rgb(12, 12, 16));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title(match_sync_step_label(match_sync_step()), pet->genes.accentColor, rgb(12, 12, 16), 16, 14);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 34);
    CoreS3.Display.printf("%s  Lv%u", species_name(pet->genes), pet->level);

    CoreS3.Display.fillRoundRect(8, 72, 304, 56, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 80);
    if (last_friend_peer_id != 0) {
        if (app_last_opponent_species[0] != '\0') {
            CoreS3.Display.printf("好友%u位  对手:%s",
                                  local_friend_count,
                                  app_last_opponent_species);
        } else {
            CoreS3.Display.printf("好友%u位  最近#%06lX",
                                  local_friend_count,
                                  static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL));
        }
    } else {
        CoreS3.Display.print("好友0位  寻找附近训练师");
    }
    draw_friendship_badge(212, 78, 90);
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 100);
    if (app_last_battle_result_valid) {
        uint16_t lastResultColor = app_last_battle_score_diff > 6 ? TFT_GREEN :
                                   (app_last_battle_score_diff < -6 ? TFT_RED : TFT_YELLOW);
        CoreS3.Display.fillCircle(21, 106, 4, lastResultColor);
        CoreS3.Display.drawCircle(21, 106, 5, TFT_WHITE);
        CoreS3.Display.setCursor(30, 100);
        uint16_t nextBonus = next_rematch_xp_bonus();
        if (nextBonus > 0) {
            CoreS3.Display.printf("上次:%s 差%+ld 再战+%uXP",
                                  battle_outcome_label(app_last_battle_outcome),
                                  static_cast<long>(app_last_battle_score_diff), nextBonus);
        } else {
            CoreS3.Display.printf("上次:%s 差%+ld XP+%u",
                                  battle_outcome_label(app_last_battle_outcome),
                                  static_cast<long>(app_last_battle_score_diff), app_last_battle_xp);
        }
    } else if (last_friend_notice[0] != '\0') {
        CoreS3.Display.printf("%s  友:%u", last_friend_notice, friendship_score());
    } else if (last_friend_peer_id != 0) {
        CoreS3.Display.printf("共%u战  友情%u/100", friend_battle_count, friendship_score());
    } else {
        CoreS3.Display.print(friendship_prompt());
    }
    draw_meter(14, 116, 282, 6, friendship_score(), 100, friendship_color());

    CoreS3.Display.fillRoundRect(8, 136, 304, 44, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 142);
    uint8_t step = match_sync_step();
    if (message == nullptr) {
        CoreS3.Display.printf("%s  0秒", match_sync_step_label(step));
    } else {
        CoreS3.Display.print(message);
    }
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 162);
    CoreS3.Display.print("靠近训练机  中键休闲");
    draw_match_sync_meter(208, 156, step, pet->genes.accentColor);
    draw_action_footer("背包", "休闲", "拍照", pet->genes.accentColor);
    display_hold_until_ms = millis() + 6000;
    play_scene_sound(kSoundMatch);
    maybe_play_friendship_hint_sound();
    play_pet_sound(pet->genes, pet->level, pet->stage);
}

static void show_friend_action_feedback(const char* message)
{
    const char* notice = (message != nullptr && message[0] != '\0') ? message : "好友状态已更新";
    const uint16_t bg = rgb(18, 22, 30);
    CoreS3.Display.fillRoundRect(12, 150, 296, 30, 8, bg);
    CoreS3.Display.drawRoundRect(12, 150, 296, 30, 8, TFT_CYAN);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_CYAN, bg);
    CoreS3.Display.setCursor(22, 158);
    CoreS3.Display.print(notice);
    display_hold_until_ms = millis() + 2500;
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
    draw_game_title("放生确认", TFT_RED, rgb(32, 14, 14), 16, 14);
    draw_element_badge(260, 16, pet.genes.element, rgb(32, 14, 14));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(32, 14, 14));
    CoreS3.Display.setCursor(16, 40);
    CoreS3.Display.printf("%s  Lv%u", species_name(pet.genes), pet.level);
    CoreS3.Display.setCursor(16, 54);
    CoreS3.Display.print("确认后会从本设备移除");

    CoreS3.Display.fillRoundRect(10, 78, 300, 78, 8, rgb(26, 14, 18));
    CoreS3.Display.drawRoundRect(10, 78, 300, 78, 8, TFT_RED);
    draw_pet_avatar(62, 132, pet.genes, false);
    CoreS3.Display.fillTriangle(128, 94, 146, 126, 110, 126, TFT_RED);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(TFT_WHITE, TFT_RED);
    CoreS3.Display.drawString("!", 128, 116);
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(26, 14, 18));
    CoreS3.Display.setCursor(156, 86);
    CoreS3.Display.print("放生后不可恢复");
    draw_release_confirm_summary(156, 102, 144, pet, bag_cursor, backpack.count,
                                 bag_cursor == backpack.selected);
    draw_growth_goal_badge(156, 128, 144, pet, pet.genes.accentColor);

    CoreS3.Display.fillRoundRect(8, 162, 304, 22, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 166);
    CoreS3.Display.print("左右取消，中键确认");
    draw_bag_slot_bar(196, 170, backpack.count, bag_cursor, backpack.selected, TFT_RED);
    draw_action_footer("取消", "确认", "取消", TFT_RED);
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
        draw_idle_screen("已放生，背包为空", false);
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
    draw_bag_screen("已放生当前伙伴");
}

static void draw_bag_screen(const char* message)
{
    bool openingBag = screen_mode != kScreenBag;
    screen_mode = kScreenBag;
    battle_result_pending = false;
    battle_exit_pending = false;
    battle_exit_visible = false;
    refresh_backpack_growth(true);

    if (backpack.count == 0) {
        CoreS3.Display.fillScreen(rgb(8, 10, 14));
        CoreS3.Display.setFont(&fonts::Font2);
        CoreS3.Display.setTextDatum(top_left);
        draw_game_title("伙伴背包", TFT_CYAN, rgb(8, 10, 14), 16, 28);

        CoreS3.Display.fillRoundRect(14, 58, 292, 112, 8, rgb(18, 22, 30));
        CoreS3.Display.drawRoundRect(14, 58, 292, 112, 8, TFT_CYAN);
        CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
        CoreS3.Display.setCursor(24, 72);
        CoreS3.Display.print("空背包 0/6");
        CoreS3.Display.setCursor(24, 98);
        CoreS3.Display.print("拍照捕捉第一只伙伴");
        CoreS3.Display.setCursor(24, 122);
        CoreS3.Display.print("左/中返回休闲");
        draw_bag_slot_bar(24, 146, backpack.count, 255, 255, TFT_CYAN);
        draw_capture_guide(188, 76, TFT_CYAN);
        draw_action_footer("休闲", "休闲", "拍照", TFT_CYAN);
        display_hold_until_ms = millis() + 2500;
        play_scene_sound(openingBag ? kSoundBag : kSoundSelect);
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
    draw_game_title((bag_cursor == backpack.selected) ? "伙伴背包 出战" : "伙伴背包 收藏",
                    pet.genes.accentColor, rgb(12, 12, 16), 16, 12);
    draw_element_badge(266, 14, pet.genes.element, rgb(12, 12, 16));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(12, 12, 16));
    CoreS3.Display.setCursor(16, 34);
    CoreS3.Display.printf("#%u/%u %s Lv%u %s", bag_cursor + 1, backpack.count,
                          species_name(pet.genes), pet.level, stage_name(pet.stage));
    CoreS3.Display.setCursor(180, 34);
    CoreS3.Display.printf("XP %u/%u", pet.xp, level_xp_need(pet.level));
    draw_bag_slot_bar(190, 54, backpack.count, bag_cursor, backpack.selected, pet.genes.accentColor);
    draw_meter(16, 66, 288, 10, pet.xp, level_xp_need(pet.level), pet.genes.accentColor);
    draw_growth_goal_badge(16, 82, 150, pet, pet.genes.accentColor);

    CoreS3.Display.fillRoundRect(178, 82, 128, 70, 8, rgb(18, 22, 30));
    CoreS3.Display.drawRoundRect(178, 82, 128, 70, 8, pet.genes.accentColor);
    draw_labeled_meter(188, 92, "力", pet_battle_power(pet), 160, pet.genes.accentColor);
    draw_labeled_meter(188, 112, "速", pet_battle_agility(pet), 160, pet.genes.accentColor);
    draw_labeled_meter(188, 132, "心", pet_battle_spirit(pet), 160, pet.genes.accentColor);

    CoreS3.Display.fillRoundRect(8, 156, 304, 26, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(14, 160);
    if (message != nullptr) {
        CoreS3.Display.print(message);
    } else if (growth_notice_recent(180) && last_growth_pet_index == bag_cursor) {
        char growthLine[64];
        format_growth_notice(growthLine, sizeof(growthLine));
        CoreS3.Display.print(growthLine);
    } else if (friend_rematch_streak > 0) {
        char goal[24];
        format_growth_goal(goal, sizeof(goal), pet);
        CoreS3.Display.printf("胜%u/%u  %s  等%us", pet.wins, pet.battles,
                              goal, seconds_until_growth(pet));
    } else {
        char goal[24];
        format_growth_goal(goal, sizeof(goal), pet);
        CoreS3.Display.printf("胜%u/%u  率%u%%  %s", pet.wins, pet.battles,
                              win_rate_percent(pet), goal);
    }
    if (message == nullptr) {
        draw_meter(14, 176, 288, 5, growth_wait_progress(pet), kGrowthIntervalSec, pet.genes.accentColor);
    }
    draw_action_footer("放生", "选中", "下一只", pet.genes.accentColor);
    display_hold_until_ms = millis() + 4500;
    bool newGrowthCue = openingBag &&
                         last_growth_event_sec != 0 &&
                         last_growth_event_sec != last_growth_sound_sec;
    if (newGrowthCue) {
        last_growth_sound_sec = last_growth_event_sec;
        play_scene_sound(kSoundLevelUp);
    } else {
        play_scene_sound(openingBag ? kSoundBag : kSoundSelect);
    }
    play_pet_sound(pet.genes, pet.level, pet.stage);
}

static void draw_wild_pet(const ImageTraits& traits, const RecognitionResult& recog)
{
    if (!recog.recognized || recog.confidence < kMinRecognitionConfidence || recog.classId == kObjectUnknown) {
        RecognitionResult fail = recog;
        fail.recognized = false;
        if (fail.failureReason == nullptr || fail.failureReason[0] == '\0') {
        fail.failureReason = "识别失败";
        }
        draw_capture_fail(traits, fail);
        return;
    }

    wild_pet = derive_pet_genes(traits, recog, millis() / 30000UL, shot_count);
    wild_traits = traits;
    wild_recognition = recog;
    has_wild_pet = true;
    screen_mode = kScreenWild;
    battle_exit_pending = false;
    battle_exit_visible = false;
    draw_pet_scene(wild_pet);
    draw_pet_badge(traits, recog, wild_pet);
    bool bagFull = backpack.count >= kMaxBackpackPets;

    CoreS3.Display.fillRoundRect(8, 160, 304, 22, 8, rgb(20, 20, 24));
    CoreS3.Display.setTextDatum(top_left);
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextColor(bagFull ? TFT_RED : TFT_YELLOW, rgb(20, 20, 24));
    CoreS3.Display.setCursor(14, 164);
    if (bagFull) {
        CoreS3.Display.print("背包已满：先放生再捕捉");
    } else {
        CoreS3.Display.printf("野外遭遇: %s  背包:%u/%u", species_name(wild_pet), backpack.count, kMaxBackpackPets);
    }
    CoreS3.Display.fillRoundRect(186, 104, 120, 48, 8, rgb(18, 22, 30));
    CoreS3.Display.drawRoundRect(186, 104, 120, 48, 8, bagFull ? TFT_RED : wild_pet.accentColor);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(196, 112);
    CoreS3.Display.printf("力%u 速%u", min<uint16_t>(99, pet_power(wild_pet)), min<uint16_t>(99, pet_agility(wild_pet)));
    draw_element_badge(262, 108, wild_pet.element, rgb(18, 22, 30));
    CoreS3.Display.setCursor(196, 132);
    CoreS3.Display.printf("心%u  XP+%u", min<uint16_t>(99, pet_spirit(wild_pet)), kCaptureXp);
    draw_bag_slot_bar(196, 144, backpack.count, 255, backpack.selected, bagFull ? TFT_RED : wild_pet.accentColor);
    draw_capture_quality_panel(8, 124, recog.presenceScore, recog.confidence);
    draw_action_footer("捕捉", "休闲", "放走", wild_pet.accentColor);
    display_hold_until_ms = millis() + 6500;
    play_scene_sound(kSoundWild);
    play_pet_sound(wild_pet, 1, 0);
}

static const char* capture_failure_label(const char* reason)
{
    if (reason == nullptr || reason[0] == '\0') {
        return "未发现清晰目标";
    }
    if (strstr(reason, "背包已满") != nullptr || strcmp(reason, "识别失败") == 0) {
        return reason;
    }
    if (strcmp(reason, "Background-like scene") == 0) {
        return "背景过多，主体不清";
    }
    if (strcmp(reason, "Model distance high") == 0) {
        return "目标特征不稳定";
    }
    if (strcmp(reason, "Model class ambiguous") == 0) {
        return "目标类别不清晰";
    }
    if (strcmp(reason, "Low model confidence") == 0 || strcmp(reason, "Low class confidence") == 0) {
        return "识别信心不足";
    }
    if (strcmp(reason, "Weak class evidence") == 0) {
        return "主体证据不足";
    }
    if (strcmp(reason, "Preprocess failed") == 0) {
        return "画面处理失败";
    }
    if (strcmp(reason, "Camera capture failed") == 0) {
        return "相机拍照失败";
    }
    return reason;
}

static void play_capture_fail_sound(bool bagFullFail)
{
    play_scene_sound(kSoundWarning);
    if (bagFullFail) {
        play_scene_sound(kSoundBag);
    }
}

static void play_match_prepare_sound(bool hasStoredPet)
{
    play_scene_sound(kSoundWarning);
    play_scene_sound(hasStoredPet ? kSoundBag : kSoundPhoto);
}

static void draw_capture_fail(const ImageTraits& traits, const RecognitionResult& recog)
{
    (void)traits;
    has_wild_pet = false;
    wild_pet = {};
    screen_mode = kScreenCaptureFail;
    battle_exit_pending = false;
    battle_exit_visible = false;
    wild_traits = traits;
    wild_recognition = recog;
    append_app_log("捕捉失败");

    CoreS3.Display.fillScreen(rgb(10, 10, 14));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("捕捉失败", TFT_RED, rgb(10, 10, 14), 16, 18);

    const char* rawFailureReason = recog.failureReason == nullptr ? "" : recog.failureReason;
    const char* failureReason = capture_failure_label(rawFailureReason);
    bool bagFullFail = strstr(rawFailureReason, "背包已满") != nullptr ||
                       strstr(failureReason, "背包已满") != nullptr;
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(10, 10, 14));
    CoreS3.Display.setCursor(16, 52);
    CoreS3.Display.printf("%s", failureReason);
    CoreS3.Display.setCursor(16, 78);
    CoreS3.Display.print(bagFullFail ? "左键进背包" : "主体放进取景框");
    CoreS3.Display.setCursor(16, 104);
    CoreS3.Display.print(bagFullFail ? "先放生一只伙伴" : "补光并简化背景");
    CoreS3.Display.setCursor(16, 130);
    CoreS3.Display.print(bagFullFail ? "再拍照捕捉" : "下一步：背包 / 休闲 / 重试");
    if (bagFullFail) {
        CoreS3.Display.fillRoundRect(184, 70, 122, 76, 8, rgb(18, 22, 30));
        CoreS3.Display.drawRoundRect(184, 70, 122, 76, 8, TFT_RED);
        CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
        CoreS3.Display.setCursor(196, 86);
        CoreS3.Display.printf("背包 %u/%u", backpack.count, kMaxBackpackPets);
        draw_bag_slot_bar(196, 114, backpack.count, 255, backpack.selected, TFT_RED);
        CoreS3.Display.setCursor(196, 132);
        CoreS3.Display.print("满员");
    } else {
        draw_capture_guide(194, 70, TFT_RED);
        draw_capture_quality_panel(16, 148, recog.presenceScore, recog.confidence);
    }
    draw_action_footer("背包", "休闲", bagFullFail ? "拍照" : "重试", TFT_RED);
    display_hold_until_ms = millis() + 4500;
    play_capture_fail_sound(bagFullFail);
}

static void draw_capture_success(const SavedPet& pet)
{
    screen_mode = kScreenIdle;
    has_wild_pet = false;
    battle_result_pending = false;
    battle_exit_pending = false;
    battle_exit_visible = false;

    draw_pet_scene(pet.genes);
    CoreS3.Display.fillRoundRect(8, 8, 304, 64, 8, rgb(12, 16, 20));
    CoreS3.Display.setFont(&fonts::Font2);
    CoreS3.Display.setTextDatum(top_left);
    draw_game_title("捕捉成功", pet.genes.accentColor, rgb(12, 16, 20), 16, 14);
    draw_element_badge(260, 18, pet.genes.element, rgb(12, 16, 20));
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(12, 16, 20));
    CoreS3.Display.setCursor(16, 38);
    CoreS3.Display.printf("%s  Lv%u  XP+%u", species_name(pet.genes), pet.level, kCaptureXp);
    draw_meter(16, 58, 150, 8, pet.xp, level_xp_need(pet.level), pet.genes.accentColor);
    draw_growth_goal_badge(178, 48, 124, pet, pet.genes.accentColor);

    CoreS3.Display.fillRoundRect(10, 78, 300, 66, 8, rgb(18, 22, 30));
    CoreS3.Display.drawRoundRect(10, 78, 300, 66, 8, pet.genes.accentColor);
    CoreS3.Display.setTextColor(TFT_WHITE, rgb(18, 22, 30));
    CoreS3.Display.setCursor(20, 88);
    CoreS3.Display.printf("力%u  速%u  心%u", pet_battle_power(pet),
                          pet_battle_agility(pet),
                          pet_battle_spirit(pet));
    CoreS3.Display.setCursor(20, 108);
    CoreS3.Display.printf("阶段:%s  性格:%s", stage_name(pet.stage), mood_name(pet.genes.mood));
    CoreS3.Display.setCursor(20, 128);
    char goal[24];
    format_growth_goal(goal, sizeof(goal), pet);
    CoreS3.Display.printf("出战:%u号  %s  等%us", backpack.selected + 1, goal, seconds_until_growth(pet));
    draw_bag_slot_bar(190, 130, backpack.count, backpack.selected, backpack.selected, pet.genes.accentColor);

    CoreS3.Display.fillRoundRect(8, 152, 304, 26, 8, rgb(18, 22, 30));
    CoreS3.Display.setTextColor(TFT_CYAN, rgb(18, 22, 30));
    CoreS3.Display.setCursor(16, 159);
    CoreS3.Display.printf("已加入背包 %u/%u，已设出战", backpack.count, kMaxBackpackPets);
    draw_action_footer("背包", "对战", "拍照", pet.genes.accentColor);
    display_hold_until_ms = millis() + 6500;
    play_scene_sound(kSoundCapture);
    play_pet_sound(pet.genes, pet.level, pet.stage);
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
            fail.failureReason = "识别失败";
        }
        draw_capture_fail(wild_traits, fail);
        return;
    }
    if (backpack.count >= kMaxBackpackPets) {
        RecognitionResult fail = wild_recognition;
        fail.recognized = false;
        fail.failureReason = "背包已满，请先放生一只";
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
    append_app_log("捕捉成功");
    draw_capture_success(pet);
}

static void release_wild_pet()
{
    has_wild_pet = false;
    play_scene_sound(kSoundRelease);
    draw_idle_screen("已放走野生伙伴，右键拍照", false);
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

static const char* button_label_for_slot(uint8_t button)
{
    uint8_t slot = min<uint8_t>(button, kButtonRight);

    if (screen_mode == kScreenWild) {
        return slot == kButtonLeft ? "捕捉" : (slot == kButtonMiddle ? "休闲" : "放走");
    }

    if (screen_mode == kScreenCaptureFail) {
        bool bagFullFail = wild_recognition.failureReason != nullptr &&
                           strstr(wild_recognition.failureReason, "背包已满") != nullptr;
        return slot == kButtonLeft ? "背包" : (slot == kButtonMiddle ? "休闲" : (bagFullFail ? "拍照" : "重试"));
    }

    if (screen_mode == kScreenBag) {
        if (backpack.count == 0) {
            return slot == kButtonRight ? "拍照" : "休闲";
        }
        return slot == kButtonLeft ? "放生" : (slot == kButtonMiddle ? "选中" : "下一只");
    }

    if (screen_mode == kScreenReleaseConfirm) {
        return slot == kButtonMiddle ? "确认" : "取消";
    }

    if (screen_mode == kScreenMatch || screen_mode == kScreenBattle) {
        return slot == kButtonLeft ? "背包" : (slot == kButtonMiddle ? "休闲" : "拍照");
    }

    return slot == kButtonLeft ? "背包" : (slot == kButtonMiddle ? "对战" : "拍照");
}

static const char* app_action_type_for_action(uint8_t action)
{
    switch (action) {
    case kActionPhoto: return "photo";
    case kActionOpenBag: return "bag";
    case kActionBackToIdle: return "idle";
    case kActionPrevPet: return "prev";
    case kActionNextPet: return "next";
    case kActionSelectPet: return "select";
    case kActionCapturePet: return "capture";
    case kActionReleasePet:
    case kActionReleaseStoredPet: return "release";
    case kActionConfirmReleaseStoredPet: return "confirm_release";
    case kActionMatchBattle: return "match";
    default: return "";
    }
}

static const char* app_action_type_for_button(uint8_t button)
{
    uint8_t action = action_for_button(button);
    if (screen_mode == kScreenReleaseConfirm && action == kActionOpenBag) {
        return "cancel";
    }
    return app_action_type_for_action(action);
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
        take_photo("拍照触发");
        break;
    case kActionOpenBag:
    {
        bool canceledRelease = screen_mode == kScreenReleaseConfirm;
        has_wild_pet = false;
        if (canceledRelease) {
            play_scene_sound(kSoundCancel);
        }
        draw_bag_screen(canceledRelease ? "已取消放生" : nullptr);
        break;
    }
    case kActionBackToIdle:
        draw_idle_screen("休闲中：可拍照、背包或对战", true);
        break;
    case kActionMatchBattle:
        draw_match_screen(nullptr);
        break;
    case kActionPrevPet:
        if (screen_mode != kScreenBag) {
            play_scene_sound(kSoundWarning);
            draw_bag_screen("请先打开背包");
        } else if (backpack.count > 0) {
            bag_cursor = (bag_cursor + backpack.count - 1) % backpack.count;
            draw_bag_screen(nullptr);
        }
        break;
    case kActionNextPet:
        if (screen_mode != kScreenBag) {
            play_scene_sound(kSoundWarning);
            draw_bag_screen("请先打开背包");
        } else if (backpack.count > 0) {
            bag_cursor = (bag_cursor + 1) % backpack.count;
            draw_bag_screen(nullptr);
        }
        break;
    case kActionSelectPet:
        if (screen_mode != kScreenBag) {
            play_scene_sound(kSoundWarning);
            draw_bag_screen("请先在背包选择");
        } else if (valid_bag_index(bag_cursor)) {
            set_active_from_backpack(bag_cursor);
            play_scene_sound(kSoundSelect);
            draw_idle_screen("伙伴已选中，中键进入对战", false);
        }
        break;
    case kActionReleaseStoredPet:
        if (screen_mode == kScreenBag) {
            draw_release_confirm_screen();
        } else {
            play_scene_sound(kSoundWarning);
            draw_bag_screen("请先到背包放生");
        }
        break;
    case kActionConfirmReleaseStoredPet:
        if (screen_mode == kScreenReleaseConfirm) {
            release_stored_pet();
        } else {
            play_scene_sound(kSoundWarning);
            draw_bag_screen("请先进入放生确认");
        }
        break;
    case kActionCapturePet:
        if (screen_mode == kScreenWild) {
            capture_wild_pet();
        } else {
            play_scene_sound(kSoundWarning);
            draw_idle_screen("请先拍照发现伙伴", false);
        }
        break;
    case kActionReleasePet:
        if (screen_mode == kScreenWild) {
            release_wild_pet();
        } else {
            play_scene_sound(kSoundWarning);
            draw_idle_screen("当前没有野生伙伴", false);
        }
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

static int32_t capture_candidate_rank(const CaptureCandidate& candidate)
{
    if (!candidate.valid) {
        return -10000;
    }
    int32_t score = candidate.qualityScore * 3 + candidate.presence.score * 2;
    if (candidate.recog.recognized) {
        score += 240 + candidate.recog.confidence * 2;
    }
    if (!candidate.preprocessed) {
        score -= 220;
    }
    if (background_similarity_penalty(candidate.traits)) {
        score -= 220;
    }
    if (candidate.traits.brightness < 32 || candidate.traits.darkRatio > 82) {
        score -= 180;
    }
    if (candidate.traits.brightRatio > 92 && candidate.traits.contrast < 28) {
        score -= 180;
    }
    if (proximity_sensor_available && candidate.proximity < kObjectNearProximity && candidate.traits.centerDelta < 44) {
        score -= 24;
    }
    return score;
}

static void restore_capture_candidate_metrics(const CaptureCandidate& candidate)
{
    last_vision_preprocess_ms = candidate.preprocessMs;
    last_vision_classify_ms = candidate.classifyMs;
    last_vision_best_distance = candidate.bestDistance;
    last_vision_margin = candidate.margin;
    last_vision_source = candidate.source == nullptr ? "none" : candidate.source;
    last_capture_proximity = candidate.proximity;
    last_capture_burst_index = candidate.burstIndex;
    last_capture_quality_score = candidate.qualityScore;
}

static void remember_capture_burst(const CaptureCandidate* candidates, uint8_t count, int8_t bestIndex)
{
    last_capture_burst_count = min<uint8_t>(count, kCaptureBurstFrames);
    last_capture_best_burst_index = bestIndex;
    for (uint8_t i = 0; i < kCaptureBurstFrames; ++i) {
        if (i < last_capture_burst_count && candidates != nullptr) {
            last_capture_burst[i] = candidates[i];
        } else {
            last_capture_burst[i] = {};
            last_capture_burst[i].burstIndex = i;
            last_capture_burst[i].source = "none";
        }
    }
}

static bool capture_burst_candidate(uint8_t burstIndex, CaptureCandidate* candidate)
{
    if (candidate == nullptr) {
        return false;
    }
    *candidate = {};
    candidate->burstIndex = burstIndex;
    candidate->source = "none";
    if (!CoreS3.Camera.get()) {
        return false;
    }

    candidate->valid = true;
    candidate->preprocessed = preprocess_frame_for_vision();
    candidate->traits = analyze_frame();
    candidate->presence = detect_subject_presence(candidate->traits);
    candidate->proximity = read_capture_proximity();
    candidate->qualityScore = capture_quality_score(candidate->traits, candidate->presence, candidate->proximity);

    RecognitionResult recog = {};
    if (candidate->preprocessed) {
        uint32_t start = millis();
        recog = classify_object_local(candidate->traits, candidate->presence);
        last_vision_classify_ms = millis() - start;
    } else {
        recog.failureReason = "Preprocess failed";
        recog.objectLabel = object_class_label(kObjectUnknown);
        recog.materialLabel = material_label_for_class(kObjectUnknown);
        recog.elementHint = candidate->traits.element;
        recog.presenceScore = candidate->presence.score;
        last_vision_classify_ms = 0;
        last_vision_source = "preprocess";
        last_vision_best_distance = 0;
        last_vision_margin = 0;
    }

    if (background_similarity_penalty(candidate->traits)) {
        recog.recognized = false;
        recog.classId = kObjectUnknown;
        recog.objectLabel = object_class_label(kObjectUnknown);
        recog.materialLabel = material_label_for_class(kObjectUnknown);
        recog.confidence = min<uint8_t>(recog.confidence, 18);
        recog.failureReason = "Background-like scene";
        last_vision_source = "background";
        last_vision_best_distance = 0;
        last_vision_margin = 0;
    }

    candidate->recog = recog;
    candidate->preprocessMs = last_vision_preprocess_ms;
    candidate->classifyMs = last_vision_classify_ms;
    candidate->bestDistance = last_vision_best_distance;
    candidate->margin = last_vision_margin;
    candidate->source = last_vision_source;

    const char* hint = capture_quality_hint(candidate->traits, candidate->presence, candidate->proximity);
    char line1[48];
    char line2[64];
    snprintf(line1, sizeof(line1), "Burst %u/%u Q%u %s",
             burstIndex + 1, kCaptureBurstFrames, candidate->qualityScore, hint);
    snprintf(line2, sizeof(line2), "P%u C%u D%ld S%ld prox%u",
             candidate->presence.score,
             recog.confidence,
             static_cast<long>(candidate->traits.centerDelta),
             static_cast<long>(candidate->traits.saturation),
             candidate->proximity);
    draw_status(line1, line2, recog.recognized ? TFT_GREEN : TFT_YELLOW);
    log_capture_sample(burstIndex, candidate->traits, recog, candidate->presence, candidate->proximity);
    CoreS3.Camera.free();
    return true;
}

static void take_photo(const char* reason)
{
    if (millis() - last_shot_ms < kPhotoCooldownMs) {
        return;
    }
    if (!camera_ok) {
        draw_status("Camera not ready", "Reset CoreS3 and try again", TFT_RED);
        return;
    }

    draw_status("Taking photo...", reason, TFT_YELLOW);
    play_scene_sound(kSoundPhoto);

    CaptureCandidate candidates[kCaptureBurstFrames] = {};
    int8_t bestIndex = -1;
    int32_t bestRank = -10000;
    for (uint8_t i = 0; i < kCaptureBurstFrames; ++i) {
        if (!capture_burst_candidate(i, &candidates[i])) {
            continue;
        }
        int32_t rank = capture_candidate_rank(candidates[i]);
        if (rank > bestRank) {
            bestRank = rank;
            bestIndex = static_cast<int8_t>(i);
        }
        CoreS3.delay(35);
    }

    remember_capture_burst(candidates, kCaptureBurstFrames, bestIndex);

    if (bestIndex >= 0) {
        CaptureCandidate& best = candidates[bestIndex];
        restore_capture_candidate_metrics(best);
        const char* hint = capture_quality_hint(best.traits, best.presence, best.proximity);
        if (!best.recog.recognized && (best.recog.failureReason == nullptr || best.recog.failureReason[0] == '\0')) {
            best.recog.failureReason = hint;
        }
        Serial.printf("vision burst=%u quality=%u prox=%u hint=%s pre=%lums cls=%lums src=%s presence=%u recognized=%u conf=%u class=%s elem=%s bestD=%u margin=%u rgb=%d,%d,%d bri=%d sat=%d ctr=%d cd=%d dark=%d bright=%d heap=%lu reason=%s\n",
                      best.burstIndex + 1, best.qualityScore, best.proximity, hint,
                      static_cast<unsigned long>(last_vision_preprocess_ms),
                      static_cast<unsigned long>(last_vision_classify_ms),
                      last_vision_source == nullptr ? "" : last_vision_source,
                      best.recog.presenceScore, best.recog.recognized ? 1 : 0,
                      best.recog.confidence, best.recog.objectLabel,
                      element_name(best.recog.elementHint),
                      last_vision_best_distance, last_vision_margin,
                      best.traits.r, best.traits.g, best.traits.b,
                      best.traits.brightness, best.traits.saturation, best.traits.contrast,
                      best.traits.centerDelta, best.traits.darkRatio, best.traits.brightRatio,
                      static_cast<unsigned long>(ESP.getFreeHeap()),
                      best.recog.failureReason == nullptr ? "" : best.recog.failureReason);
        if (best.recog.recognized) {
            draw_wild_pet(best.traits, best.recog);
        } else {
            draw_capture_fail(best.traits, best.recog);
        }
        ++shot_count;
    } else {
        ImageTraits traits = {};
        RecognitionResult recog = {};
        recog.failureReason = "Camera capture failed";
        recog.objectLabel = object_class_label(kObjectUnknown);
        recog.materialLabel = material_label_for_class(kObjectUnknown);
        draw_capture_fail(traits, recog);
    }

    clear_external_vision_hint();
    last_shot_ms = millis();
}
static void append_app_log(const char* message)
{
    if (message == nullptr || message[0] == '\0') {
        return;
    }
    snprintf(app_log_lines[app_log_next], sizeof(app_log_lines[app_log_next]),
             "%lus %s", static_cast<unsigned long>(now_sec()), message);
    app_log_next = (app_log_next + 1) % kAppLogCapacity;
    if (app_log_count < kAppLogCapacity) {
        ++app_log_count;
    }
}

static String json_string(const char* value)
{
    String out = "\"";
    if (value == nullptr) {
        value = "";
    }
    for (const char* p = value; *p != '\0'; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (static_cast<uint8_t>(c) >= 0x20) {
            out += c;
        }
    }
    out += '"';
    return out;
}

static const char* screen_mode_name()
{
    switch (screen_mode) {
    case kScreenWild: return "wild";
    case kScreenCaptureFail: return "capture_fail";
    case kScreenBag: return "bag";
    case kScreenReleaseConfirm: return "release_confirm";
    case kScreenMatch: return "match";
    case kScreenBattle: return "battle";
    case kScreenIdle:
    default:
        return "idle";
    }
}

static const char* app_battle_phase_label();

static const char* screen_mode_label()
{
    switch (screen_mode) {
    case kScreenWild: return "野外遭遇";
    case kScreenCaptureFail: return "捕捉失败";
    case kScreenBag: return "伙伴背包";
    case kScreenReleaseConfirm: return "放生确认";
    case kScreenMatch: return app_battle_phase_label();
    case kScreenBattle: return app_battle_phase_label();
    case kScreenIdle:
    default:
        return "休闲状态";
    }
}

static const char* app_battle_phase()
{
    if (screen_mode == kScreenBattle) {
        if (battle_result_pending) {
            return "clash";
        }
        if (battle_exit_visible) {
            return "exit";
        }
        if (app_last_battle_result_valid) {
            return "result";
        }
        return last_peer_seen_ms == 0 ? "connected" : "ready";
    }
    if (screen_mode == kScreenMatch) {
        if (last_peer_seen_ms != 0 || battle_runtime_state == kBattleStateReady) {
            return "ready";
        }
        if (battle_runtime_state == kBattleStatePairing || WiFi.status() == WL_CONNECTED) {
            return "connected";
        }
    }
    return "finding";
}

static const char* app_battle_phase_label()
{
    const char* phase = app_battle_phase();
    if (strcmp(phase, "clash") == 0) {
        return "宠物交锋";
    }
    if (strcmp(phase, "result") == 0) {
        return "战斗结算";
    }
    if (strcmp(phase, "exit") == 0) {
        return "退场整理";
    }
    if (strcmp(phase, "ready") == 0) {
        return "同步完成";
    }
    if (strcmp(phase, "connected") == 0) {
        return "羁绊同步";
    }
    return "寻找训练师";
}

static void append_uint(String& out, uint32_t value)
{
    out += String(value);
}

static void append_uint64(String& out, uint64_t value)
{
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    out += buffer;
}

static void append_int(String& out, int32_t value)
{
    out += String(value);
}

static void append_sd_file_entries_json(String& out, File& dir, uint8_t depth, uint16_t& count, bool& first, bool& truncated)
{
    while (count < kSdFileListMaxEntries) {
        File entry = dir.openNextFile();
        if (!entry) {
            return;
        }
        const bool isDir = entry.isDirectory();
        const char* name = entry.name();
        if (!first) {
            out += ",";
        }
        first = false;
        out += "{\"path\":";
        out += json_string(name == nullptr ? "" : name);
        out += ",\"type\":";
        out += json_string(isDir ? "dir" : "file");
        out += ",\"size\":";
        append_uint64(out, isDir ? 0 : entry.size());
        out += "}";
        ++count;
        if (isDir && depth + 1 < kSdFileListMaxDepth) {
            append_sd_file_entries_json(out, entry, depth + 1, count, first, truncated);
        }
        entry.close();
        if (count >= kSdFileListMaxEntries) {
            truncated = true;
            return;
        }
    }
    truncated = true;
}

static void append_sd_audio_assets_json(String& out)
{
    out += "\"audioAssets\":[";
    for (size_t i = 0; i < sizeof(kSoundAssetRoutes) / sizeof(kSoundAssetRoutes[0]); ++i) {
        if (i != 0) {
            out += ",";
        }
        const char* path = kSoundAssetRoutes[i].sdPath;
        File file = sd_card_present ? SD.open(path, FILE_READ) : File();
        const bool exists = file && !file.isDirectory();
        const uint64_t size = exists ? file.size() : 0;
        if (file) {
            file.close();
        }
        out += "{\"path\":";
        out += json_string(path);
        out += ",\"exists\":";
        out += exists ? "true" : "false";
        out += ",\"size\":";
        append_uint64(out, size);
        out += "}";
    }
    out += "]";
}

static void append_sd_addon_manifests_json(String& out)
{
    out += "\"addonManifests\":[";
    for (size_t i = 0; i < sizeof(kSdAddonManifestRoutes) / sizeof(kSdAddonManifestRoutes[0]); ++i) {
        if (i != 0) {
            out += ",";
        }
        const char* path = kSdAddonManifestRoutes[i].path;
        File file = sd_card_present ? SD.open(path, FILE_READ) : File();
        const bool exists = file && !file.isDirectory();
        const uint64_t size = exists ? file.size() : 0;
        if (file) {
            file.close();
        }
        out += "{\"type\":";
        out += json_string(kSdAddonManifestRoutes[i].type);
        out += ",\"path\":";
        out += json_string(path);
        out += ",\"exists\":";
        out += exists ? "true" : "false";
        out += ",\"size\":";
        append_uint64(out, size);
        out += "}";
    }
    out += "]";
}

static const char* sample_label_name(uint8_t index)
{
    switch (index) {
    case 0: return "plant_leaf";
    case 1: return "food_fruit";
    case 2: return "paper_book";
    case 3: return "electronics_screen";
    case 4: return "metal_key_coin";
    case 5: return "fabric_cloth";
    case 6: return "cup_bottle_water";
    case 7: return "toy_figure";
    case 8: return "negative";
    default: return "unknown";
    }
}

static const char* sample_scene_name(uint8_t index)
{
    switch (index) {
    case 0: return "white_wall";
    case 1: return "white_paper";
    case 2: return "desktop";
    case 3: return "glare";
    case 4: return "dark";
    case 5: return "bright";
    case 6: return "low_texture";
    case 7: return "hand_cover";
    case 8: return "unknown";
    default: return "unknown";
    }
}

static uint8_t sample_label_index(const String& label)
{
    for (uint8_t i = 0; i < 9; ++i) {
        if (label == sample_label_name(i)) {
            return i;
        }
    }
    return 255;
}

static uint8_t sample_scene_index(const String& scene)
{
    for (uint8_t i = 0; i < 9; ++i) {
        if (scene == sample_scene_name(i)) {
            return i;
        }
    }
    return 8;
}

static const char* sample_scene_from_values(int32_t brightness,
                                            int32_t saturation,
                                            int32_t contrast,
                                            int32_t centerDelta,
                                            int32_t darkRatio,
                                            int32_t brightRatio)
{
    if (darkRatio >= 72 || brightness <= 58) {
        return "dark";
    }
    if (brightRatio >= 70 || brightness >= 214) {
        return "bright";
    }
    if (brightRatio >= 24 && centerDelta >= 46) {
        return "glare";
    }
    if (brightness >= 174 && saturation <= 34 && contrast <= 42 && centerDelta <= 34) {
        return "white_wall";
    }
    if (brightness >= 158 && saturation <= 48 && contrast > 42) {
        return "white_paper";
    }
    if (contrast <= 26 && centerDelta <= 24) {
        return "low_texture";
    }
    return "unknown";
}

static uint8_t split_csv_line(const String& line, String* fields, uint8_t maxFields)
{
    uint8_t count = 0;
    int start = 0;
    while (count < maxFields && start <= static_cast<int>(line.length())) {
        int comma = line.indexOf(',', start);
        if (comma < 0) {
            fields[count++] = line.substring(start);
            break;
        }
        fields[count++] = line.substring(start, comma);
        start = comma + 1;
    }
    for (uint8_t i = 0; i < count; ++i) {
        fields[i].trim();
    }
    return count;
}

static bool sample_path_allowed(const String& path)
{
    return path.startsWith("/samples/") &&
           path.indexOf("..") < 0 &&
           path.indexOf('\\') < 0 &&
           path.indexOf("//") < 0 &&
           path.length() < 96 &&
           (path.endsWith(".csv") || path.endsWith(".ppm"));
}

static void append_sample_counts_json(String& out, const char* key, bool scenes, const uint16_t* counts, uint8_t count)
{
    out += ",\"";
    out += key;
    out += "\":[";
    for (uint8_t i = 0; i < count; ++i) {
        if (i != 0) {
            out += ",";
        }
        out += "{\"name\":";
        out += json_string(scenes ? sample_scene_name(i) : sample_label_name(i));
        out += ",\"count\":";
        append_uint(out, counts[i]);
        out += "}";
    }
    out += "]";
}

static void append_sample_row_json(String& out, const String* fields, uint8_t fieldCount)
{
    const bool hasScene = fieldCount >= 18;
    const uint8_t recognizedIndex = hasScene ? 5 : 4;
    const uint8_t confidenceIndex = hasScene ? 6 : 5;
    const uint8_t presenceIndex = hasScene ? 7 : 6;
    const uint8_t qualityIndex = hasScene ? 8 : 7;
    const uint8_t proximityIndex = hasScene ? 9 : 8;
    const uint8_t brightnessIndex = hasScene ? 10 : 9;
    const uint8_t saturationIndex = hasScene ? 11 : 10;
    const uint8_t contrastIndex = hasScene ? 12 : 11;
    const uint8_t centerDeltaIndex = hasScene ? 13 : 12;
    const uint8_t darkIndex = hasScene ? 14 : 13;
    const uint8_t brightIndex = hasScene ? 15 : 14;
    const uint8_t reasonIndex = hasScene ? 16 : 15;
    const uint8_t thumbIndex = hasScene ? 17 : 16;
    const uint8_t labelSourceIndex = hasScene ? 18 : 255;
    const uint8_t autoLabelIndex = hasScene ? 19 : 255;
    const uint8_t distanceHintIndex = hasScene ? 20 : 255;
    const int32_t brightnessValue = fieldCount > brightnessIndex ? fields[brightnessIndex].toInt() : 0;
    const int32_t saturationValue = fieldCount > saturationIndex ? fields[saturationIndex].toInt() : 0;
    const int32_t contrastValue = fieldCount > contrastIndex ? fields[contrastIndex].toInt() : 0;
    const int32_t centerDeltaValue = fieldCount > centerDeltaIndex ? fields[centerDeltaIndex].toInt() : 0;
    const int32_t darkValue = fieldCount > darkIndex ? fields[darkIndex].toInt() : 0;
    const int32_t brightValue = fieldCount > brightIndex ? fields[brightIndex].toInt() : 0;
    String scene = hasScene ? fields[4] : sample_scene_from_values(brightnessValue,
                                                                   saturationValue,
                                                                   contrastValue,
                                                                   centerDeltaValue,
                                                                   darkValue,
                                                                   brightValue);
    out += "{\"time\":";
    append_uint(out, fieldCount > 0 ? fields[0].toInt() : 0);
    out += ",\"shot\":";
    append_uint(out, fieldCount > 1 ? fields[1].toInt() : 0);
    out += ",\"burst\":";
    append_uint(out, fieldCount > 2 ? fields[2].toInt() : 0);
    out += ",\"label\":";
    out += json_string(fieldCount > 3 ? fields[3].c_str() : "");
    out += ",\"scene\":";
    out += json_string(scene.c_str());
    out += ",\"recognized\":";
    out += (fieldCount > recognizedIndex && fields[recognizedIndex].toInt() != 0) ? "true" : "false";
    out += ",\"confidence\":";
    append_uint(out, fieldCount > confidenceIndex ? fields[confidenceIndex].toInt() : 0);
    out += ",\"presence\":";
    append_uint(out, fieldCount > presenceIndex ? fields[presenceIndex].toInt() : 0);
    out += ",\"quality\":";
    append_uint(out, fieldCount > qualityIndex ? fields[qualityIndex].toInt() : 0);
    out += ",\"proximity\":";
    append_uint(out, fieldCount > proximityIndex ? fields[proximityIndex].toInt() : 0);
    out += ",\"brightness\":";
    append_int(out, brightnessValue);
    out += ",\"saturation\":";
    append_int(out, saturationValue);
    out += ",\"contrast\":";
    append_int(out, contrastValue);
    out += ",\"centerDelta\":";
    append_int(out, centerDeltaValue);
    out += ",\"dark\":";
    append_int(out, darkValue);
    out += ",\"bright\":";
    append_int(out, brightValue);
    out += ",\"reason\":";
    out += json_string(fieldCount > reasonIndex ? fields[reasonIndex].c_str() : "");
    out += ",\"thumb\":";
    out += json_string(fieldCount > thumbIndex ? fields[thumbIndex].c_str() : "");
    out += ",\"labelSource\":";
    out += json_string(fieldCount > labelSourceIndex ? fields[labelSourceIndex].c_str() : "auto");
    out += ",\"autoLabel\":";
    out += json_string(fieldCount > autoLabelIndex ? fields[autoLabelIndex].c_str() : "");
    out += ",\"distanceHint\":";
    out += json_string(fieldCount > distanceHintIndex ? fields[distanceHintIndex].c_str() : "");
    out += "}";
}

static void send_json_response(const String& body)
{
    app_http.sendHeader("Cache-Control", "no-store");
    app_http.sendHeader("Access-Control-Allow-Origin", "*");
    app_http.send(200, "application/json", body);
}

static void send_json_error(uint16_t code, const char* message)
{
    String body = "{\"ok\":false,\"error\":";
    body += json_string(message);
    body += "}";
    app_http.sendHeader("Cache-Control", "no-store");
    app_http.sendHeader("Access-Control-Allow-Origin", "*");
    app_http.send(code, "application/json", body);
}

static void append_genes_json(String& out, const PetGenes& genes)
{
    out += "\"genes\":{";
    out += "\"elementIndex\":";
    append_uint(out, static_cast<uint8_t>(genes.element));
    out += ",\"element\":";
    out += json_string(element_name(genes.element));
    out += ",\"speciesIndex\":";
    append_uint(out, genes.species);
    out += ",\"baseSpecies\":";
    out += json_string(species_name_by(genes.element, genes.species));
    out += ",\"visualVariantIndex\":";
    append_uint(out, pet_visual_variant(genes));
    out += ",\"visualVariantName\":";
    out += json_string(variant_pet_name(genes));
    out += ",\"species\":";
    out += json_string(variant_pet_name(genes));
    out += ",\"moodIndex\":";
    append_uint(out, genes.mood);
    out += ",\"mood\":";
    out += json_string(mood_name(genes.mood));
    out += ",\"bodyScale\":";
    append_uint(out, genes.bodyScale);
    out += ",\"eyeStyle\":";
    append_uint(out, genes.eyeStyle);
    out += ",\"hornStyle\":";
    append_uint(out, genes.hornStyle);
    out += ",\"tailStyle\":";
    append_uint(out, genes.tailStyle);
    out += ",\"auraPattern\":";
    append_uint(out, genes.auraPattern);
    out += ",\"patternDensity\":";
    append_uint(out, genes.patternDensity);
    out += ",\"accentColor\":";
    append_uint(out, genes.accentColor);
    out += ",\"seed\":";
    append_uint(out, genes.seed);
    out += "}";
}

static uint8_t pet_care_fullness(const SavedPet& pet)
{
    int32_t value = 58 + pet.genes.bodyScale / 5 + pet.stage * 5 + pet.level;
    value -= min<uint16_t>(40, pet.battles * 3);
    value += (now_sec() / 90 + pet.genes.seed) % 13;
    return min<uint8_t>(100, clamp_u8(value));
}

static uint8_t pet_care_energy(const SavedPet& pet)
{
    int32_t value = 52 + pet_battle_spirit(pet) / 5 + growth_wait_progress(pet) / 3;
    value -= min<uint16_t>(36, pet.battles * 2);
    value += (pet.genes.mood == 0) ? 8 : 0;
    return min<uint8_t>(100, clamp_u8(value));
}

static uint8_t pet_care_affection(const SavedPet& pet, uint8_t index)
{
    int32_t value = 26 + pet.level * 3 + pet.stage * 12 + pet.wins * 9 + win_rate_percent(pet) / 3;
    if (index == backpack.selected) {
        value += 10;
    }
    return min<uint8_t>(100, clamp_u8(value));
}

static const char* pet_care_hint(uint8_t fullness, uint8_t energy, uint8_t affection, uint8_t focus)
{
    if (fullness < 36) {
        return "Need snack";
    }
    if (energy < 36) {
        return "Need rest";
    }
    if (affection >= 82) {
        return "Close bond";
    }
    if (focus >= 76) {
        return "Battle ready";
    }
    return "Stable";
}

static void append_pet_care_json(String& out, const SavedPet& pet, uint8_t index)
{
    const uint8_t fullness = pet_care_fullness(pet);
    const uint8_t energy = pet_care_energy(pet);
    const uint8_t affection = pet_care_affection(pet, index);
    const uint8_t focus = min<uint8_t>(100, clamp_u8((pet_battle_spirit(pet) + win_rate_percent(pet) + energy) / 3));
    out += ",\"care\":{\"fullness\":";
    append_uint(out, fullness);
    out += ",\"energy\":";
    append_uint(out, energy);
    out += ",\"affection\":";
    append_uint(out, affection);
    out += ",\"focus\":";
    append_uint(out, focus);
    out += ",\"mood\":";
    out += json_string(mood_name(pet.genes.mood));
    out += ",\"hint\":";
    out += json_string(pet_care_hint(fullness, energy, affection, focus));
    out += "}";
}

static void append_pet_json(String& out, const SavedPet& pet, uint8_t index, bool detail)
{
    out += "{";
    out += "\"index\":";
    append_uint(out, index);
    out += ",\"active\":";
    out += (index == backpack.selected ? "true" : "false");
    out += ",\"elementIndex\":";
    append_uint(out, static_cast<uint8_t>(pet.genes.element));
    out += ",\"element\":";
    out += json_string(element_name(pet.genes.element));
    out += ",\"speciesIndex\":";
    append_uint(out, pet.genes.species);
    out += ",\"baseSpecies\":";
    out += json_string(species_name_by(pet.genes.element, pet.genes.species));
    out += ",\"visualVariantIndex\":";
    append_uint(out, pet_visual_variant(pet.genes));
    out += ",\"visualVariantName\":";
    out += json_string(variant_pet_name(pet.genes));
    out += ",\"species\":";
    out += json_string(variant_pet_name(pet.genes));
    out += ",\"level\":";
    append_uint(out, pet.level);
    out += ",\"stageIndex\":";
    append_uint(out, pet.stage);
    out += ",\"stage\":";
    out += json_string(stage_name(pet.stage));
    out += ",\"xp\":";
    append_uint(out, pet.xp);
    out += ",\"nextXp\":";
    append_uint(out, level_xp_need(pet.level));
    char growthGoal[24];
    format_growth_goal(growthGoal, sizeof(growthGoal), pet);
    out += ",\"growthGoal\":";
    out += json_string(growthGoal);
    out += ",\"growthWaitSec\":";
    append_uint(out, seconds_until_growth(pet));
    out += ",\"growthProgress\":";
    append_uint(out, growth_wait_progress(pet));
    out += ",\"growthIntervalSec\":";
    append_uint(out, kGrowthIntervalSec);
    out += ",\"wins\":";
    append_uint(out, pet.wins);
    out += ",\"battles\":";
    append_uint(out, pet.battles);
    out += ",\"winRate\":";
    append_uint(out, win_rate_percent(pet));
    out += ",\"power\":";
    append_uint(out, pet_battle_power(pet));
    out += ",\"agility\":";
    append_uint(out, pet_battle_agility(pet));
    out += ",\"spirit\":";
    append_uint(out, pet_battle_spirit(pet));
    append_pet_care_json(out, pet, index);
    if (detail) {
        out += ",";
        append_genes_json(out, pet.genes);
        out += ",\"capturedAtSec\":";
        append_uint(out, pet.capturedAtSec);
        out += ",\"lastGrowthSec\":";
        append_uint(out, pet.lastGrowthSec);
        out += ",\"friendship\":{\"recentPeerId\":";
        append_uint(out, last_friend_peer_id);
        out += ",\"friendCount\":";
        append_uint(out, local_friend_count);
        out += ",\"score\":";
        append_uint(out, friendship_score());
        out += ",\"battleCount\":";
        append_uint(out, friend_battle_count);
        out += ",\"label\":";
        out += json_string(friend_bond_name());
        out += ",\"rematchStreak\":";
        append_uint(out, friend_rematch_streak);
        out += ",\"lastBattleSec\":";
        append_uint(out, last_friend_battle_sec);
        out += "}";
    }
    out += "}";
}

static void append_capture_burst_json(String& out)
{
    out += ",\"burstCandidates\":[";
    for (uint8_t i = 0; i < last_capture_burst_count; ++i) {
        const CaptureCandidate& candidate = last_capture_burst[i];
        if (i != 0) {
            out += ",";
        }
        out += "{\"burst\":";
        append_uint(out, candidate.burstIndex);
        out += ",\"best\":";
        out += (last_capture_best_burst_index == static_cast<int8_t>(i)) ? "true" : "false";
        out += ",\"valid\":";
        out += candidate.valid ? "true" : "false";
        out += ",\"preprocessed\":";
        out += candidate.preprocessed ? "true" : "false";
        out += ",\"recognized\":";
        out += candidate.recog.recognized ? "true" : "false";
        out += ",\"classId\":";
        append_uint(out, candidate.recog.classId);
        out += ",\"objectLabel\":";
        out += json_string(candidate.recog.objectLabel);
        out += ",\"confidence\":";
        append_uint(out, candidate.recog.confidence);
        out += ",\"presence\":";
        append_uint(out, candidate.presence.score);
        out += ",\"quality\":";
        append_uint(out, candidate.qualityScore);
        out += ",\"proximity\":";
        append_uint(out, candidate.proximity);
        out += ",\"bestDistance\":";
        append_uint(out, candidate.bestDistance);
        out += ",\"margin\":";
        append_uint(out, candidate.margin);
        out += ",\"source\":";
        out += json_string(candidate.source);
        out += ",\"failureReason\":";
        out += json_string(candidate.recog.failureReason);
        out += ",\"preprocessMs\":";
        append_uint(out, candidate.preprocessMs);
        out += ",\"classifyMs\":";
        append_uint(out, candidate.classifyMs);
        out += ",\"brightness\":";
        append_int(out, candidate.traits.brightness);
        out += ",\"saturation\":";
        append_int(out, candidate.traits.saturation);
        out += ",\"contrast\":";
        append_int(out, candidate.traits.contrast);
        out += ",\"centerDelta\":";
        append_int(out, candidate.traits.centerDelta);
        out += "}";
    }
    out += "]";
}

static void append_recognition_json(String& out)
{
    out += "\"recognition\":{";
    out += "\"recognized\":";
    out += wild_recognition.recognized ? "true" : "false";
    out += ",\"classId\":";
    append_uint(out, wild_recognition.classId);
    out += ",\"objectLabel\":";
    out += json_string(wild_recognition.objectLabel);
    out += ",\"materialLabel\":";
    out += json_string(wild_recognition.materialLabel);
    out += ",\"failureReason\":";
    out += json_string(wild_recognition.failureReason);
    out += ",\"confidence\":";
    append_uint(out, wild_recognition.confidence);
    out += ",\"presenceScore\":";
    append_uint(out, wild_recognition.presenceScore);
    out += ",\"elementHint\":";
    out += json_string(element_name(wild_recognition.elementHint));
    out += ",\"speciesBias\":";
    append_uint(out, wild_recognition.speciesBias);
    out += ",\"traits\":{\"r\":";
    append_uint(out, wild_traits.r);
    out += ",\"g\":";
    append_uint(out, wild_traits.g);
    out += ",\"b\":";
    append_uint(out, wild_traits.b);
    out += ",\"brightness\":";
    append_int(out, wild_traits.brightness);
    out += ",\"saturation\":";
    append_int(out, wild_traits.saturation);
    out += ",\"contrast\":";
    append_int(out, wild_traits.contrast);
    out += ",\"centerDelta\":";
    append_int(out, wild_traits.centerDelta);
    out += ",\"darkRatio\":";
    append_int(out, wild_traits.darkRatio);
    out += ",\"brightRatio\":";
    append_int(out, wild_traits.brightRatio);
    out += "}";
    append_capture_burst_json(out);
    out += "}";
}

static void append_battle_replay_record_json(String& out, const BattleReplayRecord& record)
{
    out += "{\"timeSec\":";
    append_uint(out, record.timeSec);
    out += ",\"battleId\":";
    append_uint(out, record.battleId);
    out += ",\"outcome\":";
    out += json_string(record.outcome);
    out += ",\"outcomeLabel\":";
    out += json_string(battle_outcome_label(record.outcome));
    out += ",\"myScore\":";
    append_int(out, record.myScore);
    out += ",\"peerScore\":";
    append_int(out, record.peerScore);
    out += ",\"scoreDiff\":";
    append_int(out, record.scoreDiff);
    out += ",\"powerDiff\":";
    append_int(out, record.powerDiff);
    out += ",\"elementSwing\":";
    append_int(out, record.elementSwing);
    out += ",\"spiritDiff\":";
    append_int(out, record.spiritDiff);
    out += ",\"advantageLabel\":";
    out += json_string(battle_advantage_label(record.elementSwing));
    out += ",\"xpGained\":";
    append_uint(out, record.xpGained);
    out += ",\"friendBonus\":";
    append_uint(out, record.friendBonus);
    out += ",\"mySkill\":";
    out += json_string(record.mySkill);
    out += ",\"opponentSkill\":";
    out += json_string(record.opponentSkill);
    out += ",\"opponentSpecies\":";
    out += json_string(record.opponentSpecies);
    out += ",\"opponentElement\":";
    out += json_string(record.opponentElement);
    out += ",\"opponentLevel\":";
    append_uint(out, record.opponentLevel);
    out += ",\"levelUp\":";
    out += record.levelUp ? "true" : "false";
    out += ",\"stageUp\":";
    out += record.stageUp ? "true" : "false";
    out += "}";
}

static void append_battle_replays_json(String& out)
{
    out += "[";
    bool first = true;
    for (uint8_t n = 0; n < battle_replay_count; ++n) {
        uint8_t idx = static_cast<uint8_t>((battle_replay_next + kBattleReplayCapacity - 1 - n) % kBattleReplayCapacity);
        const BattleReplayRecord& record = battle_replays[idx];
        if (!record.valid) {
            continue;
        }
        if (!first) {
            out += ",";
        }
        append_battle_replay_record_json(out, record);
        first = false;
    }
    out += "]";
}

static void append_battle_json(String& out)
{
    out += "\"battle\":{";
    out += "\"active\":";
    out += (screen_mode == kScreenMatch || screen_mode == kScreenBattle) ? "true" : "false";
    out += ",\"phase\":";
    out += json_string(app_battle_phase());
    out += ",\"phaseLabel\":";
    out += json_string(app_battle_phase_label());
    out += ",\"linkText\":";
    out += json_string(battle_link_status());
    out += ",\"peerSeen\":";
    out += last_peer_seen_ms == 0 ? "false" : "true";
    uint8_t syncStep = match_sync_step();
    out += ",\"pairingStep\":";
    append_uint(out, syncStep);
    out += ",\"pairingStepLabel\":";
    out += json_string(match_sync_step_label(syncStep));
    out += ",\"waitingSec\":";
    append_uint(out, (screen_mode == kScreenMatch && match_started_ms != 0)
                     ? (millis() - match_started_ms) / 1000
                     : 0);
    out += ",\"syncAgeSec\":";
    append_uint(out, last_peer_seen_ms == 0 ? 0 : (millis() - last_peer_seen_ms) / 1000);
    uint8_t clashPhase = current_battle_clash_phase();
    int32_t clashDiff = 0;
    const char* clashLabel = "";
    if (battle_result_pending && screen_mode == kScreenBattle) {
        BattlePetPacket mine = make_battle_packet(local_pet, local_pet_sequence);
        clashDiff = battle_round_diff_for_phase(clashPhase, mine, pending_battle_packet);
        clashLabel = battle_round_title(clashPhase);
    }
    out += ",\"clashRound\":";
    append_uint(out, (battle_result_pending && screen_mode == kScreenBattle) ? clashPhase + 1 : 0);
    out += ",\"clashRoundLabel\":";
    out += json_string(clashLabel);
    out += ",\"clashRoundDiff\":";
    append_int(out, clashDiff);
    out += ",\"resultValid\":";
    out += app_last_battle_result_valid ? "true" : "false";
    out += ",\"outcome\":";
    out += json_string(app_last_battle_result_valid ? app_last_battle_outcome : "");
    out += ",\"outcomeLabel\":";
    out += json_string(app_last_battle_result_valid ? battle_outcome_label(app_last_battle_outcome) : "");
    out += ",\"battleId\":";
    append_uint(out, app_last_battle_result_valid ? app_last_battle_id : 0);
    out += ",\"myScore\":";
    append_int(out, app_last_battle_my_score);
    out += ",\"peerScore\":";
    append_int(out, app_last_battle_peer_score);
    out += ",\"scoreDiff\":";
    append_int(out, app_last_battle_score_diff);
    out += ",\"powerDiff\":";
    append_int(out, app_last_battle_power_diff);
    out += ",\"elementSwing\":";
    append_int(out, app_last_battle_element_swing);
    out += ",\"spiritDiff\":";
    append_int(out, app_last_battle_spirit_diff);
    out += ",\"advantageLabel\":";
    out += json_string(battle_advantage_label(app_last_battle_element_swing));
    out += ",\"xpGained\":";
    append_uint(out, app_last_battle_xp);
    out += ",\"friendBonus\":";
    append_uint(out, app_last_battle_friend_bonus);
    out += ",\"mySkill\":";
    out += json_string(app_last_battle_result_valid ? app_last_battle_my_skill : "");
    out += ",\"opponentSkill\":";
    out += json_string(app_last_battle_result_valid ? app_last_battle_peer_skill : "");
    out += ",\"rematchAvailable\":";
    out += friend_rematch_available() ? "true" : "false";
    out += ",\"nextRematchXp\":";
    append_uint(out, next_rematch_xp_bonus());
    out += ",\"nextFriendshipGain\":";
    append_uint(out, next_friendship_score_gain());
    out += ",\"levelUp\":";
    out += app_last_battle_level_up ? "true" : "false";
    out += ",\"stageUp\":";
    out += app_last_battle_stage_up ? "true" : "false";
    out += ",\"friendshipScore\":";
    append_uint(out, friendship_score());
    out += ",\"friendshipLabel\":";
    out += json_string(friend_bond_name());
    out += ",\"friendshipGoal\":";
    out += json_string(friendship_goal_badge());
    out += ",\"friendshipPrompt\":";
    out += json_string(friendship_prompt());
    out += ",\"friendNotice\":";
    out += json_string(last_friend_notice);
    out += ",\"friendAdded\":";
    out += last_friend_added ? "true" : "false";
    out += ",\"friendBondUp\":";
    out += last_friend_bond_up ? "true" : "false";
    out += ",\"friendBattleCount\":";
    append_uint(out, friend_battle_count);
    out += ",\"recentPeerId\":";
    append_uint(out, last_friend_peer_id);
    out += ",\"opponentSpecies\":";
    out += json_string(app_last_opponent_species);
    out += ",\"opponentElement\":";
    out += json_string(app_last_opponent_element);
    out += ",\"opponentLevel\":";
    append_uint(out, app_last_opponent_level);
    out += ",\"friendCount\":";
    append_uint(out, local_friend_count);
    out += ",\"localFriends\":[";
    bool firstFriend = true;
    for (uint8_t i = 0; i < kLocalFriendSlots; ++i) {
        const LocalFriendRecord& record = local_friends[i];
        if (record.peerId == 0) {
            continue;
        }
        if (!firstFriend) {
            out += ",";
        }
        firstFriend = false;
        out += "{\"peerId\":";
        append_uint(out, record.peerId);
        out += ",\"score\":";
        append_uint(out, record.score);
        out += ",\"battleCount\":";
        append_uint(out, record.battleCount);
        out += ",\"label\":";
        out += json_string(friend_bond_name_for(record.peerId, record.score));
        out += ",\"recent\":";
        out += record.peerId == last_friend_peer_id ? "true" : "false";
        out += "}";
    }
    out += "]";
    out += ",\"replayCapacity\":";
    append_uint(out, kBattleReplayCapacity);
    out += ",\"replayCount\":";
    append_uint(out, battle_replay_count);
    out += ",\"replays\":";
    append_battle_replays_json(out);
    out += "}";
}

static void send_app_status()
{
    refresh_backpack_growth(false);
    String out;
    out.reserve(1100);
    out += "{\"ok\":true,\"firmwareVersion\":\"v0.2-app-http\",\"deviceId\":";
    append_uint(out, device_id);
    out += ",\"ssid\":";
    out += json_string(battle_ap_ssid);
    out += ",\"httpBaseUrl\":\"http://";
    out += WiFi.softAPIP().toString();
    out += "\",\"screen\":";
    out += json_string(screen_mode_name());
    out += ",\"screenLabel\":";
    out += json_string(screen_mode_label());
    out += ",\"sdCardPresent\":";
    out += sd_card_present ? "true" : "false";
    out += ",\"buttons\":{\"left\":";
    out += json_string(button_label_for_slot(kButtonLeft));
    out += ",\"middle\":";
    out += json_string(button_label_for_slot(kButtonMiddle));
    out += ",\"right\":";
    out += json_string(button_label_for_slot(kButtonRight));
    out += "}";
    out += ",\"buttonActions\":{\"left\":";
    out += json_string(app_action_type_for_button(kButtonLeft));
    out += ",\"middle\":";
    out += json_string(app_action_type_for_button(kButtonMiddle));
    out += ",\"right\":";
    out += json_string(app_action_type_for_button(kButtonRight));
    out += "}";
    out += ",\"muted\":";
    out += audio_muted ? "true" : "false";
    out += ",";
    append_sampling_json(out);
    out += ",\"bagCount\":";
    append_uint(out, backpack.count);
    out += ",\"bagCapacity\":";
    append_uint(out, kMaxBackpackPets);
    out += ",\"selectedIndex\":";
    append_uint(out, backpack.selected);
    out += ",\"bagCursor\":";
    append_uint(out, backpack.count == 0 ? 0 : bag_cursor);
    out += ",\"growth\":{\"recent\":";
    out += growth_notice_recent(180) ? "true" : "false";
    out += ",\"petIndex\":";
    append_uint(out, last_growth_pet_index);
    out += ",\"xpGained\":";
    append_uint(out, last_growth_xp_gain);
    out += ",\"level\":";
    append_uint(out, last_growth_level);
    out += ",\"stage\":";
    out += json_string(stage_name(last_growth_stage));
    out += ",\"levelUp\":";
    out += last_growth_level_up ? "true" : "false";
    out += ",\"stageUp\":";
    out += last_growth_stage_up ? "true" : "false";
    out += "}";
    out += ",\"hasCurrentPet\":";
    out += has_local_pet ? "true" : "false";
    if (has_local_pet) {
        out += ",\"currentPet\":";
        const SavedPet* pet = selected_pet_const();
        if (pet != nullptr) {
            append_pet_json(out, *pet, backpack.selected, false);
        } else {
            out += "null";
        }
    } else {
        out += ",\"currentPet\":null";
    }
    out += ",";
    append_recognition_json(out);
    out += ",";
    append_battle_json(out);
    out += "}";
    send_json_response(out);
}

static void send_app_backpack()
{
    refresh_backpack_growth(true);
    String out;
    out.reserve(1200);
    out += "{\"ok\":true,\"count\":";
    append_uint(out, backpack.count);
    out += ",\"capacity\":";
    append_uint(out, kMaxBackpackPets);
    out += ",\"selectedIndex\":";
    append_uint(out, backpack.selected);
    out += ",\"cursorIndex\":";
    append_uint(out, backpack.count == 0 ? 0 : bag_cursor);
    out += ",\"pets\":[";
    for (uint8_t i = 0; i < backpack.count; ++i) {
        if (i != 0) {
            out += ",";
        }
        append_pet_json(out, backpack.pets[i], i, false);
    }
    out += "]}";
    send_json_response(out);
}

static void send_app_encyclopedia()
{
    String out;
    out.reserve(9800);
    uint8_t ownedTemplates = 0;
    out += "{\"ok\":true,\"total\":45,\"entries\":[";
    bool first = true;
    for (uint8_t element = 0; element < 5; ++element) {
        for (uint8_t species = 0; species < 3; ++species) {
            for (uint8_t variant = 0; variant < 3; ++variant) {
                uint8_t count = 0;
                uint8_t maxLevel = 0;
                for (uint8_t i = 0; i < backpack.count; ++i) {
                    const SavedPet& pet = backpack.pets[i];
                    if (static_cast<uint8_t>(pet.genes.element) == element &&
                        pet.genes.species % 3 == species &&
                        pet_visual_variant(pet.genes) == variant) {
                        ++count;
                        if (pet.level > maxLevel) {
                            maxLevel = pet.level;
                        }
                    }
                }
                if (count > 0) {
                    ++ownedTemplates;
                }
                if (!first) {
                    out += ",";
                }
                first = false;
                ElementType elem = static_cast<ElementType>(element);
                out += "{\"elementIndex\":";
                append_uint(out, element);
                out += ",\"element\":";
                out += json_string(element_name(elem));
                out += ",\"speciesIndex\":";
                append_uint(out, species);
                out += ",\"baseSpecies\":";
                out += json_string(species_name_by(elem, species));
                out += ",\"visualVariantIndex\":";
                append_uint(out, variant);
                out += ",\"name\":";
                out += json_string(variant_pet_name_by(elem, species, variant));
                out += ",\"owned\":";
                out += count > 0 ? "true" : "false";
                out += ",\"count\":";
                append_uint(out, count);
                out += ",\"maxLevel\":";
                append_uint(out, maxLevel);
                out += "}";
            }
        }
    }
    out += "],\"ownedTemplates\":";
    append_uint(out, ownedTemplates);
    out += "}";
    send_json_response(out);
}

static void send_app_pet_detail()
{
    int index = app_http.hasArg("index") ? app_http.arg("index").toInt() : -1;
    if (index < 0 || index >= backpack.count || index >= kMaxBackpackPets) {
        send_json_error(400, "invalid pet index");
        return;
    }
    String out;
    out.reserve(900);
    out += "{\"ok\":true,\"pet\":";
    append_pet_json(out, backpack.pets[index], static_cast<uint8_t>(index), true);
    out += "}";
    send_json_response(out);
}

static void send_app_recognition()
{
    String out;
    out.reserve(600);
    out += "{\"ok\":true,";
    append_recognition_json(out);
    out += "}";
    send_json_response(out);
}

static void send_app_battle()
{
    String out;
    out.reserve(2600);
    out += "{\"ok\":true,";
    append_battle_json(out);
    out += "}";
    send_json_response(out);
}

static void send_app_battle_replays()
{
    String out;
    out.reserve(2200);
    out += "{\"ok\":true,\"capacity\":";
    append_uint(out, kBattleReplayCapacity);
    out += ",\"count\":";
    append_uint(out, battle_replay_count);
    out += ",\"replays\":";
    append_battle_replays_json(out);
    out += "}";
    send_json_response(out);
}

static uint32_t app_log_time_sec(const char* line)
{
    if (line == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(strtoul(line, nullptr, 10));
}

static const char* app_log_message_part(const char* line)
{
    if (line == nullptr) {
        return "";
    }
    const char* space = strchr(line, ' ');
    return space == nullptr ? line : space + 1;
}

static const char* app_log_level_for(const char* message)
{
    if (message == nullptr) {
        return "info";
    }
    if (strstr(message, "失败") != nullptr ||
        strstr(message, "invalid") != nullptr ||
        strstr(message, "error") != nullptr) {
        return "error";
    }
    if (strstr(message, "missing") != nullptr ||
        strstr(message, "请先") != nullptr ||
        strstr(message, "不支持") != nullptr) {
        return "warn";
    }
    return "info";
}

static const char* app_log_category_for(const char* message)
{
    if (message == nullptr) {
        return "system";
    }
    if (strstr(message, "音效") != nullptr || strstr(message, "sound") != nullptr) {
        return "setting";
    }
    if (strstr(message, "捕捉") != nullptr || strstr(message, "photo") != nullptr ||
        strstr(message, "capture") != nullptr) {
        return "capture";
    }
    if (strstr(message, "friend") != nullptr || strstr(message, "好友") != nullptr ||
        strstr(message, "对手") != nullptr) {
        return "friend";
    }
    if (strstr(message, "battle") != nullptr || strstr(message, "match") != nullptr) {
        return "battle";
    }
    if (strstr(message, "sd card") != nullptr) {
        return "storage";
    }
    if (strstr(message, "http") != nullptr || strstr(message, "setup") != nullptr) {
        return "system";
    }
    if (strstr(message, "app action") != nullptr) {
        return "action";
    }
    return "system";
}

static const char* app_log_detail_for(const char* message)
{
    if (message == nullptr) {
        return "设备记录了一条本地事件。";
    }
    if (strstr(message, "app action photo") != nullptr) {
        return "手机网页端发送了拍照指令，CoreS3 会在本地完成拍照、识别和生成伙伴。";
    }
    if (strstr(message, "app action bag") != nullptr) {
        return "手机网页端请求打开背包，设备上的三键状态会同步切到背包流程。";
    }
    if (strstr(message, "app action prev") != nullptr || strstr(message, "app action next") != nullptr) {
        return "手机网页端在背包中切换伙伴，当前选中位置仍由设备端背包光标控制。";
    }
    if (strstr(message, "app action select") != nullptr) {
        return "手机网页端请求把背包光标所在伙伴设为当前伙伴。";
    }
    if (strstr(message, "app action match") != nullptr) {
        return "手机网页端请求进入对战匹配，设备会进入本地近距离匹配流程。";
    }
    if (strstr(message, "app action friend") != nullptr || strstr(message, "好友") != nullptr) {
        return "好友信息只记录在本次开机的本机状态中，用于展示最近对手、友情分和连战数据。";
    }
    if (strstr(message, "音效已静音") != nullptr) {
        return "网页端已关闭本次运行中的提示音和宠物音效，重启后会回到固件默认设置。";
    }
    if (strstr(message, "音效已开启") != nullptr) {
        return "网页端已恢复本次运行中的提示音和宠物音效。";
    }
    if (strstr(message, "捕捉成功") != nullptr) {
        return "识别结果通过本地限定类别判断后，伙伴已写入 Preferences 背包存档。";
    }
    if (strstr(message, "捕捉失败") != nullptr) {
        return "本地轻量识别没有达到捕捉条件，图片不会上传云端。";
    }
    if (strstr(message, "sd card missing") != nullptr) {
        return "启动时没有检测到 SD 卡；当前固件仍可离线运行，背包继续使用 Preferences。";
    }
    if (strstr(message, "sd card present") != nullptr) {
        return "启动时检测到 SD 卡；当前版本只显示状态，不把玩法数据迁移到 SD 卡。";
    }
    return "设备记录了一条本地运行事件，可用于判断最近的网页操作、捕捉、对战或设置变化。";
}

static void send_app_logs()
{
    String out;
    out.reserve(2400);
    out += "{\"ok\":true,\"logs\":[";
    for (uint8_t i = 0; i < app_log_count; ++i) {
        uint8_t index = (app_log_next + kAppLogCapacity - app_log_count + i) % kAppLogCapacity;
        if (i != 0) {
            out += ",";
        }
        const char* line = app_log_lines[index];
        const char* message = app_log_message_part(line);
        out += "{\"id\":";
        append_uint(out, i + 1);
        out += ",\"timeSec\":";
        append_uint(out, app_log_time_sec(line));
        out += ",\"message\":";
        out += json_string(message);
        out += ",\"level\":";
        out += json_string(app_log_level_for(message));
        out += ",\"category\":";
        out += json_string(app_log_category_for(message));
        out += ",\"detail\":";
        out += json_string(app_log_detail_for(message));
        out += "}";
    }
    out += "]}";
    send_json_response(out);
}

static void send_app_storage()
{
    String out;
    out.reserve(7000);
    out += "{\"ok\":true,\"sdCardPresent\":";
    out += sd_card_present ? "true" : "false";
    out += ",\"cardType\":";
    out += json_string(sd_card_present ? sd_card_type_name(SD.cardType()) : "none");
    out += ",\"cardSizeBytes\":";
    append_uint64(out, sd_card_present ? SD.cardSize() : 0);
    const uint64_t total = sd_card_present ? SD.totalBytes() : 0;
    const uint64_t used = sd_card_present ? SD.usedBytes() : 0;
    out += ",\"totalBytes\":";
    append_uint64(out, total);
    out += ",\"usedBytes\":";
    append_uint64(out, used);
    out += ",\"freeBytes\":";
    append_uint64(out, total >= used ? total - used : 0);
    out += ",";
    append_sd_audio_assets_json(out);
    out += ",";
    append_sd_addon_manifests_json(out);
    out += ",\"files\":[";
    uint16_t count = 0;
    bool first = true;
    bool truncated = false;
    if (sd_card_present) {
        File root = SD.open("/");
        if (root) {
            append_sd_file_entries_json(out, root, 0, count, first, truncated);
            root.close();
        }
    }
    out += "],\"fileCount\":";
    append_uint(out, count);
    out += ",\"truncated\":";
    out += truncated ? "true" : "false";
    out += "}";
    send_json_response(out);
}

static bool sample_arg_truthy(String value)
{
    value.trim();
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

static bool set_sample_label_value(String label, const char** error)
{
    label.trim();
    label.toLowerCase();
    if (sample_label_index(label) == 255) {
        if (error != nullptr) {
            *error = "unsupported sample label";
        }
        return false;
    }
    snprintf(sample_mode_label, sizeof(sample_mode_label), "%s", label.c_str());
    return true;
}

static bool set_sample_scene_value(String scene, const char** error)
{
    scene.trim();
    scene.toLowerCase();
    bool sceneKnown = false;
    for (uint8_t i = 0; i < 9; ++i) {
        if (scene == sample_scene_name(i)) {
            sceneKnown = true;
            break;
        }
    }
    if (!sceneKnown) {
        if (error != nullptr) {
            *error = "unsupported sample scene";
        }
        return false;
    }
    snprintf(sample_mode_scene, sizeof(sample_mode_scene), "%s", scene.c_str());
    return true;
}

static void log_sample_mode_change()
{
    char logLine[80];
    snprintf(logLine, sizeof(logLine), "sampling %s %s/%s",
             sample_mode_enabled ? "on" : "off",
             sample_mode_label,
             sample_mode_scene);
    append_app_log(logLine);
}

static void append_sampling_json(String& out)
{
    out += "\"sampling\":{\"enabled\":";
    out += sample_mode_enabled ? "true" : "false";
    out += ",\"label\":";
    out += json_string(sample_mode_label);
    out += ",\"scene\":";
    out += json_string(sample_mode_scene);
    out += ",\"labelOptions\":[";
    for (uint8_t i = 0; i < 9; ++i) {
        if (i != 0) {
            out += ",";
        }
        out += json_string(sample_label_name(i));
    }
    out += "],\"sceneOptions\":[";
    for (uint8_t i = 0; i < 9; ++i) {
        if (i != 0) {
            out += ",";
        }
        out += json_string(sample_scene_name(i));
    }
    out += "]}";
}

static void send_app_sampling()
{
    bool changed = false;
    if (app_http.hasArg("enabled")) {
        sample_mode_enabled = sample_arg_truthy(app_http.arg("enabled"));
        changed = true;
    }
    if (app_http.hasArg("label")) {
        const char* error = nullptr;
        if (!set_sample_label_value(app_http.arg("label"), &error)) {
            send_json_error(400, error == nullptr ? "unsupported sample label" : error);
            return;
        }
        changed = true;
    }
    if (app_http.hasArg("scene")) {
        const char* error = nullptr;
        if (!set_sample_scene_value(app_http.arg("scene"), &error)) {
            send_json_error(400, error == nullptr ? "unsupported sample scene" : error);
            return;
        }
        changed = true;
    }
    if (changed) {
        log_sample_mode_change();
    }

    String out;
    out.reserve(520);
    out += "{\"ok\":true,";
    append_sampling_json(out);
    out += "}";
    send_json_response(out);
}

static void send_app_capture_quality()
{
    ImageTraits traits = {};
    SubjectPresence presence = {};
    bool frameOk = false;
    bool preprocessed = false;
    uint16_t proximity = 0;
    uint8_t quality = 0;
    const char* hint = "Camera frame missing";
    const char* scene = "unknown";

    frameOk = CoreS3.Camera.get();
    if (frameOk) {
        preprocessed = preprocess_frame_for_vision();
        traits = analyze_frame();
        presence = detect_subject_presence(traits);
        proximity = read_capture_proximity();
        quality = capture_quality_score(traits, presence, proximity);
        hint = capture_quality_hint(traits, presence, proximity);
        scene = scene_label_for_traits(traits);
        last_capture_proximity = proximity;
        last_capture_quality_score = quality;
        CoreS3.Camera.free();
    }

    String out;
    out.reserve(1200);
    out += "{\"ok\":true,\"frame\":";
    out += frameOk ? "true" : "false";
    out += ",\"preprocessed\":";
    out += preprocessed ? "true" : "false";
    out += ",\"quality\":";
    append_uint(out, quality);
    out += ",\"hint\":";
    out += json_string(hint);
    out += ",\"scene\":";
    out += json_string(scene);
    out += ",\"proximity\":";
    append_uint(out, proximity);
    out += ",\"proximityAvailable\":";
    out += proximity_sensor_available ? "true" : "false";
    out += ",\"presence\":{\"present\":";
    out += presence.present ? "true" : "false";
    out += ",\"score\":";
    append_uint(out, presence.score);
    out += "},\"traits\":{\"r\":";
    append_uint(out, traits.r);
    out += ",\"g\":";
    append_uint(out, traits.g);
    out += ",\"b\":";
    append_uint(out, traits.b);
    out += ",\"brightness\":";
    append_int(out, traits.brightness);
    out += ",\"saturation\":";
    append_int(out, traits.saturation);
    out += ",\"contrast\":";
    append_int(out, traits.contrast);
    out += ",\"centerDelta\":";
    append_int(out, traits.centerDelta);
    out += ",\"darkRatio\":";
    append_int(out, traits.darkRatio);
    out += ",\"brightRatio\":";
    append_int(out, traits.brightRatio);
    out += ",\"frameWidth\":";
    append_int(out, traits.frameWidth);
    out += ",\"frameHeight\":";
    append_int(out, traits.frameHeight);
    out += "}}";
    send_json_response(out);
}

static void send_app_samples()
{
    uint16_t labelCounts[9] = {};
    uint16_t sceneCounts[9] = {};
    String recent[kSampleRecentMaxRows];
    uint8_t recentCount = 0;
    uint16_t rowCount = 0;
    uint16_t unknownLabelCount = 0;
    bool truncated = false;
    bool manifestExists = false;
    uint64_t manifestSize = 0;

    if (sd_card_present) {
        File manifest = SD.open("/samples/manifest.csv", FILE_READ);
        if (manifest && !manifest.isDirectory()) {
            manifestExists = true;
            manifestSize = manifest.size();
            while (manifest.available()) {
                String line = manifest.readStringUntil('\n');
                line.trim();
                if (line.length() == 0 || line.startsWith("time,")) {
                    continue;
                }
                if (rowCount >= kSampleSummaryMaxRows) {
                    truncated = true;
                    break;
                }
                String fields[21];
                uint8_t fieldCount = split_csv_line(line, fields, 21);
                if (fieldCount < 16) {
                    continue;
                }
                const bool hasScene = fieldCount >= 18;
                uint8_t labelIndex = sample_label_index(fields[3]);
                if (labelIndex < 9) {
                    ++labelCounts[labelIndex];
                } else {
                    ++unknownLabelCount;
                }

                String scene = hasScene ? fields[4] : sample_scene_from_values(fields[hasScene ? 10 : 9].toInt(),
                                                                                fields[hasScene ? 11 : 10].toInt(),
                                                                                fields[hasScene ? 12 : 11].toInt(),
                                                                                fields[hasScene ? 13 : 12].toInt(),
                                                                                fields[hasScene ? 14 : 13].toInt(),
                                                                                fields[hasScene ? 15 : 14].toInt());
                ++sceneCounts[sample_scene_index(scene)];

                String rowJson;
                rowJson.reserve(360);
                append_sample_row_json(rowJson, fields, fieldCount);
                for (int8_t i = min<uint8_t>(recentCount, kSampleRecentMaxRows - 1); i > 0; --i) {
                    recent[i] = recent[i - 1];
                }
                recent[0] = rowJson;
                if (recentCount < kSampleRecentMaxRows) {
                    ++recentCount;
                }
                ++rowCount;
            }
        }
        if (manifest) {
            manifest.close();
        }
    }

    String out;
    out.reserve(5600);
    out += "{\"ok\":true,\"sdCardPresent\":";
    out += sd_card_present ? "true" : "false";
    out += ",\"manifestExists\":";
    out += manifestExists ? "true" : "false";
    out += ",\"manifestPath\":\"/samples/manifest.csv\",\"manifestSize\":";
    append_uint64(out, manifestSize);
    out += ",\"rowCount\":";
    append_uint(out, rowCount);
    out += ",\"unknownLabelCount\":";
    append_uint(out, unknownLabelCount);
    out += ",\"truncated\":";
    out += truncated ? "true" : "false";
    append_sample_counts_json(out, "classes", false, labelCounts, 9);
    append_sample_counts_json(out, "scenes", true, sceneCounts, 9);
    out += ",\"recent\":[";
    for (uint8_t i = 0; i < recentCount; ++i) {
        if (i != 0) {
            out += ",";
        }
        out += recent[i];
    }
    out += "]}";
    send_json_response(out);
}

static void send_app_sample_manifest()
{
    if (!sd_card_present || !SD.exists("/samples/manifest.csv")) {
        send_json_error(404, "sample manifest not found");
        return;
    }
    File file = SD.open("/samples/manifest.csv", FILE_READ);
    if (!file || file.isDirectory()) {
        send_json_error(404, "sample manifest not readable");
        return;
    }
    app_http.sendHeader("Cache-Control", "no-store");
    app_http.sendHeader("Access-Control-Allow-Origin", "*");
    app_http.sendHeader("Content-Disposition", "attachment; filename=\"manifest.csv\"");
    app_http.streamFile(file, "text/csv");
    file.close();
}

static void send_app_sample_file()
{
    if (!sd_card_present) {
        send_json_error(404, "sd card missing");
        return;
    }
    if (!app_http.hasArg("path")) {
        send_json_error(400, "missing sample path");
        return;
    }
    String path = app_http.arg("path");
    if (!sample_path_allowed(path)) {
        send_json_error(400, "unsupported sample path");
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        send_json_error(404, "sample file not found");
        return;
    }
    const char* contentType = path.endsWith(".csv") ? "text/csv" :
                              (path.endsWith(".ppm") ? "image/x-portable-pixmap" : "application/octet-stream");
    app_http.sendHeader("Cache-Control", "no-store");
    app_http.sendHeader("Access-Control-Allow-Origin", "*");
    app_http.streamFile(file, contentType);
    file.close();
}

static const char kAppPageHtml[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#182028">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<title>M5 宠物伴侣</title>
<style>
:root{color-scheme:light dark;--bg:#f5f7fa;--card:#fff;--text:#18202a;--muted:#647184;--line:#d9e0ea;--primary:#2563eb;--ok:#0f8b4c;--warn:#b45309;--danger:#c2410c}
@media(prefers-color-scheme:dark){:root{--bg:#101418;--card:#182028;--text:#eef3f8;--muted:#9aa7b5;--line:#2b3642;--primary:#6ea8ff;--ok:#4ade80;--warn:#fbbf24;--danger:#fb923c}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
header{position:sticky;top:0;z-index:2;padding:calc(12px + env(safe-area-inset-top)) 16px 12px;background:var(--card);border-bottom:1px solid var(--line)}
h1{margin:0;font-size:20px}.head{display:flex;gap:10px;align-items:center;justify-content:space-between}.meta{margin-top:5px;font-size:12px;color:var(--muted);line-height:1.35}h2{margin:0 0 10px;font-size:17px}main{padding:14px 14px calc(82px + env(safe-area-inset-bottom));max-width:760px;margin:0 auto}.grid{display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}.row{display:flex;gap:12px;justify-content:space-between;align-items:flex-start;margin:7px 0}
.label{color:var(--muted)}.value{text-align:right;font-weight:600;overflow-wrap:anywhere}.pill{display:inline-block;border-radius:999px;padding:3px 9px;background:rgba(37,99,235,.12);color:var(--primary);font-size:12px;white-space:nowrap}
.ok{color:var(--ok)}.warn{color:var(--warn)}.danger{color:var(--danger)}button{appearance:none;border:0;border-radius:9px;background:var(--primary);color:white;font-weight:700;padding:12px 14px;font-size:15px;min-height:44px}
button:disabled{opacity:.48}
button small{display:block;margin-top:2px;font-size:12px;font-weight:500;opacity:.82}button.secondary{background:transparent;color:var(--primary);border:1px solid var(--line)}button.dangerbtn{background:var(--danger)}input,select,textarea{width:100%;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--text);padding:10px;font:inherit}textarea{min-height:72px;resize:vertical}pre{white-space:pre-wrap;overflow:auto;border:1px solid var(--line);border-radius:8px;background:var(--bg);padding:10px;font-size:12px}body.big-buttons main button{min-height:58px;font-size:17px}.actions{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px}.pet{border-top:1px solid var(--line);padding-top:10px;margin-top:10px}
.pet:first-child{border-top:0;padding-top:0;margin-top:0}.small{font-size:13px;color:var(--muted)}.goal{margin-top:6px;display:inline-block;border-radius:6px;padding:4px 8px;background:rgba(15,139,76,.12);color:var(--ok);font-weight:700}.stat{display:grid;grid-template-columns:32px 1fr 42px;gap:8px;align-items:center;margin-top:6px}.meter{height:7px;border-radius:99px;background:var(--line);overflow:hidden}.fill{height:100%;border-radius:99px;background:var(--primary)}.note{margin-top:8px;color:var(--muted);font-size:13px;line-height:1.45}.toast{display:none;margin-top:8px;padding:8px 10px;border-radius:8px;background:rgba(194,65,12,.12);color:var(--danger);font-size:13px}.toast.show{display:block}nav{position:fixed;left:0;right:0;bottom:0;z-index:5;padding-bottom:env(safe-area-inset-bottom);background:var(--card);border-top:1px solid var(--line);display:grid;grid-template-columns:repeat(7,1fr)}
.toggle{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid var(--line)}.toggle:first-child{border-top:0}.toggle input{width:22px;height:22px}.modebanner{border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-bottom:10px;background:rgba(37,99,235,.07);position:relative;overflow:hidden}.modebanner:before{content:"";position:absolute;left:0;top:0;bottom:0;width:5px;background:var(--primary)}.modebanner span{display:block;color:var(--muted);font-size:12px}.modebanner b{display:block;margin-top:2px;font-size:20px}.mode-wild:before{background:#16a34a}.mode-capture-fail:before,.mode-release-confirm:before{background:#c2410c}.mode-bag:before{background:#ca8a04}.mode-match:before{background:#2563eb}.mode-battle:before{background:#dc2626}.statusbox{display:grid;grid-template-columns:1fr 1fr;gap:8px}.statusitem{padding:10px;border:1px solid var(--line);border-radius:8px;background:rgba(37,99,235,.06)}.statusitem b{display:block;margin-top:2px}.petcard{display:grid;grid-template-columns:72px 1fr;gap:12px;align-items:center;border-top:1px solid var(--line);padding-top:12px;margin-top:12px}.petcard:first-child{border-top:0;margin-top:0;padding-top:0}.actionpanel{grid-column:1/-1;border:1px solid var(--line);border-radius:8px;padding:12px;background:rgba(37,99,235,.07)}.actionhead{display:flex;justify-content:space-between;gap:8px;align-items:center;margin-bottom:9px}.actionhead span{color:var(--muted);font-size:12px}.actionhead b{font-size:20px}.actionpanel .toolbar button{flex:1 1 120px}.bagoverview,.emptybag,.emptypet{border:1px dashed var(--line);border-radius:8px;padding:14px;background:rgba(37,99,235,.05);margin-bottom:10px}.bagoverview b,.emptybag b,.emptypet b{display:block;font-size:18px}.bagoverview .slots,.emptybag .slots,.emptypet .slots{display:grid;grid-template-columns:repeat(6,1fr);gap:5px;margin:10px 0}.bagoverview .slot,.emptybag .slot,.emptypet .slot{height:10px;border-radius:999px;background:var(--line)}.bagoverview .slot.on,.emptypet .slot.on{background:var(--primary)}.bagoverview .slot.active{background:var(--ok)}.bagoverview .slot.cursor{outline:2px solid var(--warn);outline-offset:2px}.avatar{width:72px;height:72px;border-radius:12px;border:1px solid var(--line);background:var(--bg);object-fit:cover}.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:7px}.chip{font-size:12px;border:1px solid var(--line);border-radius:999px;padding:3px 8px;color:var(--muted)}.quick{display:grid;grid-template-columns:repeat(auto-fit,minmax(104px,1fr));gap:8px}.pager{touch-action:pan-y}.swipehint,.seg{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;color:var(--muted);font-size:13px;gap:8px}.seg{justify-content:flex-start;overflow:auto;padding-bottom:2px}.seg button{min-height:34px;padding:7px 10px;font-size:12px;white-space:nowrap}.seg button.active{background:var(--primary);color:white;border-color:var(--primary)}.friendgrid{display:grid;gap:10px}.friendstreak,.friendgoal,.friendcard,.friendempty,.logdetail{border:1px solid var(--line);border-radius:8px;padding:10px;background:rgba(37,99,235,.05)}.friendstreak b,.friendgoal b,.friendempty b{display:block;font-size:18px}.streakbar{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:8px}.streakbar span{border:1px solid var(--line);border-radius:999px;padding:5px 0;text-align:center;color:var(--muted);font-size:12px}.streakbar span.on{background:var(--primary);border-color:var(--primary);color:white}.logitem{width:100%;text-align:left;margin-top:8px}.logitem small{color:var(--muted)}.rounds{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin:8px 0}.roundchip{border:1px solid var(--line);border-radius:8px;padding:7px 6px;text-align:center;font-size:12px;color:var(--muted);background:rgba(37,99,235,.06)}.roundchip b{display:block;margin-top:2px;color:var(--text);font-size:14px}.roundchip.win{border-color:rgba(15,139,76,.5);color:var(--ok)}.roundchip.lose{border-color:rgba(194,65,12,.55);color:var(--danger)}.roundchip.draw{border-color:rgba(180,83,9,.5);color:var(--warn)}.toolbar{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}.toolbar button{padding:9px 10px;min-height:38px;font-size:13px}.local{border-style:dashed}
.soundpanel{border:1px solid var(--line);border-radius:8px;padding:10px;background:rgba(37,99,235,.05);margin-top:10px}.soundgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(84px,1fr));gap:8px;margin-top:8px}.soundgrid button{min-height:36px;padding:8px;font-size:12px}
.trainingplan{border:1px solid var(--line);border-radius:8px;padding:10px;background:rgba(15,139,76,.07);margin-top:10px}.trainingplan b{display:block;font-size:18px}.plansteps{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:8px}.plansteps span{border:1px solid var(--line);border-radius:8px;padding:6px;text-align:center;font-size:12px;color:var(--muted)}
.battleentry,.battlerounds,.matchready,.capturequality,.battleexit,.battlereward,.battlematch,.battlephase,.battleverdict{border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-bottom:10px;background:rgba(37,99,235,.07)}.battleentry span,.battlerounds span,.matchready span,.capturequality span,.battleexit span,.battlephase span,.battleverdict span{display:block;color:var(--muted);font-size:12px}.battleentry b,.battlerounds b,.matchready b,.capturequality b,.battleexit b,.battlephase b,.battleverdict b{display:block;font-size:22px;margin-top:1px}.battleentry small,.battlerounds small,.matchready small,.capturequality small,.battleexit small,.battlephase small,.battleverdict small{display:block;color:var(--muted);margin-top:2px}.capturequality.ready{border-color:rgba(15,139,76,.55)}.capturequality.retry{border-color:rgba(180,83,9,.55)}.capturequality.ready .fill{background:var(--ok)}.capturequality.retry .fill{background:var(--warn)}.battleexit.active{border-color:rgba(37,99,235,.65)}.matchready.ready{border-color:rgba(15,139,76,.55)}.matchready.wait{border-color:rgba(180,83,9,.55)}.battlematch{display:grid;grid-template-columns:1fr 44px 1fr;gap:8px;align-items:center}.fighter{text-align:center;min-width:0}.fighter img{margin:0 auto 5px}.fighter b{display:block;overflow-wrap:anywhere}.fighter small{display:block;color:var(--muted);font-size:12px}.vs{border-radius:999px;background:var(--primary);color:white;text-align:center;font-weight:800;padding:9px 0}.rewardgrid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}.rewardgrid div{border:1px solid var(--line);border-radius:8px;padding:8px;background:rgba(255,255,255,.04)}.rewardgrid span{display:block;color:var(--muted);font-size:12px}.rewardgrid b{display:block;margin-top:2px}.phasedots,.roundtrack{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin:9px 0}.phasedots span,.roundtrack span{border:1px solid var(--line);border-radius:999px;padding:5px 0;text-align:center}.phasedots span.on,.roundtrack span.on{background:var(--primary);border-color:var(--primary);color:white}.battleverdict.win{border-color:rgba(15,139,76,.55)}.battleverdict.lose{border-color:rgba(194,65,12,.6)}.battleverdict.draw{border-color:rgba(180,83,9,.55)}.battleverdict.win .fill{background:var(--ok)}.battleverdict.lose .fill{background:var(--danger)}.battleverdict.draw .fill{background:var(--warn)}
.battlereplay{border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin-top:8px;background:rgba(37,99,235,.05)}.battlereplay.win{border-color:rgba(15,139,76,.45)}.battlereplay.lose{border-color:rgba(194,65,12,.5)}.battlereplay.draw{border-color:rgba(180,83,9,.45)}.replaytop{display:flex;align-items:center;justify-content:space-between;gap:8px}.replaytop b{font-size:18px}.replaytop small{color:var(--muted);font-size:12px;white-space:nowrap}
.bondbar{height:8px;border-radius:99px;background:var(--line);overflow:hidden;margin-top:8px}.bondbar .fill{height:100%;border-radius:99px;background:var(--primary)}.bondbar.bond-empty .fill{background:var(--muted)}.bondbar.bond-friend .fill{background:var(--warn)}.bondbar.bond-close .fill{background:var(--ok)}
nav button{border-radius:0;background:transparent;color:var(--muted);padding:12px 2px;font-size:12px;line-height:1.1;min-width:0;touch-action:manipulation;-webkit-tap-highlight-color:transparent;pointer-events:auto}nav button.active{color:var(--primary)}section{display:none}section.active{display:block}
</style>
</head>
<body>
<header><div class="head"><h1>M5 宠物伴侣</h1><span id="conn" class="pill warn">未连接</span></div><div class="meta"><span id="addr"></span><br><span id="last">等待刷新</span></div><div id="err" class="toast"></div></header>
<main>
<section id="home" class="active grid">
<div class="card"><h2>休闲状态</h2><div id="statusBox"></div></div>
<div class="card"><h2>当前伙伴</h2><div id="currentPet" class="small">暂无数据</div></div>
<div class="card"><h2>下一步</h2><div id="nextActions" class="quick"><button onclick="go('capture')">拍照</button><button class="secondary" onclick="go('bag')">背包</button><button class="secondary" onclick="go('battle')">对战</button></div><div id="recentSummary" class="note">等待设备状态</div></div>
<div class="card local"><h2>今日建议</h2><div id="coachTips" class="small">等待设备状态</div></div>
<div class="actions" id="buttonActions"><button onclick="go('capture')">拍照捕捉</button><button class="secondary" onclick="refreshAll()">刷新</button></div>
</section>
<section id="capture" class="grid">
<div class="card"><h2>捕捉</h2><div class="actions"><button onclick="act('photo')">触发拍照</button><button class="secondary" onclick="act('capture')">收服当前野生伙伴</button></div><p class="note">手机只发送指令，拍照、识别和宠物生成都在 CoreS3 本地完成。</p></div>
<div class="card"><h2>识别结果</h2><div id="captureRecognition"></div></div>
<div class="card"><h2>识别调试</h2><div id="qualityMeter" class="small">等待质量探测</div><div class="row"><span class="label">采样</span><span class="value"><select id="sampleEnabled" onchange="saveSampling()"><option value="0">自动标签</option><option value="1">人工标签</option></select></span></div><div class="row"><span class="label">标签</span><span class="value"><select id="sampleLabel" onchange="saveSampling()"></select></span></div><div class="row"><span class="label">场景</span><span class="value"><select id="sampleScene" onchange="saveSampling()"></select></span></div><div id="sampleSummary" class="small">等待样本摘要</div><div class="toolbar"><button onclick="refreshQuality()">刷新质量</button><button class="secondary" onclick="refreshSamples()">刷新样本</button><button class="secondary" onclick="downloadManifest()">下载 manifest</button></div></div>
<div class="card"><h2>当前三键</h2><div id="captureButtons"></div></div>
</section>
<section id="bag" class="grid">
<div class="card"><h2>背包</h2><div id="bagList" class="small pager">暂无数据</div></div>
<div class="actions" id="bagActions"><button onclick="act('bag')">打开背包</button><button class="secondary" onclick="refreshAll()">刷新</button></div>
</section>
<section id="dex" class="grid">
<div class="card"><h2>图鉴</h2><div id="dexRows" class="small">暂无图鉴数据</div></div>
</section>
<section id="battle" class="grid">
<div class="card"><h2>对战状态</h2><div id="battleRows2"></div></div>
<div class="actions" id="battleActions"><button onclick="act('match')">寻找对手</button><button class="secondary" onclick="refreshAll()">刷新</button></div>
</section>
<section id="friends" class="grid">
<div class="card"><h2>好友关系</h2><div id="friendRows" class="small">暂无好友数据</div></div>
<div class="card local"><h2>互动</h2><div class="actions" id="friendActions"><button onclick="act('friend')">添加最近对手</button><button class="secondary" onclick="act('match')">去对战</button><button class="secondary" onclick="go('more')">看事件</button><button class="secondary" onclick="refreshAll()">刷新</button></div><p class="note">好友数据是本次开机的本机记录，用来体现最近对手、友情阶段和连战奖励。</p></div>
</section>
<section id="more" class="grid">
<div class="card"><h2>事件</h2><div id="logFilters" class="seg"></div><div id="logs" class="small">暂无事件</div></div>
<div class="card"><h2>事件详情</h2><div id="logDetail" class="logdetail small">点击最近事件查看详情</div></div>
<div class="card"><h2>连接</h2><p>在手机 Wi-Fi 设置中连接 CoreS3 显示的 M5PET-xxxxxx 热点，然后用浏览器打开本页。</p><p class="small">安卓 Chrome 或 iPhone Safari 都可以使用。若浏览器提示该 Wi-Fi 无互联网，请选择继续使用此网络。</p><p class="small">默认只访问本地设备；只有在下方 AI 面板手动配置 provider 后，浏览器才会发起外部模型请求。</p></div>
<div class="card local"><h2>AI 大模型</h2><div class="row"><span class="label">Provider</span><span class="value"><select id="aiProvider" onchange="saveAiPrefs()"><option value="local">本地模板</option><option value="openai">GPT / OpenAI</option><option value="deepseek">DeepSeek</option><option value="mimo">Mimo / 兼容接口</option><option value="proxy">自有代理</option></select></span></div><div class="row"><span class="label">模型</span><span class="value"><input id="aiModel" placeholder="如 gpt-4o-mini / deepseek-chat" onchange="saveAiPrefs()"></span></div><div class="row"><span class="label">接口地址</span><span class="value"><input id="aiBaseUrl" placeholder="可选：兼容接口或代理 URL" onchange="saveAiPrefs()"></span></div><div class="row"><span class="label">API Key</span><span class="value"><input id="aiApiKey" type="password" placeholder="仅保存在本浏览器" onchange="saveAiPrefs()"></span></div><p class="note">AI 调用由手机浏览器发起；当前只发送文本 JSON 快照，不上传图片。若直连模型因 CORS 失败，请使用自己的 HTTPS 代理地址。API Key 不写入固件或仓库。</p><div class="actions"><button onclick="aiGenerate('pet')">生成宠物人格</button><button class="secondary" onclick="aiGenerate('social')">生成好友事件</button><button class="secondary" onclick="aiUseLocal()">本地模板</button></div><div id="aiStatus" class="note">未生成</div><div id="aiOutput" class="logdetail small">等待生成</div></div>
<div class="card"><h2>按键设置</h2><label class="toggle"><span>大按钮模式<br><span class="small">适合单手操作和小屏手机</span></span><input id="bigKeys" type="checkbox" onchange="savePrefs()"></label><label class="toggle"><span>放生前确认<br><span class="small">避免误触放生伙伴</span></span><input id="confirmDanger" type="checkbox" onchange="savePrefs()"></label><label class="toggle"><span>设备音效<br><span id="audioState" class="small">等待设备状态</span></span><input id="audioToggle" type="checkbox" onchange="setMute(this.checked)"></label><div id="soundPanel"></div><div class="actions"><button onclick="maybeAction('photo')">拍照</button><button class="secondary" onclick="maybeAction('bag')">背包</button><button class="secondary" onclick="maybeAction('match')">对战</button><button class="secondary" onclick="maybeAction('idle')">休闲</button><button class="dangerbtn" onclick="maybeAction('release')">放生</button></div></div>
</section>
</main>
<nav id="tabs"><button type="button" data-tab="home" class="active">首页</button><button type="button" data-tab="capture">捕捉</button><button type="button" data-tab="bag">背包</button><button type="button" data-tab="dex">图鉴</button><button type="button" data-tab="battle">对战</button><button type="button" data-tab="friends">好友</button><button type="button" data-tab="more">更多</button></nav>
<script>
const $=id=>document.getElementById(id);
const text=v=>(v===undefined||v===null||v==='')?'-':v;
const h=v=>String(text(v)).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
const num=v=>Number.isFinite(Number(v))?Number(v):0;
const pct=v=>v===undefined||v===null?'-':(Number(v)<=1?Math.round(Number(v)*100):Math.round(Number(v)))+'%';
const prefs={bigKeys:localStorage.getItem('bigKeys')==='1',confirmDanger:localStorage.getItem('confirmDanger')!=='0'};
const aiPrefs={provider:localStorage.getItem('aiProvider')||'local',model:localStorage.getItem('aiModel')||'',baseUrl:localStorage.getItem('aiBaseUrl')||'',apiKey:localStorage.getItem('aiApiKey')||''};
let bagPets=[],bagIndex=0,rawLogs=[],lastLogs=[],lastStatus=null,logFilter='all',lastTabTap=0;
 function row(k,v){return `<div class="row"><span class="label">${h(k)}</span><span class="value">${h(v)}</span></div>`}
 function phase(v,label){return label||{finding:'寻找训练师',connected:'羁绊同步',ready:'同步完成',clash:'宠物交锋',result:'战斗结算',exit:'退场整理'}[v]||'-'}
 function stat(k,v,max=99){const n=Math.max(0,Math.min(max,num(v)));const w=Math.round(n*100/Math.max(1,max));return `<div class="stat"><span>${k}</span><div class="meter"><div class="fill" style="width:${w}%"></div></div><b>${Math.round(n)}</b></div>`}
 function bondClass(score){const s=num(score);return s>=100?'bond-close':(s>=60?'bond-friend':(s>0?'bond-new':'bond-empty'))}
 function bondMeter(score){const s=Math.max(0,Math.min(100,num(score)));return `<div class="bondbar ${bondClass(s)}"><div class="fill" style="width:${Math.round(s)}%"></div></div>`}
 function setConn(ok,msg){$('conn').textContent=ok?'已连接':'未连接';$('conn').className='pill '+(ok?'ok':'warn');$('addr').textContent=msg||location.origin;if(ok)showError('')}
 function showError(msg){const e=$('err');e.textContent=msg||'';e.className='toast '+(msg?'show':'')}
 function setBusy(on){document.querySelectorAll('main button').forEach(b=>b.disabled=!!on);if(on)$('last').textContent='刷新中...'}
 function touchTime(){const d=new Date();$('last').textContent='上次刷新 '+d.toLocaleTimeString()}
 function applyPrefs(){document.body.classList.toggle('big-buttons',prefs.bigKeys);if($('bigKeys'))$('bigKeys').checked=prefs.bigKeys;if($('confirmDanger'))$('confirmDanger').checked=prefs.confirmDanger;applyAiPrefs()}
 function savePrefs(){prefs.bigKeys=!!$('bigKeys').checked;prefs.confirmDanger=!!$('confirmDanger').checked;localStorage.setItem('bigKeys',prefs.bigKeys?'1':'0');localStorage.setItem('confirmDanger',prefs.confirmDanger?'1':'0');applyPrefs()}
 function applyAiPrefs(){if($('aiProvider'))$('aiProvider').value=aiPrefs.provider;if($('aiModel'))$('aiModel').value=aiPrefs.model;if($('aiBaseUrl'))$('aiBaseUrl').value=aiPrefs.baseUrl;if($('aiApiKey'))$('aiApiKey').value=aiPrefs.apiKey}
 function saveAiPrefs(){aiPrefs.provider=$('aiProvider')?$('aiProvider').value:'local';aiPrefs.model=$('aiModel')?$('aiModel').value.trim():'';aiPrefs.baseUrl=$('aiBaseUrl')?$('aiBaseUrl').value.trim():'';aiPrefs.apiKey=$('aiApiKey')?$('aiApiKey').value.trim():'';localStorage.setItem('aiProvider',aiPrefs.provider);localStorage.setItem('aiModel',aiPrefs.model);localStorage.setItem('aiBaseUrl',aiPrefs.baseUrl);localStorage.setItem('aiApiKey',aiPrefs.apiKey)}
 function go(id){const btn=[...document.querySelectorAll('nav button')].find(b=>b.dataset.tab===id);tab(id,btn)}
 function tab(id,btn){const target=$(id);if(!target)return;document.querySelectorAll('section').forEach(s=>s.classList.remove('active'));target.classList.add('active');document.querySelectorAll('nav button').forEach(b=>b.classList.remove('active'));const activeBtn=btn||[...document.querySelectorAll('nav button')].find(b=>b.dataset.tab===id);if(activeBtn)activeBtn.classList.add('active');refreshAll()}
 function setupTabs(){const tabs=$('tabs');if(!tabs||tabs.dataset.ready)return;tabs.dataset.ready='1';const onTap=e=>{const b=e.target.closest('button[data-tab]');if(!b)return;e.preventDefault();const now=Date.now();if(e.type==='click'&&now-lastTabTap<350)return;lastTabTap=now;tab(b.dataset.tab,b)};tabs.addEventListener('pointerup',onTap);tabs.addEventListener('click',onTap)}
 async function api(path,opt){const r=await fetch(path,Object.assign({cache:'no-store'},opt||{}));if(!r.ok){let msg='HTTP '+r.status;try{const j=await r.json();if(j.error)msg=j.error}catch(e){}throw new Error(msg)}return await r.json()}
 function elementColor(e){return {火:'#ef4444',水:'#2563eb',木:'#16a34a',金:'#ca8a04',土:'#a16207'}[e]||'#64748b'}
 function avatarData(p){const c=elementColor(p&&p.element);const seed=(num(p&&p.index)*17+num(p&&p.speciesIndex)*31+num(p&&p.visualVariantIndex)*43+num(p&&p.stageIndex)*53+num(p&&p.power))%997;const name=h(text(p&&p.element).slice(0,2));const bodyShape=['circle','rounded','leaf','diamond'][seed%4];const body=bodyShape==='circle'?`<circle cx='48' cy='51' r='27' fill='${c}'/>`:bodyShape==='leaf'?`<path d='M48 20 C72 31 73 62 48 78 C23 62 24 31 48 20Z' fill='${c}'/>`:bodyShape==='diamond'?`<path d='M48 18 L75 49 L48 80 L21 49Z' fill='${c}'/>`:`<rect x='22' y='24' width='52' height='52' rx='19' fill='${c}'/>`;const ears=seed%3===0?`<path d='M31 28 L24 12 L43 24Z' fill='${c}'/><path d='M65 28 L72 12 L53 24Z' fill='${c}'/>`:seed%3===1?`<circle cx='29' cy='26' r='11' fill='${c}'/><circle cx='67' cy='26' r='11' fill='${c}'/>`:`<path d='M35 27 Q31 9 47 24Z' fill='${c}'/><path d='M61 27 Q65 9 49 24Z' fill='${c}'/>`;const mark=seed%5===0?`<path d='M48 31 L54 43 H42Z' fill='white' opacity='.8'/>`:seed%5===1?`<circle cx='48' cy='36' r='5' fill='white' opacity='.75'/>`:seed%5===2?`<path d='M39 34 H57' stroke='white' stroke-width='4' stroke-linecap='round' opacity='.72'/>`:seed%5===3?`<path d='M41 31 Q48 39 55 31' fill='none' stroke='white' stroke-width='4' stroke-linecap='round' opacity='.76'/>`:`<path d='M48 29 L51 36 L59 37 L53 42 L55 50 L48 46 L41 50 L43 42 L37 37 L45 36Z' fill='white' opacity='.68'/>`;const tail=seed%2?`<path d='M73 56 Q88 53 82 39' fill='none' stroke='${c}' stroke-width='9' stroke-linecap='round'/>`:`<path d='M21 58 Q8 64 18 75' fill='none' stroke='${c}' stroke-width='9' stroke-linecap='round'/>`;const mouth=seed%2?`M36 61 Q48 69 60 61`:`M37 64 Q48 58 59 64`;const svg=`<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 96 96'><rect width='96' height='96' rx='16' fill='${c}' opacity='.14'/>${tail}${ears}${body}${mark}<circle cx='38' cy='47' r='5' fill='white'/><circle cx='58' cy='47' r='5' fill='white'/><circle cx='39' cy='48' r='2' fill='#111827'/><circle cx='59' cy='48' r='2' fill='#111827'/><path d='${mouth}' fill='none' stroke='white' stroke-width='5' stroke-linecap='round'/><text x='48' y='89' text-anchor='middle' font-size='15' font-family='Arial' fill='${c}'>${name}</text></svg>`;return 'data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg)}
 function favKey(p){return 'fav-'+text(p&&p.index)}
 function isFav(p){return localStorage.getItem(favKey(p))==='1'}
 function toggleFav(i){const key='fav-'+i;localStorage.setItem(key,localStorage.getItem(key)==='1'?'0':'1');refreshBag()}
 function renderNoCurrentPet(){const count=num(lastStatus&&(lastStatus.bagCount??lastStatus.backpackCount));const cap=num(lastStatus&&lastStatus.bagCapacity)||6;const hasStored=count>0;const slots=Array.from({length:cap},(_,i)=>`<span class="slot ${i<count?'on':''}"></span>`).join('');return `<div class="emptypet"><b>暂无当前伙伴</b><div class="slots">${slots}</div><div class="note">${hasStored?'背包里已有伙伴，先去背包选择一只作为当前伙伴。':'先拍照捕捉第一只伙伴。'}</div><div class="toolbar"><button onclick="maybeAction('${hasStored?'bag':'photo'}')">${hasStored?'去背包选择':'拍照捕捉'}</button><button class="secondary" onclick="maybeAction('bag')">背包</button><button class="secondary" onclick="maybeAction('photo')">拍照</button></div></div>`}
 function renderPet(p){if(!p)return renderNoCurrentPet();const xpMax=Math.max(1,num(p.nextXp));const xp=Math.max(0,Math.min(xpMax,num(p.xp)));const win=Math.max(0,Math.min(100,num(p.winRate)));const waitMax=Math.max(1,num(p.growthIntervalSec)||30);const wait=Math.max(0,Math.min(waitMax,num(p.growthProgress)));const waitSec=Math.max(0,num(p.growthWaitSec));const care=p.care||{};return `<div class="petcard"><img class="avatar" alt="宠物头像" src="${avatarData(p)}"><div><b>${h(p.species)}</b> ${p.active?'<span class="pill ok">当前</span>':''} ${isFav(p)?'<span class="pill warn">收藏</span>':''}<div class="small">#${h(Number(p.index||0)+1)} · ${h(p.element)} · ${h(p.stage)} · Lv.${h(p.level)}</div><div class="chips"><span class="chip">力 ${h(p.power)}</span><span class="chip">速 ${h(p.agility)}</span><span class="chip">心 ${h(p.spirit)}</span><span class="chip">${h(care.hint||'Stable')}</span></div>${stat('XP',xp,xpMax)}${stat('胜率',win,100)}${stat('饱食',num(care.fullness),100)}${stat('精力',num(care.energy),100)}${stat('亲和',num(care.affection),100)}${stat('等待成长',wait,waitMax)}<div class="goal">${h(p.growthGoal)}</div><div class="note">等待 ${h(waitSec)}s 自然成长 · 心情 ${h(care.mood||'-')}</div><div class="toolbar"><button class="secondary" onclick="toggleFav(${Number(p.index||0)})">${isFav(p)?'取消收藏':'收藏'}</button><button class="secondary" onclick="maybeAction('select')">设为伙伴</button><button class="dangerbtn" onclick="maybeAction('release')">放生</button></div></div></div>`}
 function renderTrainingPlanCard(s){s=s||{};const p=s.currentPet||{};if(!p.species)return '';const xpMax=Math.max(1,num(p.nextXp));const xp=Math.max(0,Math.min(xpMax,num(p.xp)));const waitSec=Math.max(0,num(p.growthWaitSec));return `<div class="trainingplan"><b>训练计划</b><div class="chips"><span class="chip">${h(p.growthGoal||'继续成长')}</span><span class="chip">等待 ${h(waitSec)}s</span><span class="chip">XP ${h(xp)}/${h(xpMax)}</span></div>${stat('XP',xp,xpMax)}<div class="plansteps"><span>捕捉得 XP</span><span>对战得 XP</span><span>等待成长</span></div><div class="toolbar"><button onclick="maybeAction('match')">去对战</button><button class="secondary" onclick="maybeAction('photo')">拍照捕捉</button><button class="secondary" onclick="go('bag')">整理背包</button></div></div>`}
 function renderEmptyBag(){const slots=Array.from({length:6},()=>'<span class="slot"></span>').join('');return `<div class="emptybag"><b>背包为空 0/6</b><div class="slots">${slots}</div><div class="note">先拍照捕捉第一只伙伴；捕捉成功后会自动设为当前伙伴。</div><div class="toolbar"><button onclick="maybeAction('photo')">拍照捕捉</button><button class="secondary" onclick="go('capture')">打开捕捉页</button></div></div>`}
 function renderBagOverview(b){b=b||{};const pets=b.pets||bagPets||[];const cap=Math.max(1,num(b.capacity)||6);const count=num(b.count??pets.length);const selected=num(b.selectedIndex);const cursor=num(b.cursorIndex);const slots=Array.from({length:cap},(_,i)=>`<span class="slot ${i<count?'on':''} ${i===selected?'active':''} ${i===cursor?'cursor':''}"></span>`).join('');return `<div class="bagoverview"><b>背包 ${h(count)}/${h(cap)}</b><div class="slots">${slots}</div><div class="chips"><span class="chip">出战 #${h(selected+1)}</span><span class="chip">光标 #${h(cursor+1)}</span><span class="chip">空位 ${h(Math.max(0,cap-count))}</span></div><div class="note">绿色为出战伙伴，描边为设备当前光标。</div></div>`}
 function renderBagPager(b){if(!bagPets.length)return renderBagOverview(b)+renderEmptyBag();bagIndex=Math.max(0,Math.min(bagIndex,bagPets.length-1));const p=bagPets[bagIndex];return renderBagOverview(b)+`<div class="swipehint"><span>左右滑动切换</span><b>${bagIndex+1}/${bagPets.length}</b></div>${renderPet(p)}<div class="note">左滑下一只，右滑上一只；下方按钮会同步设备背包光标。</div>`}
 function renderDex(d){d=d||{};const entries=d.entries||[];const owned=num(d.ownedTemplates);const total=num(d.total)||entries.length||45;const cards=entries.map(x=>`<div class="logdetail small"><b>${h(x.owned?x.name:'未遇见')}</b><div class="chips"><span class="chip">${h(x.element)}</span><span class="chip">${h(x.baseSpecies)}</span><span class="chip">V${h(Number(x.visualVariantIndex||0)+1)}</span><span class="chip">${x.owned?'已拥有 '+h(x.count):'未拥有'}</span></div>${x.owned?row('最高等级',x.maxLevel):''}</div>`).join('');return `<div class="bagoverview"><b>图鉴 ${h(owned)}/${h(total)}</b><div class="note">图鉴由当前背包实时派生，不写入存档。</div></div>${cards}`}
 async function refreshDex(){if(!$('dexRows'))return;try{const d=await api('/api/v1/encyclopedia');$('dexRows').innerHTML=renderDex(d)}catch(e){$('dexRows').innerHTML=`<div class="danger">图鉴刷新失败：${h(e.message)}</div>`}}
 async function bagStep(dir){if(!bagPets.length)return;setBusy(true);try{if(!lastStatus||lastStatus.screen!=='bag')await api('/api/v1/action?type=bag',{method:'POST'});await api('/api/v1/action?type='+(dir<0?'prev':'next'),{method:'POST'});await refreshAll()}catch(e){setConn(false,'背包切换失败：'+e.message);showError('背包切换失败：'+e.message)}finally{setBusy(false)}}
 function setupBagSwipe(){const el=$('bagList');if(!el||el.dataset.swipe)return;el.dataset.swipe='1';let sx=0;el.addEventListener('touchstart',e=>{sx=e.changedTouches[0].clientX},{passive:true});el.addEventListener('touchend',e=>{const dx=e.changedTouches[0].clientX-sx;if(Math.abs(dx)>48)bagStep(dx>0?-1:1)},{passive:true})}
 function modeClass(s){return 'mode-'+String((s&&s.screen)||'idle').replace(/_/g,'-')}
 function renderModeBanner(s){return `<div class="modebanner ${modeClass(s)}"><span>当前流程</span><b>${h((s&&s.screenLabel)||'休闲状态')}</b></div>`}
 function renderStatusBox(s,btn){return `${renderModeBanner(s)}<div class="statusbox"><div class="statusitem">状态<b>${h(s.screenLabel||s.screen)}</b></div><div class="statusitem">背包<b>${h((s.bagCount??s.backpackCount??0)+'/'+(s.bagCapacity??6))}</b></div><div class="statusitem">静音<b>${s.muted?'已静音':'未静音'}</b></div><div class="statusitem">三键<b>${h(text(btn.left)+' / '+text(btn.middle)+' / '+text(btn.right))}</b></div><div class="statusitem">设备<b>${h(s.ssid)}</b></div><div class="statusitem">SD 卡<b>${s.sdCardPresent?'已检测到':'未使用'}</b></div></div>${renderGrowth(s.growth)}`}
 function renderTips(s){const tips=[];const count=s.bagCount??s.backpackCount??0;if(!s.hasCurrentPet)tips.push('先进入捕捉页拍照，获得第一只伙伴。');if(count>0)tips.push('进入背包页按顺序查看伙伴，收藏常用伙伴。');if((s.battle&&s.battle.friendshipScore||0)<60)tips.push('对战后可以添加好友，累积友情奖励。');tips.push('捕捉、背包、对战已经拆成独立页面，底部按钮切换。');return tips.map(x=>`<div>· ${h(x)}</div>`).join('')}
 function renderActionPanel(s){s=s||{};const hasPet=!!s.hasCurrentPet;const rec=s.recognition||{};const b=s.battle||{};const recent=peerShort(b.recentPeerId);let primary=hasPet?'battle':'capture';let textMain=hasPet?'去对战':'拍照捕捉';let note=hasPet?'已有当前伙伴，可以继续对战或整理背包。':'先拍照获得第一只伙伴。';if(rec.recognized){primary='capture';textMain='收服';note=`最近识别到 ${text(rec.objectLabel)}，可尝试收服。`}else if(b.resultValid){primary='friends';textMain='添加好友';note=`最近对战 ${text(b.outcomeLabel||b.outcome)}，可记录对手。`}const rematch=b.rematchAvailable?`再战 +${text(b.nextRematchXp)} XP`:'完成对战后开启再战';const friend=`${text(b.friendshipLabel||'友情')} ${text(b.friendshipScore||0)}/100`;const result=b.resultValid?`最近 ${text(b.outcomeLabel||b.outcome||b.result)}`:'等待首战';return `<div class="actionpanel"><div class="actionhead"><span>下一步</span><b>${h((s&&s.screenLabel)||'休闲状态')}</b></div><div class="toolbar"><button onclick="go('${primary}')">${h(textMain)}<small>${h(note)}</small></button><button class="secondary" onclick="go('bag')">整理背包<small>${h((s.bagCount??0)+'/'+(s.bagCapacity??6))}</small></button><button class="secondary" onclick="go('friends')">好友<small>${h(friend)}</small></button></div><div class="chips"><span class="chip">最近 ${h(recent)}</span><span class="chip">${h(result)}</span><span class="chip">${h(rematch)}</span></div></div>`}
 function renderNextActions(s){return renderActionPanel(s)}
 function renderRecentSummary(s){s=s||{};const rec=s.recognition||{};const b=s.battle||{};const bits=[];bits.push(`连接：${text(s.ssid)}`);bits.push(`识别：${text(rec.objectLabel)} ${rec.confidence?rec.confidence+'%':''}`);bits.push(`对战：${text(b.phaseLabel||b.phase)}`);bits.push(`音效：${s.muted?'静音':'开启'}`);return bits.map(x=>`<div>· ${h(x)}</div>`).join('')}
 function renderSoundPanel(s){const muted=s&&s.muted;const cues=[['idle','休闲'],['photo','拍照'],['wild','遭遇'],['bag','背包'],['capture','收服'],['release','放生'],['select','选择'],['match','匹配'],['clash','交锋'],['friend','友情'],['win','胜利'],['draw','平局'],['lose','失败'],['warning','警告'],['intro','开场'],['level','成长'],['exit','退场'],['cancel','取消']];return `<div class="soundpanel"><b>场景音效</b><div class="note">${muted?'当前静音，测试会走静音门控。':'通过设备播放已有场景音效。'}</div><div class="soundgrid">${cues.map(([k,v])=>`<button class="secondary" onclick="testSound('${k}')">${h(v)}</button>`).join('')}</div></div>`}
 function renderGrowth(g){if(!g||!g.recent)return '';const kind=g.stageUp?'进化':(g.levelUp?'升级':'成长');return row('成长提醒',`#${Number(g.petIndex||0)+1} ${kind} XP+${text(g.xpGained)} Lv.${text(g.level)} ${text(g.stage)}`)}
 function peerShort(id){const n=Number(id||0);return n?('#'+(n&0xffffff).toString(16).toUpperCase().padStart(6,'0')):'-'}
 function friendGoal(score){const s=num(score);return s>=100?'密友':(s>=60?`距密友${100-s}`:`距好友${60-s}`)}
 function renderFriendList(list){return (list||[]).slice(0,4).map(f=>`${f.recent?'最近 ':''}${peerShort(f.peerId)} ${h(f.label)} ${h(f.score)}/100 ${friendGoal(f.score)} ${h(f.battleCount)}战`).join('<br>')}
 function diffVerdict(v){const n=num(v);return (n>0?'胜':(n<0?'负':'平'))+(n>=0?'+':'')+Math.round(n)}
 function renderFriendGoalCard(b){b=b||{};const recent=peerShort(b.recentPeerId);const nextXp=b.rematchAvailable?`再战 +${text(b.nextRematchXp)} XP`:'完成对战后开启再战奖励';const gain=b.nextFriendshipGain?`友情 +${text(b.nextFriendshipGain)}`:'等待下一次对战';return `<div class="friendgoal"><b>${h(text(b.friendshipLabel||'友情'))} ${h(text(b.friendshipScore))}/100</b><div class="chips"><span class="chip">目标 ${h(b.friendshipGoal||friendGoal(b.friendshipScore))}</span><span class="chip">最近 ${h(recent)}</span><span class="chip">${h(nextXp)}</span><span class="chip">${h(gain)}</span></div>${bondMeter(b.friendshipScore)}<div class="note">${h(b.friendNotice||b.friendshipPrompt||'继续对战可以累积友情和连战奖励。')}</div><div class="toolbar"><button onclick="maybeAction('match')">去对战</button><button class="secondary" onclick="maybeAction('friend')">添加好友</button></div></div>`}
 function renderFriendStreakCard(b){b=b||{};const streak=num(b.rematchStreak);const nextFriend=b.nextFriendshipGain?`友情 +${text(b.nextFriendshipGain)}`:'等待下一次对战';const nextXp=b.rematchAvailable?`再战 +${text(b.nextRematchXp)} XP`:'再遇同一对手后开启';const goal=b.friendshipGoal||friendGoal(b.friendshipScore);const slots=[1,2,3].map(i=>`<span class="${streak>=i?'on':''}">${i}战</span>`).join('');return `<div class="friendstreak"><b>连战 ${h(streak)}/3</b><div class="streakbar">${slots}</div><div class="chips"><span class="chip">${h(nextXp)}</span><span class="chip">${h(nextFriend)}</span><span class="chip">密友目标 ${h(goal)}</span></div><div class="note">再次匹配最近对手可推进连战奖励和友情阶段。</div></div>`}
 function renderFriendEmpty(b){b=b||{};const recent=peerShort(b.recentPeerId);const canAdd=recent!=='-';return `<div class="friendempty"><b>${canAdd?'记录最近对手':'还没有本地好友'}</b><div class="chips"><span class="chip">最近 ${h(recent)}</span><span class="chip">目标 ${h(b.friendshipGoal||friendGoal(b.friendshipScore))}</span><span class="chip">${h(b.friendBattleCount||0)} 战</span></div>${bondMeter(b.friendshipScore)}<div class="note">${canAdd?'添加最近对手后，可以在这里查看友情值、连战次数和密友目标。':'先完成一次本地对战，再回来添加最近对手。'}</div><div class="toolbar"><button onclick="maybeAction('friend')">添加最近对手</button><button class="secondary" onclick="maybeAction('match')">去对战</button></div></div>`}
 function renderFriendPage(b){b=b||{};const list=b.localFriends||[];let html=renderFriendGoalCard(b)+renderFriendStreakCard(b)+row('友情阶段',`${text(b.friendshipLabel)} ${text(b.friendshipScore)}/100`)+bondMeter(b.friendshipScore)+row('当前目标',b.friendshipGoal||friendGoal(b.friendshipScore))+row('最近对手',peerShort(b.recentPeerId))+row('好友对战',b.friendBattleCount||0);if(!list.length)return html+renderFriendEmpty(b);html+='<div class="friendgrid">';list.forEach(f=>{const recent=f.recent?'<span class="pill ok">最近</span>':'';html+=`<div class="friendcard"><b>${peerShort(f.peerId)}</b> ${recent}<div class="chips"><span class="chip">${h(f.label)}</span><span class="chip">友情 ${h(f.score)}/100</span><span class="chip">${h(f.battleCount)} 战</span></div>${bondMeter(f.score)}<div class="goal">${h(friendGoal(f.score))}</div><div class="note">建议：再次匹配这位对手，累积连战和友情奖励。</div></div>`});return html+'</div>'}
 function verdictClass(v){const n=num(v);return n>0?'win':(n<0?'lose':'draw')}
 function roundChip(k,v){return `<div class="roundchip ${verdictClass(v)}"><span>${h(k)}</span><b>${h(diffVerdict(v))}</b></div>`}
 function renderRoundChips(b){return b&&b.resultValid?`<div class="rounds">${roundChip('力速',b.powerDiff)}${roundChip('五行',b.elementSwing)}${roundChip('气势',b.spiritDiff)}</div>`:row('三回合','-')}
 function renderBattleRoundTrackCard(b){b=b||{};const phase=Math.max(0,Math.min(2,num(b.clashRound)-1));const labels=['力速','五行','气势'];const track=labels.map((x,i)=>`<span class="${i<=phase?'on':''}">${x}</span>`).join('');const current=b.clashRound?`${text(b.clashRoundLabel)} 差 ${text(b.clashRoundDiff)}`:'等待开战';return `<div class="battlerounds"><span>三回合轨道</span><b>${h(current)}</b><div class="roundtrack">${track}</div><div class="rounds">${roundChip('力速',b.powerDiff)}${roundChip('五行',b.elementSwing)}${roundChip('气势',b.spiritDiff)}</div></div>`}
 function renderMatchReadyCard(b){b=b||{};const me=(lastStatus&&lastStatus.currentPet)||{};const ready=!!me.species;const recent=peerShort(b.recentPeerId);const rematch=b.rematchAvailable?`再战 +${text(b.nextRematchXp)} XP`:'完成对战后开启再战';const result=b.resultValid?`最近 ${text(b.outcomeLabel||b.outcome||b.result)}`:'等待首战';const goal=b.friendshipGoal||friendGoal(b.friendshipScore);return `<div class="matchready ${ready?'ready':'wait'}"><span>匹配准备</span><b>${h(ready?me.species:'先选择伙伴')}</b><small>${h(ready?`${text(me.element)} Lv.${text(me.level)} · ${text(me.stage)}`:'进入背包选择一只出战伙伴')}</small><div class="chips"><span class="chip">最近 ${h(recent)}</span><span class="chip">${h(result)}</span><span class="chip">${h(rematch)}</span><span class="chip">目标 ${h(goal)}</span></div><div class="toolbar"><button onclick="maybeAction('match')">寻找对手</button><button class="secondary" onclick="maybeAction('bag')">整理背包</button><button class="secondary" onclick="maybeAction('friend')">添加好友</button></div></div>`}
 function renderBattleEntryCard(b){b=b||{};const me=(lastStatus&&lastStatus.currentPet)||{};const rival=b.opponentSpecies||((b.peerSeen||b.connected)?'对手同步中':'等待对手');const ready=!!me.species;const status=b.clashRound?'已经出场':((b.peerSeen||b.connected)?'对手就位':'靠近另一台训练机');const mine=ready?`${text(me.element)} Lv.${text(me.level)} · ${text(me.stage)}`:'进入背包选择出战伙伴';return `<div class="battleentry"><span>宠物出场</span><b>${h(ready?me.species:'先选择伙伴')} VS ${h(rival)}</b><small>${h(status)} · ${h(mine)}</small></div>`}
 function renderBattleMatchup(b){b=b||{};const me=(lastStatus&&lastStatus.currentPet)||{};const rival={element:b.opponentElement||'',index:num(b.recentPeerId),speciesIndex:num(b.opponentLevel),visualVariantIndex:num(b.peerScore),stageIndex:num(b.opponentLevel),power:num(b.peerScore)};const meName=me.species||'暂无伙伴';const rivalName=b.opponentSpecies||((b.peerSeen||b.connected)?'同步中':'等待对手');return `<div class="battlematch"><div class="fighter"><img class="avatar" alt="本机伙伴" src="${avatarData(me)}"><b>${h(meName)}</b><small>${h(text(me.element))} Lv.${h(text(me.level))}</small></div><div class="vs">VS</div><div class="fighter"><img class="avatar" alt="对手伙伴" src="${avatarData(rival)}"><b>${h(rivalName)}</b><small>${h(text(b.opponentElement))} Lv.${h(text(b.opponentLevel))}</small></div></div>`}
 function renderBattlePhaseCard(b){b=b||{};const step=Math.max(0,Math.min(2,num(b.pairingStep)));const labels=['寻','连','战'];const dots=labels.map((x,i)=>`<span class="${i<=step?'on':''}">${x}</span>`).join('');const wait=b.waitingSec?`等待 ${text(b.waitingSec)}秒`:text(b.linkText||'等待对手');const round=b.clashRound?`当前 ${text(b.clashRoundLabel)} / 差 ${text(b.clashRoundDiff)}`:text(b.pairingStepLabel||wait);return `<div class="battlephase ${String(b.phase||'finding')}"><span>对战阶段</span><b>${h(phase(b.phase,b.phaseLabel))}</b><div class="phasedots">${dots}</div><small>${h(round)} · ${h(wait)}</small></div>`}
 function renderBattleVerdict(b){if(!b||!b.resultValid)return '';const diff=num(b.scoreDiff);const width=Math.min(100,Math.round(Math.abs(diff)*100/160));const bid=b.battleId?('#'+Number(b.battleId).toString(16).toUpperCase().padStart(8,'0')):'-';return `<div class="battleverdict ${verdictClass(b.scoreDiff)}"><span>结算判定</span><b>${h(b.outcomeLabel||b.outcome||b.result)}</b><small>分差 ${diff>=0?'+':''}${Math.round(diff)} · ${h(bid)}</small><div class="meter"><div class="fill" style="width:${width}%"></div></div></div>`}
 function renderBattleReward(b){b=b||{};if(!b.resultValid)return '';const growth=b.stageUp?'进化':(b.levelUp?'升级':'成长');const rematch=b.rematchAvailable?`再战 +${text(b.nextRematchXp)} XP`:'再战奖励 -';return `<div class="battlereward"><div class="rewardgrid"><div><span>XP</span><b>+${h(b.xpGained||b.xpReward||0)}</b></div><div><span>成长</span><b>${h(growth)}</b></div><div><span>再战</span><b>${h(rematch)}</b></div><div><span>友情</span><b>+${h(b.nextFriendshipGain||0)}</b></div></div>${bondMeter(b.friendshipScore)}</div>`}
 function renderBattleExitCard(b){b=b||{};if(!b.resultValid)return '';const exit=b.phase==='exit';const next=b.rematchAvailable?`再次对战可获得 +${text(b.nextRematchXp)} XP`:'可以整理背包、回休闲或重新匹配。';const recent=peerShort(b.recentPeerId);const friend=recent!=='-'?`最近对手 ${recent} · ${text(b.friendshipLabel)} ${text(b.friendshipScore)}/100`:'添加最近对手后记录友情。';return `<div class="battleexit ${exit?'active':''}"><span>下一步</span><b>${h(exit?'退场整理':'结算后行动')}</b><small>${h(next)}</small><div class="toolbar"><button onclick="maybeAction('match')">再次对战</button><button class="secondary" onclick="maybeAction('idle')">回休闲</button><button class="secondary" onclick="maybeAction('bag')">整理背包</button><button class="secondary" onclick="maybeAction('friend')">添加好友</button></div><div class="note">${h(friend)}</div></div>`}
 function renderBattleReplays(items){items=items||[];if(!items.length)return '<div class="note">暂无对战回放</div>';return `<div class="note">最近对局复盘</div>`+items.map(x=>{const bid=x.battleId?('#'+Number(x.battleId).toString(16).toUpperCase().padStart(8,'0')):'-';const growth=x.stageUp?'进化':(x.levelUp?'升级':'-');const result=x.outcomeLabel||x.outcome||'-';const score=`${text(x.myScore)} : ${text(x.peerScore)} / ${text(x.scoreDiff)}`;return `<div class="battlereplay ${verdictClass(x.scoreDiff)}"><div class="replaytop"><b>${h(result)}</b><small>${h(bid)}</small></div><div class="chips"><span class="chip">${h(text(x.opponentSpecies))} ${h(text(x.opponentElement))} Lv.${h(text(x.opponentLevel))}</span><span class="chip">XP +${h(text(x.xpGained))}</span><span class="chip">友情 +${h(text(x.friendBonus))}</span><span class="chip">${h(growth)}</span></div>${row('技能',`${text(x.mySkill)} / ${text(x.opponentSkill)}`)}${row('分数',score)}${row('回合',`力 ${text(x.powerDiff)} · 克 ${text(x.elementSwing)} · 心 ${text(x.spiritDiff)}`)}</div>`}).join('')}
 function renderBattle(b){b=b||{};const peer=b.peerSeen||b.connected?'有对手':'等待中';const growth=b.stageUp?'进化':(b.levelUp?'升级':'-');const judge=b.resultValid?`${text(b.advantageLabel)} / 分差 ${text(b.scoreDiff)}`:'等待结算';const rounds=renderRoundChips(b);const wait=b.waitingSec?`${b.waitingSec}秒`:'';const clash=b.clashRound?row('当前回合',`${text(b.clashRoundLabel)} / 差 ${text(b.clashRoundDiff)}`):'';const opponent=b.opponentSpecies?`${text(b.opponentSpecies)} ${text(b.opponentElement)} Lv.${text(b.opponentLevel)}`:'-';const bond=bondMeter(b.friendshipScore);let html=renderMatchReadyCard(b)+renderBattlePhaseCard(b)+renderBattleEntryCard(b)+renderBattleMatchup(b)+renderBattleRoundTrackCard(b)+renderBattleVerdict(b)+renderBattleReward(b)+renderBattleExitCard(b)+row('阶段',phase(b.phase,b.phaseLabel))+clash+row('状态',b.linkText||peer)+row('配对进度',`${text(b.pairingStepLabel)} ${wait}`)+row('对手伙伴',opponent)+row('对局编号',b.battleId?('#'+Number(b.battleId).toString(16).toUpperCase().padStart(8,'0')):'-')+row('结果',b.outcomeLabel||b.outcome||b.result)+row('分数',`${text(b.myScore)} : ${text(b.peerScore)}`)+row('判定',judge)+row('技能',`${text(b.mySkill)} / ${text(b.opponentSkill)}`)+rounds+row('XP',b.xpGained||b.xpReward||0)+row('再战奖励',b.rematchAvailable?`+${text(b.nextRematchXp)} XP`:'-')+row('下次友情',b.nextFriendshipGain?`+${text(b.nextFriendshipGain)}`:'-')+row('成长',growth)+row('友情',`${text(b.friendshipLabel)} ${text(b.friendshipScore)}/100`)+bond+row('友情目标',b.friendshipGoal||'-')+row('友情提醒',b.friendNotice||b.friendshipPrompt||'-')+row('好友数量',b.friendCount||0)+row('最近对手',peerShort(b.recentPeerId))+row('好友对战',b.friendBattleCount||0);const friends=renderFriendList(b.localFriends);if(friends)html+=`<div class="note">${friends}</div>`;else if(b.friendshipPrompt)html+=`<div class="note">${b.friendshipPrompt}</div>`;html+=renderBattleReplays(b.replays);return html}
 function failureText(reason){reason=text(reason);const map={'Background-like scene':'背景过多，主体不清','Model distance high':'目标特征不稳定','Model class ambiguous':'目标类别不清晰','Low model confidence':'识别信心不足','Weak class evidence':'主体证据不足','Low class confidence':'识别信心不足','Preprocess failed':'画面处理失败','Camera capture failed':'相机拍照失败'};return map[reason]||reason}
 function captureHint(r){r=r||{};const reason=failureText(r.failureReason);if(r.recognized)return `<div class="goal">已发现 ${h(r.objectLabel)}，可点击“收服”。</div>`;if(reason.includes('背包已满'))return `<div class="note danger">背包已满：先到背包放生一只，再拍照。</div>`;return `<div class="note">下一步：主体放进画面中央，补光并简化背景，然后重试拍照。</div>`}
 function renderCaptureQualityCard(r){r=r||{};const subject=Math.max(0,Math.min(100,num(r.presenceScore)));const recog=Math.max(0,Math.min(100,num(r.confidence)));const ok=!!r.recognized;const title=ok?'可以收服':'调整画面';const detail=ok?`${text(r.objectLabel)} · ${text(r.elementHint)}`:failureText(r.failureReason);return `<div class="capturequality ${ok?'ready':'retry'}"><span>捕捉质量</span><b>${h(title)}</b><small>${h(detail)}</small>${stat('主体',subject,100)}${stat('识别',recog,100)}<div class="toolbar"><button onclick="maybeAction(ok?'capture':'photo')">${ok?'收服':'重拍'}</button><button class="secondary" onclick="maybeAction('bag')">背包</button></div></div>`}
 function renderBurstCandidates(items){items=items||[];if(!items.length)return '<div class="note">还没有 burst 明细；拍照后显示 3 帧候选。</div>';return `<div class="note">Burst 候选帧</div>`+items.map(x=>{const cls=x.best?'goal':'logdetail small';const title=`#${Number(x.burst||0)+1} ${x.best?'BEST ':''}${x.recognized?'OK':'RETRY'} ${text(x.objectLabel)}`;return `<div class="${cls}"><b>${h(title)}</b>${row('质量',`${text(x.quality)} / P${text(x.presence)} / C${text(x.confidence)}`)}${row('距离',`${text(x.bestDistance)} / margin ${text(x.margin)} / ${text(x.source)}`)}${row('画面',`B${text(x.brightness)} S${text(x.saturation)} K${text(x.contrast)} CD${text(x.centerDelta)}`)}${row('原因',failureText(x.failureReason))}</div>`}).join('')}
 function renderRecognition(r){r=r||{};return renderCaptureQualityCard(r)+captureHint(r)+row('目标',r.objectLabel)+row('材质',r.materialLabel)+row('元素',r.elementHint)+stat('识别',num(r.confidence),100)+row('失败原因',failureText(r.failureReason))+renderBurstCandidates(r.burstCandidates)}
 function fillSelect(id,items,value){const el=$(id);if(!el)return;const old=el.value;el.innerHTML=(items||[]).map(x=>`<option value="${h(x)}">${h(x)}</option>`).join('');el.value=value||old||((items||[])[0]||'')}
 function applySamplingState(s){s=s||{};fillSelect('sampleLabel',s.labelOptions||[],s.label);fillSelect('sampleScene',s.sceneOptions||[],s.scene);if($('sampleEnabled'))$('sampleEnabled').value=s.enabled?'1':'0'}
 function renderQuality(q){q=q||{};if(!q.frame)return '<div class="note warn">未取得相机帧，稍后重试。</div>';const t=q.traits||{};const p=q.presence||{};return `<div class="capturequality ${q.quality>=60?'ready':'retry'}"><span>实时质量</span><b>${h(q.hint)}</b><small>${h(q.scene)} · Q${h(q.quality)} · P${h(p.score)}</small>${stat('主体',p.score,100)}${stat('质量',q.quality,100)}${row('亮度',t.brightness)}${row('饱和',t.saturation)}${row('对比',t.contrast)}${row('中心差',t.centerDelta)}${row('接近',q.proximityAvailable?q.proximity:'无')}</div>`}
 async function refreshQuality(){if(!$('qualityMeter'))return;try{const q=await api('/api/v1/capture/quality');$('qualityMeter').innerHTML=renderQuality(q)}catch(e){$('qualityMeter').innerHTML=`<div class="danger">质量探测失败：${h(e.message)}</div>`}}
 async function saveSampling(){const en=$('sampleEnabled')?$('sampleEnabled').value:'0';const label=$('sampleLabel')?$('sampleLabel').value:'negative';const scene=$('sampleScene')?$('sampleScene').value:'unknown';try{const s=await api('/api/v1/sampling?enabled='+encodeURIComponent(en)+'&label='+encodeURIComponent(label)+'&scene='+encodeURIComponent(scene),{method:'POST'});applySamplingState(s.sampling);await refreshSamples()}catch(e){showError('采样设置失败：'+e.message)}}
 function renderSampleCounts(title,items){items=(items||[]).filter(x=>x&&num(x.count)>0);if(!items.length)return `<div class="note">${h(title)} 暂无计数</div>`;return `<div class="chips"><span class="chip">${h(title)}</span>`+items.map(x=>`<span class="chip">${h(x.name)} ${h(x.count)}</span>`).join('')+`</div>`}
 function sampleFileHref(path){return '/api/v1/samples/file?path='+encodeURIComponent(path)}
 function renderSamples(s){s=s||{};if(!s.sdCardPresent)return '<div class="note warn">未检测到 SD 卡；当前仍可通过串口 sample 日志调试识别。</div>';if(!s.manifestExists)return '<div class="note warn">SD 卡已接入，但 /samples/manifest.csv 还不存在。拍照后会写入样本摘要。</div>';const recent=(s.recent||[]).map(x=>{const thumb=x&&x.thumb;const link=thumb?` <a href="${sampleFileHref(thumb)}">thumb</a>`:'';return `<div class="logdetail small">${row('标签',x.label)}${row('来源',x.labelSource||'auto')}${row('自动标签',x.autoLabel||'-')}${row('场景',x.scene)}${row('距离',x.distanceHint||'-')}${row('质量',x.quality)}${row('识别',x.recognized?'yes':'no')}${link}</div>`}).join('');return row('manifest',s.manifestPath||'/samples/manifest.csv')+row('样本行',s.rowCount)+row('截断',s.truncated?'yes':'no')+row('未知标签',s.unknownLabelCount)+renderSampleCounts('类别',s.classes)+renderSampleCounts('场景',s.scenes)+(recent?`<div class="note">最近样本</div>${recent}`:'<div class="note">暂无最近样本行</div>')}
 async function refreshSamples(){if(!$('sampleSummary'))return;try{const s=await api('/api/v1/samples');$('sampleSummary').innerHTML=renderSamples(s)}catch(e){$('sampleSummary').innerHTML=`<div class="danger">样本摘要失败：${h(e.message)}</div>`}}
 function downloadManifest(){location.href='/api/v1/samples/manifest'}
 function aiPetSnapshot(){const s=lastStatus||{};const p=s.currentPet||(bagPets||[]).find(x=>x&&x.active)||bagPets[bagIndex]||{};return {source:{imageTraits:s.imageTraits||{},recognition:s.recognition||{}},pet:{species:p.species,element:p.element,level:p.level,stage:p.stage,xp:p.xp,wins:p.wins,battles:p.battles,growthGoal:p.growthGoal,winRate:p.winRate},context:{bagCount:s.bagCount,bagCapacity:s.bagCapacity,activePet:!!p.active,screen:s.screenLabel||s.screen}}}
 function aiSocialSnapshot(){const s=lastStatus||{};const b=s.battle||{};const p=s.currentPet||(bagPets||[]).find(x=>x&&x.active)||{};return {relationship:{recentOpponent:peerShort(b.recentPeerId),friendshipScore:b.friendshipScore,bondLabel:b.friendshipLabel,friendshipGoal:b.friendshipGoal,rematchStreak:b.rematchStreak,friendBattleCount:b.friendBattleCount,friendCount:b.friendCount},battle:{phase:b.phaseLabel||b.phase,result:b.outcomeLabel||b.outcome||b.result,scoreDiff:b.scoreDiff,xpReward:b.xpGained||b.xpReward,friendBonusXp:b.friendBonusXp||b.friendXpBonus,rounds:{powerDiff:b.powerDiff,elementSwing:b.elementSwing,spiritDiff:b.spiritDiff}},pets:{mine:{name:p.species,element:p.element,level:p.level,stage:p.stage},opponent:{name:b.opponentSpecies||'最近对手',element:b.opponentElement||'',level:b.opponentLevel||''}},events:(lastLogs||[]).slice(0,5)}}
 function aiLocal(kind,snap){if(kind==='pet'){const p=snap.pet||{};const r=(snap.source&&snap.source.recognition)||{};const base=text(p.species||'伙伴');const obj=text(r.objectLabel);const elem=text(p.element||r.elementHint);return {petName:base==='-'?'新伙伴':base,personality:`${elem}系伙伴，性格稳定，喜欢用自己的节奏观察周围。`,catchStory:`它由一次本地拍照捕捉而来，识别线索是 ${obj}，所有生成都来自设备本地数据。`,evolutionHint:`继续积累 XP、参与对战并关注成长目标：${text(p.growthGoal)}。`,idleLine:'我会在背包里等你下一次出发。',battleLine:'这一次也稳稳来吧。',flavorText:`${elem}元素与 ${obj} 线索组合出的本地模板文案。`}}const rel=snap.relationship||{};const b=snap.battle||{};return {eventTitle:'新的羁绊记录',eventText:`最近对手 ${text(rel.recentOpponent)} 和你的伙伴完成了一次 ${text(b.result)} 对战，友情 ${text(rel.friendshipScore)}/100。`,relationshipLabel:text(rel.bondLabel||rel.friendshipGoal),nextActionHint:'再次对战或添加最近对手，可以继续累积友情和连战奖励。',rematchFlavorText:'它已经记住了这位对手，下次交锋会更有默契。'}}
 function aiPrompt(kind,snap){const fields=kind==='pet'?'petName, personality, catchStory, evolutionHint, idleLine, battleLine, flavorText':'eventTitle, eventText, relationshipLabel, nextActionHint, rematchFlavorText';const role=kind==='pet'?'五行宠物机的宠物人格生成器':'五行宠物机的好友社交事件生成器';return `你是${role}。只根据输入事实生成中文展示文案，不改变等级、元素、战绩、友情分或对战结果。不要暴露 HOST、CLIENT、UDP、MAC 地址等底层信息。严格输出 JSON，字段为：${fields}。\n输入：\n${JSON.stringify(snap,null,2)}`}
 function aiEndpoint(provider){const base=aiPrefs.baseUrl;if(provider==='openai')return base||'https://api.openai.com/v1/chat/completions';if(provider==='deepseek')return base||'https://api.deepseek.com/chat/completions';return base}
 function aiModel(provider){return aiPrefs.model||(provider==='openai'?'gpt-4o-mini':(provider==='deepseek'?'deepseek-chat':''))}
 function aiHeaders(provider){const headers={'Content-Type':'application/json'};if(provider!=='proxy'&&aiPrefs.apiKey)headers.Authorization='Bearer '+aiPrefs.apiKey;return headers}
 function aiBody(provider,kind,snap){const prompt=aiPrompt(kind,snap);if(provider==='proxy')return {kind,snapshot:snap,prompt};return {model:aiModel(provider),messages:[{role:'system',content:'只输出有效 JSON。不要输出 Markdown。'},{role:'user',content:prompt}],response_format:{type:'json_object'},temperature:.8}}
 function aiTextFromResponse(j){if(typeof j==='string')return j;if(j.output_text)return j.output_text;if(j.text)return j.text;if(j.result)return typeof j.result==='string'?j.result:JSON.stringify(j.result);const c=j.choices&&j.choices[0]&&j.choices[0].message&&j.choices[0].message.content;if(c)return c;const content=j.output&&j.output[0]&&j.output[0].content;if(Array.isArray(content)){const part=content.find(x=>x.text||x.output_text);if(part)return part.text||part.output_text}return JSON.stringify(j)}
 function aiRender(obj,kind){const title=kind==='pet'?'宠物人格':'好友事件';return `<b>${title}</b>`+Object.keys(obj).map(k=>row(k,obj[k])).join('')+`<pre>${h(JSON.stringify(obj,null,2))}</pre>`}
 function aiUseLocal(){saveAiPrefs();const kind=(lastStatus&&lastStatus.battle&&lastStatus.battle.recentPeerId)?'social':'pet';const snap=kind==='pet'?aiPetSnapshot():aiSocialSnapshot();const obj=aiLocal(kind,snap);$('aiStatus').textContent='已使用本地模板，没有联网';$('aiOutput').innerHTML=aiRender(obj,kind)}
 async function aiGenerate(kind){saveAiPrefs();const snap=kind==='pet'?aiPetSnapshot():aiSocialSnapshot();const provider=aiPrefs.provider||'local';$('aiStatus').textContent='生成中...';try{let obj;if(provider==='local'){obj=aiLocal(kind,snap)}else{const url=aiEndpoint(provider);if(!url)throw new Error('请填写接口地址');if(provider!=='proxy'&&!aiPrefs.apiKey)throw new Error('请填写 API Key，或改用本地模板/自有代理');const r=await fetch(url,{method:'POST',headers:aiHeaders(provider),body:JSON.stringify(aiBody(provider,kind,snap))});if(!r.ok)throw new Error('HTTP '+r.status);const txt=aiTextFromResponse(await r.json());obj=JSON.parse(txt)}$('aiStatus').textContent=provider==='local'?'已使用本地模板':'已生成：'+provider;$('aiOutput').innerHTML=aiRender(obj,kind)}catch(e){const obj=aiLocal(kind,snap);$('aiStatus').textContent='AI 调用失败，已回退本地模板：'+e.message;$('aiOutput').innerHTML=aiRender(obj,kind)}}
 function renderButtonActions(labels,actions){const names=['左键','中键','右键'];return ['left','middle','right'].map((k,i)=>{const action=(actions&&actions[k])||'idle';return `<button class="${i?'secondary':''}" onclick="maybeAction('${action}')">${names[i]}<small>${h(labels&&labels[k])}</small></button>`}).join('')+'<button class="secondary" onclick="refreshAll()">刷新<small>同步状态</small></button>'}
 function updateActionBars(labels,actions){const html=renderButtonActions(labels,actions);['buttonActions','bagActions','battleActions','captureButtons','friendActions'].forEach(id=>{const el=$(id);if(el)el.innerHTML=html});const home=$('buttonActions');if(home)home.innerHTML=`<button onclick="go('capture')">去捕捉<small>拍照/收服</small></button><button class="secondary" onclick="go('bag')">看背包<small>左滑右滑</small></button><button class="secondary" onclick="go('battle')">去对战<small>匹配/结果</small></button><button class="secondary" onclick="go('friends')">好友<small>友情/连战</small></button><button class="secondary" onclick="refreshAll()">刷新<small>同步状态</small></button>`;const cap=$('captureButtons');if(cap)cap.innerHTML=`<button onclick="maybeAction('photo')">拍照<small>寻找野生伙伴</small></button><button class="secondary" onclick="maybeAction('capture')">收服<small>当前野生伙伴</small></button><button class="secondary" onclick="maybeAction('release')">释放<small>当前野生伙伴</small></button>`;const bag=$('bagActions');if(bag)bag.innerHTML=`<button onclick="maybeAction('bag')">打开背包<small>进入设备背包页</small></button><button class="secondary" onclick="bagStep(-1)">上一只<small>右滑同效</small></button><button class="secondary" onclick="bagStep(1)">下一只<small>左滑同效</small></button><button class="secondary" onclick="maybeAction('select')">设为伙伴<small>选中出战</small></button><button class="dangerbtn" onclick="maybeAction('release')">放生<small>需要确认</small></button>`;const battle=$('battleActions');if(battle)battle.innerHTML=`<button onclick="maybeAction('match')">寻找对手<small>进入匹配</small></button><button class="secondary" onclick="maybeAction('friend')">添加好友<small>记录最近对手</small></button><button class="secondary" onclick="maybeAction('idle')">回休闲<small>退出对战页</small></button><button class="secondary" onclick="refreshAll()">刷新<small>同步对战</small></button>`;const friends=$('friendActions');if(friends)friends.innerHTML=`<button onclick="maybeAction('friend')">添加最近对手<small>本机记录</small></button><button class="secondary" onclick="maybeAction('match')">去对战<small>继续累积友情</small></button><button class="secondary" onclick="go('more')">好友事件<small>筛选记录</small></button><button class="secondary" onclick="refreshAll()">刷新<small>同步好友</small></button>`}
 async function refreshStatus(){const s=await api('/api/v1/status');lastStatus=s;const btn=s.buttons||{};const acts=s.buttonActions||{};setConn(true,s.ssid||s.httpBaseUrl);$('statusBox').innerHTML=renderStatusBox(s,btn);$('coachTips').innerHTML=renderTips(s);updateActionBars(btn,acts);applySamplingState(s.sampling);$('currentPet').innerHTML=renderPet(s.currentPet)+renderTrainingPlanCard(s);$('captureRecognition').innerHTML=renderRecognition(s.recognition);$('battleRows2').innerHTML=renderBattle(s.battle);$('friendRows').innerHTML=renderFriendPage(s.battle);$('nextActions').innerHTML=renderNextActions(s);$('recentSummary').innerHTML=renderRecentSummary(s);if($('soundPanel'))$('soundPanel').innerHTML=renderSoundPanel(s);if($('audioToggle'))$('audioToggle').checked=!!s.muted;if($('audioState'))$('audioState').textContent=s.muted?'当前已静音，点击可开启':'当前有音效，点击可静音';touchTime()}
 async function refreshBag(){const b=await api('/api/v1/backpack');bagPets=b.pets||[];const idx=Number(b.cursorIndex??b.selectedIndex??0);bagIndex=Number.isFinite(idx)?idx:0;$('bagList').innerHTML=renderBagPager(b)}
 function showLogDetail(id){const x=lastLogs.find(e=>typeof e==='string'?e===id:String(e.id)===String(id));if(!x||!$('logDetail'))return;if(typeof x==='string'){$('logDetail').innerHTML=row('事件',x)+`<div class="note">旧格式本地事件。</div>`;return}$('logDetail').innerHTML=row('时间',`${h(x.timeSec)}s`)+row('分类',x.category)+row('级别',x.level)+row('事件',x.message)+`<div class="note">${h(x.detail)}</div>`}
 function logCount(k){const xs=rawLogs||[];return xs.filter(x=>k==='all'||(x&&x.category===k)).length}
 function renderLogFilters(){const defs=[['all','全部'],['capture','捕捉'],['battle','对战'],['friend','好友'],['setting','设置'],['system','系统'],['action','操作']];$('logFilters').innerHTML=defs.map(([k,v])=>`<button class="secondary ${logFilter===k?'active':''}" onclick="setLogFilter('${k}')">${v}<small>${logCount(k)}</small></button>`).join('')}
 function setLogFilter(k){logFilter=k;renderLogFilters();$('logs').innerHTML=renderLogs()}
 function renderLogs(logs){if(logs)rawLogs=logs;lastLogs=(rawLogs||[]).slice(-20).reverse();const list=lastLogs.filter(x=>logFilter==='all'||(x&&x.category===logFilter));if(!list.length)return '暂无事件';return list.map(x=>typeof x==='string'?`<button class="logitem secondary" onclick="showLogDetail('${h(x)}')">${h(x)}<small>旧格式事件</small></button>`:`<button class="logitem secondary" onclick="showLogDetail('${h(x.id)}')">${h(x.message)}<small>${h(x.timeSec)}s · ${h(x.category)} · ${h(x.level)}</small></button>`).join('')}
 async function refreshLogs(){const l=await api('/api/v1/logs');const html=renderLogs(l.logs);renderLogFilters();$('logs').innerHTML=html}
 async function refreshAll(){setBusy(true);try{await refreshStatus();try{await refreshBag()}catch(e){}try{await refreshLogs()}catch(e){}try{await refreshSamples()}catch(e){}const active=document.querySelector('section.active');if(active&&active.id==='capture')try{await refreshQuality()}catch(e){}if(active&&active.id==='dex')try{await refreshDex()}catch(e){}}catch(e){setConn(false,'连接失败，保留上次数据：'+e.message);showError('连接失败：'+e.message)}finally{setBusy(false)}}
 function setMute(on){act(on?'mute':'unmute')}
 async function testSound(cue){setBusy(true);try{await api('/api/v1/action?type=sound&cue='+encodeURIComponent(cue),{method:'POST'});await refreshAll()}catch(e){setConn(false,'音效测试失败：'+e.message);showError('音效测试失败：'+e.message)}finally{setBusy(false)}}
 function maybeAction(type){if(prefs.confirmDanger&&(type==='release'||type==='confirm_release')){if(!confirm('确认执行“'+(type==='release'?'放生':'确认放生')+'”？'))return}act(type)}
 async function act(type){setBusy(true);try{await api('/api/v1/action?type='+encodeURIComponent(type),{method:'POST'});await refreshAll()}catch(e){setConn(false,'操作失败：'+e.message);showError('操作失败：'+e.message);$('logs').innerHTML=`<div class="danger">操作失败：${h(e.message)}</div>`}finally{setBusy(false)}}
applyPrefs();setupTabs();setupBagSwipe();refreshAll();refreshSamples();setInterval(()=>{if(!$('err').classList.contains('show'))refreshAll()},2000);
</script>
</body>
</html>)rawliteral";

static void send_app_page()
{
    app_http.sendHeader("Cache-Control", "no-store");
    app_http.send_P(200, "text/html; charset=utf-8", kAppPageHtml);
}

static bool app_action_from_type(const String& type, uint8_t* action, const char** error)
{
    if (action == nullptr) {
        if (error != nullptr) {
            *error = "invalid action target";
        }
        return false;
    }
    if (type == "photo") {
        *action = kActionPhoto;
    } else if (type == "bag") {
        *action = kActionOpenBag;
    } else if (type == "match") {
        *action = kActionMatchBattle;
    } else if (type == "idle") {
        *action = kActionBackToIdle;
    } else if (type == "prev") {
        if (screen_mode != kScreenBag) {
            if (error != nullptr) {
                *error = "请先打开背包";
            }
            return false;
        }
        *action = kActionPrevPet;
    } else if (type == "next") {
        if (screen_mode != kScreenBag) {
            if (error != nullptr) {
                *error = "请先打开背包";
            }
            return false;
        }
        *action = kActionNextPet;
    } else if (type == "select") {
        if (screen_mode != kScreenBag) {
            if (error != nullptr) {
                *error = "请先打开背包再选择伙伴";
            }
            return false;
        }
        *action = kActionSelectPet;
    } else if (type == "capture") {
        if (screen_mode != kScreenWild) {
            if (error != nullptr) {
                *error = "请先拍照发现野生伙伴";
            }
            return false;
        }
        *action = kActionCapturePet;
    } else if (type == "release") {
        if (screen_mode == kScreenWild) {
            *action = kActionReleasePet;
        } else if (screen_mode == kScreenBag) {
            *action = kActionReleaseStoredPet;
        } else {
            if (error != nullptr) {
                *error = "请先进入背包或野外遭遇";
            }
            return false;
        }
    } else if (type == "confirm_release") {
        if (screen_mode != kScreenReleaseConfirm) {
            if (error != nullptr) {
                *error = "请先进入放生确认";
            }
            return false;
        }
        *action = kActionConfirmReleaseStoredPet;
    } else if (type == "cancel") {
        *action = (screen_mode == kScreenReleaseConfirm) ? kActionOpenBag : kActionBackToIdle;
    } else {
        if (error != nullptr) {
            *error = "不支持的操作";
        }
        return false;
    }
    return true;
}

static bool serial_sound_cue(const String& value, uint8_t* cue);

static void handle_app_action()
{
    if (!app_http.hasArg("type")) {
        send_json_error(400, "missing action type");
        return;
    }
    String type = app_http.arg("type");
    type.toLowerCase();
    if (type == "mute" || type == "unmute" || type == "toggle_mute") {
        if (type == "toggle_mute") {
            audio_muted = !audio_muted;
        } else {
            audio_muted = (type == "mute");
        }
        if (audio_muted) {
            CoreS3.Speaker.stop();
            CoreS3.Speaker.end();
            append_app_log("音效已静音");
        } else {
            append_app_log("音效已开启");
        }
        send_app_status();
        return;
    }
    if (type == "friend") {
        char message[48];
        if (!add_current_friend(message, sizeof(message))) {
            send_json_error(400, message);
            return;
        }
        append_app_log(message);
        show_friend_action_feedback(message);
        send_app_status();
        return;
    }
    if (type == "sound") {
        if (!app_http.hasArg("cue")) {
            send_json_error(400, "missing sound cue");
            return;
        }
        String cueName = app_http.arg("cue");
        cueName.toLowerCase();
        uint8_t cue = kSoundIdle;
        if (!serial_sound_cue(cueName, &cue)) {
            send_json_error(400, "unsupported sound cue");
            return;
        }
        play_scene_sound(cue);
        char logLine[64];
        snprintf(logLine, sizeof(logLine), "sound cue %s%s", cueName.c_str(), audio_muted ? " muted" : "");
        append_app_log(logLine);
        send_app_status();
        return;
    }

    uint8_t action = kActionNone;
    const char* actionError = nullptr;
    if (!app_action_from_type(type, &action, &actionError)) {
        send_json_error(400, actionError == nullptr ? "unsupported action type" : actionError);
        return;
    }
    char logLine[80];
    snprintf(logLine, sizeof(logLine), "app action %s", type.c_str());
    append_app_log(logLine);
    handle_ui_action(action);
    send_app_status();
}

static void print_serial_control_help()
{
    Serial.println("serial controls: BTN L|M|R, ACT photo|bag|match|idle|prev|next|select|capture|release|confirm_release|cancel|friend|mute|unmute|toggle_mute, SAMPLE on|off [label] [scene]|label <label>|scene <scene>|status, EDGE_HINT <class> <confidence> <presence>, HUSKY_HINT <class> <confidence> <presence>, SOUND <cue>, STATUS, BAGSTATUS, SDINFO, SDPUT <path> <bytes>");
}

static void print_serial_sampling_status()
{
    Serial.printf("sampling enabled=%u label=%s scene=%s\n",
                  sample_mode_enabled ? 1 : 0,
                  sample_mode_label,
                  sample_mode_scene);
}

static void print_serial_external_hint_status()
{
    if (!external_vision_hint_active()) {
        Serial.println("external_hint active=0");
        return;
    }
    uint32_t ttl = static_cast<uint32_t>(
        max<int32_t>(0, static_cast<int32_t>(external_vision_hint.expiresAtMs - millis())));
    Serial.printf("external_hint active=1 source=%s class=%s confidence=%u presence=%u ttl_ms=%lu\n",
                  external_vision_source_label(external_vision_hint.source),
                  object_class_label(external_vision_hint.classId),
                  external_vision_hint.confidence,
                  external_vision_hint.presence,
                  static_cast<unsigned long>(ttl));
}

static void print_serial_control_status()
{
    Serial.printf("ui screen=%s label=%s buttons=%s/%s/%s bag=%u/%u battle=%s %s\n",
                  screen_mode_name(),
                  screen_mode_label(),
                  button_label_for_slot(kButtonLeft),
                  button_label_for_slot(kButtonMiddle),
                  button_label_for_slot(kButtonRight),
                  backpack.count,
                  kMaxBackpackPets,
                  app_battle_phase(),
                  app_battle_phase_label());
    const SavedPet* pet = selected_pet_const();
    if (pet != nullptr) {
        char growthGoal[24];
        format_growth_goal(growthGoal, sizeof(growthGoal), *pet);
        Serial.printf("pet active=%u/%u species=%s level=%u stage=%s xp=%u/%u goal=%s stats=%u/%u/%u wins=%u/%u\n",
                      backpack.selected + 1,
                      backpack.count,
                      species_name(pet->genes),
                      pet->level,
                      stage_name(pet->stage),
                      pet->xp,
                      level_xp_need(pet->level),
                      growthGoal,
                      pet_battle_power(*pet),
                      pet_battle_agility(*pet),
                      pet_battle_spirit(*pet),
                      pet->wins,
                      pet->battles);
    } else {
        Serial.printf("pet active=none bag=%u/%u\n", backpack.count, kMaxBackpackPets);
    }
    if (last_friend_peer_id != 0) {
        Serial.printf("friend recent=%06lX label=%s score=%u/100 battles=%u streak=%u next_xp=%u notice=%s\n",
                      static_cast<unsigned long>(last_friend_peer_id & 0xffffffUL),
                      friend_bond_name(),
                      friendship_score(),
                      friend_battle_count,
                      friend_rematch_streak,
                      next_rematch_xp_bonus(),
                      last_friend_notice[0] == '\0' ? "-" : last_friend_notice);
    } else {
        Serial.println("friend recent=none");
    }
    if (app_last_battle_result_valid) {
        Serial.printf("battle last=%s id=%08lX diff=%ld xp=%u friend_bonus=%u\n",
                      battle_outcome_label(app_last_battle_outcome),
                      static_cast<unsigned long>(app_last_battle_id),
                      static_cast<long>(app_last_battle_score_diff),
                      app_last_battle_xp,
                      app_last_battle_friend_bonus);
    } else {
        Serial.println("battle last=none");
    }
    print_serial_sampling_status();
    print_serial_external_hint_status();
}

static void print_serial_bag_status()
{
    refresh_backpack_growth(true);
    String out;
    out.reserve(5600);
    out += "{\"ok\":true,\"count\":";
    append_uint(out, backpack.count);
    out += ",\"capacity\":";
    append_uint(out, kMaxBackpackPets);
    out += ",\"selectedIndex\":";
    append_uint(out, backpack.selected);
    out += ",\"pets\":[";
    for (uint8_t i = 0; i < backpack.count; ++i) {
        if (i != 0) {
            out += ",";
        }
        append_pet_json(out, backpack.pets[i], i, true);
    }
    out += "]}";
    Serial.println(out);
}

static bool serial_button_slot(const String& value, uint8_t* slot)
{
    if (slot == nullptr || value.length() == 0) {
        return false;
    }
    char c = static_cast<char>(toupper(value.charAt(0)));
    if (c == 'L' || c == '0') {
        *slot = kButtonLeft;
        return true;
    }
    if (c == 'M' || c == '1' || c == 'C') {
        *slot = kButtonMiddle;
        return true;
    }
    if (c == 'R' || c == '2') {
        *slot = kButtonRight;
        return true;
    }
    return false;
}

static bool serial_sound_cue(const String& value, uint8_t* cue)
{
    if (cue == nullptr || value.length() == 0) {
        return false;
    }
    String name = value;
    name.toLowerCase();
    if (name == "idle") {
        *cue = kSoundIdle;
    } else if (name == "photo") {
        *cue = kSoundPhoto;
    } else if (name == "wild") {
        *cue = kSoundWild;
    } else if (name == "bag") {
        *cue = kSoundBag;
    } else if (name == "capture") {
        *cue = kSoundCapture;
    } else if (name == "release") {
        *cue = kSoundRelease;
    } else if (name == "select") {
        *cue = kSoundSelect;
    } else if (name == "match") {
        *cue = kSoundMatch;
    } else if (name == "clash") {
        *cue = kSoundBattleClash;
    } else if (name == "friend") {
        *cue = kSoundFriend;
    } else if (name == "win") {
        *cue = kSoundWin;
    } else if (name == "draw") {
        *cue = kSoundDraw;
    } else if (name == "lose") {
        *cue = kSoundLose;
    } else if (name == "warning") {
        *cue = kSoundWarning;
    } else if (name == "intro") {
        *cue = kSoundTrainerIntro;
    } else if (name == "level") {
        *cue = kSoundLevelUp;
    } else if (name == "exit") {
        *cue = kSoundBattleExit;
    } else if (name == "cancel") {
        *cue = kSoundCancel;
    } else {
        return false;
    }
    return true;
}

static void handle_serial_sample_command(String value)
{
    value.trim();
    if (value.length() == 0) {
        print_serial_sampling_status();
        return;
    }
    int space = value.indexOf(' ');
    String action = space < 0 ? value : value.substring(0, space);
    String rest = space < 0 ? "" : value.substring(space + 1);
    action.trim();
    rest.trim();
    action.toLowerCase();

    const char* error = nullptr;
    if (action == "status") {
        print_serial_sampling_status();
        return;
    }
    if (action == "on" || action == "off" || action == "1" || action == "0") {
        sample_mode_enabled = (action == "on" || action == "1");
        if (rest.length() > 0) {
            int secondSpace = rest.indexOf(' ');
            String label = secondSpace < 0 ? rest : rest.substring(0, secondSpace);
            String scene = secondSpace < 0 ? "" : rest.substring(secondSpace + 1);
            if (!set_sample_label_value(label, &error)) {
                Serial.printf("serial error: %s\n", error == nullptr ? "unsupported sample label" : error);
                return;
            }
            if (scene.length() > 0 && !set_sample_scene_value(scene, &error)) {
                Serial.printf("serial error: %s\n", error == nullptr ? "unsupported sample scene" : error);
                return;
            }
        }
        log_sample_mode_change();
        Serial.printf("serial ok: SAMPLE %s %s/%s\n",
                      sample_mode_enabled ? "on" : "off",
                      sample_mode_label,
                      sample_mode_scene);
        return;
    }
    if (action == "label") {
        if (!set_sample_label_value(rest, &error)) {
            Serial.printf("serial error: %s\n", error == nullptr ? "unsupported sample label" : error);
            return;
        }
        log_sample_mode_change();
        Serial.printf("serial ok: SAMPLE label %s\n", sample_mode_label);
        return;
    }
    if (action == "scene") {
        if (!set_sample_scene_value(rest, &error)) {
            Serial.printf("serial error: %s\n", error == nullptr ? "unsupported sample scene" : error);
            return;
        }
        log_sample_mode_change();
        Serial.printf("serial ok: SAMPLE scene %s\n", sample_mode_scene);
        return;
    }
    Serial.println("serial error: use SAMPLE on|off [label] [scene], SAMPLE label <label>, SAMPLE scene <scene>, or SAMPLE status");
}

static void handle_serial_external_hint_command(String value, uint8_t source)
{
    value.trim();
    int firstSpace = value.indexOf(' ');
    int secondSpace = firstSpace < 0 ? -1 : value.indexOf(' ', firstSpace + 1);
    if (firstSpace <= 0 || secondSpace <= firstSpace + 1) {
        Serial.println("serial error: use EDGE_HINT/HUSKY_HINT <class> <confidence> <presence>");
        return;
    }

    String label = value.substring(0, firstSpace);
    String confidenceText = value.substring(firstSpace + 1, secondSpace);
    String presenceText = value.substring(secondSpace + 1);
    label.trim();
    confidenceText.trim();
    presenceText.trim();

    uint8_t classId = kObjectUnknown;
    if (!object_class_from_label(label, &classId)) {
        Serial.println("serial error: unsupported external class label");
        return;
    }
    uint8_t confidence = clamp_u8(strtol(confidenceText.c_str(), nullptr, 10));
    uint8_t presence = clamp_u8(strtol(presenceText.c_str(), nullptr, 10));
    if (classId == kObjectUnknown) {
        clear_external_vision_hint();
        Serial.printf("serial ok: %s_HINT fallback class=%s\n",
                      external_vision_source_label(source),
                      label.c_str());
        return;
    }
    if (confidence < kExternalVisionMinConfidence || presence < kExternalVisionMinPresence) {
        clear_external_vision_hint();
        Serial.printf("serial ok: %s_HINT fallback low confidence=%u presence=%u\n",
                      external_vision_source_label(source),
                      confidence,
                      presence);
        return;
    }

    external_vision_hint.valid = true;
    external_vision_hint.classId = classId;
    external_vision_hint.confidence = confidence;
    external_vision_hint.presence = presence;
    external_vision_hint.source = source;
    external_vision_hint.expiresAtMs = millis() + kExternalVisionHintTtlMs;
    Serial.printf("serial ok: %s_HINT class=%s confidence=%u presence=%u ttl_ms=%lu\n",
                  external_vision_source_label(source),
                  object_class_label(classId),
                  confidence,
                  presence,
                  static_cast<unsigned long>(kExternalVisionHintTtlMs));
}

static void handle_serial_control_line(String line)
{
    line.trim();
    if (line.length() == 0) {
        return;
    }

    int space = line.indexOf(' ');
    String command = space < 0 ? line : line.substring(0, space);
    String value = space < 0 ? "" : line.substring(space + 1);
    command.toUpperCase();
    value.trim();

    if (command == "HELP" || command == "?") {
        print_serial_control_help();
        return;
    }
    if (command == "STATUS") {
        print_serial_control_status();
        return;
    }
    if (command == "BAGSTATUS") {
        print_serial_bag_status();
        return;
    }
    if (command == "SDINFO") {
        log_sd_storage_report();
        return;
    }
    if (command == "SAMPLE") {
        handle_serial_sample_command(value);
        return;
    }
    if (command == "EDGE_HINT") {
        handle_serial_external_hint_command(value, kExternalVisionEdge);
        return;
    }
    if (command == "HUSKY_HINT") {
        handle_serial_external_hint_command(value, kExternalVisionHusky);
        return;
    }
    if (command == "SDPUT") {
        int split = value.lastIndexOf(' ');
        if (split <= 0) {
            Serial.println("serial error: use SDPUT /audio/ui/name.raw <bytes>");
            return;
        }
        String path = value.substring(0, split);
        String sizeText = value.substring(split + 1);
        path.trim();
        sizeText.trim();
        receive_serial_sd_file(path, static_cast<uint32_t>(strtoul(sizeText.c_str(), nullptr, 10)));
        return;
    }
    if (command == "BTN") {
        uint8_t slot = kButtonLeft;
        if (!serial_button_slot(value, &slot)) {
            Serial.println("serial error: use BTN L|M|R");
            return;
        }
        handle_external_button(slot);
        Serial.printf("serial ok: BTN %u -> %s\n", slot, button_label_for_slot(slot));
        return;
    }
    if (command == "ACT") {
        value.toLowerCase();
        if (value == "mute" || value == "unmute" || value == "toggle_mute") {
            audio_muted = value == "toggle_mute" ? !audio_muted : (value == "mute");
            if (audio_muted) {
                CoreS3.Speaker.stop();
                CoreS3.Speaker.end();
                append_app_log("音效已静音");
            } else {
                append_app_log("音效已开启");
            }
            Serial.printf("serial ok: ACT %s -> sound %s\n", value.c_str(), audio_muted ? "muted" : "on");
            return;
        }
        if (value == "friend") {
            char message[48];
            if (!add_current_friend(message, sizeof(message))) {
                Serial.printf("serial error: %s\n", message);
                return;
            }
            append_app_log(message);
            show_friend_action_feedback(message);
            Serial.printf("serial ok: ACT friend -> %s\n", message);
            return;
        }

        uint8_t action = kActionNone;
        const char* actionError = nullptr;
        if (!app_action_from_type(value, &action, &actionError)) {
            Serial.printf("serial error: %s\n", actionError == nullptr ? "unsupported action type" : actionError);
            return;
        }
        handle_external_action(action);
        Serial.printf("serial ok: ACT %s\n", value.c_str());
        return;
    }
    if (command == "SOUND" || command == "SND") {
        uint8_t cue = kSoundIdle;
        if (!serial_sound_cue(value, &cue)) {
            Serial.println("serial error: use SOUND idle|photo|wild|bag|capture|release|select|match|clash|friend|win|draw|lose|warning|intro|level|exit|cancel");
            return;
        }
        play_scene_sound(cue);
        Serial.printf("serial ok: SOUND %s%s\n", value.c_str(), audio_muted ? " (muted)" : "");
        return;
    }

    Serial.println("serial error: unknown command");
    print_serial_control_help();
}

static void service_serial_control()
{
    static String line;
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            handle_serial_control_line(line);
            line = "";
            continue;
        }
        if (line.length() < 96) {
            line += c;
        } else {
            line = "";
            Serial.println("serial error: command too long");
        }
    }
}

static void start_app_http_server()
{
    app_http.on("/app", HTTP_GET, send_app_page);
    app_http.on("/api/v1/status", HTTP_GET, send_app_status);
    app_http.on("/api/v1/backpack", HTTP_GET, send_app_backpack);
    app_http.on("/api/v1/encyclopedia", HTTP_GET, send_app_encyclopedia);
    app_http.on("/api/v1/pets", HTTP_GET, send_app_pet_detail);
    app_http.on("/api/v1/recognition/last", HTTP_GET, send_app_recognition);
    app_http.on("/api/v1/battle", HTTP_GET, send_app_battle);
    app_http.on("/api/v1/battle/replays", HTTP_GET, send_app_battle_replays);
    app_http.on("/api/v1/logs", HTTP_GET, send_app_logs);
    app_http.on("/api/v1/storage", HTTP_GET, send_app_storage);
    app_http.on("/api/v1/sampling", HTTP_GET, send_app_sampling);
    app_http.on("/api/v1/sampling", HTTP_POST, send_app_sampling);
    app_http.on("/api/v1/capture/quality", HTTP_GET, send_app_capture_quality);
    app_http.on("/api/v1/samples", HTTP_GET, send_app_samples);
    app_http.on("/api/v1/samples/manifest", HTTP_GET, send_app_sample_manifest);
    app_http.on("/api/v1/samples/file", HTTP_GET, send_app_sample_file);
    app_http.on("/api/v1/action", HTTP_POST, handle_app_action);
    app_http.onNotFound([]() {
        send_json_error(404, "not found");
    });
    app_http.begin();
    app_http_started = true;
    append_app_log("http api started");
}

static void service_app_http()
{
    if (app_http_started) {
        app_http.handleClient();
    }
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
    detect_sd_card();
    log_sd_storage_report();
    init_proximity_sensor();
    comm_ok = init_pet_comms();
    if (comm_ok) {
        start_app_http_server();
    }
    log_battle_identity();
    draw_status("五行宠物机", comm_ok ? "对战连接已就绪" : "对战连接未就绪", comm_ok ? TFT_CYAN : TFT_YELLOW);
    CoreS3.delay(500);
    draw_status("五行宠物机", "相机初始化中...", TFT_CYAN);

    camera_ok = CoreS3.Camera.begin();
    if (!camera_ok) {
        draw_status("相机初始化失败", "请检查 CoreS3 相机", TFT_RED);
    }

    CoreS3.Speaker.setVolume(kSoundVolume);
    if (audio_muted) {
        CoreS3.Speaker.stop();
        CoreS3.Speaker.end();
    }
    load_backpack();
    load_friendship_state();
    refresh_backpack_growth(true);
    restore_selected_pet();
    if (has_local_pet) {
        publish_local_pet();
    }
    append_app_log("setup complete");
    play_trainer_intro();
    draw_idle_screen("准备就绪：背包 / 对战 / 拍照", false);
}

void loop()
{
    CoreS3.update();
    refresh_backpack_growth(false);
    service_serial_control();

    if (CoreS3.Touch.getCount() && CoreS3.Touch.getDetail(0).wasClicked()) {
        auto detail = CoreS3.Touch.getDetail(0);
        handle_touch(detail.x);
        return;
    }

    service_pet_comms();
    service_app_http();
    refresh_battle_clash_progress();
    resolve_pending_battle_result();
    resolve_pending_battle_exit();
    refresh_match_status();

    static uint32_t last_ui_ms = 0;
    if (screen_mode == kScreenIdle && millis() > display_hold_until_ms && millis() - last_ui_ms > 600) {
        last_ui_ms = millis();
        char line2[64];
        const SavedPet* pet = selected_pet_const();
        if (pet == nullptr) {
            snprintf(line2, sizeof(line2), "背包:%u/%u  中键:对战", backpack.count, kMaxBackpackPets);
        } else {
            snprintf(line2, sizeof(line2), "出战 Lv%u  中键:对战", pet->level);
        }
        draw_status("休闲状态  左:背包  右:拍照", line2, TFT_GREEN);
    }
}
