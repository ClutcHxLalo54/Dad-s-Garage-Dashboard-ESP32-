// ESP-32
#include <Wire.h>
#include <PCA9557.h>
#include <WiFi.h>
#include <time.h>
#include "gfx_conf.h"

//Weather
#include <HTTPClient.h>
#include <ArduinoJson.h>

PCA9557 Out;

#define TOUCH_ADDR 0x5D
#define SCREEN_W 800
#define SCREEN_H 480

// ===== WIFI FOR REAL CLOCK =====
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* TZ_INFO   = "CST6CDT,M3.2.0/2,M11.1.0/2";

// Your calibrated values
#define RAW_X_MIN 25
#define RAW_X_MAX 766
#define RAW_Y_MIN 44
#define RAW_Y_MAX 467

// ================= SCREEN STATES =================
enum ScreenState {
  SCREEN_HOME,
  SCREEN_CLOCK_WEATHER,
  SCREEN_DRAW,
  SCREEN_FISHING,
  SCREEN_SETTINGS,
  SCREEN_SECRET
};

ScreenState currentScreen = SCREEN_HOME;
bool screenNeedsRedraw = true;
bool wasTouching = false;
unsigned long lastTapTime = 0;
const unsigned long TAP_COOLDOWN = 250;

// ================= THEMES =================
struct Theme {
  const char* name;
  uint16_t bg;
  uint16_t header;
  uint16_t panel;
  uint16_t accent;
  uint16_t text;
  uint16_t subtext;
};

Theme themes[] = {
  { "Dark",   0x0000, 0x03EF, 0x18C3, 0xFD20, TFT_WHITE, TFT_LIGHTGREY },
  { "Light",  0xFFFF, 0x7BEF, 0xEF7D, 0x001F, TFT_BLACK, 0x52AA },
  { "Red",    0x1000, 0x7800, 0x3000, 0xF800, TFT_WHITE, 0xFBEF },
  { "Ocean",  0x0410, 0x03EF, 0x14B9, 0x001F, TFT_WHITE, 0xAEDC },
  { "Sunset", 0x3000, 0xF980, 0xA145, 0xFD20, TFT_WHITE, 0xFBEF },
  { "Forest", 0x0180, 0x02C0, 0x14A2, 0xFFE0, TFT_WHITE, 0xBDF7 },
  { "Ice",    0xCE7F, 0x85FF, 0xB75F, 0x001F, TFT_BLACK, 0x52AA }
};

int currentThemeIndex = 0;

// ================= BUTTON STRUCT =================
struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t fillColor;
  uint16_t textColor;
};

// ================= HOME BUTTONS =================
Button btnClockWeather = { 40, 100, 340, 140, "CLOCK / WEATHER", 0x0410, TFT_WHITE };
Button btnDraw         = { 420, 100, 340, 140, "DRAW",            0x001F, TFT_WHITE };
Button btnFishing      = { 40, 280, 340, 140, "FISHING",         0x7800, TFT_WHITE };
Button btnSettings     = { 420, 280, 340, 140, "SETTINGS",        0x7BE0, TFT_BLACK };

// Shared buttons
Button btnBack         = { 20, 18, 140, 44, "BACK",   TFT_RED,    TFT_WHITE };
Button btnTheme        = { 240, 90,  320, 70, "CHANGE THEME", 0x001F, TFT_WHITE };
Button btnPrev         = { 70, 400, 140, 50, "PREV",  0x001F, TFT_WHITE };
Button btnNext         = { 590, 400, 140, 50, "NEXT", 0x001F, TFT_WHITE };
Button btnTimeMode     = { 240, 180, 320, 70, "MILITARY TIME", 0x7800, TFT_WHITE };
Button btnSecondsMode  = { 240, 270, 320, 70, "SECONDS", 0x03EF, TFT_WHITE };

// Draw buttons
Button btnDrawBack     = { 15, 10, 120, 45, "BACK",  TFT_RED,    TFT_WHITE };
Button btnClear        = { 155, 10, 120, 45, "CLEAR", 0x7800,    TFT_WHITE };
Button btnColor        = { 295, 10, 120, 45, "COLOR", 0x001F,    TFT_WHITE };
Button btnBrush        = { 435, 10, 120, 45, "BRUSH", 0xFD20,    TFT_BLACK };

#define DRAW_BAR_H 70

// ================= DRAW APP STATE =================
uint16_t drawColors[] = { TFT_YELLOW, TFT_CYAN, TFT_GREEN, TFT_RED, TFT_WHITE, TFT_MAGENTA, TFT_ORANGE };
const int drawColorCount = sizeof(drawColors) / sizeof(drawColors[0]);
int drawColorIndex = 0;
uint16_t currentDrawColor = TFT_YELLOW;
int brushSize = 4;

// ================= FISHING APP STATE =================
float fishingWindMph = 0.0;
float fishingGustMph = 0.0;
int fishingHourNow = 12;
int fishingMinuteNow = 0;

String fishingRating = "Checking...";
String fishingReason = "Loading conditions...";

String fishingAction = "WAIT";
String fishingBestWindow = "Checking today's sun times...";
String fishingWindLabel = "Unknown";
String fishingAdvice = "Checking live conditions...";

// ================= CLOCK STATE =================
unsigned long lastClockRefresh = 0;
unsigned long bootMillis = 0;
bool wifiAttempted = false;
bool wifiConnectedForClock = false;
bool use24Hour = false;
bool showSeconds = true;
int lastDisplayedMinute = -1;
int lastDisplayedSecond = -1;

// ================= WEATHER STATE =================
float weatherTempF = 0.0;
String weatherText = "Loading...";
unsigned long lastWeatherUpdate = 0;
bool weatherValid = false;
bool isDaytimeNow = true;

String sunriseISO = "";
String sunsetISO  = "";
int sunriseMinutes = 360;   // default 6:00 AM
int sunsetMinutes  = 1080;  // default 6:00 PM
bool sunTimesValid = false;

// Navarre, Florida
const float WEATHER_LAT = 30.4016;
const float WEATHER_LON = -86.8636;

// ================= Boot Screen =================
void drawBootScreen(const char* line1, const char* line2);
void updateBootProgress(int percent, uint16_t color = TFT_GREEN);
void trySetupClock();
void fetchWeather();
void updateFishingConditions();
const char* weatherCodeToText(int code);

// ================= LOW LEVEL TOUCH =================
bool readReg(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((reg >> 8) & 0xFF);
  Wire.write(reg & 0xFF);

  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)len) != len) return false;

  for (size_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return true;
}

bool writeReg8(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((reg >> 8) & 0xFF);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void touchReset() {
  Wire.begin(19, 20);

  Out.reset();
  Out.setMode(IO_OUTPUT);

  Out.setState(IO0, IO_LOW);
  Out.setState(IO1, IO_LOW);
  delay(20);

  Out.setState(IO0, IO_HIGH);
  delay(100);

  Out.setMode(IO1, IO_INPUT);
  delay(200);
}

int mapClamped(int v, int inMin, int inMax, int outMin, int outMax) {
  int r = map(v, inMin, inMax, outMin, outMax);

  if (outMin < outMax) {
    if (r < outMin) r = outMin;
    if (r > outMax) r = outMax;
  } else {
    if (r > outMin) r = outMin;
    if (r < outMax) r = outMax;
  }

  return r;
}

bool getTouchPoint(int &x, int &y, bool &pressed) {
  uint8_t buf[10];

  if (!readReg(0x814E, buf, 10)) {
    pressed = false;
    return false;
  }

  uint8_t status = buf[0];
  pressed = (status & 0x80) && ((status & 0x0F) > 0);

  if (pressed) {
    uint16_t rawX = buf[2] | (buf[3] << 8);
    uint16_t rawY = buf[4] | (buf[5] << 8);

    x = mapClamped(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_W - 1);
    y = mapClamped(rawY, RAW_Y_MIN, RAW_Y_MAX, 0, SCREEN_H - 1);
  }

  writeReg8(0x814E, 0);
  return true;
}

// ================= UI HELPERS =================
Theme currentTheme() {
  return themes[currentThemeIndex];
}

bool pointInButton(int px, int py, const Button &b) {
  int pad = 12;
  return (px >= b.x - pad && px < (b.x + b.w + pad) &&
          py >= b.y - pad && py < (b.y + b.h + pad));
}

void drawButton(const Button &b, int textSize = 3) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 18, b.fillColor);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 18, TFT_WHITE);

  tft.setFont(nullptr);
  tft.setTextSize(textSize);
  tft.setTextColor(b.textColor, b.fillColor);

  int textW = tft.textWidth(b.label);
  int textH = 8 * textSize;

  int tx = b.x + (b.w - textW) / 2;
  int ty = b.y + (b.h - textH) / 2;

  tft.setCursor(tx, ty);
  tft.print(b.label);
}

void drawHeader(const char* title) {
  Theme th = currentTheme();

  tft.fillRect(0, 0, SCREEN_W, 80, th.header);
  tft.drawLine(0, 79, SCREEN_W, 79, TFT_WHITE);

  tft.setFont(nullptr);
  tft.setTextSize(3);
  tft.setTextColor(th.text, th.header);

  int textW = tft.textWidth(title);
  int textH = 8 * 3;

  int tx = (SCREEN_W - textW) / 2;
  int ty = (80 - textH) / 2;

  tft.setCursor(tx, ty);
  tft.print(title);
}

void drawCenteredText(const char* txt, int y, uint16_t fg, uint16_t bg, int size) {
  tft.setTextSize(size);
  tft.setTextColor(fg, bg);

  int textW = tft.textWidth(txt);
  int tx = (SCREEN_W - textW) / 2;

  tft.setCursor(tx, y);
  tft.print(txt);
}

void fillPanel(int x, int y, int w, int h) {
  Theme th = currentTheme();
  tft.fillRoundRect(x, y, w, h, 16, th.panel);
  tft.drawRoundRect(x, y, w, h, 16, TFT_WHITE);
}

void setScreen(ScreenState s) {
  currentScreen = s;
  screenNeedsRedraw = true;
}

//============= Weather Cases =============
const char* weatherCodeToText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1:
    case 2:
    case 3: return "Partly Cloudy";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing Rain";
    case 71:
    case 73:
    case 75: return "Snow";
    case 77: return "Snow Grains";
    case 80:
    case 81:
    case 82: return "Rain Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Storm / Hail";
    default: return "Unknown";
  }
}

int parseISOTimeToMinutes(const String& isoTime) {
  // expected format: 2026-03-18T06:58
  int tIndex = isoTime.indexOf('T');
  if (tIndex < 0 || isoTime.length() < tIndex + 6) return -1;

  int hh = isoTime.substring(tIndex + 1, tIndex + 3).toInt();
  int mm = isoTime.substring(tIndex + 4, tIndex + 6).toInt();

  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
  return hh * 60 + mm;
}

String minutesTo12HourString(int totalMinutes) {
  int hh = (totalMinutes / 60) % 24;
  int mm = totalMinutes % 60;

  bool pm = hh >= 12;
  int displayHour = hh % 12;
  if (displayHour == 0) displayHour = 12;

  char buf[16];
  sprintf(buf, "%d:%02d %s", displayHour, mm, pm ? "PM" : "AM");
  return String(buf);
}

// ================= CLOCK HELPERS =================
void trySetupClock() {
  if (wifiAttempted) return;
  wifiAttempted = true;

  drawBootScreen("Starting up...", "Preparing WiFi");
  updateBootProgress(10);

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("No WiFi credentials entered.");
    drawBootScreen("No WiFi set", "Using local runtime clock");
    updateBootProgress(100, TFT_YELLOW);
    delay(1000);
    wifiConnectedForClock = false;
    return;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  drawBootScreen("Connecting to WiFi...", WIFI_SSID);
  updateBootProgress(20);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(500);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  int dots = 0;

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");

    dots++;
    int progress = 20 + (dots * 3);
    if (progress > 70) progress = 70;
    updateBootProgress(progress);

    // redraw status text occasionally so it feels alive
    if (dots % 4 == 0) {
      drawBootScreen("Connecting to WiFi...", WIFI_SSID);
      updateBootProgress(progress);
    }
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    drawBootScreen("WiFi connected!", WiFi.localIP().toString().c_str());
    updateBootProgress(80);

    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

    Serial.println("Waiting for NTP time...");
    drawBootScreen("Syncing clock...", "Getting network time");
    updateBootProgress(90);

    struct tm timeinfo;
    unsigned long ntpStart = millis();

    while (!getLocalTime(&timeinfo) && millis() - ntpStart < 10000) {
      Serial.print("*");
      delay(300);
    }
    Serial.println();

      if (getLocalTime(&timeinfo)) {
      Serial.println("Time synced!");
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
      wifiConnectedForClock = true;

      fetchWeather();
      lastWeatherUpdate = millis();

      drawBootScreen("Time synced!", "Loading dashboard...");
      updateBootProgress(100);
      delay(800);
    } else {
      Serial.println("NTP sync failed.");
      wifiConnectedForClock = false;

      drawBootScreen("WiFi OK, time sync failed", "Using fallback clock");
      updateBootProgress(100, TFT_YELLOW);
      delay(1200);
    }

  } else {
    Serial.println("WiFi connection failed.");
    wifiConnectedForClock = false;

    drawBootScreen("WiFi connection failed", "Using fallback clock");
    updateBootProgress(100, TFT_RED);
    delay(1200);
  }
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherValid = false;
    weatherText = "No WiFi";
    fishingRating = "No WiFi";
    fishingReason = "Connect WiFi for live fishing data";
    return;
  }

  HTTPClient http;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LAT, 4) +
               "&longitude=" + String(WEATHER_LON, 4) +
               "&current=temperature_2m,weather_code,wind_speed_10m,wind_gusts_10m,is_day" +
               "&daily=sunrise,sunset" +
               "&temperature_unit=fahrenheit" +
               "&wind_speed_unit=mph" +
               "&timezone=auto";

  Serial.println("Fetching weather/fishing data...");
  Serial.println(url);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      weatherTempF   = doc["current"]["temperature_2m"] | 0.0;
      fishingWindMph = doc["current"]["wind_speed_10m"] | 0.0;
      fishingGustMph = doc["current"]["wind_gusts_10m"] | 0.0;

      int code       = doc["current"]["weather_code"] | -1;
      int isDayInt   = doc["current"]["is_day"] | 1;

      weatherText = weatherCodeToText(code);
      isDaytimeNow = (isDayInt == 1);
      weatherValid = true;

      sunriseISO = "";
      sunsetISO  = "";
      sunTimesValid = false;

      if (doc["daily"]["sunrise"][0].is<const char*>()) {
        sunriseISO = (const char*)doc["daily"]["sunrise"][0];
      }
      if (doc["daily"]["sunset"][0].is<const char*>()) {
        sunsetISO = (const char*)doc["daily"]["sunset"][0];
      }

      int sr = parseISOTimeToMinutes(sunriseISO);
      int ss = parseISOTimeToMinutes(sunsetISO);

      if (sr >= 0 && ss >= 0) {
        sunriseMinutes = sr;
        sunsetMinutes = ss;
        sunTimesValid = true;
      }

      time_t now = time(nullptr);
      if (now > 100000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        fishingHourNow = timeinfo.tm_hour;
        fishingMinuteNow = timeinfo.tm_min;
      } else {
        unsigned long totalSec = (millis() - bootMillis) / 1000;
        fishingHourNow = (totalSec / 3600) % 24;
        fishingMinuteNow = (totalSec / 60) % 60;
      }

      updateFishingConditions();

      Serial.print("Temp F: ");
      Serial.println(weatherTempF);
      Serial.print("Wind mph: ");
      Serial.println(fishingWindMph);
      Serial.print("Gust mph: ");
      Serial.println(fishingGustMph);
      Serial.print("Weather: ");
      Serial.println(weatherText);
      Serial.print("Fishing rating: ");
      Serial.println(fishingRating);
    } else {
      Serial.println("Weather JSON parse failed.");
      weatherValid = false;
      weatherText = "Parse fail";
      fishingRating = "Data Error";
      fishingReason = "Weather data could not be read";
    }
  } else {
    Serial.print("Weather HTTP error: ");
    Serial.println(httpCode);
    weatherValid = false;
    weatherText = "Fetch fail";
    fishingRating = "Fetch Fail";
    fishingReason = "Could not reach weather service";
  }

  http.end();
}

void updateFishingConditions() {
  int score = 0;

  int nowMinutes = fishingHourNow * 60 + fishingMinuteNow;
  int morningStart = sunriseMinutes;
  int morningEnd   = sunriseMinutes + 120;   // 2 hours after sunrise
  int eveningStart = sunsetMinutes - 120;    // 2 hours before sunset
  int eveningEnd   = sunsetMinutes;

  bool morningWindow = false;
  bool eveningWindow = false;
  bool middayWindow  = false;
  bool nightWindow   = false;

  if (sunTimesValid) {
    morningWindow = (nowMinutes >= morningStart && nowMinutes <= morningEnd);
    eveningWindow = (nowMinutes >= eveningStart && nowMinutes <= eveningEnd);
    middayWindow  = (nowMinutes > morningEnd && nowMinutes < eveningStart);
    nightWindow   = (nowMinutes < morningStart || nowMinutes > eveningEnd);
  } else {
    // fallback if sunrise/sunset ever fail
    morningWindow = (fishingHourNow >= 6 && fishingHourNow <= 9);
    eveningWindow = (fishingHourNow >= 17 && fishingHourNow <= 20);
    middayWindow  = (fishingHourNow >= 10 && fishingHourNow <= 16);
    nightWindow   = (fishingHourNow >= 21 || fishingHourNow <= 5);
  }

  // ---------- Time of day ----------
  if (morningWindow || eveningWindow) {
    score += 3;
  } else if (middayWindow) {
    score -= 1;
  } else if (nightWindow) {
    score -= 1;
  }

  // ---------- Wind ----------
  if (fishingWindMph <= 6) {
    score += 4;
    fishingWindLabel = "Light";
  }
  else if (fishingWindMph <= 10) {
    score += 2;
    fishingWindLabel = "Moderate";
  }
  else if (fishingWindMph <= 15) {
    score += 0;
    fishingWindLabel = "Breezy";
  }
  else if (fishingWindMph <= 20) {
    score -= 2;
    fishingWindLabel = "Rough";
  }
  else {
    score -= 4;
    fishingWindLabel = "Very Rough";
  }

  // ---------- Gusts ----------
  if (fishingGustMph >= 25) score -= 2;
  if (fishingGustMph >= 30) score -= 1;

  // ---------- Weather ----------
  if (weatherText == "Clear" || weatherText == "Partly Cloudy") {
    score += 1;
  }
  else if (weatherText == "Rain" || weatherText == "Rain Showers" || weatherText == "Drizzle") {
    score -= 1;
  }
  else if (weatherText == "Thunderstorm" || weatherText == "Storm / Hail") {
    score -= 5;
  }
  else if (weatherText == "Fog") {
    score -= 1;
  }

  // ---------- Final rating ----------
  if (score >= 5) {
    fishingRating = "GOOD";
    fishingAction = "GO NOW";
  }
  else if (score >= 1) {
    fishingRating = "FAIR";
    fishingAction = "OKAY";
  }
  else {
    fishingRating = "BAD";
    fishingAction = "WAIT";
  }

  // ---------- Best window ----------
  if (sunTimesValid) {
    String sunriseStr = minutesTo12HourString(sunriseMinutes);
    String sunsetStr  = minutesTo12HourString(sunsetMinutes);

    if (morningWindow) {
      fishingBestWindow = "Best now: sunrise window until " + minutesTo12HourString(morningEnd);
    }
    else if (eveningWindow) {
      fishingBestWindow = "Best now: sunset window until " + sunsetStr;
    }
    else if (nowMinutes < morningStart) {
      fishingBestWindow = "Next best: around sunrise (" + sunriseStr + ")";
    }
    else if (middayWindow) {
      fishingBestWindow = "Better later: 2 hrs before sunset (" + minutesTo12HourString(eveningStart) + ")";
    }
    else {
      fishingBestWindow = "Next best: tomorrow around sunrise";
    }
  } else {
    if (morningWindow || eveningWindow) {
      fishingBestWindow = "Right now is a better window";
    }
    else if (fishingHourNow < 6) {
      fishingBestWindow = "Best soon: 6-9 AM";
    }
    else if (fishingHourNow < 17) {
      fishingBestWindow = "Better later: 5-8 PM";
    }
    else {
      fishingBestWindow = "Best tomorrow: 6-9 AM";
    }
  }

  // ---------- Reason + advice ----------
  if (fishingRating == "GOOD") {
    if (fishingWindMph <= 10) {
      fishingReason = "Good low-light window with fishable wind";
      fishingAdvice = "Great time for pier, surf, or shoreline fishing.";
    } else {
      fishingReason = "Good feeding window, but some chop";
      fishingAdvice = "Fishable now, just expect rougher water.";
    }
  }
  else if (fishingRating == "FAIR") {
    if (fishingWindMph > 15) {
      fishingReason = "Window is decent, but wind hurts conditions";
      fishingAdvice = "Try protected spots or wait for calmer wind.";
    } else if (middayWindow) {
      fishingReason = "Midday usually slows the bite";
      fishingAdvice = "You can go, but closer to sunset should be better.";
    } else {
      fishingReason = "Conditions are okay, just not prime";
      fishingAdvice = "Worth trying if you already planned to go.";
    }
  }
  else {
    if (weatherText == "Thunderstorm" || weatherText == "Storm / Hail") {
      fishingReason = "Storm conditions are unsafe";
      fishingAdvice = "Do not fish now. Wait until weather clears.";
    } else if (fishingWindMph > 20 || fishingGustMph > 25) {
      fishingReason = "Wind is too rough right now";
      fishingAdvice = "Wait for calmer wind before heading out.";
    } else if (middayWindow) {
      fishingReason = "Slow window and weak overall conditions";
      fishingAdvice = "Try again near sunset or around tomorrow's sunrise.";
    } else {
      fishingReason = "Not an ideal Navarre window right now";
      fishingAdvice = "Check again around sunrise or before sunset.";
    }
  }
}

void getClockStrings(char* line1, char* line2, char* sourceLine) {
  time_t now = time(nullptr);

  if (wifiConnectedForClock && now > 100000) {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (use24Hour) {
      if (showSeconds) {
        strftime(line1, 16, "%H:%M:%S", &timeinfo);
      } else {
        strftime(line1, 16, "%H:%M", &timeinfo);
      }
    } else {
      if (showSeconds) {
        strftime(line1, 16, "%I:%M:%S", &timeinfo);
      } else {
        strftime(line1, 16, "%I:%M", &timeinfo);
      }
    }

    strftime(line2, 40, "%A, %B %d, %Y", &timeinfo);
    strcpy(sourceLine, "NTP time");
  } else {
    unsigned long totalSec = (millis() - bootMillis) / 1000;
    int hh = (totalSec / 3600) % 24;
    int mm = (totalSec / 60) % 60;
    int ss = totalSec % 60;

    if (use24Hour) {
      if (showSeconds) {
        sprintf(line1, "%02d:%02d:%02d", hh, mm, ss);
      } else {
        sprintf(line1, "%02d:%02d", hh, mm);
      }
    } else {
      int displayHour = hh % 12;
      if (displayHour == 0) displayHour = 12;

      if (showSeconds) {
        sprintf(line1, "%02d:%02d:%02d", displayHour, mm, ss);
      } else {
        sprintf(line1, "%02d:%02d", displayHour, mm);
      }
    }

    strcpy(line2, "Sunday March 15, 2026");
    strcpy(sourceLine, "Set WiFi for real date/time");
  }
}

void drawClockWeatherDynamic() {
  Theme th = currentTheme();
  int tw;

  char timeLine[16];
  char dateLine[40];
  char sourceLine[40];
  getClockStrings(timeLine, dateLine, sourceLine);

  // Clear main content area
  tft.fillRect(0, 80, SCREEN_W, SCREEN_H - 80, th.bg);

  // Huge time
  tft.setTextColor(th.text, th.bg);
  tft.setFont(&fonts::FreeMonoBold24pt7b);
  tft.setTextSize(3);

  tw = tft.textWidth(timeLine);
  tft.setCursor((SCREEN_W - tw) / 2, 145);
  tft.print(timeLine);

  // Date
  tft.setTextColor(TFT_GREEN, th.bg);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextSize(1);

  tw = tft.textWidth(dateLine);
  tft.setCursor((SCREEN_W - tw) / 2, 280);
  tft.print(dateLine);

  // Weather
  tft.setTextColor(TFT_YELLOW, th.bg);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextSize(1);

  String weatherLine;
  if (weatherValid) {
    weatherLine = String(weatherTempF, 1) + "F  " + weatherText;
  } else {
    weatherLine = weatherText;
  }

  tw = tft.textWidth(weatherLine.c_str());
  tft.setCursor((SCREEN_W - tw) / 2, 335);
  tft.print(weatherLine);

}

// ================= DRAW APP HELPERS =================
void drawDrawToolbar() {
  Theme th = currentTheme();

  tft.fillRect(0, 0, SCREEN_W, DRAW_BAR_H, th.header);
  tft.drawLine(0, DRAW_BAR_H - 1, SCREEN_W, DRAW_BAR_H - 1, TFT_WHITE);

  drawButton(btnDrawBack, 2);
  drawButton(btnClear, 2);
  drawButton(btnColor, 2);
  drawButton(btnBrush, 2);

  // small status
  tft.fillRoundRect(585, 12, 190, 46, 12, th.panel);
  tft.drawRoundRect(585, 12, 190, 46, 12, TFT_WHITE);

  tft.fillRect(600, 25, 24, 20, currentDrawColor);
  tft.drawRect(600, 25, 24, 20, TFT_WHITE);

  tft.setTextColor(th.text, th.panel);
  tft.setTextSize(2);
  tft.setCursor(635, 24);
  tft.print("B:");
  tft.print(brushSize);
}

void clearDrawCanvas() {
  Theme th = currentTheme();
  tft.fillRect(0, DRAW_BAR_H, SCREEN_W, SCREEN_H - DRAW_BAR_H, th.bg);
}

void cycleDrawColor() {
  drawColorIndex++;
  if (drawColorIndex >= drawColorCount) drawColorIndex = 0;
  currentDrawColor = drawColors[drawColorIndex];
}

void cycleBrushSize() {
  brushSize += 2;
  if (brushSize > 12) brushSize = 2;
}

// ================= FISHING APP HELPERS =================
void drawFishingInfoArea() {
  Theme th = currentTheme();

  tft.fillRoundRect(50, 100, 700, 340, 20, th.panel);
  tft.drawRoundRect(50, 100, 700, 340, 20, TFT_WHITE);

  tft.setFont(nullptr);

  // Title
  tft.setTextSize(3);
  tft.setTextColor(th.text, th.panel);
  int tw = tft.textWidth("Navarre Fishing Outlook (NOW)");
  tft.setCursor((SCREEN_W - tw) / 2, 120);
  tft.print("Navarre Fishing Outlook (NOW)");

  // Main action box
  uint16_t actionColor = TFT_YELLOW;
  if (fishingAction == "GO NOW") actionColor = TFT_GREEN;
  else if (fishingAction == "WAIT") actionColor = TFT_RED;

  tft.fillRoundRect(280, 155, 240, 50, 14, actionColor);
  tft.drawRoundRect(280, 155, 240, 50, 14, TFT_WHITE);

  tft.setTextColor(TFT_BLACK, actionColor);
  tft.setTextSize(3);
  tw = tft.textWidth(fishingAction.c_str());
  tft.setCursor((SCREEN_W - tw) / 2, 170);
  tft.print(fishingAction);

  // Rating
  tft.setTextColor(th.text, th.panel);
  tft.setTextSize(2);
  String line0 = "Rating: " + fishingRating;
  tft.setCursor(95, 225);
  tft.print(line0);

  String line1 = "Temp: " + String(weatherTempF, 1) + " F";
  String line2 = "Weather: " + weatherText;
  String line3 = "Wind: " + String((int)round(fishingWindMph)) + " mph (" + fishingWindLabel + ")";
  String line4 = "Gusts: " + String((int)round(fishingGustMph)) + " mph";

  String sunLine;
  if (sunTimesValid) {
    sunLine = "Sunrise: " + minutesTo12HourString(sunriseMinutes) +
              "   Sunset: " + minutesTo12HourString(sunsetMinutes);
  } else {
    sunLine = "Sunrise / Sunset unavailable";
  }

  String line5 = "Best window: " + fishingBestWindow;

  tft.setCursor(95, 245);
  tft.print(line1);

  tft.setCursor(95, 272);
  tft.print(line2);

  tft.setCursor(95, 299);
  tft.print(line3);

  tft.setCursor(95, 326);
  tft.print(line4);

  tft.setCursor(95, 353);
  tft.print(sunLine);

  tft.setCursor(95, 380);
  tft.print(line5);

  // Reason box
  tft.fillRoundRect(80, 405, 640, 25, 10, th.bg);
  tft.drawRoundRect(80, 405, 640, 25, 10, TFT_WHITE);
  tft.setTextColor(th.text, th.bg);
  tft.setTextSize(2);

  tw = tft.textWidth(fishingReason.c_str());
  tft.setCursor((SCREEN_W - tw) / 2, 411);
  tft.print(fishingReason);

  // Advice box
  tft.fillRoundRect(80, 440, 640, 25, 10, th.bg);
  tft.drawRoundRect(80, 440, 640, 25, 10, TFT_WHITE);

  tw = tft.textWidth(fishingAdvice.c_str());
  tft.setCursor((SCREEN_W - tw) / 2, 446);
  tft.print(fishingAdvice);
}

// ================= SCREEN DRAW FUNCTIONS =================
void drawHomeScreen() {
  Theme th = currentTheme();
  tft.fillScreen(th.bg);

  drawHeader("Dad's Garage");

  drawButton(btnClockWeather, 3);
  drawButton(btnDraw, 4);
  drawButton(btnFishing, 4);
  drawButton(btnSettings, 3);

  tft.setFont(nullptr);
  tft.setTextColor(th.subtext, th.bg);
  tft.setTextSize(3);
  tft.setCursor(18, 440);
  tft.print("Made by Caleb :)");

  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, currentTheme().header);

  // position in top-right corner
  tft.setCursor(730, 20);
  tft.print("><>");
}

void drawClockWeatherScreen() {
  Theme th = currentTheme();
  tft.fillScreen(th.bg);
  drawHeader("Navarre");
  drawButton(btnBack, 2);
  drawClockWeatherDynamic();
}

void drawDrawScreen() {
  Theme th = currentTheme();
  tft.fillScreen(th.bg);
  drawDrawToolbar();
  clearDrawCanvas();

  tft.setFont(nullptr);
  tft.setTextColor(th.subtext, th.bg);
  tft.setTextSize(2);
  tft.setCursor(20, 90);
  tft.print("Made By Caleb :)");
}

void drawSecretScreen() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(4);

  const char* msg = "Love you dad!";

  int tw = tft.textWidth(msg);
  tft.setCursor((SCREEN_W - tw) / 2, SCREEN_H / 2 - 20);
  tft.print(msg);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawFishingScreen() {
  Theme th = currentTheme();
  tft.fillScreen(th.bg);
  drawHeader("Fishing Advisor");
  drawButton(btnBack, 2);
  drawFishingInfoArea();
}

void drawSettingsScreen() {
  Theme th = currentTheme();
  tft.fillScreen(th.bg);
  drawHeader("Settings");
  drawButton(btnBack, 2);
  drawButton(btnTheme, 3);

  Button localTimeBtn = btnTimeMode;
  localTimeBtn.label = use24Hour ? "TIME: 24 HOUR" : "TIME: 12 HOUR";
  drawButton(localTimeBtn, 2);

  Button localSecondsBtn = btnSecondsMode;
  localSecondsBtn.label = showSeconds ? "SECONDS: ON" : "SECONDS: OFF";
  drawButton(localSecondsBtn, 2);

  fillPanel(140, 380, 520, 70);
  tft.setTextColor(th.text, th.panel);
  tft.setTextSize(2);

  String settingsLine = "Theme: ";
  settingsLine += themes[currentThemeIndex].name;
  settingsLine += "   Time: ";
  settingsLine += (use24Hour ? "24H" : "12H");
  settingsLine += "   Sec: ";
  settingsLine += (showSeconds ? "ON" : "OFF");

  int tw = tft.textWidth(settingsLine.c_str());
  tft.setCursor((SCREEN_W - tw) / 2, 405);
  tft.print(settingsLine);
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_HOME:
      drawHomeScreen();
      break;
    case SCREEN_CLOCK_WEATHER:
      drawClockWeatherScreen();
      break;
    case SCREEN_DRAW:
      drawDrawScreen();
      break;
    case SCREEN_FISHING:
      drawFishingScreen();
      break;
    case SCREEN_SETTINGS:
      drawSettingsScreen();
      break;
    case SCREEN_SECRET:
      drawSecretScreen();
      break;
  }
}

// ================= TOUCH HANDLERS =================
void handleHomeTouch(int x, int y) {
  if (pointInButton(x, y, btnClockWeather)) {
    lastDisplayedMinute = -1;
    lastDisplayedSecond = -1;
    setScreen(SCREEN_CLOCK_WEATHER);
  }
  else if (pointInButton(x, y, btnDraw)) {
    setScreen(SCREEN_DRAW);
    wasTouching = true;
  }
  else if (pointInButton(x, y, btnFishing)) {
    setScreen(SCREEN_FISHING);
  }
  else if (pointInButton(x, y, btnSettings)) {
    setScreen(SCREEN_SETTINGS);
  }
  else if (x > 700 && y < 80) {
  setScreen(SCREEN_SECRET);
  }
}

void handleClockWeatherTouch(int x, int y) {
  if (pointInButton(x, y, btnBack)) {
    setScreen(SCREEN_HOME);
  }
}

void handleFishingTouch(int x, int y) {
  if (pointInButton(x, y, btnBack)) {
    setScreen(SCREEN_HOME);
  }
}

void handleSettingsTouch(int x, int y) {
  if (pointInButton(x, y, btnBack)) {
    setScreen(SCREEN_HOME);
  }
  else if (pointInButton(x, y, btnTheme)) {
    currentThemeIndex++;
    if (currentThemeIndex >= (int)(sizeof(themes) / sizeof(themes[0]))) {
      currentThemeIndex = 0;
    }
    screenNeedsRedraw = true;
  }
  else if (pointInButton(x, y, btnTimeMode)) {
    use24Hour = !use24Hour;
    lastDisplayedMinute = -1;
    lastDisplayedSecond = -1;
    lastClockRefresh = 0;
    screenNeedsRedraw = true;
  }
  else if (pointInButton(x, y, btnSecondsMode)) {
    showSeconds = !showSeconds;
    lastDisplayedMinute = -1;
    lastDisplayedSecond = -1;
    lastClockRefresh = 0;
    screenNeedsRedraw = true;
  }
}

void handleDrawTap(int x, int y) {
  if (pointInButton(x, y, btnDrawBack)) {
    setScreen(SCREEN_HOME);
  }
  else if (pointInButton(x, y, btnClear)) {
    drawDrawToolbar();
    clearDrawCanvas();
  }
  else if (pointInButton(x, y, btnColor)) {
    cycleDrawColor();
    drawDrawToolbar();
  }
  else if (pointInButton(x, y, btnBrush)) {
    cycleBrushSize();
    drawDrawToolbar();
  }
}

void handleDrawHold(int x, int y) {
  if (y >= DRAW_BAR_H) {
    tft.fillCircle(x, y, brushSize, currentDrawColor);
  }
}

// ================= PERIODIC SCREEN UPDATES =================
void handleLiveUpdates() {
  if (wifiConnectedForClock && millis() - lastWeatherUpdate >= 600000UL) {
    fetchWeather();
    lastWeatherUpdate = millis();

    if (currentScreen == SCREEN_CLOCK_WEATHER) {
      drawClockWeatherDynamic();
    }
    else if (currentScreen == SCREEN_FISHING) {
      drawFishingInfoArea();
    }
  }

  if (currentScreen == SCREEN_CLOCK_WEATHER) {
    time_t now = time(nullptr);

    if (wifiConnectedForClock && now > 100000) {
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);

      if (showSeconds) {
        if (timeinfo.tm_sec != lastDisplayedSecond) {
          lastDisplayedSecond = timeinfo.tm_sec;
          lastDisplayedMinute = timeinfo.tm_min;
          drawClockWeatherDynamic();
        }
      } else {
        if (timeinfo.tm_min != lastDisplayedMinute) {
          lastDisplayedMinute = timeinfo.tm_min;
          drawClockWeatherDynamic();
        }
      }
    } else {
      // fallback runtime clock if WiFi/NTP is unavailable
      unsigned long totalSec = (millis() - bootMillis) / 1000;
      int mm = (totalSec / 60) % 60;
      int ss = totalSec % 60;

      if (showSeconds) {
        if (ss != lastDisplayedSecond) {
          lastDisplayedSecond = ss;
          lastDisplayedMinute = mm;
          drawClockWeatherDynamic();
        }
      } else {
        if (mm != lastDisplayedMinute) {
          lastDisplayedMinute = mm;
          drawClockWeatherDynamic();
        }
      }
    }
  }

  if (currentScreen == SCREEN_FISHING) {
    if (millis() - lastClockRefresh >= 60000UL) {
      lastClockRefresh = millis();

      time_t now = time(nullptr);
      if (wifiConnectedForClock && now > 100000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        fishingHourNow = timeinfo.tm_hour;
        fishingMinuteNow = timeinfo.tm_min;
      }

      updateFishingConditions();
      drawFishingInfoArea();
    }
  }
}

// ================= Loading Screen =================
void drawBootScreen(const char* line1, const char* line2) {
  tft.fillScreen(TFT_BLACK);

  tft.setFont(nullptr);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(4);

  int w1 = tft.textWidth("Dad's Garage");
  tft.setCursor((SCREEN_W - w1) / 2, 140);
  tft.print("Dad's Garage");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  int w2 = tft.textWidth(line1);
  tft.setCursor((SCREEN_W - w2) / 2, 240);
  tft.print(line1);

  if (strlen(line2) > 0) {
    int w3 = tft.textWidth(line2);
    tft.setCursor((SCREEN_W - w3) / 2, 280);
    tft.print(line2);
  }

  // simple loading bar outline
  tft.drawRoundRect(200, 330, 400, 26, 8, TFT_WHITE);
}

void updateBootProgress(int percent, uint16_t color) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  int fillW = map(percent, 0, 100, 0, 396);
  tft.fillRect(202, 332, 396, 22, TFT_BLACK);
  tft.fillRect(202, 332, fillW, 22, color);

  char pct[10];
  sprintf(pct, "%d%%", percent);

  tft.setFont(nullptr);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int wp = tft.textWidth(pct);
  tft.setCursor((SCREEN_W - wp) / 2, 370);
  tft.print(pct);
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  bootMillis = millis();

  touchReset();

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  currentDrawColor = drawColors[0];
  drawBootScreen("Booting...", "Please wait");
  updateBootProgress(5);

  trySetupClock();

  screenNeedsRedraw = true;
}

void loop() {
  if (screenNeedsRedraw) {
    drawCurrentScreen();
    screenNeedsRedraw = false;
  }

  handleLiveUpdates();

  int x = 0, y = 0;
  bool touching = false;

  if (!getTouchPoint(x, y, touching)) {
    delay(5);
    return;
  }

  if (currentScreen == SCREEN_DRAW) {
  if (touching && !wasTouching) {
    if (millis() - lastTapTime > TAP_COOLDOWN) {
      handleDrawTap(x, y);
      lastTapTime = millis();
    }
  }

  if (touching && y >= DRAW_BAR_H) {
    handleDrawHold(x, y);
  }
  } else {
    if (touching && !wasTouching) {
      if (millis() - lastTapTime > TAP_COOLDOWN) {
        switch (currentScreen) {
          case SCREEN_HOME:
            handleHomeTouch(x, y);
            break;
          case SCREEN_CLOCK_WEATHER:
            handleClockWeatherTouch(x, y);
            break;
          case SCREEN_FISHING:
            handleFishingTouch(x, y);
            break;
          case SCREEN_SETTINGS:
            handleSettingsTouch(x, y);
            break;
          case SCREEN_SECRET:
            setScreen(SCREEN_HOME);
            break;
          default:
            break;
        }
        lastTapTime = millis();
      }
    }
  }

  wasTouching = touching;
  delay(5);
}