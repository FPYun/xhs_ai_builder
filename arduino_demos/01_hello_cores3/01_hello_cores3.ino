#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 30);
  M5.Display.println("CoreS3 OK");
  M5.Display.setTextSize(1);
  M5.Display.println("Hello from local Arduino demo.");
}

void loop() {
  M5.update();

  static uint32_t last_ms = 0;
  if (millis() - last_ms >= 1000) {
    last_ms = millis();
    Serial.printf("uptime=%lu ms\n", (unsigned long)millis());
    M5.Display.fillRect(20, 100, 260, 30, TFT_BLACK);
    M5.Display.setCursor(20, 100);
    M5.Display.printf("Uptime: %lu s", (unsigned long)(millis() / 1000));
  }
}

