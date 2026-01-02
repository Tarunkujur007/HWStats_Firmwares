#include <LovyanGFX.hpp>
#include <JPEGDEC.h>
#include <WiFi.h>
#include <FS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "Gobold_Thin50pt7b.h"
#include "templar.h"
#include "ezio.h"
#include "AbstergoIndustries.h"

#define DESIGN_ACCENT lgfx::colors::TFT_RED 

// --- LGFX設定 (そのまま) ---
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9488     _panel_instance;
  lgfx::Bus_SPI           _bus_instance;   
  lgfx::Light_PWM         _light_instance;
  // lgfx::Touch_XPT2046     _touch_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI3_HOST;   
      cfg.spi_mode = 3;
      cfg.freq_write = 20000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc   = 9;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { 
      auto cfg = _panel_instance.config();
      cfg.pin_cs           =    10;
      cfg.pin_rst          =    14;
      cfg.pin_busy         =    -1;
      cfg.panel_width      =   320;
      cfg.panel_height     =   480;
      cfg.offset_x         =     0;
      cfg.offset_y         =     0;
      cfg.offset_rotation  =     0;
      cfg.dummy_read_pixel =     8;
      cfg.dummy_read_bits  =     1;
      cfg.readable         =  true;
      cfg.invert           = false;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       =  true; 
      _panel_instance.config(cfg);
    }

    { 
      auto cfg = _light_instance.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    // { 
    //   auto cfg = _touch_instance.config();
    //   cfg.x_min      = 0;
    //   cfg.x_max      = 319;
    //   cfg.y_min      = 0;
    //   cfg.y_max      = 479;
    //   cfg.pin_int    = 17;
    //   cfg.bus_shared = true;
    //   cfg.offset_rotation = 0;
    //   cfg.spi_host = SPI3_HOST;
    //   cfg.freq = 1000000;
    //   cfg.pin_sclk = 12;
    //   cfg.pin_mosi = 11;
    //   cfg.pin_miso = 13;
    //   cfg.pin_cs   =  16;
    //   _touch_instance.config(cfg);
    //   _panel_instance.setTouch(&_touch_instance);
    // }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
JPEGDEC jpeg;
LGFX_Sprite UISprite(&tft);

// --- Global Variables ---
String serialData = "", cpuTemp = "0", cpuLoad = "0", vramTotal = "0", vramUsed = "0";
String ramUsed = "0.0", ramAvail = "0.0", ramTotal = "0.0", meridiem = "AM", day = "01", date = "01";
String  minute = "01", hour = "01", gpuName = "GPU", cpuName = "CPU", customText = "", gameName= "";
int mode = 0, cpuPwr = 0, vramLoad = 0, currentFps = 0, avgFps = 0, volume = 0,virtualMemoryLoad = 0;
int vrmTemp = 0, cpuFanSpeed = 0, cpuFanLoad = 0, gpuFanSpeed = 0, gpuFanLoad = 0, disk0Usage = 0;
int gpuTemp = 0, gpuLoad = 0, gpuPwr = 0, disk1Usage = 0, ramLoad = 0, cpuPumpSpeed = 0;
float cpuClk = 0.0, gpuClk = 0.0, FrameTime = 0.0, download = 0.0, upload = 0.0, cmosVoltage = 0.0;
float ramUsedFloat = 0.0, ramTotalFloat = 0.0, virtualMemoryUsed = 0.0, virtualMemoryAvail = 0.0;

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

// --- CONFIG & FS FUNCTIONS ---

void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, file);
    if (!error) {
      cfg_ssid   = doc["ssid"].as<String>();
      cfg_pass   = doc["pass"].as<String>();
      cfg_city   = doc["city"].as<String>();
      cfg_lat    = doc["lat"].as<String>();
      cfg_lon    = doc["lon"].as<String>();
      cfg_api    = doc["apikey"].as<String>();
      cfg_offset = doc["offset"].as<String>();
      
      // NEW: Load 12hr setting (default to false if missing)
      cfg_12hr   = doc["is12h"] | false; 
    }
    file.close();
  } else {
    cfg_ssid = "";
  }
}

void saveConfigFromSerial(String data) {
  // Format: *offset;city;lat;lon;ssid;pass;api;12h
  data.remove(0, 1); // Remove '*'
  
  int i1 = data.indexOf(';');
  int i2 = data.indexOf(';', i1 + 1);
  int i3 = data.indexOf(';', i2 + 1);
  int i4 = data.indexOf(';', i3 + 1);
  int i5 = data.indexOf(';', i4 + 1);
  int i6 = data.indexOf(';', i5 + 1);
  int i7 = data.indexOf(';', i6 + 1); // NEW: Find 7th semicolon

  if (i6 == -1) return; // Malformed

  cfg_offset = data.substring(0, i1);
  cfg_city   = data.substring(i1 + 1, i2);
  cfg_lat    = data.substring(i2 + 1, i3);
  cfg_lon    = data.substring(i3 + 1, i4);
  cfg_ssid   = data.substring(i4 + 1, i5);
  cfg_pass   = data.substring(i5 + 1, i6);
  
  // Update: API is now between i6 and i7 (or end if i7 is -1 for backward compatibility)
  if (i7 != -1) {
      cfg_api = data.substring(i6 + 1, i7);
      String mode12 = data.substring(i7 + 1);
      cfg_12hr = (mode12 == "1");
  } else {
      cfg_api = data.substring(i6 + 1);
      cfg_12hr = false; // Default if old app used
  }

  // Save to LittleFS
  File file = LittleFS.open("/config.json", "w");
  StaticJsonDocument<1024> doc;
  doc["ssid"] = cfg_ssid;
  doc["pass"] = cfg_pass;
  doc["city"] = cfg_city;
  doc["lat"] = cfg_lat;
  doc["lon"] = cfg_lon;
  doc["apikey"] = cfg_api;
  doc["offset"] = cfg_offset;
  doc["is12h"] = cfg_12hr; // NEW
  
  serializeJson(doc, file);
  file.close();
  
  // Reload and Restart logic (Keep your existing restart code here)
  loadConfig();
  UISprite.fillSprite(TFT_BLACK);
  UISprite.setTextColor(TFT_GREEN);
  UISprite.setTextDatum(textdatum_t::middle_center);
  UISprite.setFont(&FreeSansBold12pt7b);
  UISprite.drawString("CONFIG SAVED!", 160, 200);
  UISprite.pushSprite(0,0);
  delay(2000);
  ESP.restart();
}

// --- NETWORK FUNCTIONS ---

void connectWiFi() {
  if(cfg_ssid.length() == 0) return; 
  
  WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(cfg_offset.toInt(), 0, "pool.ntp.org");
  }
}

String getAQILabel(int aqiIndex) {
    switch(aqiIndex) {
        case 1: return "Good";
        case 2: return "Moderate";
        case 3: return "Sensitive"; // Short for "Unhealthy for Sensitive Groups"
        case 4: return "Unhealthy";
        case 5: return "V.unhealthy";
        case 6: return "Hazardous";
        default: return "Unknown";
    }
}

void fetchWeather() {
  if(WiFi.status() == WL_CONNECTED && cfg_api.length() > 0) {
    HTTPClient http;
    String url = "http://api.weatherapi.com/v1/forecast.json?key=" + cfg_api + 
                 "&q=" + cfg_lat + "," + cfg_lon + 
                 "&days=1&aqi=yes&alerts=no";

    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      StaticJsonDocument<4096> doc; 
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        float t = doc["current"]["temp_c"];
        weatherTemp = String(t, 1) + " C";
        const char* cond = doc["current"]["condition"]["text"];
        weatherDesc = String(cond);
        weatherHum = doc["current"]["humidity"];
        weatherUV  = doc["current"]["uv"];
        
        if (doc["current"].containsKey("air_quality")) {
            weatherAQI = doc["current"]["air_quality"]["us-epa-index"];
        }

        JsonObject today = doc["forecast"]["forecastday"][0]["day"];
        weatherMaxT = today["maxtemp_c"];
        weatherMinT = today["mintemp_c"];
        weatherRainChance = today["daily_chance_of_rain"];
      } 
    } 
    else {
      weatherDesc = "API Error";
    }
    http.end();
  }
}

// --- DRAWING FUNCTIONS ---

void Boot(void) {
    tft.fillScreen(TFT_WHITE);
    tft.setTextColor(TFT_BLACK);
    tft.setFont(&FreeSansBold24pt7b);
    tft.drawString("HWStats", 60, 109);
    tft.drawString("Display", 60, 157);
    
    tft.setFont(&FreeSans12pt7b);
    tft.drawString("build: 1.3.1", 60, 215);
    tft.drawString("Version: ILI9488_SPI", 60, 240);
    tft.drawString("Firmware: OG01A", 60, 265);

    for(int i = 0; i < 260; i ++) { 
      tft.fillRoundRect(30, 313, i, 24, 11, TFT_BLACK);
      delay(5);
    }
}


void drawClockWeather() {
  // --- 1. DATA FETCH & SAFETY CHECKS ---
  if (cfg_ssid.length() == 0) {
     UISprite.fillSprite(TFT_BLACK);
     UISprite.setTextColor(TFT_RED);
    //  UISprite.setTextDatum(textdatum_t::middle_center);
     UISprite.setFont(&FreeSansBold12pt7b);
     UISprite.drawString("Oops ! No config found :(", 5, 184);
     UISprite.drawString("Please flash config by", 5, 244);
     UISprite.drawString("HWStats application to see", 5, 275);
     UISprite.drawString("Internet Clock", 5, 307);

     UISprite.pushSprite(0,0);
     return;
  }

  if (WiFi.status() != WL_CONNECTED) {
     connectWiFi(); 
     if(WiFi.status() == WL_CONNECTED) fetchWeather(); 
  }
  
  static unsigned long lastWeatherUpdate = 0;
  if (millis() - lastWeatherUpdate > 900000) { 
    fetchWeather();
    lastWeatherUpdate = millis();
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return; 


  // --- 2. DRAWING UI (Using your exact coordinates) ---
  
  UISprite.fillSprite(TFT_BLACK); 
  UISprite.setTextColor(TFT_WHITE);
  UISprite.setTextSize(1);
  UISprite.setTextDatum(textdatum_t::top_left); // Default datum

  // --- Layer 2: Time ---
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
  UISprite.setTextDatum(textdatum_t::top_center); // Align text by its top-center point
  UISprite.drawString(timeStr, 160, 130);         // Draw at X=160 (Middle of 320px screen), Y=130
  UISprite.setTextDatum(textdatum_t::top_left);


  // --- Weather clock UI: Graphics (Rectangles) ---
  UISprite.fillRect(95, 33, 58, 58, DESIGN_ACCENT);   // date square
  UISprite.fillRect(178, 84, 25, 25, DESIGN_ACCENT);  // simple square
  UISprite.fillRect(158, 59, 5, 103, DESIGN_ACCENT);  // upper divider
  UISprite.fillRect(158, 244, 5, 76, DESIGN_ACCENT);  // lower divider


  // --- Text: AM/PM & Day Name ---
  UISprite.setFreeFont(&FreeSansBold12pt7b);
  
  // Meridiem
  if (cfg_12hr) {
      String suffix = (timeinfo.tm_hour >= 12) ? "PM" : "AM";
      UISprite.drawString(suffix, 204, 250); 
  }
  
  // Day Name (e.g., "Sat")
  char dayName[10];
  strftime(dayName, 10, "%a", &timeinfo);
  UISprite.drawString(dayName, 175, 116);

  UISprite.setTextSize(2); 
  UISprite.setFreeFont(&FreeSans12pt7b);

  UISprite.drawString(weatherTemp, 175, 283); // Temp
  
  char dayNum[5];
  sprintf(dayNum, "%02d", timeinfo.tm_mday);
  UISprite.drawString(dayNum, 100, 43);       // Date Number


  // --- Text: Right Aligned Data (Month, City, Condition) ---
  UISprite.setTextDatum(textdatum_t::top_right);
  
  // Month
  char monthStr[10];
  strftime(monthStr, 10, "%b", &timeinfo);
  String mStr = String(monthStr);
  mStr.toUpperCase(); 
  UISprite.drawString(mStr, 152, 97);

  UISprite.setTextSize(1); // Reset size for the rest
  
  // Condition & City
  UISprite.drawString(weatherDesc, 152, 245);
  UISprite.drawString(cfg_city, 152, 270);
  
  UISprite.setTextDatum(textdatum_t::top_left); // Reset datum


  // --- Text: Bottom Stats ---
  // AQI
  UISprite.drawString("AQI:", 136, 448);
  UISprite.drawString(getAQILabel(weatherAQI), 190, 448); // Changed to use Label instead of number
  
  // Humidity
  UISprite.drawString("Humidity:", 136, 415);
  UISprite.drawString(String(weatherHum) + "%", 241, 415);
  
  // UV
  UISprite.drawString("UV:", 5, 448);
  UISprite.drawString(String(weatherUV, 0), 50, 448);
  
  // Rain
  UISprite.drawString("Rain:", 5, 415);
  UISprite.drawString(String(weatherRainChance) + "%", 61, 415);
  
  // Min/Max Temps
  UISprite.drawString("max:", 176, 328);
  UISprite.drawString(String(weatherMaxT, 0), 228, 330);
  
  UISprite.drawString("min:", 176, 356);
  UISprite.drawString(String(weatherMinT, 0), 223, 358);


  // --- 3. Final Push ---
  UISprite.pushSprite(0, 0);
}


void parseSerialData(String data) { 
  int start, end;
  start = data.indexOf("~"); 
  end = data.indexOf("|", start);
  mode = data.substring(start + 1, end).toInt();

   // CPU Temp (A)
  start = data.indexOf("A"); 
  end = data.indexOf("|", start);
  cpuTemp = data.substring(start + 1, end);
  
  // CPU Load (B)
  start = data.indexOf("B");
  end = data.indexOf("|", start);
  cpuLoad = data.substring(start + 1, end);
  
  // GPU Temp (C)
  start = data.indexOf("C");
  end = data.indexOf("|", start);
  gpuTemp = data.substring(start + 1, end).toInt();

  // GPU Load (D)
  start = data.indexOf("D");
  end = data.indexOf("|", start);
  gpuLoad = data.substring(start + 1, end).toInt();

  // RAM Used (E)
  start = data.indexOf("E");
  end = data.indexOf("|", start);
  ramUsed = data.substring(start + 1, end);
  ramUsedFloat = ramUsed.toFloat();

  // RAM Total = RAM Avail (F) + RAM Used (E)
  start = data.indexOf("F");
  end = data.indexOf("|", start);
  ramAvail = data.substring(start + 1, end);
  ramTotalFloat = ramAvail.toFloat()+ramUsedFloat;
  ramTotal = String(ramTotalFloat);

  // RAM Load (G)
  start = data.indexOf("G");
  end = data.indexOf("|", start);
  ramLoad = data.substring(start + 1, end).toInt();

  // CPU Power (H)
  start = data.indexOf("H");
  end = data.indexOf("|", start);
  cpuPwr = data.substring(start + 1, end).toInt();

  // CPU Clock in Mhz to Ghz (I)
  start = data.indexOf("I");
  end = data.indexOf("|", start);
  cpuClk = data.substring(start + 1, end).toFloat() / 1000;

  // GPU Power (J)
  start = data.indexOf("J");
  end = data.indexOf("|", start);
  gpuPwr = data.substring(start + 1, end).toInt();

  // GPU Clock in Mhz to Ghz (K)
  start = data.indexOf("K");
  end = data.indexOf("|", start);
  gpuClk = data.substring(start + 1, end).toFloat() / 1000;

  // Vram Total (L)
  start = data.indexOf("L");
  end = data.indexOf("|", start);
  vramTotal = data.substring(start + 1, end);

  // Vram in use (M)
  start = data.indexOf("M");
  end = data.indexOf("|", start);
  vramUsed = data.substring(start + 1, end);

  // Vram Load (N)
  start = data.indexOf("N");
  end = data.indexOf("|", start);
  vramLoad = data.substring(start + 1, end).toInt();

  // Current FPS (O)
  start = data.indexOf("O");
  end = data.indexOf("|", start);
  currentFps = data.substring(start + 1, end).toInt();

  // Avg Fps (P)
  start = data.indexOf("P");
  end = data.indexOf("|", start);
  avgFps = data.substring(start + 1, end).toInt();

  // Frame time (Q)
  start = data.indexOf("Q");
  end = data.indexOf("|", start);
  FrameTime = data.substring(start + 1, end).toFloat();

  // Volume (R)
  start = data.indexOf("R");
  end = data.indexOf("|", start);
  volume = data.substring(start + 1, end).toInt();

  // Download Speed (S)
  start = data.indexOf("S");
  end = data.indexOf("|", start);
  download = data.substring(start + 1, end).toFloat();

  // Upload Speed (T)
  start = data.indexOf("T");
  end = data.indexOf("|", start);
  upload = data.substring(start + 1, end).toFloat();

  // Virtual Memory in use (U)
  start = data.indexOf("U");
  end = data.indexOf("|", start);
  virtualMemoryUsed = data.substring(start + 1, end).toFloat();

  // Virtual Memory available (V)
  start = data.indexOf("V");
  end = data.indexOf("|", start);
  virtualMemoryAvail = data.substring(start + 1, end).toFloat();

  // Virtual Memory load (W)
  start = data.indexOf("W");
  end = data.indexOf("|", start);
  virtualMemoryLoad = data.substring(start + 1, end).toInt();

  // VRM Temperature (X)
  start = data.indexOf("X");
  end = data.indexOf("|", start);
  vrmTemp = data.substring(start + 1, end).toInt();

  // CMOS Voltage (Y)
  start = data.indexOf("Y");
  end = data.indexOf("|", start);
  cmosVoltage = data.substring(start + 1, end).toFloat();

  // CPU Fan Speed (Z)
  start = data.indexOf("Z");
  end = data.indexOf("|", start);
  cpuFanSpeed = data.substring(start + 1, end).toInt();

  // CPU Fan Load (a)
  start = data.indexOf("a");
  end = data.indexOf("|", start);
  cpuFanLoad = data.substring(start + 1, end).toInt();

  // GPU Fan Speed (b)
  start = data.indexOf("b");
  end = data.indexOf("|", start);
  gpuFanSpeed = data.substring(start + 1, end).toInt();

  // GPU Fan Load (c)
  start = data.indexOf("c");
  end = data.indexOf("|", start);
  gpuFanLoad = data.substring(start + 1, end).toInt();

  start = data.indexOf("d");
  end = data.indexOf("|", start);
  cpuPumpSpeed = data.substring(start + 1, end).toInt();

  // CPU Name ($)
  start = data.indexOf("$");
  end = data.indexOf("|", start);
  cpuName = data.substring(start + 1, end);

  // GPU Name (&)
  start = data.indexOf("&");
  end = data.indexOf("|", start);
  gpuName = data.substring(start + 1, end);

  // Disk 0 Used Space
  start = data.indexOf("f0");
  end = data.indexOf("|", start);
  disk0Usage = data.substring(start + 2, end).toInt();

  // Disk 1 Used Space
  start = data.indexOf("f1");
  end = data.indexOf("|", start);
  disk1Usage = data.substring(start + 2, end).toInt();

  // Time (@)
  start = data.indexOf("@");
  end = data.indexOf("|", start);
  if(end > start + 7) {
    hour = data.substring(start + 1, end-6);
    minute = data.substring(start + 4, end-3);
    meridiem = data.substring(start + 7, end);
  }

  // Date (%)
  start = data.indexOf("%");
  end = data.indexOf("|", start);
  day = data.substring(start + 1, start+5);
  date = data.substring(start + 5, end-3);

  // Game name
  start = data.indexOf("^");
  end = data.indexOf("|", start);
  gameName = data.substring(start + 1, end);

  // Custom text
  start = data.indexOf("#");
  end = data.indexOf("|", start);
  customText = data.substring(start + 1, end);
}
void draw0() {
  UISprite.fillSprite(TFT_WHITE);
  UISprite.setTextColor(TFT_BLACK); 

  UISprite.setFont(&Gobold_Thin50pt7b);
  UISprite.drawString(cpuTemp + " C", 0, 5);

  // Black separator
  UISprite.fillRect(0, 153, 320, 8, TFT_BLACK);

  // CPU Name & Load
  UISprite.setTextSize(1);
  UISprite.setFont(&fonts::FreeSansBold12pt7b);
  UISprite.drawString(cpuName, 90, 132);

  // Gpu temp bar
  int gpuTempBar = map(gpuTemp, 0, 100, 0, 300);
  UISprite.fillRect(10, 171, gpuTempBar, 21, TFT_BLACK);

  // GPU Info Labels
  UISprite.drawString(gpuName, 10, 200);
  UISprite.setFont(&fonts::FreeSans12pt7b);
  UISprite.drawString(cpuLoad + "%", 10, 132);
  UISprite.drawString("Usage", 10, 228);
  UISprite.drawString("Temperature", 10, 256);
  UISprite.drawString("Power", 10, 284);

  // Black Bar 2
  UISprite.fillRect(0, 314, 320, 8, TFT_BLACK);

  // RAM Bars
  UISprite.fillRect(10, 352, 150, 26, TFT_BLACK);
  UISprite.drawRect(160, 352, 150, 26, TFT_BLACK);
  
  UISprite.setTextColor(TFT_WHITE);
  UISprite.drawString("Use  "+ ramUsed + "GB", 15, 356); 
  
  UISprite.setTextColor(TFT_BLACK);
  UISprite.drawString("Free  "+ ramAvail + "GB", 165, 356);

  // RAM Info and total
  UISprite.setFont(&fonts::FreeSansBold12pt7b);
  UISprite.drawString("Ram   "+ ramTotal + "GB", 10, 327);
  
  // Logo
  UISprite.drawJpg(AbstergoIndustries,sizeof(AbstergoIndustries), 0,390);

  // GPU Stats Values
  UISprite.setFont(&fonts::FreeSans12pt7b);
  UISprite.setTextDatum(textdatum_t::top_right);
  UISprite.drawNumber(gpuLoad, 290, 228);
  UISprite.drawNumber(gpuTemp, 290, 256);
  UISprite.drawNumber(gpuPwr, 290, 284);

  UISprite.drawString("%", 315, 228); 
  UISprite.drawString("C", 310, 256);
  UISprite.drawString("W", 315, 284);
  UISprite.setTextDatum(textdatum_t::top_left);

  // 2. Push Sprite to Screen
  UISprite.pushSprite(0, 0);
}

void draw1() {
    UISprite.fillSprite(TFT_WHITE); 
    UISprite.setTextColor(TFT_BLACK);
    UISprite.drawJpg(ezio, sizeof(ezio), 140,0);
    UISprite.setFont(&Gobold_Thin50pt7b);
    UISprite.drawNumber(currentFps, 5, -7);

    UISprite.setFreeFont(&FreeSansBold18pt7b);
    UISprite.drawNumber(avgFps, 5, 139);
    UISprite.drawFloat(FrameTime,1, 88, 139);

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
    UISprite.drawString(vramUsed+"GB/"+vramTotal+"GB", 82, 218);

    UISprite.drawString(ramUsed+"GB/"+ramTotal+"GB", 78, 274);
    UISprite.drawString("Ram", 9, 274);
    UISprite.fillRect(0, 420, 320, 60, TFT_BLACK);
    UISprite.setTextWrap(true); // Enable wrapping
    UISprite.setCursor(5,341);
    UISprite.print(gameName);

    UISprite.fillRect(0, 297, 225, 5, TFT_BLACK);
    UISprite.setTextColor(TFT_WHITE);
    UISprite.drawString("CPU", 5, 427);
    UISprite.drawString("GPU", 5, 457);
    UISprite.drawString(cpuTemp+"C", 104, 428);
    UISprite.drawFloat(cpuClk,1, 234, 428);
    UISprite.drawString("Ghz", 270, 428);

    UISprite.drawNumber(gpuTemp, 104, 461);
    UISprite.drawChar('C', 130, 478);
    UISprite.drawFloat(gpuClk,1, 235, 461);
    UISprite.drawString("Ghz", 270, 461);
    UISprite.pushSprite(0, 0);
}
void draw2() {

    UISprite.fillSprite(TFT_WHITE); 
    UISprite.setTextColor(TFT_BLACK); 
    UISprite.setFont(&Gobold_Thin50pt7b); 
    UISprite.drawJpg(templar, sizeof(templar), 110,90);
    
    // Draw Time
    UISprite.drawString(hour, 5, -7);
    UISprite.drawString(minute, 5, 100);

    // Draw AM/PM
    UISprite.setFont(&FreeSansBold24pt7b);
    UISprite.drawString(meridiem, 105, 150);

    // Draw Date
    UISprite.setFont(&FreeSansBold12pt7b);
    UISprite.drawString(day +"|"+ date, 5, 218);
    UISprite.drawString("Upload",5,264);
    UISprite.drawString("Download",5,319);
    
    // Draw Divider Lines (Black)
    UISprite.fillRect(0, 248, 110, 8, TFT_BLACK); 

    // --- Stats Labels ---
    UISprite.setFont(&FreeSans12pt7b);
    UISprite.setTextDatum(textdatum_t::top_right);
    UISprite.drawFloat(upload,1,55,289);
    UISprite.drawFloat(download,1,55,344);
    UISprite.drawString("Mb/s",110, 289);
    UISprite.drawString("Mb/s",110, 344);
    UISprite.setTextDatum(textdatum_t::top_left);

    // --- Bottom custom text ---
    UISprite.fillRect(0, 425, 320, 55, TFT_BLACK);   // Black background rect
    UISprite.setTextColor(TFT_WHITE);                // White Text
    UISprite.setFont(&FreeSansBold12pt7b);
    UISprite.setTextWrap(true); // Enable wrapping
    UISprite.setCursor(0, 432);   // Set starting position
    UISprite.print(customText);

    // PUSH SPRITE TO SCREEN
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
    UISprite.drawString(cpuTemp+"C", 67, 10);  //cpu temp
    UISprite.drawString(cpuLoad+"%", 162, 10);  // cpu load
    UISprite.drawString(ramUsed+"Gb/"+ramTotal+"Gb", 83, 153); //ram usage / total
    UISprite.drawChar('W', 280, 26);    // cpu pwr   w
    UISprite.drawString("Ghz", 259, 40);   // cpu clk GHZ
    UISprite.drawString("Rpm", 122, 40);     // cpu fan speed rpm
    UISprite.drawChar('%', 285, 170);  //ram Load %
    UISprite.drawChar('/', 164, 254);   // virtual mem /
    UISprite.drawString("Gb", 216, 236);   // virtual memory GB
    UISprite.drawChar('%', 295, 253);        // virtual memory %
    UISprite.drawString("D1", 75, 342);           // disk0
    UISprite.drawString("D2", 212, 342);       // disk1
    UISprite.drawChar('%', 154, 358);        // disk 0 usage %
    UISprite.drawChar('%', 295, 358);         // disk 1 usage %
    UISprite.drawChar('%', 100, 441);          // volume %
    UISprite.drawChar('C', 107, 475);          // vrm temp C
    UISprite.drawChar('V', 280, 474);         // cmos voltage   C
    UISprite.drawString("Rpm", 269, 425);        // CPU pump speed RPM

    UISprite.setTextDatum(textdatum_t::top_right);
    UISprite.drawNumber(cpuPwr, 278, 10);  //cpu pwr
    UISprite.drawFloat(cpuClk,1, 257,40);   // cpu clk
    UISprite.drawNumber(cpuFanSpeed, 120, 40);  //cpu fan speed
    UISprite.drawNumber(ramLoad, 283, 153);  //ram Load
    UISprite.drawFloat(virtualMemoryUsed,1, 165, 236); // virtual memory used
    UISprite.drawFloat(virtualMemoryUsed+virtualMemoryAvail,1, 216, 236); // virtual memory total
    UISprite.drawNumber(virtualMemoryLoad, 293, 236);    // virtual memory load
    UISprite.drawNumber(disk0Usage, 152, 342);            // disk0 usage
    UISprite.drawNumber(disk1Usage, 293, 342);          // disk1 usage
    UISprite.drawNumber(volume, 98, 426);              // volume
    UISprite.drawNumber(vrmTemp, 105, 457);              // vrm temp
    UISprite.drawFloat(cmosVoltage,2, 278, 456);           // cmos voltage 
    UISprite.drawNumber(cpuPumpSpeed, 267, 425);            // CPU pump speed 
    UISprite.setTextDatum(textdatum_t::top_left);

    UISprite.setTextColor(TFT_WHITE);
    UISprite.drawString(vramUsed+"Gb/"+vramTotal+"Gb", 82, 195);  //vram
    UISprite.drawChar('C', 108, 95);       // gpu temp C
    UISprite.drawChar('%', 202, 94);      // gpu load %
    UISprite.drawChar('W', 280, 94);         // gpu pwr W
    UISprite.drawString("Ghz", 259, 114);       // gpu clk  Ghz
    UISprite.drawString("Rpm", 122, 114);     // gpu fan speed  RPM
    UISprite.drawChar('%', 289, 211);    // vram Load   %
    UISprite.drawString("Fps", 136, 274);    // current FPS   "FPS"
    UISprite.drawString("Avg", 97, 302);         // avg fps avg
    UISprite.drawNumber(avgFps, 140, 304);         // avg fps
    UISprite.drawString("ms", 283, 275);         // frame time ms
    UISprite.drawString("Mb/s", 138, 384);       // upload speed Mb/s
    UISprite.drawString("Mb/s", 268, 384);       // download speed Mb/s

    UISprite.setTextDatum(textdatum_t::top_right);
    UISprite.drawNumber(gpuTemp, 106, 78);           // gpu temp
    UISprite.drawNumber(gpuLoad, 200, 78);          // gpu load
    UISprite.drawNumber(gpuPwr, 278, 78);          // gpu pwr
    UISprite.drawFloat(gpuClk,1, 257, 114);        // gpu clk
    UISprite.drawNumber(gpuFanSpeed, 120, 114);         // gpu fan speed
    UISprite.drawNumber(vramLoad, 287, 195);           // vram Load
    UISprite.drawNumber(currentFps, 134, 275);            // current FPS
    UISprite.drawFloat(FrameTime,1, 281, 275);        // frame time 
    UISprite.drawFloat(upload,1, 134, 384);        // upload speed
    UISprite.drawFloat(download,1, 263, 384);        // download speed
    UISprite.setTextDatum(textdatum_t::top_left);

    UISprite.drawString("GPU", 5, 94);
    UISprite.drawString("VRAM", 5, 195);
    UISprite.drawString("PERF.", 5, 288);
    UISprite.drawString("NET", 5, 384);
    UISprite.pushSprite(0,0);
}

void draw(void) {
    // Layer 1
    UISprite.fillSprite(TFT_BLACK); 
    UISprite.setTextColor(TFT_WHITE);
    UISprite.setTextSize(1);
    // Layer 2
    UISprite.setFont(&Gobold_Thin50pt7b);
    UISprite.drawString("12:00", 48, 130);     // current time
    // Layer 3
    UISprite.fillRect(95, 33, 58, 58, 0xF206);   // date square
    UISprite.fillRect(178, 84, 25, 25, 0xF206);    // simple square
    UISprite.fillRect(158, 59, 5, 103, 0xF206);    // upper divider
    UISprite.fillRect(158, 244, 5, 76, 0xF206);   //  lower divider

    UISprite.setFreeFont(&FreeSansBold12pt7b);
    UISprite.drawString("AM", 204, 250);     // meridiem
    UISprite.drawString("Sat", 175, 116);    // day

    UISprite.setTextSize(2);
    UISprite.setFreeFont(&FreeSans12pt7b);
    UISprite.drawString("24C", 175, 283);    // weather temp
    UISprite.drawString("24", 100, 43);    //date
    UISprite.setTextDatum(textdatum_t::top_right);
    UISprite.drawString("SEPT", 155, 97);    // month


    UISprite.setTextSize(1);
    UISprite.drawString("Clear", 155, 245);
    UISprite.drawString("Rourkela", 155, 270);
    UISprite.setTextDatum(textdatum_t::top_left);
    UISprite.drawString("AQI:", 7, 415);
    UISprite.drawString("999", 59, 415);
    UISprite.drawString("Humidity:", 156, 416);
    UISprite.drawString("100%", 254, 416);
    UISprite.drawString("UV:", 9, 448);
    UISprite.drawString("999", 55, 446);
    UISprite.drawString("Rain:", 158, 448);
    UISprite.drawString("100%", 217, 449);
    
    UISprite.drawString("max:", 176, 328);
    UISprite.drawString("min:", 178, 356);
    UISprite.drawString("32", 228, 330);
    UISprite.drawString("32", 223, 358);
    UISprite.pushSprite(0,0);
}

// --- SETUP & LOOP ---

void setup() {
  Serial.begin(115200); 

  // Mount FS (Don't print debug text)
  LittleFS.begin(true, "/littlefs", 10, "spiffs");
  
  // Load Config into variables
  loadConfig(); 

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_WHITE);
  
  // Initialize Sprite
  UISprite.setColorDepth(8); 
  UISprite.createSprite(320, 480);
  
  // 1. Show Boot Logo
  Boot();

  // Set time tracker (Starts the 10s wait for Serial)
  lastDataTime = millis();
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("*")) {
      // If user is flashing config, handle it
      saveConfigFromSerial(input);
    } 
    else {
      // Normal PC Data received
      lastDataTime = millis(); // Reset timeout
      
      // If we were in Standby (Clock/Oops screen), wake up immediately
      if (isStandby) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        isStandby = false;
        // Don't need to clear screen, draw0/1/2/3 will overwrite it
      }
      
      parseSerialData(input);
      
      switch (mode) {
        case 0: draw0(); break; 
        case 1: draw1(); break; 
        case 2: draw2(); break; 
        case 3: draw3(); break; 
        default: draw0(); break;
      }
    }
  } 
  else {
    // If no data received for 10 seconds
    if (millis() - lastDataTime > 10000) {
      if (!isStandby) isStandby = true;
      
      // Go to Clock/Weather (handles "No Config" inside)
      drawClockWeather();
    }
  }
}







