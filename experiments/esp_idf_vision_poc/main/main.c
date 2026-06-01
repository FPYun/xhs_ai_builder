#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "m5_pet_vision_poc";

typedef struct {
    bool mapped;
    const char *class_label;
    uint8_t confidence;
    uint8_t presence;
    const char *fallback_reason;
} poc_recognition_hint_t;

static const char *const k_class_labels[] = {
    "plant_leaf",
    "food_fruit",
    "paper_book",
    "electronics_screen",
    "metal_key_coin",
    "fabric_cloth",
    "cup_bottle_water",
    "toy_figure",
    "negative",
};

static poc_recognition_hint_t map_stub_output(uint8_t class_index, uint8_t confidence, uint8_t presence)
{
    if (class_index >= 8) {
        return (poc_recognition_hint_t){
            .mapped = false,
            .class_label = "unknown",
            .confidence = confidence,
            .presence = presence,
            .fallback_reason = "negative_or_unmapped",
        };
    }
    if (confidence < 58 || presence < 42) {
        return (poc_recognition_hint_t){
            .mapped = false,
            .class_label = k_class_labels[class_index],
            .confidence = confidence,
            .presence = presence,
            .fallback_reason = "low_external_confidence",
        };
    }
    return (poc_recognition_hint_t){
        .mapped = true,
        .class_label = k_class_labels[class_index],
        .confidence = confidence,
        .presence = presence,
        .fallback_reason = "",
    };
}

static void log_contract_smoke_test(void)
{
    const int64_t start_us = esp_timer_get_time();
    const poc_recognition_hint_t positive = map_stub_output(0, 82, 76);
    const poc_recognition_hint_t negative = map_stub_output(8, 90, 0);
    const int64_t elapsed_us = esp_timer_get_time() - start_us;

    ESP_LOGI(TAG,
             "contract positive mapped=%d class=%s confidence=%u presence=%u",
             positive.mapped,
             positive.class_label,
             positive.confidence,
             positive.presence);
    ESP_LOGI(TAG,
             "contract negative mapped=%d class=%s reason=%s",
             negative.mapped,
             negative.class_label,
             negative.fallback_reason);
    ESP_LOGI(TAG, "contract smoke elapsed_us=%" PRId64, elapsed_us);
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "ESP-IDF PoC boot target=ESP32-S3 cores=%u", chip.cores);
    ESP_LOGI(TAG,
             "heap internal=%u psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "camera/model bring-up is intentionally not wired in this stub");
    ESP_LOGI(TAG, "output contract stays class/confidence/presence/fallback only");

    log_contract_smoke_test();

    while (true) {
        ESP_LOGI(TAG,
                 "idle heap internal=%u psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
