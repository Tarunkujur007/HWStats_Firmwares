#include <LovyanGFX.hpp>
#include <JPEGDEC.h>
#include <WiFi.h>
#include <FS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include "Gobold_Thin50pt7b.h"
#include "templar.h"
#include "ezio.h"
#include "AbstergoIndustries.h"

#define DESIGN_ACCENT lgfx::colors::TFT_RED

// --- LGFX CONFIGURATION ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 3;
      cfg.freq_write = 40000000; // High speed SPI
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 10;
      cfg.pin_mosi = 13;
      cfg.pin_miso = -1;
      cfg.pin_dc = 12;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 14;
      cfg.pin_rst = 21;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 11;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
JPEGDEC jpeg;
LGFX_Sprite UISprite(&tft);
Preferences preferences;

// --- GLOBALS ---
const int BUTTON_PIN = 1;
const int brightnessLevels[] = { 255, 195, 128, 64, 15 };
const int maxLevels = 5;    
int currentLevelIndex = 0;  
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;  

String serialData = "", cpuTemp = "0", cpuLoad = "0", vramTotal = "0", vramUsed = "0";
String ramUsed = "0.0", ramAvail = "0.0", ramTotal = "0.0", meridiem = "AM", day = "01", date = "01";
String minute = "01", hour = "01", gpuName = "GPU", cpuName = "CPU", customText = "", gameName = "";
int mode = 0, lastMode = -1; 
int cpuPwr = 0, vramLoad = 0, currentFps = 0, avgFps = 0, volume = 0, virtualMemoryLoad = 0;
int vrmTemp = 0, cpuFanSpeed = 0, cpuFanLoad = 0, gpuFanSpeed = 0, gpuFanLoad = 0, disk0Usage = 0;
int gpuTemp = 0, gpuLoad = 0, gpuPwr = 0, disk1Usage = 0, ramLoad = 0, cpuPumpSpeed = 0;
float cpuClk = 0.0, gpuClk = 0.0, FrameTime = 0.0, download = 0.0, upload = 0.0, cmosVoltage = 0.0;
float ramUsedFloat = 0.0, ramTotalFloat = 0.0, virtualMemoryUsed = 0.0, virtualMemoryAvail = 0.0;

std::vector<String> slideShowImages;
bool slideShowInit = false;
unsigned long lastSlideShowTime = 0;
int slideShowIndex = -1;
const int slideShowInterval = 5000;

unsigned long lastDataTime = 0;
bool isStandby = false;
String cfg_ssid = "", cfg_pass = "", cfg_city = "", cfg_lat = "", cfg_lon = "", cfg_api = "", cfg_offset = "";
bool cfg_12hr = false;
String weatherTemp = "--";
String weatherDesc = "Loading...";
int weatherHum = 0;
float weatherUV = 0;
int weatherAQI = 0;
float weatherMaxT = 0;
float weatherMinT = 0;
int weatherRainChance = 0;

// --- DRAWING HELPERS ---
int jpegDrawCallback(JPEGDRAW *pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

void drawJpg(const uint8_t *jpgArray, size_t arraySize, int x, int y) {
  if (jpeg.openFLASH((uint8_t *)jpgArray, arraySize, jpegDrawCallback)) {
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    tft.startWrite();
    jpeg.decode(x, y, 0);
    tft.endWrite();
    jpeg.close();
  }
}

// --- VISUAL FEEDBACK FUNCTIONS ---

void showConfigSavedScreen() {
    UISprite.fillSprite(TFT_BLACK);
    UISprite.setTextColor(TFT_GREEN);
    UISprite.setTextDatum(textdatum_t::middle_center);
    UISprite.setFont(&FreeSansBold12pt7b);
    
    // Draw "Success" Icon (Simple Checkmark logic or text)
    UISprite.drawString("CONFIGURATION", 160, 200);
    UISprite.drawString("SAVED!", 160, 240);
    
    UISprite.pushSprite(0, 0);
}

void showConnectingScreen(String ssid) {
    UISprite.fillSprite(TFT_BLACK);
    UISprite.setTextDatum(textdatum_t::middle_center);
    UISprite.setFont(&FreeSansBold12pt7b);

    UISprite.setTextColor(TFT_WHITE);
    UISprite.drawString("Connecting to:", 160, 180);
    
    UISprite.setTextColor(TFT_YELLOW);
    UISprite.drawString(ssid, 160, 220); // Show SSID Name

    UISprite.setTextColor(TFT_LIGHTGREY);
    UISprite.setFont(&FreeSans12pt7b);
    UISprite.drawString("Please wait...", 160, 260);

    UISprite.pushSprite(0, 0);
}

// --- CONFIG & NETWORK ---

void loadConfig() {
  preferences.begin("display-cfg", true);
  cfg_ssid = preferences.getString("ssid", "");
  cfg_pass = preferences.getString("pass", "");
  cfg_city = preferences.getString("city", "New York");
  cfg_lat  = preferences.getString("lat", "40.71");
  cfg_lon  = preferences.getString("lon", "-74.00");
  cfg_api  = preferences.getString("api", "");
  cfg_offset = preferences.getString("offset", "19800"); 
  cfg_12hr = preferences.getBool("is12h", false);
  preferences.end();
}

void saveConfigFromSerial(String data) {
  data.remove(0, 1); // Remove '*'
  int i1 = data.indexOf(';');
  int i2 = data.indexOf(';', i1 + 1);
  int i3 = data.indexOf(';', i2 + 1);
  int i4 = data.indexOf(';', i3 + 1);
  int i5 = data.indexOf(';', i4 + 1);
  int i6 = data.indexOf(';', i5 + 1);
  int i7 = data.indexOf(';', i6 + 1);

  if (i6 == -1) return; 

  cfg_offset = data.substring(0, i1);
  cfg_city = data.substring(i1 + 1, i2);
  cfg_lat = data.substring(i2 + 1, i3);
  cfg_lon = data.substring(i3 + 1, i4);
  cfg_ssid = data.substring(i4 + 1, i5);
  cfg_pass = data.substring(i5 + 1, i6);

  if (i7 != -1) {
    cfg_api = data.substring(i6 + 1, i7);
    String mode12 = data.substring(i7 + 1);
    cfg_12hr = (mode12 == "1");
  } else {
    cfg_api = data.substring(i6 + 1);
    cfg_12hr = false; 
  }

  preferences.begin("display-cfg", false); 
  preferences.putString("ssid", cfg_ssid);
  preferences.putString("pass", cfg_pass);
  preferences.putString("city", cfg_city);
  preferences.putString("lat", cfg_lat);
  preferences.putString("lon", cfg_lon);
  preferences.putString("api", cfg_api);
  preferences.putString("offset", cfg_offset);
  preferences.putBool("is12h", cfg_12hr);
  preferences.end();

  // --- NEW: Visual Feedback ---
  showConfigSavedScreen();
  delay(2000); // Wait 2s so user can read it
  ESP.restart();
}

void connectWiFi() {
  if (cfg_ssid.length() == 0) return;

  // --- NEW: Show Status ---
  showConnectingScreen(cfg_ssid);
  // -----------------------

  WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(cfg_offset.toInt(), 0, "pool.ntp.org");
    
    // Success Message
    UISprite.fillSprite(TFT_BLACK);
    UISprite.setTextColor(TFT_GREEN);
    UISprite.setTextDatum(textdatum_t::middle_center);
    UISprite.setFont(&FreeSansBold12pt7b);
    UISprite.drawString("Connected!", 160, 240);
    UISprite.pushSprite(0, 0);
    delay(1000);
  } else {
    // Fail Message
    UISprite.fillSprite(TFT_BLACK);
    UISprite.setTextColor(TFT_RED);
    UISprite.setTextDatum(textdatum_t::middle_center);
    UISprite.setFont(&FreeSansBold12pt7b);
    UISprite.drawString("Connection Failed!", 160, 240);
    UISprite.pushSprite(0, 0);
    delay(1000);
  }
}

String getAQILabel(int aqiIndex) {
  switch (aqiIndex) {
    case 1: return "Good";
    case 2: return "Moderate";
    case 3: return "Sensitive"; 
    case 4: return "Unhealthy";
    case 5: return "V.unhealthy";
    case 6: return "Hazardous";
    default: return "Unknown";
  }
}

void fetchWeather() {
  if (WiFi.status() == WL_CONNECTED && cfg_api.length() > 0) {
    HTTPClient http;
    String url = "http://api.weatherapi.com/v1/forecast.json?key=" + cfg_api + "&q=" + cfg_lat + "," + cfg_lon + "&days=1&aqi=yes&alerts=no";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      StaticJsonDocument<4096> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        float t = doc["current"]["temp_c"];
        weatherTemp = String(t, 1) + " C";
        const char *cond = doc["current"]["condition"]["text"];
        weatherDesc = String(cond);
        weatherHum = doc["current"]["humidity"];
        weatherUV = doc["current"]["uv"];
        if (doc["current"].containsKey("air_quality")) {
          weatherAQI = doc["current"]["air_quality"]["us-epa-index"];
        }
        JsonObject today = doc["forecast"]["forecastday"][0]["day"];
        weatherMaxT = today["maxtemp_c"];
        weatherMinT = today["mintemp_c"];
        weatherRainChance = today["daily_chance_of_rain"];
      }
    } else {
      weatherDesc = "API Error";
    }
    http.end();
  }
}

// --- BOOT ---
void Boot(void) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setFont(&FreeSansBold24pt7b);
  tft.drawString("HWStats", 60, 109);
  tft.drawString("Display", 60, 157);
  tft.setFont(&FreeSans12pt7b);
  tft.drawString("build: 1.5.0", 60, 215);
  tft.drawString("Version: ILI9488_SPI", 60, 240);
  tft.drawString("Firmware: OG01A", 60, 265);
  for (int i = 0; i < 260; i++) {
    tft.fillRect(30, 313, i, 15, TFT_BLACK);
    delay(2); 
  }
}

// --- SCREENS ---
void drawClockWeather() {
  if (cfg_ssid.length() == 0) {
    UISprite.fillSprite(TFT_BLACK);
    UISprite.setTextColor(TFT_RED);
    UISprite.setFont(&FreeSansBold12pt7b);
    UISprite.drawString("Oops! No config found :(", 5, 184);
    UISprite.drawString("Please flash config by", 5, 244);
    UISprite.drawString("HWStats application to see", 5, 275);
    UISprite.drawString("Internet Clock", 5, 307);
    UISprite.pushSprite(0, 0);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) fetchWeather();
  }
  static unsigned long lastWeatherUpdate = 0;
  if (millis() - lastWeatherUpdate > 900000) {
    fetchWeather();
    lastWeatherUpdate = millis();
  }
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  UISprite.fillSprite(TFT_BLACK);
  UISprite.setTextColor(TFT_WHITE);
  UISprite.setTextSize(1);
  UISprite.setTextDatum(textdatum_t::top_left);
  UISprite.setFont(&Gobold_Thin50pt7b);

  char timeStr[10];
  if (cfg_12hr) {
    int h = timeinfo.tm_hour;
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
    sprintf(timeStr, "%02d:%02d", h, timeinfo.tm_min);
  } else {
    sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }
  UISprite.setTextDatum(textdatum_t::top_center);
  UISprite.drawString(timeStr, 160, 130);
  UISprite.setTextDatum(textdatum_t::top_left);

  UISprite.fillRect(95, 33, 58, 58, DESIGN_ACCENT);
  UISprite.fillRect(178, 84, 25, 25, DESIGN_ACCENT);
  UISprite.fillRect(158, 59, 5, 103, DESIGN_ACCENT);
  UISprite.fillRect(158, 244, 5, 76, DESIGN_ACCENT);

  UISprite.setFreeFont(&FreeSansBold12pt7b);
  if (cfg_12hr) {
    String suffix = (timeinfo.tm_hour >= 12) ? "PM" : "AM";
    UISprite.drawString(suffix, 204, 250);
  }
  char dayName[10];
  strftime(dayName, 10, "%a", &timeinfo);
  UISprite.drawString(dayName, 175, 116);
  UISprite.setTextSize(2);
  UISprite.setFreeFont(&FreeSans12pt7b);
  UISprite.drawString(weatherTemp, 175, 283);
  char dayNum[5];
  sprintf(dayNum, "%02d", timeinfo.tm_mday);
  UISprite.drawString(dayNum, 100, 43);

  UISprite.setTextDatum(textdatum_t::top_right);
  char monthStr[10];
  strftime(monthStr, 10, "%b", &timeinfo);
  String mStr = String(monthStr);
  mStr.toUpperCase();
  UISprite.drawString(mStr, 152, 97);
  UISprite.setTextSize(1);
  UISprite.drawString(weatherDesc, 152, 245);
  UISprite.drawString(cfg_city, 152, 270);
  UISprite.setTextDatum(textdatum_t::top_left);

  UISprite.drawString("AQI:", 136, 448);
  UISprite.drawString(getAQILabel(weatherAQI), 190, 448);
  UISprite.drawString("Humidity:", 136, 415);
  UISprite.drawString(String(weatherHum) + "%", 241, 415);
  UISprite.drawString("UV:", 5, 448);
  UISprite.drawString(String(weatherUV, 0), 50, 448);
  UISprite.drawString("Rain:", 5, 415);
  UISprite.drawString(String(weatherRainChance) + "%", 61, 415);
  UISprite.drawString("max:", 176, 328);
  UISprite.drawString(String(weatherMaxT, 0), 228, 330);
  UISprite.drawString("min:", 176, 356);
  UISprite.drawString(String(weatherMinT, 0), 223, 358);
  UISprite.pushSprite(0, 0);
}

void parseSerialData(String data) {
  int start, end;
  start = data.indexOf("~");
  end = data.indexOf("|", start);
  mode = data.substring(start + 1, end).toInt();

  start = data.indexOf("A"); end = data.indexOf("|", start); cpuTemp = data.substring(start + 1, end);
  start = data.indexOf("B"); end = data.indexOf("|", start); cpuLoad = data.substring(start + 1, end);
  start = data.indexOf("C"); end = data.indexOf("|", start); gpuTemp = data.substring(start + 1, end).toInt();
  start = data.indexOf("D"); end = data.indexOf("|", start); gpuLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("E"); end = data.indexOf("|", start); ramUsed = data.substring(start + 1, end); ramUsedFloat = ramUsed.toFloat();
  start = data.indexOf("F"); end = data.indexOf("|", start); ramAvail = data.substring(start + 1, end);
  ramTotalFloat = ramAvail.toFloat() + ramUsedFloat;
  ramTotal = String(ramTotalFloat);
  start = data.indexOf("G"); end = data.indexOf("|", start); ramLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("H"); end = data.indexOf("|", start); cpuPwr = data.substring(start + 1, end).toInt();
  start = data.indexOf("I"); end = data.indexOf("|", start); cpuClk = data.substring(start + 1, end).toFloat() / 1000;
  start = data.indexOf("J"); end = data.indexOf("|", start); gpuPwr = data.substring(start + 1, end).toInt();
  start = data.indexOf("K"); end = data.indexOf("|", start); gpuClk = data.substring(start + 1, end).toFloat() / 1000;
  start = data.indexOf("L"); end = data.indexOf("|", start); vramTotal = data.substring(start + 1, end);
  start = data.indexOf("M"); end = data.indexOf("|", start); vramUsed = data.substring(start + 1, end);
  start = data.indexOf("N"); end = data.indexOf("|", start); vramLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("O"); end = data.indexOf("|", start); currentFps = data.substring(start + 1, end).toInt();
  start = data.indexOf("P"); end = data.indexOf("|", start); avgFps = data.substring(start + 1, end).toInt();
  start = data.indexOf("Q"); end = data.indexOf("|", start); FrameTime = data.substring(start + 1, end).toFloat();
  start = data.indexOf("R"); end = data.indexOf("|", start); volume = data.substring(start + 1, end).toInt();
  start = data.indexOf("S"); end = data.indexOf("|", start); download = data.substring(start + 1, end).toFloat();
  start = data.indexOf("T"); end = data.indexOf("|", start); upload = data.substring(start + 1, end).toFloat();
  start = data.indexOf("U"); end = data.indexOf("|", start); virtualMemoryUsed = data.substring(start + 1, end).toFloat();
  start = data.indexOf("V"); end = data.indexOf("|", start); virtualMemoryAvail = data.substring(start + 1, end).toFloat();
  start = data.indexOf("W"); end = data.indexOf("|", start); virtualMemoryLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("X"); end = data.indexOf("|", start); vrmTemp = data.substring(start + 1, end).toInt();
  start = data.indexOf("Y"); end = data.indexOf("|", start); cmosVoltage = data.substring(start + 1, end).toFloat();
  start = data.indexOf("Z"); end = data.indexOf("|", start); cpuFanSpeed = data.substring(start + 1, end).toInt();
  start = data.indexOf("a"); end = data.indexOf("|", start); cpuFanLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("b"); end = data.indexOf("|", start); gpuFanSpeed = data.substring(start + 1, end).toInt();
  start = data.indexOf("c"); end = data.indexOf("|", start); gpuFanLoad = data.substring(start + 1, end).toInt();
  start = data.indexOf("d"); end = data.indexOf("|", start); cpuPumpSpeed = data.substring(start + 1, end).toInt();
  start = data.indexOf("$"); end = data.indexOf("|", start); cpuName = data.substring(start + 1, end);
  start = data.indexOf("&"); end = data.indexOf("|", start); gpuName = data.substring(start + 1, end);
  start = data.indexOf("f0"); end = data.indexOf("|", start); disk0Usage = data.substring(start + 2, end).toInt();
  start = data.indexOf("f1"); end = data.indexOf("|", start); disk1Usage = data.substring(start + 2, end).toInt();
  start = data.indexOf("@"); end = data.indexOf("|", start);
  if (end > start + 7) {
    hour = data.substring(start + 1, end - 6);
    minute = data.substring(start + 4, end - 3);
    meridiem = data.substring(start + 7, end);
  }
  start = data.indexOf("%"); end = data.indexOf("|", start); day = data.substring(start + 1, start + 5); date = data.substring(start + 5, end - 3);
  start = data.indexOf("^"); end = data.indexOf("|", start); gameName = data.substring(start + 1, end);
  start = data.indexOf("#"); end = data.indexOf("|", start); customText = data.substring(start + 1, end);
}

void draw0() {
  UISprite.fillSprite(TFT_WHITE);
  UISprite.setTextColor(TFT_BLACK);
  UISprite.setFont(&Gobold_Thin50pt7b);
  UISprite.drawString(cpuTemp + " C", 0, 5);
  UISprite.fillRect(0, 153, 320, 8, TFT_BLACK);
  UISprite.setTextSize(1);
  UISprite.setFont(&fonts::FreeSansBold12pt7b);
  UISprite.drawString(cpuName, 90, 132);
  int gpuTempBar = map(gpuTemp, 0, 100, 0, 300);
  UISprite.fillRect(10, 171, gpuTempBar, 21, TFT_BLACK);
  UISprite.drawString(gpuName, 10, 200);
  UISprite.setFont(&fonts::FreeSans12pt7b);
  UISprite.drawString(cpuLoad + "%", 10, 132);
  UISprite.drawString("Usage", 10, 228);
  UISprite.drawString("Temperature", 10, 256);
  UISprite.drawString("Power", 10, 284);
  UISprite.fillRect(0, 314, 320, 8, TFT_BLACK);
  UISprite.fillRect(10, 352, 150, 26, TFT_BLACK);
  UISprite.drawRect(160, 352, 150, 26, TFT_BLACK);
  UISprite.setTextColor(TFT_WHITE);
  UISprite.drawString("Use  " + ramUsed + "GB", 15, 356);
  UISprite.setTextColor(TFT_BLACK);
  UISprite.drawString("Free  " + ramAvail + "GB", 165, 356);
  UISprite.setFont(&fonts::FreeSansBold12pt7b);
  UISprite.drawString("Ram   " + ramTotal + "GB", 10, 327);
  UISprite.drawJpg(AbstergoIndustries, sizeof(AbstergoIndustries), 0, 390);
  UISprite.setFont(&fonts::FreeSans12pt7b);
  UISprite.setTextDatum(textdatum_t::top_right);
  UISprite.drawNumber(gpuLoad, 290, 228);
  UISprite.drawNumber(gpuTemp, 290, 256);
  UISprite.drawNumber(gpuPwr, 290, 284);
  UISprite.drawString("%", 315, 228);
  UISprite.drawString("C", 310, 256);
  UISprite.drawString("W", 315, 284);
  UISprite.setTextDatum(textdatum_t::top_left);
  UISprite.pushSprite(0, 0);
}
void draw1() {
  UISprite.fillSprite(TFT_WHITE);
  UISprite.setTextColor(TFT_BLACK);
  UISprite.drawJpg(ezio, sizeof(ezio), 140, 0);
  UISprite.setFont(&Gobold_Thin50pt7b);
  UISprite.drawNumber(currentFps, 5, -7);
  UISprite.setFreeFont(&FreeSansBold18pt7b);
  UISprite.drawNumber(avgFps, 5, 139);
  UISprite.drawFloat(FrameTime, 1, 88, 139);
  UISprite.setFreeFont(&FreeSansBold12pt7b);
  UISprite.drawString("AVG", 5, 117);
  UISprite.drawString("F.Time", 88, 117);
  UISprite.drawString("Now Playing:", 5, 309);
  UISprite.fillRect(0, 183, 226, 5, TFT_BLACK);
  int vramBar = map(vramLoad, 0, 100, 0, 200);
  int ramBar = map(ramLoad, 0, 100, 0, 200);
  UISprite.fillRect(5, 192, vramBar, 21, TFT_BLACK);
  UISprite.fillRect(5, 248, ramLoad, 21, TFT_BLACK);
  UISprite.setFreeFont(&FreeSans12pt7b);
  UISprite.drawString("Vram", 5, 218);
  UISprite.drawString(vramUsed + "GB/" + vramTotal + "GB", 82, 218);
  UISprite.drawString(ramUsed + "GB/" + ramTotal + "GB", 78, 274);
  UISprite.drawString("Ram", 9, 274);
  UISprite.fillRect(0, 420, 320, 60, TFT_BLACK);
  UISprite.setTextWrap(true);
  UISprite.setCursor(5, 341);
  UISprite.print(gameName);
  UISprite.fillRect(0, 297, 225, 5, TFT_BLACK);
  UISprite.setTextColor(TFT_WHITE);
  UISprite.drawString("CPU", 5, 427);
  UISprite.drawString("GPU", 5, 457);
  UISprite.drawString(cpuTemp + "C", 104, 428);
  UISprite.drawFloat(cpuClk, 1, 234, 428);
  UISprite.drawString("Ghz", 270, 428);
  UISprite.drawNumber(gpuTemp, 104, 461);
  UISprite.drawChar('C', 130, 478);
  UISprite.drawFloat(gpuClk, 1, 235, 461);
  UISprite.drawString("Ghz", 270, 461);
  UISprite.pushSprite(0, 0);
}
void draw2() {
  UISprite.fillSprite(TFT_WHITE);
  UISprite.setTextColor(TFT_BLACK);
  UISprite.setFont(&Gobold_Thin50pt7b);
  UISprite.drawJpg(templar, sizeof(templar), 110, 90);
  UISprite.drawString(hour, 5, -7);
  UISprite.drawString(minute, 5, 100);
  UISprite.setFont(&FreeSansBold24pt7b);
  UISprite.drawString(meridiem, 105, 150);
  UISprite.setFont(&FreeSansBold12pt7b);
  UISprite.drawString(day + "|" + date, 5, 218);
  UISprite.drawString("Upload", 5, 264);
  UISprite.drawString("Download", 5, 319);
  UISprite.fillRect(0, 248, 110, 8, TFT_BLACK);
  UISprite.setFont(&FreeSans12pt7b);
  UISprite.setTextDatum(textdatum_t::top_right);
  UISprite.drawFloat(upload, 1, 55, 289);
  UISprite.drawFloat(download, 1, 55, 344);
  UISprite.drawString("Mb/s", 110, 289);
  UISprite.drawString("Mb/s", 110, 344);
  UISprite.setTextDatum(textdatum_t::top_left);
  UISprite.fillRect(0, 425, 320, 55, TFT_BLACK);
  UISprite.setTextColor(TFT_WHITE);
  UISprite.setFont(&FreeSansBold12pt7b);
  UISprite.setTextWrap(true);
  UISprite.setCursor(0, 432);
  UISprite.print(customText);
  UISprite.pushSprite(0, 0);
}
void draw3() {
  UISprite.fillSprite(TFT_WHITE);
  UISprite.fillRect(0, 68, 320, 75, 0x0);
  UISprite.fillRect(0, 182, 320, 45, 0x0);
  UISprite.fillRect(0, 265, 320, 66, 0x0);
  UISprite.fillRect(0, 371, 320, 45, 0x0);
  UISprite.setTextColor(TFT_BLACK);
  UISprite.setFreeFont(&FreeSansBold12pt7b);
  UISprite.drawString("CPU", 5, 26);
  UISprite.drawString("RAM", 5, 153);
  UISprite.drawString("VIRTUAL", 5, 236);
  UISprite.drawString("DISK", 5, 342);
  UISprite.drawString("VOL", 5, 426);
  UISprite.drawString("VRM", 5, 457);
  UISprite.drawString("PUMP", 144, 426);
  UISprite.drawString("CMOS", 143, 457);
  UISprite.setFreeFont(&FreeSans12pt7b);
  UISprite.drawString(cpuTemp + "C", 67, 10);
  UISprite.drawString(cpuLoad + "%", 162, 10);
  UISprite.drawString(ramUsed + "Gb/" + ramTotal + "Gb", 83, 153);
  UISprite.drawChar('W', 280, 26);
  UISprite.drawString("Ghz", 259, 40);
  UISprite.drawString("Rpm", 122, 40);
  UISprite.drawChar('%', 285, 170);
  UISprite.drawChar('/', 164, 254);
  UISprite.drawString("Gb", 216, 236);
  UISprite.drawChar('%', 295, 253);
  UISprite.drawString("D1", 75, 342);
  UISprite.drawString("D2", 212, 342);
  UISprite.drawChar('%', 154, 358);
  UISprite.drawChar('%', 295, 358);
  UISprite.drawChar('%', 100, 441);
  UISprite.drawChar('C', 107, 475);
  UISprite.drawChar('V', 280, 474);
  UISprite.drawString("Rpm", 269, 425);
  UISprite.setTextDatum(textdatum_t::top_right);
  UISprite.drawNumber(cpuPwr, 278, 10);
  UISprite.drawFloat(cpuClk, 1, 257, 40);
  UISprite.drawNumber(cpuFanSpeed, 120, 40);
  UISprite.drawNumber(ramLoad, 283, 153);
  UISprite.drawFloat(virtualMemoryUsed, 1, 165, 236);
  UISprite.drawFloat(virtualMemoryUsed + virtualMemoryAvail, 1, 216, 236);
  UISprite.drawNumber(virtualMemoryLoad, 293, 236);
  UISprite.drawNumber(disk0Usage, 152, 342);
  UISprite.drawNumber(disk1Usage, 293, 342);
  UISprite.drawNumber(volume, 98, 426);
  UISprite.drawNumber(vrmTemp, 105, 457);
  UISprite.drawFloat(cmosVoltage, 2, 278, 456);
  UISprite.drawNumber(cpuPumpSpeed, 267, 425);
  UISprite.setTextDatum(textdatum_t::top_left);
  UISprite.setTextColor(TFT_WHITE);
  UISprite.drawString(vramUsed + "Gb/" + vramTotal + "Gb", 82, 195);
  UISprite.drawChar('C', 108, 95);
  UISprite.drawChar('%', 202, 94);
  UISprite.drawChar('W', 280, 94);
  UISprite.drawString("Ghz", 259, 114);
  UISprite.drawString("Rpm", 122, 114);
  UISprite.drawChar('%', 289, 211);
  UISprite.drawString("Fps", 136, 274);
  UISprite.drawString("Avg", 97, 302);
  UISprite.drawNumber(avgFps, 140, 304);
  UISprite.drawString("ms", 283, 275);
  UISprite.drawString("Mb/s", 138, 384);
  UISprite.drawString("Mb/s", 268, 384);
  UISprite.setTextDatum(textdatum_t::top_right);
  UISprite.drawNumber(gpuTemp, 106, 78);
  UISprite.drawNumber(gpuLoad, 200, 78);
  UISprite.drawNumber(gpuPwr, 278, 78);
  UISprite.drawFloat(gpuClk, 1, 257, 114);
  UISprite.drawNumber(gpuFanSpeed, 120, 114);
  UISprite.drawNumber(vramLoad, 287, 195);
  UISprite.drawNumber(currentFps, 134, 275);
  UISprite.drawFloat(FrameTime, 1, 281, 275);
  UISprite.drawFloat(upload, 1, 134, 384);
  UISprite.drawFloat(download, 1, 263, 384);
  UISprite.setTextDatum(textdatum_t::top_left);
  UISprite.drawString("GPU", 5, 94);
  UISprite.drawString("VRAM", 5, 195);
  UISprite.drawString("PERF.", 5, 288);
  UISprite.drawString("NET", 5, 384);
  UISprite.pushSprite(0, 0);
}

void draw4() {
  // MEMORY MANAGEMENT: Delete Sprite to free RAM for images
  UISprite.deleteSprite();

  // 1. Initialize & Scan
  if (!slideShowInit || slideShowImages.size() == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.setFont(&FreeSansBold12pt7b);
    tft.drawString("Scanning...", 160, 200);

    slideShowImages.clear();
    
    File root = LittleFS.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        String fname = file.name();
        delay(1); 
        if (fname.endsWith(".jpg") || fname.endsWith(".jpeg") || fname.endsWith(".JPG")) {
          if (!fname.startsWith("/")) fname = "/" + fname;
          slideShowImages.push_back(fname);
        }
        file = root.openNextFile();
      }
    }
    slideShowInit = true;
    slideShowIndex = -1; 
    lastSlideShowTime = 0; 
  }

  // 2. Handle No Images
  if (slideShowImages.size() == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("No Images Found", 160, 220);
    return;
  }

  // 3. Slideshow Logic
  if (millis() - lastSlideShowTime > slideShowInterval || slideShowIndex == -1) {
    slideShowIndex++;
    if (slideShowIndex >= slideShowImages.size()) slideShowIndex = 0;

    String imgPath = slideShowImages[slideShowIndex];
    
    if (LittleFS.exists(imgPath)) {
        File file = LittleFS.open(imgPath, "r");
        if (file) {
           size_t len = file.size();
           // Try PSRAM first, fallback to internal RAM
           uint8_t *buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
           if (!buf) buf = (uint8_t*)malloc(len);

           if (buf) {
             file.read(buf, len);
             tft.drawJpg(buf, len, 0, 0);
             free(buf);
           } else {
             tft.fillScreen(TFT_RED);
             tft.setTextColor(TFT_WHITE);
             tft.drawString("Image Too Large!", 160, 200);
           }
           file.close();
        }
    }
    lastSlideShowTime = millis();
  }
}

// --- HANDLERS ---
void handleFileUpload(String input) {
  UISprite.deleteSprite(); 

  int colonIndex = input.lastIndexOf(':');
  if (colonIndex == -1) { UISprite.createSprite(320, 480); return; }

  String fileName = input.substring(1, colonIndex); 
  if (!fileName.startsWith("/")) fileName = "/" + fileName;
  long fileSize = input.substring(colonIndex + 1).toInt();
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("Uploading...", 160, 200);
  tft.drawString(fileName, 160, 240);

  File file = LittleFS.open(fileName, "w");
  if (!file) {
    UISprite.createSprite(320, 480); return;
  }

  Serial.println("READY");

  long bytesRead = 0;
  unsigned long timeout = millis();
  uint8_t buf[1024]; 

  while (bytesRead < fileSize) {
    if (Serial.available()) {
      int len = Serial.readBytes(buf, min((long)sizeof(buf), fileSize - bytesRead));
      file.write(buf, len);
      bytesRead += len;
      timeout = millis(); 
      Serial.println("NEXT"); // HANDSHAKE

      // Progress Bar
      int percent = (bytesRead * 100) / fileSize;
      tft.fillRect(0, 270, 320, 5, TFT_DARKGREY);
      tft.fillRect(0, 270, map(percent, 0, 100, 0, 320), 5, TFT_GREEN);
    }
    if (millis() - timeout > 10000) {
      file.close();
      LittleFS.remove(fileName); 
      Serial.println("TIMEOUT");
      ESP.restart(); return;
    }
  }

  file.close();
  Serial.println("DONE"); 
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("Success!", 160, 240);
  delay(500);
  
  slideShowInit = false; 
}

void handleDeleteAll() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&FreeSansBold12pt7b);
  tft.drawString("Deleting...", 160, 220);
  
  LittleFS.format();
  slideShowImages.clear();
  slideShowInit = false;

  Serial.println("DONE");
  tft.fillScreen(TFT_GREEN);
  tft.drawString("Wiped!", 160, 240);
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(921600); // HIGH SPEED
  Serial.setRxBufferSize(4096); 
  
  LittleFS.begin(true, "/littlefs", 10, "spiffs");
  loadConfig();

  tft.init();
  tft.setRotation(0);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  preferences.begin("display-cfg", true); 
  currentLevelIndex = preferences.getInt("b_index", 0); 
  preferences.end();
  tft.setBrightness(brightnessLevels[currentLevelIndex]);
  tft.fillScreen(TFT_WHITE);

  UISprite.setColorDepth(8);
  UISprite.createSprite(320, 480);
  Boot();
  lastDataTime = millis();
}

void loop() {
  // Button
  if (digitalRead(BUTTON_PIN) == LOW) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      currentLevelIndex++;
      if (currentLevelIndex >= maxLevels) currentLevelIndex = 0;
      tft.setBrightness(brightnessLevels[currentLevelIndex]);
      preferences.begin("display-cfg", false); 
      preferences.putInt("b_index", currentLevelIndex);
      preferences.end(); 
      lastDebounceTime = millis();
    }
  }

  if (Serial.available() > 0) {
    char c = Serial.peek(); 

    // UPLOAD
    if (c == '`') { 
       String input = Serial.readStringUntil('\n');
       input.trim();
       handleFileUpload(input);
       return; 
    } 
    // COMMANDS
    else if (c == 'd') {
       String input = Serial.readStringUntil('\n');
       input.trim();
       if (input == "del:all") { handleDeleteAll(); return; }
       parseSerialData(input); 
    }
    else if (c == '*') { 
       String input = Serial.readStringUntil('\n');
       input.trim();
       saveConfigFromSerial(input);
       return;
    }
    
    // STATS
    String input = Serial.readStringUntil('\n');
    input.trim();
    lastDataTime = millis();  
    if (isStandby) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      isStandby = false;
    }
    parseSerialData(input);

    // --- MEMORY MANAGEMENT ---
    if (mode != lastMode) {
      if (mode == 4) {
         UISprite.deleteSprite();
         slideShowInit = false; 
      } 
      else if (lastMode == 4 || lastMode == -1) {
         UISprite.deleteSprite();
         UISprite.createSprite(320, 480);
      }
      lastMode = mode;
    }

    switch (mode) {
      case 0: draw0(); break;
      case 1: draw1(); break;
      case 2: draw2(); break;
      case 3: draw3(); break;
      case 4: draw4(); break;
      default: draw0(); break;
    }
  } else {
    // STANDBY
    if (millis() - lastDataTime > 10000) {
      if (!isStandby) {
         isStandby = true;
         if (mode == 4) { UISprite.createSprite(320, 480); mode = 0; lastMode = 0; }
      }
      drawClockWeather();
    }
  }
}