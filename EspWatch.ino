#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include "RussoOne18pt4b.h"
#include "MoonPhases7x7.h"
#include "secrets.h"
#include "certs.h"

#define setTimezone(tz) \
  setenv("TZ", tz, 1); \
  tzset()
GFXfont clockFont = RussoOne18pt4b;
GFXfont astralFont = MoonPhases7x7;

//-------------------- PINS
#define BTN_LUP     0
#define CON_TX      1
#define BTN_LDOWN   2
#define CON_RX      3
#define I2C_SCL     4
#define I2C_SDA     5
#define BTN_DOWN   12
#define BTN_UP     13
#define BTN_CENTER 14
#define LED_RGB    15
#define LED_WHITE  16

int buttons[5] = {
  BTN_LDOWN,
  BTN_LUP,
  BTN_DOWN,
  BTN_UP,
  BTN_CENTER
};
const int buttonsLen = sizeof buttons / sizeof buttons[0];
#define FUNC_LUP    0
#define FUNC_LDOWN  1
#define FUNC_DOWN   2
#define FUNC_UP     3
#define FUNC_CENTER 4

#define LED_COUNT 1
Adafruit_NeoPixel strip(LED_COUNT, LED_RGB, NEO_GRB + NEO_KHZ800);

#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ssid = STASSID;
const char* password = STAPSK;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void handleLUpClock();
void handleLDownClock();
void handleDownClock();
void handleUpClock();
void handleCenterClock();
void (*buttonFunctions[5])(){
  handleLUpClock,
  handleLDownClock,
  handleDownClock,
  handleUpClock,
  handleCenterClock
};

String menu[] = {
  " ",
  "Timezones",
  "Weather",
  "IP"
};
#define M_MAIN  0
#define M_TZ    1
#define M_WTH   2
#define M_IP    3
const int menuLen = sizeof(menu) / sizeof(menu[0]);
int menuPointer;
const char* timezones[] = {
  "BRT+3",
  "EST+4",
  "PST+7",
  "UTC+0",
  "CET-1",
  "JST-9",
  "XYZ+0"
};
const char* timezoneNames[] = {
  "Brasilia Time",
  "Eastern Standard Time",
  "Pacific Standard Time",
  "Coordinated Universal Time",
  "Central European Time",
  "Japan Time",
  "Here"
};
const int tzLen = sizeof timezones / sizeof timezones[0];
int tzPointer;

void (*faces[3])(){
  displayClock,
  displayAnalogClock,
  displayZenithClock
};
const int faceLen = sizeof faces / sizeof faces[0];
int facePointer;

// OpenWeatherMap API settings
const char* weatherApiHost = "api.openweathermap.org";
const String weatherApiKey = WEATHER_API_KEY;
const String weatherApiCity = "Camocim";
const String weatherApiCountry = "BR";
const char* googleApiHost = "www.googleapis.com";
const String googleApiKey = GOOGLE_API_KEY;

X509List cert(cert_GTS_Root_R1);

// HTTPClient instance
HTTPClient http;
WiFiClient client;
std::unique_ptr<BearSSL::WiFiClientSecure> clientS(new BearSSL::WiFiClientSecure);

time_t rawtime = 788896740;
struct tm ts;
char buf[80];

String weatherDescription;
String temperature;
String feelsLike;
String humidity;
time_t sunrise;
time_t sunset;
time_t lastWeather;
time_t wTimezone;
time_t lastWeatherTz;

String locationJson = "{}";
double latitude;
double longitude;
double accuracy;

bool noNtp = false;
bool whiteLed = false;
bool otaUpgrade = false;

void initializeHardware() {
  for (int i = 0; i < buttonsLen; i++) {
    pinMode(buttons[i], INPUT_PULLUP);
  }
  pinMode(LED_WHITE, OUTPUT);
  digitalWrite(LED_WHITE, HIGH);
  strip.begin();
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
}

void setup() {
  // Initialize pins, buttons, and sensors
  initializeHardware();
  setRGBLed(0, 0, 255);

  // Connect to WiFi
  connectToWiFi();

  // Initialize OTA updates
  initializeOTA();

  // Initialize the display
  display.begin(i2c_Address, true);  // Address 0x3C default
  display.display();
  display.setTextColor(SH110X_WHITE);

  // Fetch initial time and weather data
  configTime("BRT+3", "pool.ntp.org");
  timeClient.begin();
  timeClient.forceUpdate();
  fetchLocationData();
  fetchWeatherData();
  if (!timeClient.isTimeSet()) {
    timeClient.setTimeOffset(lastWeather);
    noNtp = true;
  }
  clientS->setX509Time(lastWeather);
  delay(1000);
  setRGBLed();
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("ESP-A11V1R15");
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    setRGBLed(255, 0, 0);
    delay(5000);
    ESP.restart();
  }
  clientS->setFingerprint(fingerprint_upload_video_google_com);
  clientS->setTrustAnchors(&cert);
  menu[M_IP] = "IP: " + WiFi.localIP().toString();
}

void initializeOTA() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
    display.clearDisplay();
    menuPointer = M_IP;
    displayMenu();
    display.setTextSize(7);
    display.setCursor(0, 0);
    display.print("OTA");
    display.display();
    otaUpgrade = true;
  });
  ArduinoOTA.onEnd([]() {
    setRGBLed();
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    setRGBLed(255 - (progress / (total / 255.0)), (progress / (total / 255.0)), 0);
    Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    setRGBLed(255, 0, 0, 255);
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
    setRGBLed();
  });
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  if (otaUpgrade) return;
  timeClient.update();
  rawtime = timeClient.getEpochTime();
  if ((String)getenv("TZ") == "XYZ+0")
    rawtime += wTimezone;
  ts = *localtime(&rawtime);
  if (noNtp && timeClient.isTimeSet()) {
    timeClient.setTimeOffset(0);
    noNtp = false;
  }
  display.clearDisplay();
  for (int i = 0; i < buttonsLen; i++) {
    if (digitalRead(buttons[i]) == LOW) buttonFunctions[i]();
  }
  if (menuPointer == M_WTH) {
    displayWeather();
  } else {
    faces[facePointer]();
  }
  displayMenu();
  display.setContrast(0);
  display.display();
}

void fetchWeatherData() {
  setRGBLed(0, 0, 255);
  // Make API request
  String url = "http://" + (String)weatherApiHost + "/data/2.5/weather?q=" + (String)weatherApiCity + "," + (String)weatherApiCountry + "&units=metric&appid=" + (String)weatherApiKey;
  Serial.print("Fetching weather data from: ");
  Serial.println(url);
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      String wD = doc["weather"][0]["description"];
      String t = doc["main"]["temp"];
      String fL = doc["main"]["feels_like"];
      String h = doc["main"]["humidity"];
      int sS = doc["sys"]["sunset"];
      int sR = doc["sys"]["sunrise"];
      int d = doc["dt"];
      int tz = doc["timezone"];
      wD[0] = toupper(wD[0]);
      weatherDescription = wD;
      temperature = t;
      feelsLike = fL;
      humidity = h;
      lastWeather = d;
      wTimezone = tz;
      lastWeatherTz = d + tz;
      sunrise = sR;
      sunset = sS;
    }
    http.end();
  } else {
    Serial.println("Unable to connect to the API");
    setRGBLed(255, 0, 0);
    delay(500);
  }
  setRGBLed();
}

void fetchLocationData() {
  setRGBLed(0, 0, 255);
  // Use the Google Geolocation API to get latitude and longitude
  String locationUrl = "https://" + (String)googleApiHost + "/geolocation/v1/geolocate?key=" + (String)googleApiKey;
  Serial.print("Fetching location data from Google Geolocation API: ");
  Serial.println(locationUrl);

  if (http.begin(*clientS, locationUrl, 443, "", true)) {
    http.addHeader("Content-Length", (String)locationJson.length());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Host", (String)googleApiHost);
    int httpCode = http.POST(locationJson);
    if (httpCode == HTTP_CODE_OK) {
      String locationPayload = http.getString();
      Serial.println("Location API Response:");
      Serial.println(locationPayload);
      DynamicJsonDocument locationDoc(1024);
      deserializeJson(locationDoc, locationPayload);

      // Extract latitude and longitude from the Geolocation API response
      double lat = locationDoc["location"]["lat"];
      double lng = locationDoc["location"]["lng"];
      double acc = locationDoc["accuracy"];
      latitude = lat;
      longitude = lng;
      accuracy = acc;

      // You can store or use these coordinates as needed
    } else {
      Serial.print("Unable to retrieve location data from Google Geolocation API, ");
      Serial.println(httpCode);
      setRGBLed(255, 0, 0);
      delay(500);
    }
    http.end();
  } else {
    Serial.println("Unable to connect to the Google Geolocation API");
    setRGBLed(255, 0, 0);
    delay(500);
  }
  setRGBLed();
}

void displayWeather() {
  if ((String)getenv("TZ") != "XYZ+0")
    ts = *localtime(&lastWeather);
  else
    ts = *localtime(&lastWeatherTz);
  strftime(buf, sizeof(buf), "%d/%m/%y %H:%M", &ts);
  display.setCursor(0, 0);
  display.println("Weather in " + (String)weatherApiCity + ":");
  display.println(weatherDescription);
  display.println("Temperature: " + temperature + "C");
  display.println("Feels like " + feelsLike + "C");
  display.println("Humidity: " + humidity + "%");
  display.print("At ");
  display.println(buf);
  display.println(String(accuracy, 0) + "m from " + String(latitude, 3) + ", " + String(longitude, 3));
}

#define ANALOG_CENTER_X 64
#define ANALOG_CENTER_Y 28
#define ANALOG_RADIUS 25

void displayAnalogClock() {
  int secondsAngle = map(ts.tm_sec, 0, 59, 0, 360);
  int minutesAngle = map(ts.tm_min, 0, 59, 0, 360);
  int hoursAngle = map(ts.tm_hour % 12, 0, 11, 0, 360) + map(ts.tm_min, 0, 59, 0, 30);

  display.setCursor(56, 0);
  display.print("XII");
  display.setCursor(59, 49);
  display.print("VI");
  display.setCursor(88, 25);
  display.print("III");
  display.setCursor(29, 25);
  display.print("IX");
  strftime(buf, sizeof(buf), "%Z", &ts);
  display.setCursor(110, 0);
  if ((String)getenv("TZ") != "XYZ+0")
    display.println(buf);

  for (int h = 0; h <= 11; h++) {
    int hX = ANALOG_CENTER_X + int(ANALOG_RADIUS * 0.9 * cos((30 * h) * DEG_TO_RAD));
    int hY = ANALOG_CENTER_Y + int(ANALOG_RADIUS * 0.9 * sin((30 * h) * DEG_TO_RAD));
    if ((30 * h) % 90 != 0)
      display.drawPixel(hX, hY, SH110X_WHITE);
  }

  int secondsX = ANALOG_CENTER_X + int(ANALOG_RADIUS * 0.8 * cos((secondsAngle - 90) * DEG_TO_RAD));
  int secondsY = ANALOG_CENTER_Y + int(ANALOG_RADIUS * 0.8 * sin((secondsAngle - 90) * DEG_TO_RAD));
  display.drawLine(ANALOG_CENTER_X, ANALOG_CENTER_Y, secondsX, secondsY, SH110X_WHITE);

  int minutesX = ANALOG_CENTER_X + int(ANALOG_RADIUS * 0.7 * cos((minutesAngle - 90) * DEG_TO_RAD));
  int minutesY = ANALOG_CENTER_Y + int(ANALOG_RADIUS * 0.7 * sin((minutesAngle - 90) * DEG_TO_RAD));
  display.drawLine(ANALOG_CENTER_X, ANALOG_CENTER_Y, minutesX, minutesY, SH110X_WHITE);

  int hoursX = ANALOG_CENTER_X + int(ANALOG_RADIUS * 0.5 * cos((hoursAngle - 90) * DEG_TO_RAD));
  int hoursY = ANALOG_CENTER_Y + int(ANALOG_RADIUS * 0.5 * sin((hoursAngle - 90) * DEG_TO_RAD));
  display.drawLine(ANALOG_CENTER_X, ANALOG_CENTER_Y, hoursX, hoursY, SH110X_WHITE);
}

#define ZENITH_CENTER_X 62
#define ZENITH_CENTER_Y 55
#define ZENITH_RADIUS 55

void displayZenithClock() {
  int sunAngle = map(rawtime, sunrise, sunset, 0, 180);

  int sunX = ZENITH_CENTER_X + int(ZENITH_RADIUS * cos((sunAngle - 180) * DEG_TO_RAD));
  float sunHeight = sin((sunAngle - 180) * DEG_TO_RAD);
  int sunY = ZENITH_CENTER_Y + int(ZENITH_RADIUS * sunHeight);
  display.setFont(&astralFont);
  if (sunHeight <= 0) {
    setRGBLed(kToRGB(-sunHeight * 4500 + 2000));  // Sun range is 2000K to 6500K
    display.setCursor(sunX, sunY);
    display.print("/");
  } else {
    display.setCursor(100, 10);
    display.print(moonPhase());
  }
  display.setFont();
  display.fillRect(0, ZENITH_CENTER_Y, SCREEN_WIDTH, 9, SH110X_BLACK);
  display.drawFastHLine(0, ZENITH_CENTER_Y - 1, SCREEN_WIDTH, SH110X_WHITE);
  strftime(buf, sizeof(buf), "%H:%M", &ts);
  display.setCursor(51, 28);
  display.print(buf);
  display.setCursor(45, 36);
  if (rawtime < sunrise) {
    ts = *localtime(&sunrise);
    strftime(buf, sizeof(buf), "<%H:%M", &ts);
    display.print(buf);
  } else if (rawtime < sunset) {
    ts = *localtime(&sunset);
    strftime(buf, sizeof(buf), " %H:%M>", &ts);
    display.print(buf);
  }
}

void displayClock() {
  strftime(buf, sizeof(buf), "%A %d/%m/%Y", &ts);
  display.setCursor(0, 0);
  display.println(buf);
  strftime(buf, sizeof(buf), "%Z", &ts);
  display.setCursor(110, 17);
  if ((String)getenv("TZ") != "XYZ+0")
    display.println(buf);
  if (noNtp) {
    display.setCursor(110, 25);
    display.println("*");
  }
  strftime(buf, sizeof(buf), "%S", &ts);
  display.setCursor(110, 38);
  display.print(buf);
  display.setFont(&clockFont);
  strftime(buf, sizeof(buf), "%H:%M", &ts);
  display.setCursor(6, 44);
  display.print(buf);
  display.setFont(&astralFont);
  display.setCursor(121, -1);
  display.print(moonPhase());
  display.setFont();
}

void displayMenu() {
  display.setCursor(0, 56);
  display.print(menu[menuPointer]);
}

void setRGBLed(int r, int g, int b, int l) {
  strip.clear();
  strip.setBrightness(l);
  strip.setPixelColor(0, r, g, b);
  strip.show();
}

void setRGBLed(int r, int g, int b) {
  setRGBLed(r, g, b, 5);
}

void setRGBLed(int c, int l) {
  strip.clear();
  strip.setBrightness(l);
  strip.setPixelColor(0, c);
  strip.show();
}

void setRGBLed(int c) {
  setRGBLed(c, 5);
}

void setRGBLed() {
  strip.clear();
  strip.show();
}

int lastSecondWhiteLED = 0;
void handleLUpClock() {
  digitalWrite(LED_WHITE, whiteLed ? HIGH : LOW);
  whiteLed = !whiteLed;
}

void handleLDownClock() {
  menuPointer = 0;
  menu[M_TZ] = "Timezones";
  buttonFunctions[FUNC_UP] = handleUpClock;
  buttonFunctions[FUNC_DOWN] = handleDownClock;
  buttonFunctions[FUNC_CENTER] = handleCenterClock;
}

void handleDownClock() {
  menuPointer++;
  if (menuPointer >= menuLen) menuPointer = 0;
}

void handleUpClock() {
  menuPointer--;
  if (menuPointer < 0) menuPointer = menuLen - 1;
}

void handleDownTimezone() {
  tzPointer++;
  if (tzPointer >= tzLen) tzPointer = 0;
  menu[M_TZ] = timezoneNames[tzPointer];
}

void handleUpTimezone() {
  tzPointer--;
  if (tzPointer < 0) tzPointer = tzLen - 1;
  menu[M_TZ] = timezoneNames[tzPointer];
}

void handleCenterTimezone() {
  setTimezone(timezones[tzPointer]);
}

void handleCenterClock() {
  if (menuPointer == M_TZ) {
    menu[M_TZ] = timezoneNames[tzPointer];
    buttonFunctions[FUNC_UP] = handleUpTimezone;
    buttonFunctions[FUNC_DOWN] = handleDownTimezone;
    buttonFunctions[FUNC_CENTER] = handleCenterTimezone;
  } else if (menuPointer == M_WTH) {
    fetchLocationData();
    fetchWeatherData();
  } else if (menuPointer == M_MAIN) {
    facePointer++;
    if (facePointer >= faceLen) facePointer = 0;
    setRGBLed();
  }
}

int kToRGB(int kTemp) {
  int r;
  int g;
  int b;

  kTemp = kTemp / 100;

  if (kTemp <= 66) {
    r = 255;
  } else {
    r = kTemp - 60;
    r = 329.698727466 * pow(r, -0.1332047592);
    if (r < 0) {
      r = 0;
    }
    if (r > 255) {
      r = 255;
    }
  }

  if (kTemp <= 66) {
    g = kTemp;
    g = 99.4708025861 * log(g) - 161.1195681661;
    if (g < 0) {
      g = 0;
    }
    if (g > 255) {
      g = 255;
    }
  } else {
    g = kTemp - 60;
    g = 288.1221695283 * pow(g, -0.0755148492);
    if (g < 0) {
      g = 0;
    }
    if (g > 255) {
      g = 255;
    }
  }

  if (kTemp >= 66) {
    b = 255;
  } else {
    if (kTemp <= 19) {
      b = 0;
    } else {
      b = kTemp - 10;
      b = 138.5177312231 * log(b) - 305.0447927307;
      if (b < 0) {
        b = 0;
      }
      if (b > 255) {
        b = 255;
      }
    }
  }

  int rgb = r * 0x10000 + g * 0x100 + b;
  return rgb;
}

int moonPhase() {
  struct tm tts = *localtime(&rawtime);
  double jd = 0;
  double ed = 0;
  int b = 0;
  jd = julianDate(tts.tm_year + 1900, tts.tm_mon, tts.tm_mday);
  jd = int(jd - 2244116.75); // start at Jan 1 1972
  jd /= 29.53; // divide by the moon cycle
  b = jd;
  jd -= b; // leaves the fractional part of jd
  ed = jd * 29.53; // days elapsed this month
  b = jd * 8 + 0.5;
  b = b & 7;
  return b;
}

double julianDate(int y, int m, int d) {
  int mm, yy;
  double k1, k2, k3;
  double j;
  yy = y - int((12 - m) / 10);
  mm = m + 9;
  if (mm >= 12) {
    mm = mm - 12;
  }
  k1 = 365.25 * (yy + 4172);
  k2 = int((30.6001 * mm) + 0.5);
  k3 = int((((yy / 100) + 4) * 0.75) - 38);
  j = k1 + k2 + d + 59;
  j = j - k3;
  return j;
}