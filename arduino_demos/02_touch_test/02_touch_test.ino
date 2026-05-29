#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 20);
  M5.Display.println("Touch test");
  M5.Display.setTextSize(1);
  M5.Display.println("Touch the screen to draw.");
}

void loop() {
  M5.update();
  auto touch = M5.Touch.getDetail();

  if (touch.isPressed()) {
    int x = touch.x;
    int y = touch.y;
    M5.Display.fillCircle(x, y, 4, TFT_ORANGE);
    Serial.printf("touch x=%d y=%d\n", x, y);
  }
}

