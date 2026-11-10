
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <RTClib.h>
#include <DHT.h>
#include <math.h>

// =============== PIN CONFIGURATION =================

#define OLED_SDA 16
#define OLED_SCL 17
#define BUTTON_PIN 27
#define DHT_PIN 26
#define DHT_TYPE DHT11

#define SCREEN_TIMEOUT 10000
#define DEBOUNCE_TIME 40

// =============== MODULES ===========================

Adafruit_SH1106G display(128, 64, &Wire, -1);
RTC_DS1307 rtc;
DHT dht(DHT_PIN, DHT_TYPE);

// =============== SYSTEM VARIABLES ==================

bool rtcOK = false;
bool dhtOK = false;

float temperature = NAN;
float humidity = NAN;

int currentPage = 0;

// Pages: 0 = Clock, 1 = Environment, 2 = System
// Page 3 = Idle animation

bool idleMode = true;

unsigned long lastActivity = 0;
unsigned long lastDHTRead = 0;
unsigned long lastPageUpdate = 0;
unsigned long lastFrame = 0;

int buttonState = HIGH;
int lastButtonReading = HIGH;
unsigned long lastDebounceTime = 0;

// =============== ROBOT ANIMATION ===================

const int EYE_W = 36;
const int EYE_H = 40;
const int EYE_LX = 20;
const int EYE_RX = 72;
const int EYE_CY = 30;

float lookX = 0;
float lookY = 0;
float targetX = 0;
float targetY = 0;

unsigned long nextLook = 0;
unsigned long nextBlink = 0;
unsigned long blinkStart = 0;
bool blinking = false;

enum Mood {
  NORMAL,
  ANGRY,
  FUNNY,
  LOVE,
  SLEEPY,
  SAD
};

Mood mood = NORMAL;
Mood previousMood = NORMAL;

unsigned long nextMood = 0;
unsigned long moodEnd = 0;

int funnyStyle = 0;

// =============== CLOCK PAGE ========================

void drawClock() {

  DateTime now = rtc.now();

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("REAL TIME CLOCK");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setTextSize(2);
  display.setCursor(13, 23);

  if (now.hour() < 10) display.print("0");
  display.print(now.hour());
  display.print(":");

  if (now.minute() < 10) display.print("0");
  display.print(now.minute());
  display.print(":");

  if (now.second() < 10) display.print("0");
  display.print(now.second());

  display.setTextSize(1);
  display.setCursor(20, 50);

  if (now.day() < 10) display.print("0");
  display.print(now.day());
  display.print("/");

  if (now.month() < 10) display.print("0");
  display.print(now.month());
  display.print("/");
  display.print(now.year());

  display.display();
}

// =============== ENVIRONMENT PAGE ==================

void drawEnvironment() {

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ENVIRONMENT");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setCursor(0, 20);
  display.print("TEMP");

  display.setTextSize(2);
  display.setCursor(58, 17);

  if (isnan(temperature)) {
    display.print("--");
  } else {
    display.print(temperature, 1);
  }

  display.print(" C");

  display.setTextSize(1);
  display.setCursor(0, 46);
  display.print("HUMIDITY");

  display.setTextSize(2);
  display.setCursor(58, 42);

  if (isnan(humidity)) {
    display.print("--");
  } else {
    display.print(humidity, 0);
  }

  display.print("%");

  display.display();
}

// =============== SYSTEM PAGE =======================

void drawSystem() {

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SYSTEM STATUS");

  display.drawLine(0, 12, 127, 12, SH110X_WHITE);

  display.setCursor(0, 19);
  display.print("OLED : READY");

  display.setCursor(0, 31);
  display.print("RTC  : ");
  display.print(rtcOK ? "ONLINE" : "ERROR");

  display.setCursor(0, 43);
  display.print("DHT11: ");
  display.print(dhtOK ? "ONLINE" : "ERROR");

  display.setCursor(0, 55);
  display.print("ESP32: RUNNING");

  display.display();
}

// =============== PAGE MANAGER ======================

void updatePage() {

  if (idleMode) return;

  switch (currentPage) {

    case 0:
      if (rtcOK) drawClock();
      break;

    case 1:
      drawEnvironment();
      break;

    case 2:
      drawSystem();
      break;
  }
}

// =============== ROBOT EYE DRAWING =================

void drawEye(int x, int y, int w, int h) {

  int r = min(10, min(w / 2, h / 2));

  display.fillRoundRect(
    x, y, w, h, r, SH110X_WHITE
  );
}

void drawHeart(int cx, int cy, int s) {

  int r = s / 2;

  display.fillCircle(cx-r, cy-r/2, r, SH110X_WHITE);
  display.fillCircle(cx+r, cy-r/2, r, SH110X_WHITE);

  display.fillTriangle(
    cx-s, cy-r/3,
    cx+s, cy-r/3,
    cx, cy+s,
    SH110X_WHITE
  );
}

// =============== MOOD MANAGEMENT ===================

void updateMood(unsigned long now) {

  if (mood == NORMAL) {

    if (now >= nextMood) {

      Mood m;

      do {
        m = (Mood)random(1, 6);
      } while (m == previousMood);

      mood = m;
      previousMood = m;

      funnyStyle = random(0, 2);

      moodEnd = now + random(2800, 5000);
    }

  } else if (now >= moodEnd) {

    mood = NORMAL;
    nextMood = now + random(6000, 12000);
  }
}

// =============== ANIMATED ROBOT FACE ===============

void drawRobotEyes() {

  unsigned long now = millis();

  updateMood(now);

  // Eye direction
  if (now >= nextLook) {

    targetX = random(-10, 11);
    targetY = random(-6, 7);

    if (random(0, 4) == 0) {
      targetX = 0;
      targetY = 0;
    }

    nextLook = now + random(1200, 3500);
  }

  lookX += (targetX - lookX) * 0.25;
  lookY += (targetY - lookY) * 0.25;

  // Blinking
  int h = EYE_H;

  if (!blinking && now >= nextBlink) {

    blinking = true;
    blinkStart = now;
  }

  if (blinking) {

    unsigned long t = now - blinkStart;

    if (t < 80) {
      h = EYE_H - (36 * t) / 80;
    }

    else if (t < 160) {
      h = 4 + (36 * (t - 80)) / 80;
    }

    else {

      blinking = false;
      nextBlink = now + random(2000, 5500);
    }
  }

  h = max(4, h);

  display.clearDisplay();

  int cy = EYE_CY + (int)lookY;
  int top = cy - h / 2;

  int lx = EYE_LX + (int)lookX;
  int rx = EYE_RX + (int)lookX;

  // NORMAL
  if (mood == NORMAL) {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);
  }

  // ANGRY
  else if (mood == ANGRY) {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);

    int slant = (22 * h) / EYE_H;

    display.fillTriangle(
      lx, top, lx+EYE_W, top,
      lx+EYE_W, top+slant, SH110X_BLACK
    );

    display.fillTriangle(
      rx, top, rx+EYE_W, top,
      rx, top+slant, SH110X_BLACK
    );
  }

  // FUNNY
  else if (mood == FUNNY) {

    if (funnyStyle == 0) {

      int c = EYE_CY + (int)(3 * sin(now / 120.0));

      drawEye(EYE_LX, c-20, EYE_W, 40);
      drawEye(EYE_RX, c-20, EYE_W, 40);

      display.fillCircle(38, c+14, 15, SH110X_BLACK);
      display.fillCircle(90, c+14, 15, SH110X_BLACK);

    } else {

      bool swap = ((now / 450) % 2) == 0;

      int lw = swap ? 36 : 26;
      int lh = swap ? 40 : 22;

      int rw = swap ? 26 : 36;
      int rh = swap ? 22 : 40;

      drawEye(38-lw/2, EYE_CY-lh/2, lw, lh);
      drawEye(90-rw/2, EYE_CY-rh/2, rw, rh);
    }
  }

  // LOVE
  else if (mood == LOVE) {

    int s = 15 + (int)(3 * sin(now / 120.0));

    drawHeart(38, EYE_CY-3, s);
    drawHeart(90, EYE_CY-3, s);
  }

  // SLEEPY
  else if (mood == SLEEPY) {

    drawEye(EYE_LX, 15, EYE_W, 30);
    drawEye(EYE_RX, 15, EYE_W, 30);

    int droop = 10 + (int)(10 * (0.5 + 0.5 * sin(now / 500.0)));

    display.fillRect(19, 14, 38, droop, SH110X_BLACK);
    display.fillRect(71, 14, 38, droop, SH110X_BLACK);

    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(100, 10);
    display.print("zZ");
  }

  // SAD
  else {

    drawEye(lx, top, EYE_W, h);
    drawEye(rx, top, EYE_W, h);

    display.fillTriangle(
      lx, top, lx+EYE_W, top,
      lx, top+18, SH110X_BLACK
    );

    display.fillTriangle(
      rx, top, rx+EYE_W, top,
      rx+EYE_W, top+18, SH110X_BLACK
    );

    int ty = 35 + (int)((now / 60) % 20);

    if (ty < 62) {
      display.fillCircle(lx+8, ty, 2, SH110X_WHITE);
    }
  }

  display.display();
}

// =============== BUTTON HANDLER ====================

void handleButton() {

  unsigned long now = millis();

  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = now;
  }

  if (now - lastDebounceTime > DEBOUNCE_TIME) {

    if (reading != buttonState) {

      buttonState = reading;

      if (buttonState == LOW) {

        lastActivity = now;

        if (idleMode) {

          idleMode = false;
          currentPage = 0;

        } else {

          currentPage++;

          if (currentPage >= 3) {
            currentPage = 0;
          }
        }

        updatePage();

        Serial.print("Page: ");
        Serial.println(currentPage + 1);
      }
    }
  }

  lastButtonReading = reading;
}

// =============== SETUP =============================

void setup() {

  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(0x3C, true)) {

    Serial.println("OLED ERROR");
    while (1);
  }

  display.clearDisplay();
  display.display();

  randomSeed(micros());

  // RTC
  rtcOK = rtc.begin();

  if (rtcOK && !rtc.isrunning()) {

    Serial.println("RTC STOPPED - SETTING COMPILE TIME");

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // DHT11
  dht.begin();

  delay(2000);

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  dhtOK = !isnan(temperature) && !isnan(humidity);

  unsigned long now = millis();

  nextBlink = now + 2000;
  nextLook = now + 1000;
  nextMood = now + random(5000, 9000);

  lastActivity = now;
  lastDHTRead = now;

  idleMode = true;

  Serial.println("SYSTEM READY");
  Serial.println("ROBOT ANIMATION ACTIVE");
}

// =============== MAIN LOOP =========================

void loop() {

  unsigned long now = millis();

  handleButton();

  // Read DHT11 every 2 seconds
  if (now - lastDHTRead >= 2000) {

    lastDHTRead = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

      temperature = t;
      humidity = h;
      dhtOK = true;

    } else {

      dhtOK = false;
    }
  }

  // Return to animation after inactivity
  if (!idleMode &&
      now - lastActivity >= SCREEN_TIMEOUT) {

    idleMode = true;

    mood = NORMAL;
    nextMood = now + 3000;

    Serial.println("IDLE ANIMATION");
  }

  // Robot animation refresh
  if (idleMode && now - lastFrame >= 30) {

    lastFrame = now;
    drawRobotEyes();
  }

  // Information page refresh
  if (!idleMode && now - lastPageUpdate >= 500) {

    lastPageUpdate = now;
    updatePage();
  }
}
