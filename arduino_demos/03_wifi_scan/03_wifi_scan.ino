#include <M5Unified.h>
#include <WiFi.h>

void drawScan() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(10, 10);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.println("WiFi scan");

  int count = WiFi.scanNetworks();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  if (count <= 0) {
    M5.Display.println("No networks found.");
    return;
  }

  int rows = min(count, 10);
  for (int i = 0; i < rows; ++i) {
    M5.Display.printf("%2d %-20.20s %4d dBm\n",
                      i + 1,
                      WiFi.SSID(i).c_str(),
                      WiFi.RSSI(i));
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  M5.Display.setRotation(1);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(500);
  drawScan();
}

void loop() {
  M5.update();

  static uint32_t last_scan = 0;
  if (millis() - last_scan > 15000) {
    last_scan = millis();
    drawScan();
  }
}

