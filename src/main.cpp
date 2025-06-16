/*
  ESP32-WROOM Home Automation System - Enhanced
  Features:
  - Static IP configuration via config.ini
  - Multi-room, multi-relay control (8 channels)
  - Modern responsive web dashboard
  - Advanced authentication with session tokens
  - Sensor data logging (temperature, humidity)
  - Scene management (preset configurations)
  - Voice assistant integration (Jarvis API)
  - EEPROM storage for configurations
  - OTA update capability
  - Comprehensive logging system
  - Birthday reminder feature
  - Enhanced security with password reset
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Update.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <TimeAlarms.h> 
#include "esp_task_wdt.h"


// ----------- DEBUG SETTINGS -----------
// Change DEBUG_LEVEL to control verbosity:
// 0 = No debug output
// 1 = Error messages only
// 2 = Errors and warnings
// 3 = Errors, warnings, and info
// 4 = Verbose (all messages)
#define DEBUG_LEVEL 0

// Component-specific debugging flags
#define DEBUG_WIFI false       // WiFi connection debugging
#define DEBUG_AUTH false  // Disable just auth debugging
#define DEBUG_SENSORS false    // Sensor reading debugging
#define DEBUG_RELAYS false     // Relay operation debugging
#define DEBUG_HTTP false       // HTTP server requests debugging
#define DEBUG_FILESYSTEM false // SPIFFS file operations debugging


// Colors for better readability in Serial Monitor
#define DEBUG_COLOR_ERROR "\033[31m"    // Red
#define DEBUG_COLOR_WARNING "\033[33m"  // Yellow
#define DEBUG_COLOR_INFO "\033[36m"     // Cyan
#define DEBUG_COLOR_VERBOSE "\033[32m"  // Green
#define DEBUG_COLOR_RESET "\033[0m"     // Reset to default
#define TEMP_HISTORY_SIZE 7 


// Debug print macros
#if DEBUG_LEVEL > 0
  #define DEBUG_ERROR(comp, msg) if(DEBUG_##comp) { Serial.print(DEBUG_COLOR_ERROR "[ERROR]["); Serial.print(#comp); Serial.print("] "); Serial.print(msg); Serial.println(DEBUG_COLOR_RESET); }
#else
  #define DEBUG_ERROR(comp, msg)
#endif

#if DEBUG_LEVEL > 1
  #define DEBUG_WARN(comp, msg) if(DEBUG_##comp) { Serial.print(DEBUG_COLOR_WARNING "[WARN]["); Serial.print(#comp); Serial.print("] "); Serial.print(msg); Serial.println(DEBUG_COLOR_RESET); }
#else
  #define DEBUG_WARN(comp, msg)
#endif

#if DEBUG_LEVEL > 2
  #define DEBUG_INFO(comp, msg) if(DEBUG_##comp) { Serial.print(DEBUG_COLOR_INFO "[INFO]["); Serial.print(#comp); Serial.print("] "); Serial.print(msg); Serial.println(DEBUG_COLOR_RESET); }
#else
  #define DEBUG_INFO(comp, msg)
#endif

#if DEBUG_LEVEL > 3
  #define DEBUG_VERBOSE(comp, msg) if(DEBUG_##comp) { Serial.print(DEBUG_COLOR_VERBOSE "[VERBOSE]["); Serial.print(#comp); Serial.print("] "); Serial.print(msg); Serial.println(DEBUG_COLOR_RESET); }
#else
  #define DEBUG_VERBOSE(comp, msg)
#endif

// Debug function for dumping binary data (like cookies)
#if DEBUG_LEVEL > 0
  void debugHexDump(const char* title, const uint8_t* data, size_t length) {
    if (!data || length == 0) return;
    Serial.printf("\n%s [%u bytes]:\n", title, length);
    for (size_t i = 0; i < length; i++) {
      Serial.printf("%02X ", data[i]);
      if ((i + 1) % 16 == 0) Serial.println();
    }
    Serial.println();
  }
#else
  #define debugHexDump(title, data, length)
#endif

// ----------- END DEBUG SETTINGS -----------

// ----------- CONFIGURABLE SECTION -----------

// --- Pin Definitions ---
#define RELAY_COUNT 8
const int relayPins[RELAY_COUNT] = {2, 15, 16, 17, 18, 19, 21, 22};
#define DHTPIN 4
#define DHTTYPE DHT11
#define STATUS_LED 23
#define BUTTON_PIN 5  // For physical button control

#define MAX_FW_SIZE 1572864 // 1.5 MB for OTA updates

// --- EEPROM ---
#define EEPROM_SIZE 512
#define USERNAME_ADDR 0
#define PASSWORD_ADDR 32
#define RELAY_ADDR 64
#define BIRTHDAY_ADDR 96
#define SCENE_ADDR 128

// --- Room/Relay Mapping ---
const char* roomNames[] = {"Living Room", "Bedroom", "Kitchen", "Bathroom", "Garage", "Porch", "Study", "Spare"};
const int roomRelayMap[RELAY_COUNT] = {0, 0, 1, 2, 3, 4, 5, 6}; // relay i -> room index

// --- Scene Definitions ---
struct Scene {
  const char* name;
  const char* icon;
  bool relayStates[RELAY_COUNT];
};

Scene scenes[] = {
  {"Good Night", "nights_stay", {false, true, false, false, false, true, false, false}},
  {"Good Morning", "wb_sunny", {true, false, true, false, false, false, false, false}},
  {"Movie Mode", "movie", {false, false, false, false, false, false, true, false}},
  {"Away Mode", "not_listed_location", {false, false, false, false, true, false, false, false}},
  {"All On", "power", {true, true, true, true, true, true, true, true}},
  {"All Off", "power_off", {false, false, false, false, false, false, false, false}}
};
const int SCENE_COUNT = sizeof(scenes)/sizeof(scenes[0]);

// --- Sensor Configuration ---
#define MAX_DATA_POINTS 3000
#define SENSOR_READ_INTERVAL 5000  // 5 seconds

// --- Time Configuration ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // GMT+5:30
const int daylightOffset_sec = 0;

// ----------- END CONFIGURABLE SECTION -----------

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiClientSecure client;


#define MAX_ROUTINES 10

// Global Variables
String savedUsername, savedPassword, savedBirthday, sessionToken = "";
bool relayStates[RELAY_COUNT] = {false};
String logBuffer = "";
float currentTemp = NAN, currentHum = NAN;
unsigned long lastSensorRead = 0;
bool buttonPressed = false;
unsigned long lastButtonPress = 0;
int statusHistoryCount = 0;
float dailyTempAverage[TEMP_HISTORY_SIZE] = {0};
float dailyHumAverage[TEMP_HISTORY_SIZE] = {0};
int currentDayIndex = 0;
unsigned long lastDayUpdate = 0;
float tempSum = 0;
float humSum = 0;
int sampleCount = 0;

// Forward declarations for security and event functions
bool isIPBlocked(const String& ip);
void recordLoginAttempt(const String& ip, bool success);
void recordRelayEvent(int relayNum, bool state, const String& source);
void setupSchedules();
void checkSchedules();
void checkSchedules();
void addLog(const String& entry);
void saveSchedulesToEEPROM();

// ----------- Helper Functions -----------
struct Routine {
  String name;
  String time; // "HH:MM" 24h format
  int relayNum;
  bool state; // true=ON, false=OFF
  bool active;
};
Routine routines[MAX_ROUTINES];
int routineCount = 0;
// Add a new routine
bool addRoutine(const String& name, const String& time, int relayNum, bool state) {
  if (routineCount >= MAX_ROUTINES) return false;
  routines[routineCount].name = name;
  routines[routineCount].time = time;
  routines[routineCount].relayNum = relayNum;
  routines[routineCount].state = state;
  routines[routineCount].active = true;
  routineCount++;
  addLog("Routine added: " + name + " at " + time + " for Relay " + String(relayNum) + (state ? " ON" : " OFF"));
  return true;
}

// Remove a routine by index
bool removeRoutine(int idx) {
  if (idx < 0 || idx >= routineCount) return false;
  for (int i = idx; i < routineCount - 1; i++) {
    routines[i] = routines[i + 1];
  }
  routineCount--;
  addLog("Routine removed at index: " + String(idx));
  return true;
}

// Check and execute routines (call in loop)
void checkRoutines() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char nowStr[6];
  snprintf(nowStr, sizeof(nowStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  for (int i = 0; i < routineCount; i++) {
    if (routines[i].active && routines[i].time == String(nowStr)) {
      int relayIdx = routines[i].relayNum - 1;
      if (relayIdx >= 0 && relayIdx < RELAY_COUNT) {
        relayStates[relayIdx] = routines[i].state;
        digitalWrite(relayPins[relayIdx], relayStates[relayIdx] ? HIGH : LOW);
        addLog("Routine triggered: " + routines[i].name + " (Relay " + String(routines[i].relayNum) + (routines[i].state ? " ON" : " OFF") + ")");
        routines[i].active = false; // One-time routine, deactivate after running
      }
    }
  }
}

struct SensorDataPoint {
  time_t timestamp;
  float temperature;
  float humidity;
};
SensorDataPoint dataPoints[MAX_DATA_POINTS];
int dataCount = 0;

// Schedule management
#define MAX_SCHEDULES 10

struct Schedule {
  int id;
  bool active;
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
  bool days[7]; // Sun-Sat
  int relayNum;
  bool state; // keep for compatibility, but not used for on/off anymore
  bool repeat;
  AlarmID_t alarmId;
  String name;
};

Schedule schedules[MAX_SCHEDULES];
int scheduleCount = 0;

// Weather data structure
struct WeatherData {
  String description;
  float temperature;
  float feelsLike;
  int humidity;
  float windSpeed;
  long sunrise;
  long sunset;
  String iconCode;
  unsigned long lastUpdate;
};

WeatherData weather;
const String WEATHER_API_KEY = "e5074258d34949dd1310d451504f2043"; // Add this to config.ini
String WEATHER_CITY = "Hyderabad"; // Add this to config.ini

// Add to Global Variables section
struct LoginAttempt {
  String ipAddress;
  time_t timestamp;
  bool success;
};

#define MAX_LOGIN_ATTEMPTS 10
LoginAttempt loginAttempts[MAX_LOGIN_ATTEMPTS];
int loginAttemptCount = 0;
const int MAX_FAILED_ATTEMPTS = 5;
const int LOCKOUT_DURATION = 300; // 5 minutes in seconds

// Add after weather data struct
#define MAX_STATUS_HISTORY 100

struct StatusEvent {
  time_t timestamp;
  int relayNum;
  bool state;
  String source; // "manual", "schedule", "api"
};

StatusEvent statusHistory[MAX_STATUS_HISTORY];


// ----------- Helper Functions -----------

String generateSessionToken() {
  String token = "";
  for (int i = 0; i < 32; i++) token += String(random(0, 16), HEX);
  return token;
}

void addLog(const String& entry) {
  struct tm timeinfo;
  char timestamp[25] = "";
  if (getLocalTime(&timeinfo)) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
  }
  logBuffer += "[" + String(timestamp) + "] " + entry + "\n";
  
  // Keep log buffer from growing too large
  if (logBuffer.length() > 8192) {
    logBuffer = logBuffer.substring(logBuffer.length() - 8192);
  }
  
  Serial.println(entry);
}

bool isLoggedIn() {
  return true;
  // if (!server.hasHeader("Cookie")) return false;
  // String cookie = server.header("Cookie");
  // int idx = cookie.indexOf("ESPSESSIONID=");
  // if (idx == -1) return false;
  // int start = idx + strlen("ESPSESSIONID=");
  // int end = cookie.indexOf(';', start);
  // String token = (end == -1) ? cookie.substring(start) : cookie.substring(start, end);
  // token.trim();
  // sessionToken.trim();
  // return token == sessionToken && sessionToken.length() > 0;
}

bool requireLogin() {
  if (!isLoggedIn()) {
    Serial.println("[DEBUG] requireLogin: not logged in, redirecting");
    server.sendHeader("Location", "/login");
    // DO NOT set the session cookie here!
    server.send(302, "text/plain", "Redirecting to login...");
    return true;
  }
  Serial.println("[DEBUG] requireLogin: user is logged in");
  return false;
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  return "text/plain";
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

String getINIValue(const String& filePath, const String& key, const String& defaultValue) {
  if (!SPIFFS.exists(filePath)) return defaultValue;
  File file = SPIFFS.open(filePath, "r");
  if (!file) return defaultValue;
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    int sep = line.indexOf('=');
    if (sep != -1) {
      String lineKey = line.substring(0, sep);
      lineKey.trim();
      if (lineKey == key) {
        String val = line.substring(sep + 1);
        val.trim();
        file.close();
        return val;
      }
    }
  }
  file.close();
  return defaultValue;
}

String stripQuotes(const String& s) {
  if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) {
    return s.substring(1, s.length() - 1);
  }
  return s;
}

void connectWiFiStatic() {
  if (SPIFFS.exists("/config.ini")) {
    Serial.println("Found config.ini file");
    
    String ssid = stripQuotes(getINIValue("/config.ini", "wifi_ssid", ""));
    String pass = stripQuotes(getINIValue("/config.ini", "wifi_password", ""));
    String staticIP = getINIValue("/config.ini", "static_ip", "");
    String gatewayStr = getINIValue("/config.ini", "gateway", "");
    String subnetStr = getINIValue("/config.ini", "subnet", "");
    String dnsStr = getINIValue("/config.ini", "dns", "");

    Serial.println("Read values:");
    Serial.println("SSID: " + ssid);
    Serial.println("Password: " + pass);
    Serial.println("Static IP: " + staticIP);
    Serial.println("Gateway: " + gatewayStr);
    Serial.println("Subnet: " + subnetStr);
    Serial.println("DNS: " + dnsStr);

    WiFi.mode(WIFI_STA);

    bool useStatic = staticIP.length() > 0 && gatewayStr.length() > 0 && subnetStr.length() > 0;
    if (useStatic) {
      IPAddress ip, gw, sn, dn;
      if (ip.fromString(staticIP) && gw.fromString(gatewayStr) && sn.fromString(subnetStr)) {
        if (dnsStr.length() > 0 && dn.fromString(dnsStr)) {
          WiFi.config(ip, gw, sn, dn);
        } else {
          WiFi.config(ip, gw, sn);
        }
        Serial.println("Attempting WiFi connection (static IP)...");
      } else {
        Serial.println("Invalid static IP configuration, falling back to DHCP.");
        useStatic = false;
      }
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.print("Connecting to WiFi");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
      delay(500);
      Serial.print(".");
      retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
      addLog("Connected to WiFi: " + WiFi.localIP().toString());
    } else {
      Serial.println("\nFailed to connect to WiFi.");
      addLog("Failed to connect to WiFi.");
      // Instead of ESP.restart(), enter a wait loop
      while (true) {
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
        delay(500);
      }
    }
  } else {
    Serial.println("config.ini file not found!");
  }
}

void loadCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  char buf[32];
  
  // Load username
  for (int i = 0; i < 31; i++) buf[i] = EEPROM.read(USERNAME_ADDR + i);
  buf[31] = '\0';
  savedUsername = String(buf);
  
  // Load password
  for (int i = 0; i < 31; i++) buf[i] = EEPROM.read(PASSWORD_ADDR + i);
  buf[31] = '\0';
  savedPassword = String(buf);
  
  // Load birthday
  for (int i = 0; i < 15; i++) buf[i] = EEPROM.read(BIRTHDAY_ADDR + i);
  buf[15] = '\0';
  savedBirthday = String(buf);
  
  EEPROM.end();

  // Default credentials if none set
  if (savedUsername.length() == 0 || savedPassword.length() == 0) {
    savedUsername = "admin";
    savedPassword = "admin";
    savedBirthday = "2000-01-01";
  }
}

void saveCredentials(const String& username, const String& password, const String& birthday = "") {
  EEPROM.begin(EEPROM_SIZE);
  
  // Save username
  for (int i = 0; i < 31; i++) {
    EEPROM.write(USERNAME_ADDR + i, i < username.length() ? username[i] : 0);
  }
  
  // Save password
  for (int i = 0; i < 31; i++) {
    EEPROM.write(PASSWORD_ADDR + i, i < password.length() ? password[i] : 0);
  }
  
  // Save birthday if provided
  if (birthday.length() > 0) {
    for (int i = 0; i < 15; i++) {
      EEPROM.write(BIRTHDAY_ADDR + i, i < birthday.length() ? birthday[i] : 0);
    }
    savedBirthday = birthday;
  }
  
  EEPROM.commit();
  EEPROM.end();
  
  savedUsername = username;
  savedPassword = password;
  addLog("Credentials updated");
}

void saveRelayStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < RELAY_COUNT; i++) {
    EEPROM.write(RELAY_ADDR + i, relayStates[i] ? 1 : 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadRelayStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < RELAY_COUNT; i++) {
    byte val = EEPROM.read(RELAY_ADDR + i);
    relayStates[i] = (val == 1);
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
  EEPROM.end();
}

void saveSceneStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < SCENE_COUNT; i++) {
    for (int j = 0; j < RELAY_COUNT; j++) {
      EEPROM.write(SCENE_ADDR + (i * RELAY_COUNT) + j, scenes[i].relayStates[j] ? 1 : 0);
    }
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadSceneStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < SCENE_COUNT; i++) {
    for (int j = 0; j < RELAY_COUNT; j++) {
      byte val = EEPROM.read(SCENE_ADDR + (i * RELAY_COUNT) + j);
      // Cast away const to modify the scene states
      const_cast<bool&>(scenes[i].relayStates[j]) = (val == 1);
    }
  }
  EEPROM.end();
}

void checkBirthday() {
  if (savedBirthday.length() != 10) return; // YYYY-MM-DD format
  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  
  char dateBuf[11];
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  String currentDate = String(dateBuf);
                      
  if (currentDate.substring(5) == savedBirthday.substring(5)) { // Compare MM-DD
    addLog("Today is the birthday! (" + savedBirthday + ")");
    // You could trigger a special scene here
  }
}

void applyScene(int sceneIndex) {
  if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
  
  for (int i = 0; i < RELAY_COUNT; i++) {
    relayStates[i] = scenes[sceneIndex].relayStates[i];
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
  saveRelayStates();
  addLog("Applied scene: " + String(scenes[sceneIndex].name));
}
void handleRoutinesGet() {
  if (requireLogin()) return;
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.createNestedArray("routines");
  for (int i = 0; i < routineCount; i++) {
    JsonObject r = arr.createNestedObject();
    r["name"] = routines[i].name;
    r["time"] = routines[i].time;
    r["relayNum"] = routines[i].relayNum;
    r["state"] = routines[i].state;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleRoutinesPost() {
  if (requireLogin()) return;
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  String name = doc["name"];
  String time = doc["time"];
  int relay = doc["relay"];
  bool state = doc["state"];
  if (!addRoutine(name, time, relay, state)) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Max routines reached or invalid data\"}");
    return;
  }
  server.send(200, "application/json", "{\"success\":true}");
}
void handleButtonPress() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = HIGH;
  int buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > 50) {
    if (buttonState == LOW && lastButtonState == HIGH) {
      // Button pressed - toggle all relays
      bool allOn = true;
      for (int i = 0; i < RELAY_COUNT; i++) {
        if (!relayStates[i]) {
          allOn = false;
          break;
        }
      }
      
      for (int i = 0; i < RELAY_COUNT; i++) {
        relayStates[i] = !allOn;
        digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
      }
      saveRelayStates();
      addLog("Physical button pressed - all relays " + String(allOn ? "OFF" : "ON"));
    }
  }
  
  lastButtonState = buttonState;
}

// ----------- Web Handlers -----------

void handleLogin() {
  Serial.println("[DEBUG] handleLogin called, method: " + String(server.method() == HTTP_GET ? "GET" : "POST"));
  if (server.method() == HTTP_GET) {
    if (handleFileRead("/login.html")) {
      Serial.println("[DEBUG] Served /login.html from SPIFFS");
      return;
    }
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>IoT Dashboard Login</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      --bg-color: #f4f7fb;
      --card-bg: #fff;
      --text-color: #222;
      --text-secondary: #888;
      --primary-color: #0072ff;
      --secondary-color: #00c6ff;
      --border-color: #eee;
      --shadow-color: rgba(0,0,0,0.07);
    }
    .dark-mode {
      --bg-color: #121212;
      --card-bg: #1e1e1e;
      --text-color: #e0e0e0;
      --text-secondary: #aaa;
      --primary-color: #0099ff;
      --secondary-color: #00c6ff;
      --border-color: #333;
      --shadow-color: rgba(0,0,0,0.3);
    }

    body {
      background: var(--bg-color);
      color: var(--text-color);
      transition: background 0.3s ease;
    }

    .login-container {
      background: var(--card-bg);
      border-radius: 24px;
      box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18);
      padding: 2.8rem 2.2rem 2.2rem 2.2rem;
      width: 100%;
      max-width: 370px;
    }

    .login-title {
      font-size: 2.2rem;
      font-weight: 800;
      color: var(--primary-color);
      margin-bottom: 1.7rem;
      letter-spacing: 1.5px;
      text-align: center;
    }

    .input-group {
      margin-bottom: 1.3rem;
    }

    .input-group label {
      display: block;
      margin-bottom: 0.5rem;
      color: var(--primary-color);
      font-weight: 600;
    }

    .input-group input {
      width: 100%;
      padding: 0.8rem 1.1rem;
      border: none;
      border-radius: 8px;
      font-size: 1.08rem;
      background: #f0f7fa;
    }

    .login-btn {
      width: 100%;
      padding: 0.9rem;
      background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%);
      color: #fff;
      border: none;
      border-radius: 8px;
      font-size: 1.15rem;
      text-align: center;
      color: #aaa;
      font-size: 1rem;
    }

    .forgot-link {
      color: var(--primary-color);
      text-decoration: underline;
      font-size: 1rem;
      font-weight: 500;
      margin-top: 1.2rem;
      display: inline-block;
    }

    .error-message {
      color: #e74c3c;
      background: #fff0f0;
      border-radius: 6px;
      padding: 0.6rem 1.1rem;
      margin-top: 1.1rem;
      text-align: center;
      font-weight: 600;
      display: %ERROR_DISPLAY%;
    }

    .dark-toggle {
      position: fixed;
      bottom: 20px;
      right: 20px;
      background: var(--primary-color);
      color: #fff;
      border: none;
      border-radius: 50%;
      width: 50px;
      height: 50px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
      transition: all 0.3s ease;
    }
  </style>
</head>
<body>
  <div class="login-container">
    <div class="login-title">IoT Dashboard</div>
    <form id="loginForm" method="POST" action="/login" autocomplete="on">
      <div class="input-group">
        <label for="username">Username</label>
        <input type="text" id="username" name="username" autocomplete="username" required>
      </div>
      <div class="input-group">
        <label for="password">Password</label>
        <input type="password" id="password" name="password" autocomplete="current-password" required>
      </div>
      <button class="login-btn" type="submit">Login</button>
    </form>
    <div class="error-message">%ERROR_MSG%</div>
    <div style="margin-top:1.2rem; text-align:center;">
      <a href="/resetpass" class="forgot-link">Forgot password?</a>
    </div>
    <div class="footer">©️ 2024 IoT Dashboard</div>
  </div>
  <script>
    // Dark mode toggle
    const toggleDarkMode = () => {
      document.body.classList.toggle('dark-mode');
      const isDark = document.body.classList.contains('dark-mode');
      localStorage.setItem('dark-mode', isDark);
    }

    // Load dark mode preference
    const loadDarkMode = () => {
      const isDark = JSON.parse(localStorage.getItem('dark-mode'));
      if (isDark) {
        document.body.classList.add('dark-mode');
      }
    }

    // Initialize dark mode
    loadDarkMode();
  </script>
</body>
</html>
)rawliteral";
    html.replace("%ERROR_MSG%", "");
    html.replace("%ERROR_DISPLAY%", "none");
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    String username = server.arg("username");
    String password = server.arg("password");
    Serial.println("[DEBUG] Login POST: username=" + username + ", password=" + password);

    String clientIP = server.client().remoteIP().toString();
  
    if (isIPBlocked(clientIP)) {
      DEBUG_WARN(AUTH, "Blocked login attempt from IP: " + clientIP);
      server.send(429, "text/html", "<html><body><h2>Too many failed login attempts</h2><p>Please try again later.</p></body></html>");
      return;
    }
  
    if (username == savedUsername && password == savedPassword) {
      sessionToken = generateSessionToken();
      Serial.println("[DEBUG] New sessionToken: " + sessionToken);

      // Set the session cookie here!
      server.sendHeader("Set-Cookie", "ESPSESSIONID=" + sessionToken + "; Path=/; HttpOnly");
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "Redirecting to dashboard...");
      delay(100);
      addLog("User logged in");
      recordLoginAttempt(clientIP, true);
      Serial.println("[DEBUG] Login success, JS redirect to /");
    } else {
      String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>IoT Dashboard Login</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      --bg-color: #f4f7fb;
      --card-bg: #fff;
      --text-color: #222;
      --text-secondary: #888;
      --primary-color: #0072ff;
      --secondary-color: #00c6ff;
      --border-color: #eee;
      --shadow-color: rgba(0,0,0,0.07);
    }

    .dark-mode {
      --bg-color: #121212;
      --card-bg: #1e1e1e;
      --text-color: #e0e0e0;
      --text-secondary: #aaa;
      --primary-color: #0099ff;
      --secondary-color: #00c6ff;
      --border-color: #333;
      --shadow-color: rgba(0,0,0,0.3);
    }

    body {
      background: var(--bg-color);
      color: var(--text-color);
      transition: background 0.3s ease;
    }

    .login-container {
      background: var(--card-bg);
      border-radius: 24px;
      box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18);
      padding: 2.8rem 2.2rem 2.2rem 2.2rem;
      width: 100%;
      max-width: 370px;
    }

    .login-title {
      font-size: 2.2rem;
      font-weight: 800;
      color: var(--primary-color);
      margin-bottom: 1.7rem;
      letter-spacing: 1.5px;
      text-align: center;
    }

    .input-group {
      margin-bottom: 1.3rem;
    }

    .input-group label {
      display: block;
      margin-bottom: 0.5rem;
      color: var(--primary-color);
      font-weight: 600;
    }

    .input-group input {
      width: 100%;
      padding: 0.8rem 1.1rem;
      border: none;
      border-radius: 8px;
      font-size: 1.08rem;
      background: #f0f7fa;
    }

    .login-btn {
      width: 100%;
      padding: 0.9rem;
      background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%);
      color: #fff;
      border: none;
      border-radius: 8px;
      font-size: 1.15rem;
      font-weight: 700;
      cursor: pointer;
      margin-top: 0.7rem;
    }

    .footer {
      margin-top: 2.2rem;
      text-align: center;
      color: #aaa;
      font-size: 1rem;
    }

    .forgot-link {
      color: var(--primary-color);
      text-decoration: underline;
      font-size: 1rem;
      font-weight: 500;
      margin-top: 1.2rem;
      display: inline-block;
    }

    .error-message {
      color: #e74c3c;
      background: #fff0f0;
      border-radius: 6px;
      padding: 0.6rem 1.1rem;
      margin-top: 1.1rem;
      text-align: center;
      font-weight: 600;
      display: block;
    }

    .dark-toggle {
      position: fixed;
      bottom: 20px;
      right: 20px;
      background: var(--primary-color);
      color: #fff;
      border: none;
      border-radius: 50%;
      width: 50px;
      height: 50px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
      transition: all 0.3s ease;
    }
  </style>
</head>
<body>
  <div class="login-container">
    <div class="login-title">IoT Dashboard</div>
    <form id="loginForm" method="POST" action="/login" autocomplete="on">
      <div class="input-group">
        <label for="username">Username</label>
        <input type="text" id="username" name="username" autocomplete="username" required>
      </div>
      <div class="input-group">
        <label for="password">Password</label>
        <input type="password" id="password" name="password" autocomplete="current-password" required>
      </div>
      <button class="login-btn" type="submit">Login</button>
    </form>
    <div class="error-message">Invalid username or password</div>
    <div style="margin-top:1.2rem; text-align:center;">
      <a href="/resetpass" class="forgot-link">Forgot password?</a>
    </div>
    <div class="footer">©️ 2024 IoT Dashboard</div>
  </div>
  <script>
    // Dark mode toggle
    const toggleDarkMode = () => {
      document.body.classList.toggle('dark-mode');
      const isDark = document.body.classList.contains('dark-mode');
      localStorage.setItem('dark-mode', isDark);
    }

    // Load dark mode preference
    const loadDarkMode = () => {
      const isDark = JSON.parse(localStorage.getItem('dark-mode'));
      if (isDark) {
        document.body.classList.add('dark-mode');
      }
    }

    // Initialize dark mode
    loadDarkMode();
  </script>
</body>
</html>
)rawliteral";
      server.send(200, "text/html", html);
      recordLoginAttempt(clientIP, false);
      addLog("Failed login attempt");
      Serial.println("[DEBUG] Login failed");
    }
  }
}

void handleLogout() {
  sessionToken = "";
  server.sendHeader("Set-Cookie", "ESPSESSIONID=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  server.sendHeader("Set-Cookie", "ESPSESSIONID=" + sessionToken + "; Path=/; HttpOnly");
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "Logged out");
}

void handleRoot() {
  if (requireLogin()) return;
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>SmartHome</title>
  <link href="https://fonts.googleapis.com/css?family=Segoe+UI:700,400&display=swap" rel="stylesheet">
  <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    #offlineBanner {
      display: none;
      position: fixed;
      top: 0; left: 0; width: 100vw;
      background: #e74c3c;
      color: #fff;
      text-align: center;
      padding: 8px 0;
      z-index: 9999;
      font-weight: 600;
      letter-spacing: 1px;
    }
    body.offline #offlineBanner { display: block; }
    body {
      background: #eef3fc;
      font-family: 'Segoe UI', Arial, sans-serif;
      margin: 0; padding: 0;
      color: #222;
    }
     <style>
   #offlineBanner {
      display: none;
      position: fixed;
      top: 0; left: 0; width: 100vw;
      background: #e74c3c;
      color: #fff;
      text-align: center;
      padding: 8px 0;
      z-index: 9999;
      font-weight: 600;
      letter-spacing: 1px;
    }
    body.offline #offlineBanner { display: block; }
    body {
      background: #eef3fc;
      font-family: 'Segoe UI', Arial, sans-serif;
      margin: 0; padding: 0;
      color: #222;
    }
      /* Add to your <style> section */
.device-card {
  background: #fff;
  border-radius: 16px;
  box-shadow: 0 2px 12px #e3e9f7;
  padding: 22px 20px;
  min-width: 260px;
  max-width: 340px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  align-items: flex-start;
  position: relative;
  border: 1px solid transparent;
  overflow: hidden;
  will-change: transform, box-shadow;
  margin-bottom: 18px;
}
.device-card:hover {
  transform: translateY(-8px);
  box-shadow: 0 14px 24px rgba(37, 99, 235, 0.15), 0 6px 12px rgba(37, 99, 235, 0.08), 0 0 0 1px rgba(37, 99, 235, 0.05);
  border-color: rgba(59, 130, 246, 0.2);
  background: linear-gradient(to bottom right, #ffffff, #f8faff);
}
    .sidebar {
      width: 220px; background: #1e3a8a; color: #fff; height: 100vh; position: fixed; left:0; top:0; display:flex; flex-direction:column; align-items:center; padding-top:32px; z-index: 100;
    }
    .sidebar .logo { font-size:1.5rem; font-weight:700; margin-bottom:32px; }
    .sidebar button { background:none; border:none; color:#fff; font-size:1.1rem; margin:12px 0; cursor:pointer; width:100%; text-align:left; padding:10px 24px; border-radius:8px; transition:background 0.2s;}
    .sidebar button.active, .sidebar button:hover { background:#2563eb; }
    .main-content { margin-left:220px; padding:32px; }
    .header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 18px 32px 10px 32px; background: #fff; box-shadow: 0 2px 8px #e3e9f7;
      margin-left: 220px;
    }
    .header .logo { font-size: 1.5rem; font-weight: 700; display: flex; align-items: center; gap: 10px; }
    .header .status { font-size: 1rem; color: #27ae60; margin-right: 18px; }
    .header .settings-btn, .header .ota-btn { background: #2563eb; color: #fff; border: none; border-radius: 8px; padding: 8px 18px; font-size: 1rem; cursor: pointer; margin-left: 8px;}
    .dashboard-main { max-width: 1200px; margin: 30px auto; padding: 0 20px; }
    .dashboard-cards { display: flex; gap: 24px; margin-bottom: 24px; }
    .dashboard-card {
      background: #fff; border-radius: 16px; box-shadow: 0 2px 12px #e3e9f7;
      padding: 24px 32px; flex: 1 1 220px; min-width: 200px; display: flex; flex-direction: column; gap: 8px;
      transition: all 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
      border: 1px solid transparent;
      position: relative;
      overflow: hidden;
      will-change: transform, box-shadow;
    }
    .dashboard-card:hover {
      transform: translateY(-8px);
      box-shadow: 0 14px 24px rgba(37, 99, 235, 0.15), 0 6px 12px rgba(37, 99, 235, 0.08), 0 0 0 1px rgba(37, 99, 235, 0.05);
      border-color: rgba(59, 130, 246, 0.2);
      background: linear-gradient(to bottom right, #ffffff, #f8faff);
    }
    .dashboard-card .material-icons { font-size: 2rem; color: #2563eb; }
    .dashboard-card .card-title { font-size: 1.1rem; font-weight: 600; }
    .dashboard-card .card-value { font-size: 2rem; font-weight: 700; }
    .dashboard-card .card-sub { font-size: 0.98rem; color: #888; }
    .dashboard-tabs { display: flex; gap: 18px; margin: 18px 0 12px 0; }
    .dashboard-tab {
      background: none; border: none; font-size: 1.1rem; font-weight: 600; color: #2563eb;
      padding: 8px 18px; border-radius: 8px 8px 0 0; cursor: pointer; transition: background 0.2s;
    }
    .dashboard-tab.active, .dashboard-tab:hover { background: #eaf3ff; color: #222; }
    .devices-row { display: flex; flex-wrap: wrap; gap: 24px; margin-bottom: 24px; }
    .device-card {
      background: #fff; border-radius: 16px; box-shadow: 0 2px 12px #e3e9f7;
      padding: 22px 20px; flex: 1 1 260px; min-width: 240px; max-width: 340px;
      display: flex; flex-direction: column; gap: 10px; align-items: flex-start; position: relative;
      transition: all 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
      border: 1px solid transparent;
      overflow: hidden;
      will-change: transform, box-shadow;
    }
    .device-card:hover {
      transform: translateY(-8px);
      box-shadow: 0 14px 24px rgba(37, 99, 235, 0.15), 0 6px 12px rgba(37, 99, 235, 0.08), 0 0 0 1px rgba(37, 99, 235, 0.05);
      border-color: rgba(59, 130, 246, 0.2);
      background: linear-gradient(to bottom right, #ffffff, #f8faff);
    }
    .device-card .material-icons { font-size: 2rem; }
    .device-card .device-title { font-size: 1.1rem; font-weight: 600; }
    .device-card .device-status { font-size: 0.98rem; color: #888; }
    .device-card .toggle-switch { position: absolute; top: 22px; right: 22px; }
    .toggle-switch { position: relative; display: inline-block; width: 48px; height: 28px; }
    .toggle-switch input { opacity: 0; width: 0; height: 0; }
    .slider-toggle {
      position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
      background: #ccc; transition: .4s; border-radius: 28px;
    }
    .slider-toggle:before {
      position: absolute; content: ""; height: 20px; width: 20px; left: 4px; bottom: 4px;
      background: #fff; transition: .4s; border-radius: 50%; box-shadow: 0 2px 6px #e3e9f7;
    }
    .toggle-switch input:checked + .slider-toggle { background: #2563eb; }
    .toggle-switch input:checked + .slider-toggle:before { transform: translateX(20px); }
    .slider-row { display: flex; align-items: center; gap: 10px; }
    .slider-row input[type=range] { width: 120px; }
    .quick-actions { display: flex; gap: 12px; margin: 18px 0 18px 0; }
    .quick-action-btn { background: #fff; color: #2563eb; border: 2px solid #2563eb; border-radius: 8px; padding: 8px 18px; font-size: 1rem; cursor: pointer; transition: background 0.2s; }
    .quick-action-btn:hover { background: #2563eb; color: #fff; }
    .energy-section { background: #fff; border-radius: 16px; box-shadow: 0 2px 12px #e3e9f7; padding: 24px 32px; margin-bottom: 32px; }
    .energy-header { display: flex; align-items: center; justify-content: space-between; }
    .energy-title { font-size: 1.2rem; font-weight: 700; }
    .energy-tabs { display: flex; gap: 10px; }
    .energy-tab { background: #eaf3ff; border: none; border-radius: 6px; padding: 6px 16px; font-size: 1rem; color: #2563eb; cursor: pointer; }
    .energy-tab.active { background: #2563eb; color: #fff; }
    #energyChart { width: 100%; height: 180px; margin-top: 10px; }
    .routines-section { margin-bottom: 32px; }
    .routines-header { display: flex; align-items: center; justify-content: space-between; }
    .routines-title { font-size: 1.2rem; font-weight: 700; }
    .routines-list { display: flex; gap: 24px; margin-top: 18px; }
    .routine-card {
      background: #fff; border-radius: 16px; box-shadow: 0 2px 12px #e3e9f7;
      padding: 22px 20px; min-width: 260px; display: flex; flex-direction: column; gap: 8px; position: relative;
      transition: all 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
      border: 1px solid transparent;
      overflow: hidden;
      will-change: transform, box-shadow;
    }
    .routine-card:hover {
      transform: translateY(-8px);
      box-shadow: 0 14px 24px rgba(37, 99, 235, 0.15), 0 6px 12px rgba(37, 99, 235, 0.08), 0 0 0 1px rgba(37, 99, 235, 0.05);
      border-color: rgba(59, 130, 246, 0.2);
      background: linear-gradient(to bottom right, #ffffff, #f8faff);
    }
    .routine-card .material-icons { font-size: 1.5rem; }
    .routine-title { font-size: 1.1rem; font-weight: 600; }
    .routine-time { font-size: 0.98rem; color: #888; }
    .routine-list { margin: 10px 0 0 0; padding: 0 0 0 18px; color: #27ae60; font-size: 0.98rem; }
    .routine-toggle { position: absolute; top: 22px; right: 22px; }
    .add-routine-btn { background: #2563eb; color: #fff; border: none; border-radius: 8px; padding: 10px 22px; font-size: 1rem; cursor: pointer; }
    .footer { margin: 32px 0 0 0; text-align: center; color: #aaa; font-size: 1rem; }
    .system-info { margin: 24px 0; background: #f8fafc; border-radius: 12px; padding: 18px 24px; font-size: 1.05rem; color: #444; transition: all 0.4s cubic-bezier(0.165, 0.84, 0.44, 1); border-radius: 12px;}
    .system-info:hover { transform: translateY(-5px); box-shadow: 0 8px 20px rgba(0, 0, 0, 0.12);}
    .log-section { background: #fff; border-radius: 12px; box-shadow: 0 2px 8px #e3e9f7; padding: 18px 24px; margin-bottom: 24px; }
    .log-section h3 { margin-top: 0; color: #2563eb; }
    .log-list { max-height: 120px; overflow-y: auto; font-size: 0.98rem; color: #333; background: #f8fafc; border-radius: 8px; padding: 8px 12px; }
    .dark-toggle { position: fixed; bottom: 20px; right: 20px; background: #2563eb; color: #fff; border: none; border-radius: 50%; width: 50px; height: 50px; display: flex; align-items: center; justify-content: center; cursor: pointer; box-shadow: 0 2px 10px rgba(0,0,0,0.2); transition: all 0.3s ease; }
    .dark-mode { background: #181c24 !important; color: #e0e0e0 !important; }
    .dark-mode .dashboard-card, .dark-mode .device-card, .dark-mode .energy-section, .dark-mode .routine-card, .dark-mode .log-section { background: #232a36 !important; color: #e0e0e0 !important; }
    .dark-mode .dashboard-tab, .dark-mode .energy-tab { color: #90cdf4 !important; }
    .dark-mode .dashboard-tab.active, .dark-mode .energy-tab.active { background: #2563eb !important; color: #fff !important; }
    .dark-mode .quick-action-btn { background: #232a36 !important; color: #90cdf4 !important; border-color: #2563eb !important; }
    .dark-mode .quick-action-btn:hover { background: #2563eb !important; color: #fff !important; }
    @media (max-width: 900px) {
      .dashboard-cards, .devices-row, .routines-list { flex-direction: column; }
      .energy-section, .dashboard-main { padding: 0 5px; }
      .main-content, .header { margin-left: 0; padding: 12px; }
      .sidebar { position: static; width: 100vw; height: auto; flex-direction: row; justify-content: flex-start; }
    }
  </style>
</head>
<body>
<div id="offlineBanner">Device Disconnected - Showing Last Known Data</div>
  <div class="sidebar">
    <div class="logo">SmartHome Hub</div>
    <button class="active">Dashboard</button>
    <button onclick="window.location.href='/settings'">Settings</button>
    <button onclick="window.location.href='/schedules'">Schedules</button>
    <button onclick="window.location.href='/logs'">Logs</button>
    <button onclick="window.location.href='/ota'">OTA Update</button>
  </div>
  <div class="header">
    <div class="logo"><span class="material-icons">home</span> Welcome Home</div>
    <div>
      <span class="status" id="espStatus">ESP32 Status: <span style="color:#27ae60;">● Connected</span></span>
      <button class="settings-btn" onclick="window.location.href='/settings'">Settings</button>
      <button class="ota-btn" onclick="window.location.href='/ota'">OTA Update</button>
      <button class="settings-btn" onclick="toggleDarkMode()" title="Toggle dark mode"><span class="material-icons">dark_mode</span></button>
    </div>
  </div>
  <div class="main-content">
    <div class="dashboard-main">
      <div class="dashboard-cards">
        <div class="dashboard-card">
          <span class="material-icons">thermostat</span>
          <div class="card-title">Indoor Temperature</div>
          <div class="card-value" id="tempCard">--°C</div>
          <div class="card-sub" id="tempSub"></div>
        </div>
        <div class="dashboard-card">
          <span class="material-icons">water_drop</span>
          <div class="card-title">Humidity</div>
          <div class="card-value" id="humCard">--%</div>
          <div class="card-sub" id="humSub"></div>
        </div>
        <div class="dashboard-card">
          <span class="material-icons">bolt</span>
          <div class="card-title">Energy Usage</div>
          <div class="card-value" id="energyCard">-- kWh</div>
          <div class="card-sub" id="energySub"></div>
        </div>
        <div class="dashboard-card">
          <span class="material-icons">devices</span>
          <div class="card-title">Active Devices</div>
          <div class="card-value" id="activeDevices">--</div>
          <div class="card-sub" id="activeDevicesSub"></div>
        </div>
      </div>
      <div class="quick-actions">
        <button class="quick-action-btn" onclick="applyScene(0)">
          <span class="material-icons">nights_stay</span> Good Night
        </button>
        <button class="quick-action-btn" onclick="applyScene(1)">
          <span class="material-icons">wb_sunny</span> Good Morning
        </button>
        <button class="quick-action-btn" onclick="applyScene(4)">
          <span class="material-icons">power</span> All On
        </button>
        <button class="quick-action-btn" onclick="applyScene(5)">
          <span class="material-icons">power_off</span> All Off
        </button>
        <button class="quick-action-btn" onclick="window.location.href='/logs'">
          <span class="material-icons">list</span> Logs
        </button>
        <button class="quick-action-btn" onclick="window.location.href='/schedules'">
          <span class="material-icons">schedule</span> Schedules
        </button>
      </div>
      <div class="dashboard-tabs">
        <button class="dashboard-tab active" onclick="showRoom('all', this)">All Rooms</button>
        <button class="dashboard-tab" onclick="showRoom('living', this)">Living Room</button>
        <button class="dashboard-tab" onclick="showRoom('kitchen', this)">Kitchen</button>
        <button class="dashboard-tab" onclick="showRoom('bedroom', this)">Bedroom</button>
        <button class="dashboard-tab" onclick="showRoom('bathroom', this)">Bathroom</button>
        <button class="dashboard-tab" onclick="showRoom('office', this)">Office</button>
      </div>
      <div class="devices-row" id="devicesRow"></div>
      <div class="energy-section">
        <div class="energy-header">
          <div class="energy-title">Energy Consumption</div>
          <div class="energy-tabs">
            <button class="energy-tab active" onclick="setEnergyRange('day', this)">Day</button>
            <button class="energy-tab" onclick="setEnergyRange('week', this)">Week</button>
            <button class="energy-tab" onclick="setEnergyRange('month', this)">Month</button>
          </div>
        </div>
        <canvas id="energyChart"></canvas>
      </div>
      <div class="routines-section">
        <div class="routines-header">
          <div class="routines-title">Automation Routines</div>
          <button class="add-routine-btn" onclick="showRoutineModal()">+ New Routine</button>
        </div>
        <div class="routines-list" id="routinesList"></div>
      </div>
      <div class="system-info" id="systemInfo">
        <span>Uptime:</span> <span id="uptime">--</span> &nbsp;|&nbsp;
        <span>IP:</span> <span id="ipAddr">--</span> &nbsp;|&nbsp;
        <span>Free Heap:</span> <span id="freeHeap">--</span> bytes
      </div>
      <div class="log-section">
        <h3>Recent System Logs</h3>
        <div class="log-list" id="logList"></div>
      </div>
      <div class="footer">
        ESP32 Home Automation Dashboard<br>
        Connected to: ESP32-WROOM-32
      </div>
    </div>
  </div>
  <button class="dark-toggle" onclick="toggleDarkMode()" title="Toggle dark mode"><span class="material-icons">dark_mode</span></button>
  <script>
    // Persistent fetch: always show last known data, update if online
    function persistentFetch(key, url, renderFn, fallback = []) {
      let cached = localStorage.getItem(key);
      if (cached) {
        try { renderFn(JSON.parse(cached)); } catch (e) { renderFn(fallback); }
      } else {
        renderFn(fallback);
      }
      fetch(url, { credentials: 'include' })
        .then(r => r.json())
        .then(data => {
          renderFn(data);
          localStorage.setItem(key, JSON.stringify(data));
          document.body.classList.remove('offline');
        })
        .catch(() => {
          document.body.classList.add('offline');
        });
    }
    // --- UI Logic for Dashboard ---
    function toggleDarkMode() {
      document.body.classList.toggle('dark-mode');
      localStorage.setItem('dark-mode', document.body.classList.contains('dark-mode'));
    }
    (function() {
      if (localStorage.getItem('dark-mode') === 'true') {
        document.body.classList.add('dark-mode');
      }
    })();
    function showRoutineModal() {
      document.getElementById('routineModal').style.display = 'flex';
    }
    function closeRoutineModal() {
      document.getElementById('routineModal').style.display = 'none';
      document.getElementById('routineError').style.display = 'none';
    }
    function addRoutine() {
      const name = document.getElementById('routineName').value;
      const time = document.getElementById('routineTime').value;
      const relay = document.getElementById('routineRelay').value;
      const state = document.getElementById('routineState').value;
      const error = document.getElementById('routineError');
      if (!name || !time) {
        error.textContent = "Name and time required";
        error.style.display = 'block';
        return;
      }
      fetch('/routines', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({name, time, relay:parseInt(relay), state:state=="1"})
      })
      .then(r=>r.json())
      .then(j=>{
        if(j.success) {
          closeRoutineModal();
          loadRoutines();
        } else {
          error.textContent = j.error || "Failed to add routine";
          error.style.display = 'block';
        }
      });
    }
    function loadRoutines() {
      persistentFetch('routines', '/routines', renderRoutines, []);
    }
    function renderRoutines(json) {
      let html = '';
      if(json.routines && json.routines.length) {
        json.routines.forEach(r=>{
          html += `<div class="routine-card">
            <span class="material-icons">alarm</span>
            <div class="routine-title">${r.name}</div>
            <div class="routine-time">${r.time}</div>
            <div class="routine-list">Relay ${r.relayNum} ${r.state?'ON':'OFF'}</div>
          </div>`;
        });
      } else {
        html = '<div style="color:#888;">No routines yet.</div>';
      }
      document.getElementById('routinesList').innerHTML = html;
    }
    function updateESPStatus() {
      fetch('/wifiStatus')
        .then(r => r.json())
        .then(json => {
          const statusEl = document.getElementById('espStatus');
          if (json.connected) {
            statusEl.innerHTML = 'ESP32 Status: <span style="color:#27ae60;">● Connected</span>';
          } else {
            statusEl.innerHTML = 'ESP32 Status: <span style="color:#e74c3c;">● Disconnected</span>';
          }
        })
        .catch(() => {
          const statusEl = document.getElementById('espStatus');
          statusEl.innerHTML = 'ESP32 Status: <span style="color:#e74c3c;">● Disconnected</span>';
        });
    }
    function applyScene(idx) {
      fetch('/scene?idx=' + idx, { credentials: 'include' })
        .then(r => r.json())
        .then(() => {
          updateRelayStatus();
          loadDevices();
        });
    }
    function showRoom(room, btn) {
      document.querySelectorAll('.dashboard-tab').forEach(tab => tab.classList.remove('active'));
      btn.classList.add('active');
      // Optionally filter devices here
    }
    function updateRelayStatus() {
      persistentFetch('relayStatus', '/relayStatus', renderRelays, []);
    }
    function renderRelays(json) {
      let html = '';
      if(json.relays) {
        json.relays.forEach(relay => {
          html += `<div style="margin-bottom:8px;">
            <span class="material-icons" style="vertical-align:middle;color:${relay.state?'#27ae60':'#e74c3c'}">${relay.state?'toggle_on':'toggle_off'}</span>
            <span style="font-weight:600;">${relay.name}</span>
            <label class="toggle-switch" style="margin-left:12px;">
              <input type="checkbox" ${relay.state?'checked':''} onchange="toggleRelay(${relay.num},this.checked)">
              <span class="slider-toggle"></span>
            </label>
          </div>`;
        });
      }
      if(document.getElementById('relayStatusList')) document.getElementById('relayStatusList').innerHTML = html;
    }
    function toggleRelay(num, state) {
      fetch(`/relay?num=${num}&state=${state?1:0}`, { credentials: 'include' })
        .then(() => {
          updateRelayStatus();
          loadDevices();
        });
    }
    function loadDevices() {
      // You can use persistentFetch here if you want to cache device status
      fetch('/relayStatus', { credentials: 'include' })
        .then(r => r.json())
        .then(json => {
          let html = '';
          // Example: Living Room Light (with brightness and color)
          html += `
          <div class="device-card">
            <div style="display:flex;align-items:center;justify-content:space-between;">
              <div>
                <span class="material-icons" style="color:#fbc02d;font-size:2.2rem;">lightbulb</span>
                <span style="font-weight:700;font-size:1.1rem;">Living Room Light</span><br>
                <span style="font-size:0.95em;color:#888;">Philips Hue</span>
              </div>
              <label class="toggle-switch">
                <input type="checkbox" checked>
                <span class="slider-toggle"></span>
              </label>
            </div>
            <div style="margin:12px 0 8px 0;">
              <div style="font-size:0.98em;">Brightness</div>
              <input type="range" min="0" max="100" value="75" style="width:100%;">
            </div>
            <div style="font-size:0.98em;">Color</div>
            <div style="display:flex;gap:8px;margin:8px 0;">
              <span style="width:22px;height:22px;border-radius:50%;background:#f44336;display:inline-block;"></span>
              <span style="width:22px;height:22px;border-radius:50%;background:#2196f3;display:inline-block;"></span>
              <span style="width:22px;height:22px;border-radius:50%;background:#4caf50;display:inline-block;"></span>
              <span style="width:22px;height:22px;border-radius:50%;background:#ffeb3b;display:inline-block;"></span>
              <span style="width:22px;height:22px;border-radius:50%;background:#9c27b0;display:inline-block;"></span>
              <span style="width:22px;height:22px;border-radius:50%;background:#eee;display:inline-block;border:1px solid #ccc;text-align:center;line-height:22px;">+</span>
            </div>
            <div style="font-size:0.92em;color:#888;">Last updated: 2 min ago &nbsp; <a href="#">Details</a></div>
          </div>
          `;
            // Example: Thermostat
      html += `
      <div class="device-card">
        <div style="display:flex;align-items:center;justify-content:space-between;">
          <div>
            <span class="material-icons" style="color:#e57373;font-size:2.2rem;">thermostat</span>
            <span style="font-weight:700;font-size:1.1rem;">Living Room Thermostat</span><br>
            <span style="font-size:0.95em;color:#888;">Nest</span>
          </div>
          <label class="toggle-switch">
            <input type="checkbox" checked>
            <span class="slider-toggle"></span>
          </label>
        </div>
        <div style="margin:18px 0 8px 0;text-align:center;">
          <div style="display:inline-block;width:80px;height:80px;border-radius:50%;background:#fff3f3;border:6px solid #e57373;position:relative;">
            <div style="font-size:2rem;font-weight:700;line-height:80px;">22°C</div>
            <div style="position:absolute;bottom:8px;width:100%;font-size:0.95em;color:#888;">Target: 23°C</div>
          </div>
        </div>
        <div style="text-align:center;font-size:1.1em;">Temperature <b>23°C</b></div>
        <div style="font-size:0.92em;color:#888;">Last updated: 5 min ago &nbsp; <a href="#">Details</a></div>
      </div>
      `;

      // Example: Door Lock
      html += `
      <div class="device-card">
        <div style="display:flex;align-items:center;justify-content:space-between;">
          <div>
            <span class="material-icons" style="color:#43a047;font-size:2.2rem;">lock</span>
            <span style="font-weight:700;font-size:1.1rem;">Front Door Lock</span><br>
            <span style="font-size:0.95em;color:#888;">August</span>
          </div>
          <span style="background:#43a047;color:#fff;padding:2px 12px;border-radius:12px;font-size:0.98em;">Locked</span>
        </div>
        <div style="margin:18px 0 8px 0;text-align:center;">
          <span class="material-icons" style="font-size:3.5rem;color:#43a047;">lock</span>
        </div>
        <div style="display:flex;justify-content:space-between;font-size:0.98em;color:#222;">
          <span>Last Locked<br><b>10:32 AM</b></span>
          <span>Last Unlocked<br><b>8:15 AM</b></span>
          <span>Battery<br><b>85%</b></span>
        </div>
        <div style="font-size:0.92em;color:#888;">Last updated: 1 min ago &nbsp; <a href="#">Details</a></div>
      </div>
      `;

      // Example: Camera
      html += `
      <div class="device-card">
        <div style="display:flex;align-items:center;justify-content:space-between;">
          <div>
            <span class="material-icons" style="color:#2196f3;font-size:2.2rem;">videocam</span>
            <span style="font-weight:700;font-size:1.1rem;">Front Door Camera</span><br>
            <span style="font-size:0.95em;color:#888;">Ring</span>
          </div>
          <label class="toggle-switch">
            <input type="checkbox" checked>
            <span class="slider-toggle"></span>
          </label>
        </div>
        <div style="margin:18px 0 8px 0;text-align:center;">
          <span class="material-icons" style="font-size:3.5rem;color:#bbb;">videocam</span>
        </div>
        <div style="display:flex;gap:8px;justify-content:center;margin-bottom:8px;">
          <button style="background:#2563eb;color:#fff;border:none;border-radius:6px;padding:4px 14px;cursor:pointer;">Live View</button>
          <button style="background:#f4f7fb;color:#2563eb;border:none;border-radius:6px;padding:4px 14px;cursor:pointer;">Recordings</button>
          <button style="background:#f4f7fb;color:#2563eb;border:none;border-radius:6px;padding:4px 14px;cursor:pointer;">Settings</button>
        </div>
        <div style="font-size:0.92em;color:#888;">Last motion: 15 min ago &nbsp; <a href="#">Details</a></div>
      </div>
      `;

      // Add New Device card
      html += `
      <div class="device-card" style="border:2px dashed #2563eb;background:#f8fafc;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:220px;">
        <div style="background:#eaf3ff;width:54px;height:54px;border-radius:50%;display:flex;align-items:center;justify-content:center;margin-bottom:12px;">
          <span class="material-icons" style="color:#2563eb;font-size:2.2rem;">add</span>
        </div>
        <div style="font-weight:600;font-size:1.1rem;">Add New Device</div>
        <div style="font-size:0.98em;color:#888;text-align:center;margin:8px 0 0 0;">Connect a new smart device to your system</div>
      </div>
      `;

      document.getElementById('devicesRow').innerHTML = html;
    });
  }
  let energyChart;
  function loadEnergy(range='day') {
    persistentFetch('energyData', '/sensor/data', renderEnergyChart, {data:[]});
  }
  function renderEnergyChart(json) {
    let labels = [], data = [];
    if(json.data) {
        json.data.forEach(point => {
          labels.push(new Date(point.timestamp*1000).toLocaleTimeString());
          data.push(point.temperature);
        });
      }
      if (!energyChart) {
        energyChart = new Chart(document.getElementById('energyChart').getContext('2d'), {
          type: 'line',
          data: { labels: labels, datasets: [{ label: 'Energy', data: data, borderColor: '#2563eb', fill: false }] },
          options: { responsive: true, plugins: { legend: { display: false } } }
        });
      } else {
        energyChart.data.labels = labels;
        energyChart.data.datasets[0].data = data;
        energyChart.update();
      }
    }
    function setEnergyRange(range, btn) {
      document.querySelectorAll('.energy-tab').forEach(tab => tab.classList.remove('active'));
      btn.classList.add('active');
      loadEnergy(range);
    }
    function renderSensorCards(json) {
      document.getElementById('tempCard').textContent = (json.temperature !== undefined && !isNaN(json.temperature)) ? json.temperature + '°C' : '--°C';
      document.getElementById('humCard').textContent = (json.humidity !== undefined && !isNaN(json.humidity)) ? json.humidity + '%' : '--%';
      document.getElementById('energyCard').textContent = (json.yesterdayTemp !== undefined && !isNaN(json.yesterdayTemp)) ? json.yesterdayTemp + ' kWh' : '-- kWh';
      document.getElementById('energySub').textContent = (json.yesterdayHum !== undefined && !isNaN(json.yesterdayHum)) ? "Yesterday: " + json.yesterdayHum + "%" : "";
    }
    function renderLogs(txt) {
      document.getElementById('logList').textContent = txt.split('\n').slice(-20).join('\n');
    }
    function renderSystemInfo(json) {
      document.getElementById('uptime').textContent = json.uptime || '--';
      document.getElementById('ipAddr').textContent = json.ip || '--';
      document.getElementById('freeHeap').textContent = json.heap || '--';
    }
    function closeCameraModal() {
      document.getElementById('cameraModal').style.display = 'none';
    }
    window.onload = function() {
      updateRelayStatus();
      loadDevices();
      loadEnergy();
      loadRoutines();
      loadSystemInfo();
      loadLogs();
      loadSensorCards();
      updateESPStatus();
      persistentFetch('routines', '/routines', renderRoutines, []);
      persistentFetch('relayStatus', '/relayStatus', renderRelays, []);
      persistentFetch('sensor', '/sensor', renderSensorCards, {});
      persistentFetch('logs', '/logs', renderLogs, []);
      persistentFetch('energyData', '/sensor/data', renderEnergyChart, []);
      setInterval(updateESPStatus, 2000);
    }
  </script>
  <!-- Routine Modal -->
  <div id="routineModal" style="display:none;position:fixed;top:0;left:0;width:100vw;height:100vh;background:rgba(0,0,0,0.25);z-index:999;align-items:center;justify-content:center;">
    <div style="background:#fff;padding:24px 28px;border-radius:12px;min-width:320px;box-shadow:0 4px 24px #2563eb22;">
      <h3>Add Routine</h3>
      <div style="margin-bottom:10px;">
        <input id="routineName" placeholder="Routine Name" style="width:100%;padding:8px;margin-bottom:8px;">
        <input id="routineTime" type="time" style="width:100%;padding:8px;margin-bottom:8px;">
        <select id="routineRelay" style="width:100%;padding:8px;margin-bottom:8px;">
          <option value="1">Relay 1</option>
          <option value="2">Relay 2</option>
          <option value="3">Relay 3</option>
          <option value="4">Relay 4</option>
          <option value="5">Relay 5</option>
          <option value="6">Relay 6</option>
          <option value="7">Relay 7</option>
          <option value="8">Relay 8</option>
        </select>
        <select id="routineState" style="width:100%;padding:8px;">
          <option value="1">ON</option>
          <option value="0">OFF</option>
        </select>
      </div>
      <button onclick="addRoutine()" style="background:#2563eb;color:#fff;padding:8px 18px;border:none;border-radius:6px;">Add</button>
      <button onclick="closeRoutineModal()" style="margin-left:10px;">Cancel</button>
      <div id="routineError" style="color:#e74c3c;margin-top:8px;display:none;"></div>
    </div>
  </div>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleWiFiStatus() {
  StaticJsonDocument<64> doc;
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSettings() {
  if (requireLogin()) return;
  
  if (handleFileRead("/settings.html")) return;
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Settings</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      --bg-color: #f4f7fb;
      --card-bg: #fff;
      --text-color: #222;
      --text-secondary: #888;
      --primary-color: #0072ff;
      --secondary-color: #00c6ff;
      --border-color: #eee;
      --shadow-color: rgba(0,0,0,0.07);
    }
    .dark-mode {
      --bg-color: #121212;
      --card-bg: #1e1e1e;
      --text-color: #e0e0e0;
      --text-secondary: #aaa;
      --primary-color: #0099ff;
      --secondary-color: #00c6ff;
      --border-color: #333;
      --shadow-color: rgba(0,0,0,0.3);
    }
    body {
      background: var(--bg-color);
      color: var(--text-color);
      transition: background 0.3s ease;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .settings-container {
      background: var(--card-bg);
      border-radius: 10px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.1);
      width: 100%;
      max-width: 600px;
      padding: 40px;
    }
    .settings-title {
      color: #2c3e50;
      font-size: 22px;
      font-weight: 600;
      margin-bottom: 30px;
      text-align: center;
    }
    .settings-tabs {
      display: flex;
      border-bottom: 1px solid #eee;
      margin-bottom: 20px;
    }
    .settings-tab {
      padding: 10px 20px;
      cursor: pointer;
      font-weight: 500;
      color: #7f8c8d;
    }
    .settings-tab.active {
      color: #0072ff;
      border-bottom: 2px solid #0072ff;
    }
    .settings-content {
      display: none;
    }
    .settings-content.active {
      display: block;
    }
    .input-group {
      margin-bottom: 20px;
    }
    .input-group label {
      display: block;
      margin-bottom: 8px;
      color: #2c3e50;
      font-size: 14px;
      font-weight: 500;
    }
    .input-group input, .input-group select {
      width: 100%;
      padding: 12px 15px;
      border: 1px solid #e0e0e0;
      border-radius: 6px;
      font-size: 14px;
      transition: border-color 0.3s;
    }
    .input-group input:focus, .input-group select:focus {
      outline: none;
      border-color: #3498db;
    }
    .settings-button {
      width: 100%;
      padding: 12px;
      background-color: #3498db;
      color: white;
      border: none;
      border-radius: 6px;
      font-size: 16px;
      font-weight: 500;
      cursor: pointer;
      transition: background-color 0.3s;
    }
    .settings-button:hover {
      background-color: #2980b9;
    }
    .error-message {
      color: #e74c3c;
      font-size: 14px;
      margin-top: 15px;
      display: none;
    }
    .success-message {
      color: #27ae60;
      font-size: 14px;
      margin-top: 15px;
      display: none;
    }
    .scene-config {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-bottom: 20px;
    }
    .scene-item {
      flex: 1 1 200px;
      background: #f8f9fa;
      padding: 15px;
      border-radius: 8px;
    }
    .scene-item h3 {
      margin-top: 0;
      color: #2c3e50;
    }
    .scene-toggle {
      display: flex;
      align-items: center;
      margin-bottom: 8px;
    }
    .scene-toggle label {
      margin-left: 8px;
    }
    @media (max-width: 700px) {
      .settings-container { padding: 16px; }
      .scene-item { min-width: 140px; }
    }
    @media (max-width: 480px) {
      .settings-container { padding: 6px; }
      .scene-item { min-width: 100px; }
    }
  </style>
</head>
<body>
  <div class="settings-container">
    <div class="settings-title">System Settings</div>
    <div class="settings-tabs">
      <div class="settings-tab active" onclick="showTab('credentials')">Credentials</div>
      <div class="settings-tab" onclick="showTab('scenes')">Scenes</div>
      <div class="settings-tab" onclick="showTab('system')">System</div>
    </div>
    <div id="credentials" class="settings-content active">
      <form id="credentialsForm">
        <div class="input-group">
          <label for="username">Username</label>
          <input type="text" id="username" name="username" value="%USERNAME%" required>
        </div>
        <div class="input-group">
          <label for="password">Password</label>
          <input type="password" id="password" name="password" required>
        </div>
        <div class="input-group">
          <label for="birthday">Birthday (for notifications)</label>
          <input type="date" id="birthday" name="birthday" value="%BIRTHDAY%">
        </div>
        <button type="button" class="settings-button" onclick="saveCredentials()">Save Credentials</button>
        <div id="credError" class="error-message"></div>
        <div id="credSuccess" class="success-message"></div>
      </form>
    </div>
    <div id="scenes" class="settings-content">
      <div class="scene-config">
        %SCENE_CONFIG%
      </div>
      <button type="button" class="settings-button" onclick="saveScenes()">Save Scenes</button>
      <div id="sceneError" class="error-message"></div>
      <div id="sceneSuccess" class="success-message"></div>
    </div>
    <div id="system" class="settings-content">
      <div class="input-group">
        <label for="restart">Restart System</label>
        <button type="button" class="settings-button" onclick="restartSystem()">Restart</button>
      </div>
      <div class="input-group">
        <label for="logs">System Logs</label>
        <textarea id="logs" rows="10" style="width:100%;" readonly>%LOGS%</textarea>
      </div>
      <div class="input-group">
        <label for="sensorData">Sensor Data (Last 24h)</label>
        <div id="sensorChart" style="width:100%; height:200px; background:#f8fafc;"></div>
      </div>
    </div>
  </div>
  <script>
    // Tab switching logic
    function showTab(tabId) {
      document.querySelectorAll('.settings-tab').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.settings-content').forEach(content => content.classList.remove('active'));
      document.querySelector(`.settings-tab[onclick="showTab('${tabId}')"]`).classList.add('active');
      document.getElementById(tabId).classList.add('active');
    }
    // Save credentials
    function saveCredentials() {
      const form = document.getElementById('credentialsForm');
      const formData = new FormData(form);
      const errorEl = document.getElementById('credError');
      const successEl = document.getElementById('credSuccess');
      fetch('/settings/credentials', {
        method: 'POST',
        body: formData,
        credentials: 'include'
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          errorEl.style.display = 'none';
          successEl.textContent = 'Credentials updated successfully!';
          successEl.style.display = 'block';
          setTimeout(() => successEl.style.display = 'none', 3000);
        } else {
          successEl.style.display = 'none';
          errorEl.textContent = data.error || 'Failed to update credentials';
          errorEl.style.display = 'block';
        }
      })
      .catch(error => {
        successEl.style.display = 'none';
        errorEl.textContent = 'Network error';
        errorEl.style.display = 'block';
      });
    }
    // Save scenes
    function saveScenes() {
      const scenes = [];
      document.querySelectorAll('.scene-item').forEach(item => {
        const scene = {
          name: item.querySelector('h3').textContent,
          states: []
        };
        item.querySelectorAll('input[type="checkbox"]').forEach(checkbox => {
          scene.states.push(checkbox.checked);
        });
        scenes.push(scene);
      });
      fetch('/settings/scenes', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scenes }),
        credentials: 'include'
      })
      .then(response => response.json())
      .then(data => {
        const errorEl = document.getElementById('sceneError');
        const successEl = document.getElementById('sceneSuccess');
        if (data.success) {
          errorEl.style.display = 'none';
          successEl.textContent = 'Scenes updated successfully!';
          successEl.style.display = 'block';
          setTimeout(() => successEl.style.display = 'none', 3000);
        } else {
          successEl.style.display = 'none';
          errorEl.textContent = data.error || 'Failed to update scenes';
          errorEl.style.display = 'block';
        }
      });
    }
    // Restart system
    function restartSystem() {
      if (confirm('Are you sure you want to restart the system?')) {
        fetch('/system/restart', { credentials: 'include' })
          .then(() => {
            setTimeout(() => {
              alert('System is restarting...');
              window.location.href = '/';
            }, 2000);
          });
      }
    }
    // (Optional) Load sensor data for chart
    fetch('/sensor/data', { credentials: 'include' })
      .then(response => response.json())
      .then(data => {
        // TODO: Render chart with data
      });
  </script>
</body>
</html>
)rawliteral";

  // Build scene configuration HTML
  String sceneConfigHtml;
  for (int i = 0; i < SCENE_COUNT; i++) {
    sceneConfigHtml += "<div class=\"scene-item\"><h3>" + String(scenes[i].name) + "</h3>";
    for (int j = 0; j < RELAY_COUNT; j++) {
      sceneConfigHtml += "<div class=\"scene-toggle\">";
      sceneConfigHtml += "<input type=\"checkbox\" id=\"scene" + String(i) + "_relay" + String(j) + "\" ";
      sceneConfigHtml += scenes[i].relayStates[j] ? "checked" : "";
      sceneConfigHtml += "><label for=\"scene" + String(i) + "_relay" + String(j) + "\">Relay " + String(j+1) + "</label>";
      sceneConfigHtml += "</div>";
    }
    sceneConfigHtml += "</div>";
  }

  // Replace placeholders
  html.replace("%USERNAME%", savedUsername);
  html.replace("%BIRTHDAY%", savedBirthday);
  html.replace("%SCENE_CONFIG%", sceneConfigHtml);
  html.replace("%LOGS%", logBuffer.substring(logBuffer.length() - 2000)); // Last 2000 chars
  
  server.send(200, "text/html", html);
}

void handleSettingsCredentials() {
  if (requireLogin()) return;
  
  if (server.method() == HTTP_POST) {
    String newUser = server.arg("username");
    String newPass = server.arg("password");
    String newBirthday = server.arg("birthday");
    
    if (newUser.length() == 0 || newPass.length() == 0) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Username and password cannot be empty\"}");
      return;
    }
    
    saveCredentials(newUser, newPass, newBirthday);
    server.send(200, "application/json", "{\"success\":true}");
  }
}

void handleSettingsScenes() {
  if (requireLogin()) return;
  
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, body);
    JsonArray sceneArray = doc["scenes"].as<JsonArray>();

    if (sceneArray.size() != SCENE_COUNT) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid scene count\"}");
      return;
    }
    
    for (int i = 0; i < SCENE_COUNT; i++) {
      JsonArray states = sceneArray[i]["states"];
      if (states.size() != RELAY_COUNT) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay count in scene\"}");
        return;
      }
      
      for (int j = 0; j < RELAY_COUNT; j++) {
        const_cast<bool&>(scenes[i].relayStates[j]) = states[j];
      }
    }
    
    saveSceneStates();
    server.send(200, "application/json", "{\"success\":true}");
  }
}

void handleSystemRestart() {
  if (requireLogin()) return;
  
  server.send(200, "application/json", "{\"success\":true}");
  addLog("System restart initiated");
  delay(1000);
  ESP.restart();
}

void handleResetPass() {
  if (handleFileRead("/resetpass.html")) return;
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Reset Password</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      --bg-color: #f4f7fb;
      --card-bg: #fff;
      --text-color: #222;
      --text-secondary: #888;
      --primary-color: #0072ff;
      --secondary-color: #00c6ff;
      --border-color: #eee;
      --shadow-color: rgba(0,0,0,0.07);
    }

    .dark-mode {
      --bg-color: #121212;
      --card-bg: #1e1e1e;
      --text-color: #e0e0e0;
      --text-secondary: #aaa;
      --primary-color: #0099ff;
      --secondary-color: #00c6ff;
      --border-color: #333;
      --shadow-color: rgba(0,0,0,0.3);
    }

    body {
      background: var(--bg-color);
      color: var(--text-color);
      transition: background 0.3s ease;
    }

    .reset-container {
      background: var(--card-bg);
      border-radius: 24px;
      box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18);
      padding: 2.8rem 2.2rem 2.2rem 2.2rem;
      width: 100%;
      max-width: 370px;
    }

    .reset-title {
      font-size: 2.2rem;
      font-weight: 800;
      color: var(--primary-color);
      margin-bottom: 1.7rem;
      letter-spacing: 1.5px;
      text-align: center;
    }

    .input-group {
      margin-bottom: 1.3rem;
    }

    .input-group label {
      display: block;
      margin-bottom: 0.5rem;
      color: var(--primary-color);
      font-weight: 600;
    }

    .input-group input {
      width: 100%;
      padding: 0.8rem 1.1rem;
      border: none;
      border-radius: 8px;
      font-size: 1.08rem;
      background: #f0f7fa;
    }

    .reset-btn {
      width: 100%;
      padding: 0.9rem;
      background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%);
      color: #fff;
      border: none;
      border-radius: 8px;
      font-size: 1.15rem;
      font-weight: 700;
      cursor: pointer;
      margin-top: 0.7rem;
    }

    .footer {
      margin-top: 2.2rem;
      text-align: center;
      color: #aaa;
      font-size: 1rem;
    }

    .error-message {
      color: #e74c3c;
      background: #fff0f0;
      border-radius: 6px;
      padding: 0.6rem 1.1rem;
      margin-top: 1.1rem;
      text-align: center;
      font-weight: 600;
      display: none;
    }

    .success-message {
      color: #27ae60;
      background: #f0fff4;
      border-radius: 6px;
      padding: 0.6rem 1.1rem;
      margin-top: 1.1rem;
      text-align: center;
      font-weight: 600;
      display: none;
    }

    .dark-toggle {
      position: fixed;
      bottom: 20px;
      right: 20px;
      background: var(--primary-color);
      color: #fff;
      border: none;
      border-radius: 50%;
      width: 50px;
      height: 50px;
      display: flex;
      align-items: center;
      justify-content: center;
      cursor: pointer;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
      transition: all 0.3s ease;
    }
  </style>
</head>
<body>
  <div class="reset-container">
    <div class="reset-title">Reset Password</div>
    <form id="resetForm">
      <div class="input-group">
        <label for="current_password">Current Password</label>
        <input type="password" id="current_password" name="current_password" required>
      </div>
      <div class="input-group">
        <label for="username">New Username</label>
        <input type="text" id="username" name="username" required>
      </div>
      <div class="input-group">
        <label for="password">New Password</label>
        <input type="password" id="password" name="password" required>
      </div>
      <button type="button" class="reset-btn" onclick="resetPassword()">Reset</button>
      <div id="errorMessage" class="error-message"></div>
      <div id="successMessage" class="success-message"></div>
    </form>
    <div class="footer">©️ 2024 IoT Dashboard</div>
  </div>
  <script>
    function resetPassword() {
      const currentPass = document.getElementById('current_password').value;
      const username = document.getElementById('username').value;
      const password = document.getElementById('password').value;
      const errorEl = document.getElementById('errorMessage');
      const successEl = document.getElementById('successMessage');
      
      if (!currentPass || !username || !password) {
        errorEl.textContent = 'All fields are required';
        errorEl.style.display = 'block';
        successEl.style.display = 'none';
        return;
      }
      
      fetch('/resetpass', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          current_password: currentPass,
          username: username,
          password: password
        })
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          errorEl.style.display = 'none';
          successEl.textContent = 'Password reset successful! Redirecting to login...';
          successEl.style.display = 'block';
          setTimeout(() => window.location.href = '/login', 2000);
        } else {
          successEl.style.display = 'none';
          errorEl.textContent = data.error || 'Password reset failed';
          errorEl.style.display = 'block';
        }
      });
    }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleResetPassPost() {
  if (requireLogin()) return;
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  String currentPass = doc["current_password"];
  String newUser = doc["username"];
  String newPass = doc["password"];
  
  if (currentPass.length() == 0 || newUser.length() == 0 || newPass.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"All fields are required\"}");
    return;
  }
  
  if (currentPass != savedPassword) {
    server.send(401, "application/json", "{\"success\":false,\"error\":\"Current password is incorrect\"}");
    return;
  }
  
  saveCredentials(newUser, newPass);
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRelayToggle() {
  if (requireLogin()) return;
  
  if (!server.hasArg("num") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }
  
  int num = server.arg("num").toInt();
  int state = server.arg("state").toInt();
  
  // Example for relay number validation in handleRelayToggle()
  if (num < 1 || num > RELAY_COUNT) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay number\"}");
    return;
  }
  
  relayStates[num - 1] = (state == 1);



  digitalWrite(relayPins[num - 1], relayStates[num - 1] ? HIGH : LOW);
  saveRelayStates();
  addLog("Relay " + String(num) + (relayStates[num - 1] ? " ON" : " OFF"));
  
  // Record the relay event
  recordRelayEvent(num, relayStates[num - 1], "api");
  
  server.send(200, "application/json", "{\"success\":true}");
}


void handleRelayStatus() {
  if (requireLogin()) return;

  StaticJsonDocument<512> doc;
  JsonArray relays = doc.createNestedArray("relays");
  for (int i = 0; i < RELAY_COUNT; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["num"] = i + 1;
    relay["name"] = roomNames[i];
    relay["state"] = relayStates[i];
  }
  doc["timestamp"] = millis() / 1000;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}


void handleScene() {
  if (requireLogin()) return;
  
  if (!server.hasArg("idx")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing scene index\"}");
    return;
  }
  
  int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= SCENE_COUNT) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid scene index\"}");
    return;
  }
  
  applyScene(idx);
  
  DynamicJsonDocument doc(1024);
  JsonArray states = doc.createNestedArray("states");
  for (int i = 0; i < RELAY_COUNT; i++) {
    states.add(relayStates[i]);
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSensor() {
  if (requireLogin()) return;

  StaticJsonDocument<256> doc;
  doc["temperature"] = currentTemp;
  doc["humidity"] = currentHum;
  doc["yesterdayTemp"] = dailyTempAverage[1];
  doc["yesterdayHum"] = dailyHumAverage[1];

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSimpleLogs() {
  if (requireLogin()) return;
  
  server.send(200, "text/plain", logBuffer);
}

void handleJarvisRelay() {
  // Example: /api/jarvis/relay?room=0&switch=1&state=on
  if (!server.hasArg("room") || !server.hasArg("switch") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }
  
  int relayNum = server.arg("switch").toInt();
  bool state = (server.arg("state") == "on");
  
  if (relayNum < 1 || relayNum > RELAY_COUNT) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay number\"}");
    return;
  }
  
  relayStates[relayNum - 1] = state;
  digitalWrite(relayPins[relayNum - 1], state ? HIGH : LOW);
  saveRelayStates();
  addLog("Jarvis: Room " + server.arg("room") + " Relay " + String(relayNum) + (state ? " ON" : " OFF"));
  
  // Record the relay event
  recordRelayEvent(relayNum, state, "api");
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleOTAUpdate() {
  if (requireLogin()) return;
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (upload.totalSize > MAX_FW_SIZE) {
      server.send(400, "text/plain", "Firmware file too large!");
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleOTAFinish() {
  if (requireLogin()) return;
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

void handleOTAWeb() {
  if (requireLogin()) return;
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>OTA Firmware Update</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #eef3fc; color: #222; }
    .ota-container { max-width: 400px; margin: 60px auto; background: #fff; border-radius: 12px; box-shadow: 0 2px 12px #e3e9f7; padding: 32px; }
    h2 { text-align: center; }
    input[type=file] { width: 100%; margin-bottom: 18px; }
    .ota-btn { width: 100%; padding: 12px; background: #2563eb; color: #fff; border: none; border-radius: 8px; font-size: 1.1rem; cursor: pointer; }
    .ota-btn:active { background: #1746a2; }
    .ota-status { margin-top: 18px; text-align: center; }
  </style>
</head>
<body>
  <div class="ota-container">
    <h2>OTA Firmware Update</h2>
    <form id="otaForm" method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" required>
      <button class="ota-btn" type="submit">Upload & Update</button>
    </form>
    <div class="ota-status" id="otaStatus"></div>
    <div style="margin-top:18px; text-align:center;">
      <a href="/">Back to Dashboard</a>
    </div>
  <div class="ota-status" id="otaStatus"></div> 
<div class="ota-status" id="otaStatus"></div>
<pre id="otaConsole" style="background:#222;color:#0f0;padding:12px 8px;border-radius:8px;min-height:60px;max-height:180px;overflow:auto;font-size:0.98rem;margin-top:10px;"></pre>

<script>
const MAX_FW_SIZE = 1572864; // 1.5MB (adjust to your partition table)
const otaConsole = document.getElementById('otaConsole');

function logToConsole(msg) {
  otaConsole.textContent += msg + '\n';
  otaConsole.scrollTop = otaConsole.scrollHeight;
}

document.getElementById('otaForm').onsubmit = function(e) {
  e.preventDefault();
  var form = e.target;
  var fileInput = form.querySelector('input[type="file"]');
  var file = fileInput.files[0];
  otaConsole.textContent = '';
  if (file.size > MAX_FW_SIZE) {
    document.getElementById('otaStatus').innerText = 'Firmware file is too large! Max allowed: ' + (MAX_FW_SIZE/1024/1024).toFixed(2) + ' MB';
    logToConsole('❌ File too large: ' + (file.size/1024/1024).toFixed(2) + ' MB');
    return false;
  }
  logToConsole('Selected file: ' + file.name + ' (' + (file.size/1024).toFixed(1) + ' KB)');
  logToConsole('Starting upload...');
  var data = new FormData(form);
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      let percent = Math.round(e.loaded / e.total * 100);
      document.getElementById('otaStatus').innerText = 'Uploading: ' + percent + '%';
      logToConsole('Uploading: ' + percent + '% (' + (e.loaded/1024).toFixed(1) + ' KB / ' + (e.total/1024).toFixed(1) + ' KB)');
    }
  };
  xhr.onloadstart = function() {
    logToConsole('Upload started...');
  };
  xhr.onerror = function() {
    document.getElementById('otaStatus').innerText = 'Update failed!';
    logToConsole('❌ Upload failed (network error)');
  };
  xhr.onload = function() {
    if (xhr.status == 200) {
      document.getElementById('otaStatus').innerText = 'Update successful! Rebooting...';
      logToConsole('✅ Update successful! Device rebooting...');
      setTimeout(function(){ location.href = '/'; }, 4000);
    } else {
      document.getElementById('otaStatus').innerText = 'Update failed!';
      logToConsole('❌ Update failed! HTTP status: ' + xhr.status);
    }
  };
  xhr.send(data);
};
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleSystemInfo() {
  if (requireLogin()) return;

  StaticJsonDocument<256> doc;

  // Uptime in seconds
  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  unsigned long min = sec / 60;
  unsigned long hr = min / 60;
  char uptimeStr[32];
  snprintf(uptimeStr, sizeof(uptimeStr), "%lu:%02lu:%02lu", hr, min % 60, sec % 60);
  doc["uptime"] = uptimeStr;

  // IP address
  doc["ip"] = WiFi.localIP().toString();

  // Free heap
  doc["heap"] = ESP.getFreeHeap();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleDeviceStatus() {
  if (requireLogin()) return;
  
  StaticJsonDocument<256> doc;
  
  // Count active devices (relays that are turned on)
  int activeCount = 0;
  for (int i = 0; i < RELAY_COUNT; i++) {
    if (relayStates[i]) activeCount++;
  }
  
  // Calculate new devices added today based on status history
  int newDevices = 0;
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  // Track which relays have already been counted
  bool relaysCounted[RELAY_COUNT] = {false};
  
  // Loop through status history to find events from today
  for (int i = 0; i < statusHistoryCount; i++) {
    struct tm event_time;
    localtime_r(&statusHistory[i].timestamp, &event_time);
    
    // Check if this event is from today
    if (event_time.tm_year == timeinfo.tm_year && 
        event_time.tm_mon == timeinfo.tm_mon && 
        event_time.tm_mday == timeinfo.tm_mday) {
        
      int relayIdx = statusHistory[i].relayNum - 1;
      if (relayIdx >= 0 && relayIdx < RELAY_COUNT) {
        // If this relay was turned ON today and we haven't counted it yet
        if (statusHistory[i].state && !relaysCounted[relayIdx]) {
          newDevices++;
          relaysCounted[relayIdx] = true;
        }
      }
    }
  }
  
  doc["activeCount"] = activeCount;
  doc["totalCount"] = RELAY_COUNT;
  doc["newDevices"] = newDevices;
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void updateDailyTemperature() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  unsigned long now = millis();
  if (now - lastDayUpdate > 86400000 || lastDayUpdate == 0) {
    // Shift history
    for (int i = TEMP_HISTORY_SIZE - 1; i > 0; i--) {
      dailyTempAverage[i] = dailyTempAverage[i-1];
      dailyHumAverage[i] = dailyHumAverage[i-1];
    }
    // Store the true daily average (mean of all readings)
    if (sampleCount > 0) {
      dailyTempAverage[0] = tempSum / sampleCount;
      dailyHumAverage[0] = humSum / sampleCount;
    } else {
      dailyTempAverage[0] = currentTemp;
      dailyHumAverage[0] = currentHum;
    }
    // Reset for next day
    tempSum = 0;
    humSum = 0;
    sampleCount = 0;
    lastDayUpdate = now;
    DEBUG_INFO(SENSORS, "Updated daily averages: " + String(dailyTempAverage[0]) + "°C, " + String(dailyHumAverage[0]) + "%");
  }
}

bool isIPBlocked(const String& ip) {
  // Check if IP is in blocklist
  for (int i = 0; i < loginAttemptCount; i++) {
    if (loginAttempts[i].ipAddress == ip) {
      int failCount = 0;
      time_t now;
      time(&now);
      
      // Count recent failed attempts from this IP
      for (int j = 0; j < loginAttemptCount; j++) {
        if (loginAttempts[j].ipAddress == ip && 
            !loginAttempts[j].success && 
            difftime(now, loginAttempts[j].timestamp) < LOCKOUT_DURATION) {
          failCount++;
        }
      }
      
      // Block if too many failed attempts
      if (failCount >= MAX_FAILED_ATTEMPTS) {
        return true;
      }
    }
  }
  return false;
}

void recordLoginAttempt(const String& ip, bool success) {
  // Add to the circular buffer
  if (loginAttemptCount < MAX_LOGIN_ATTEMPTS) {
    loginAttempts[loginAttemptCount].ipAddress = ip;
    time(&loginAttempts[loginAttemptCount].timestamp);
    loginAttempts[loginAttemptCount].success = success;
    loginAttemptCount++;
  } else {
    // Shift existing entries
    for (int i = 0; i < MAX_LOGIN_ATTEMPTS - 1; i++) {
      loginAttempts[i] = loginAttempts[i + 1];
    }
    // Add new entry at the end
    loginAttempts[MAX_LOGIN_ATTEMPTS - 1].ipAddress = ip;
    time(&loginAttempts[MAX_LOGIN_ATTEMPTS - 1].timestamp);
    loginAttempts[MAX_LOGIN_ATTEMPTS - 1].success = success;
  }
}

void recordRelayEvent(int relayNum, bool state, const String& source) {
  // Add to circular buffer
  if (statusHistoryCount < MAX_STATUS_HISTORY) {
    statusHistory[statusHistoryCount].relayNum = relayNum;
    time(&statusHistory[statusHistoryCount].timestamp);
    statusHistory[statusHistoryCount].state = state;
    statusHistory[statusHistoryCount].source = source;
    statusHistoryCount++;
  } else {
    // Shift entries
    for (int i = 0; i < MAX_STATUS_HISTORY - 1; i++) {
      statusHistory[i] = statusHistory[i + 1];
    }
    // Add new entry at the end
    statusHistory[MAX_STATUS_HISTORY - 1].relayNum = relayNum;
    time(&statusHistory[MAX_STATUS_HISTORY - 1].timestamp);
    statusHistory[MAX_STATUS_HISTORY - 1].state = state;
    statusHistory[MAX_STATUS_HISTORY - 1].source = source;
  }
}

void handleSensorData() {
  if (requireLogin()) return;

  StaticJsonDocument<4096> doc;
  JsonArray arr = doc["data"].to<JsonArray>();
  for (int i = 0; i < dataCount; i++) {
    JsonObject point = arr.createNestedObject();
    point["timestamp"] = dataPoints[i].timestamp;
    point["temperature"] = dataPoints[i].temperature;
    point["humidity"] = dataPoints[i].humidity;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void setupSchedules() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    schedules[i].active = false;
    schedules[i].id = i;
    schedules[i].alarmId = dtINVALID_ALARM_ID;
  }
}

void handleSchedules() {
  if (requireLogin()) return;

  if (server.method() == HTTP_GET) {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Automation Schedule</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
  <style>
    #offlineBanner {
      display: none;
      position: fixed;
      top: 0; left: 0; width: 100vw;
      background: #e74c3c;
      color: #fff;
      text-align: center;
      padding: 8px 0;
      z-index: 9999;
      font-weight: 600;
      letter-spacing: 1px;
    }
    body.offline #offlineBanner { display: block; }
    body { background: #f4f7fb; font-family: 'Segoe UI', Arial, sans-serif; color: #222; margin: 0; }
    .main-flex {
      display: flex;
      gap: 32px;
      max-width: 1400px;
      margin: 40px auto;
      justify-content: center;
      flex-wrap: wrap;
    }
    .card {
      background: #fff;
      border-radius: 18px;
      box-shadow: 0 4px 24px #e3e9f7;
      padding: 36px 32px;
      flex: 1 1 320px;
      min-width: 0;
      max-width: 350px;
      transition: box-shadow 0.2s;
      margin-bottom: 24px;
      box-sizing: border-box;
    }
    .card:hover { box-shadow: 0 8px 32px #2563eb22; }
    .card h3 { margin-top: 0; font-size: 1.3rem; font-weight: 700; color: #2563eb; letter-spacing: 0.5px; }
    #savedSchedules table { width: 100%; border-collapse: collapse; margin-top: 12px; }
    #savedSchedules th, #savedSchedules td { padding: 8px 6px; text-align: center; font-size: 1rem; }
    #savedSchedules th { background: #f4f7fb; color: #2563eb; font-weight: 600; border-bottom: 2px solid #e3e9f7; }
    #savedSchedules tr { border-bottom: 1px solid #f0f0f0; }
    #savedSchedules tr:last-child { border-bottom: none; }
    .delete-btn { background: #e74c3c; color: #fff; border: none; border-radius: 6px; padding: 6px 12px; cursor: pointer; font-size: 1em; }
    .delete-btn:hover { background: #c0392b; }
    input[type="text"], textarea, select, input[type="time"] {
      width: 100%; padding: 10px 12px; border-radius: 8px; border: 1px solid #e0e0e0;
      margin-top: 6px; font-size: 1rem; background: #f8fafc; transition: border 0.2s;
      box-sizing: border-box;
    }
    input[type="text"]:focus, textarea:focus, select:focus, input[type="time"]:focus {
      border: 1.5px solid #2563eb; outline: none;
    }
    .save-btn { background: #2563eb; color: #fff; border: none; border-radius: 8px; padding: 14px 0; font-size: 1.1rem; cursor: pointer; width: 100%; margin-top: 18px; font-weight: 600; box-shadow: 0 2px 8px #2563eb22; transition: background 0.2s; }
    .save-btn:hover { background: #1746a2; }
    .days-row { display: flex; gap: 8px; margin: 12px 0; justify-content: center; flex-wrap: wrap; }
    .day-btn { width: 38px; height: 38px; border-radius: 50%; display: flex; align-items: center; justify-content: center; background: #f4f7fb; color: #2563eb; font-weight: 600; cursor: pointer; border:2px solid #f4f7fb; transition:all 0.2s; font-size: 1.1rem; }
    .day-btn.selected { background: #2563eb; color: #fff; border-color: #2563eb; box-shadow: 0 2px 8px #2563eb33; }
    .section-title { font-weight:600; margin-top:24px; color: #2563eb; font-size: 1.05rem; }
    .adv-options label { display:block; margin-bottom:8px; font-size: 0.98rem; }
    .cond-box { background:#f8fafc; border-radius:8px; padding:10px 14px; margin-bottom:8px; display:flex; align-items:center; gap:8px; font-size: 0.98rem; }
    .priority-select { width: 100%; padding: 8px; border-radius: 6px; border: 1px solid #e0e0e0; margin-top: 6px; }
    @media (max-width: 1100px) {
      .main-flex { flex-direction: column; align-items: stretch; gap: 0; }
      .card { max-width: 98vw; margin: 18px auto; }
    }
    @media (max-width: 700px) {
      .main-flex { flex-direction: column; gap: 0; margin: 10px auto; }
      .card { max-width: 99vw; min-width: 0; width: 100%; padding: 18px 8px; }
      .card h3 { font-size: 1.1rem; }
      .save-btn { font-size: 1rem; padding: 10px 0; }
      .section-title { font-size: 1rem; margin-top: 16px; }
      .days-row { gap: 4px; }
      .day-btn { width: 32px; height: 32px; font-size: 1rem; }
      input[type="text"], textarea, select, input[type="time"] { font-size: 0.98rem; padding: 8px 8px; }
      #savedSchedules th, #savedSchedules td { font-size: 0.95rem; padding: 5px 2px; }
      #savedSchedules { overflow-x: auto; }
    }
    @media (max-width: 480px) {
      .main-flex { margin: 0; }
      .card { padding: 10px 2vw; width: 100%; min-width: 0; }
      .save-btn { font-size: 0.98rem; }
      .section-title { font-size: 0.98rem; }
      .day-btn { width: 28px; height: 28px; font-size: 0.95rem; }
      #savedSchedules { overflow-x: auto; }
    }
  </style>
</head>
<body>
<div id="offlineBanner">Device Disconnected - Showing Last Known Data</div>
  <form id="automationForm">
    <div class="main-flex">
      <div class="card">
        <h3>Automation Details</h3>
        <label>Name<br><input type="text" id="autoName" style="width:100%;padding:8px;" required></label>
        <label class="section-title">Description<br>
          <textarea id="autoDesc" style="width:100%;padding:8px;" rows="3"></textarea>
        </label>
        <label class="section-title">Status<br>
          <label class="toggle-switch">
            <input type="checkbox" id="autoActive" checked>
            <span class="slider-toggle"></span>
          </label>
        </label>
        <label class="section-title">Device<br>
          <select id="relayNum" style="width:100%;padding:8px;">
            <option value="1">Relay 1 - Living Room</option>
            <option value="2">Relay 2 - Bedroom</option>
            <option value="3">Relay 3 - Kitchen</option>
            <option value="4">Relay 4 - Bathroom</option>
            <option value="5">Relay 5 - Garage</option>
            <option value="6">Relay 6 - Porch</option>
            <option value="7">Relay 7 - Study</option>
            <option value="8">Relay 8 - Spare</option>
          </select>
        </label>
        <div id="devicesList">
          <div class="cond-box"><span class="material-icons" style="color:#fbc02d;">lightbulb</span> Living Room Light</div>
          <div class="cond-box"><span class="material-icons" style="color:#e57373;">thermostat</span> Thermostat</div>
        </div>
      </div>
      <div class="card">
        <h3>Schedule Type</h3>
        <label><input type="radio" name="schedType" value="daily" checked> Daily Schedule</label><br>
        <label><input type="radio" name="schedType" value="weekly"> Weekly Schedule</label><br>
        <label><input type="radio" name="schedType" value="custom"> Custom Schedule</label>
        <div class="section-title">Days</div>
        <div class="days-row">
          <span class="day-btn selected" data-day="1">M</span>
          <span class="day-btn selected" data-day="2">T</span>
          <span class="day-btn selected" data-day="3">W</span>
          <span class="day-btn selected" data-day="4">T</span>
          <span class="day-btn selected" data-day="5">F</span>
          <span class="day-btn" data-day="6">S</span>
          <span class="day-btn" data-day="0">S</span>
        </div>
        <div class="section-title">Time</div>
        <label>Start Time <input type="time" id="startTime" value="07:00"></label>
        <label>End Time (Optional) <input type="time" id="endTime" value="07:30"></label>
        <div class="section-title">Repeat</div>
        <select id="repeatSelect" class="priority-select">
          <option value="everyday">Every day</option>
          <option value="weekdays">Weekdays</option>
          <option value="weekends">Weekends</option>
          <option value="custom">Custom</option>
        </select>
      </div>
      <div class="card">
        <h3>Advanced Options</h3>
        <div class="adv-options">
          <label><input type="checkbox"> Run only when someone is home</label>
          <label><input type="checkbox"> Skip on holidays</label>
          <label><input type="checkbox" checked> Send notification when run</label>
          <label><input type="checkbox"> Run only if light level is below threshold</label>
        </div>
        <div class="section-title">Conditions</div>
        <div class="cond-box"><span class="material-icons" style="color:#2196f3;">cloud</span> Weather <span style="font-size:0.95em;color:#888;">Only run if not raining</span></div>
        <button type="button" class="save-btn" onclick="alert('Add Condition')">+ Add Condition</button>
        <div class="section-title">Priority</div>
        <select class="priority-select">
          <option>Normal</option>
          <option>High</option>
          <option>Low</option>
        </select>
        <button type="submit" class="save-btn" style="margin-top:24px;">Save Schedule</button>
      </div>
    </div>
    <!-- Move Saved Schedules below the main-flex for no overlap -->
    <div class="card" style="margin:32px auto;max-width:900px;">
      <h3>Saved Schedules</h3>
      <div id="savedSchedules"></div>
    </div>
  </form>
  <script>
    document.querySelectorAll('.day-btn').forEach(btn => {
      btn.onclick = () => btn.classList.toggle('selected');
    });

    document.getElementById('automationForm').onsubmit = function(e) {
      e.preventDefault();
      let days = Array(7).fill(false);
      document.querySelectorAll('.day-btn.selected').forEach(btn => {
        days[parseInt(btn.dataset.day)] = true;
      });
      const name = document.getElementById('autoName').value;
      const desc = document.getElementById('autoDesc').value;
      const active = document.getElementById('autoActive').checked;
      const startTime = document.getElementById('startTime').value;
      const endTime = document.getElementById('endTime').value;
      const repeat = document.getElementById('repeatSelect').value;
      const relayNum = parseInt(document.getElementById('relayNum').value);
      const state = true;
      const schedule = {
        name,
        active,
        startTime,
        endTime,
        relayNum,
        state,
        days,
        repeat: true
      };
      fetch('/schedules', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({schedules: [schedule]})
      })
      .then(r => r.json())
      .then(j => {
        if (j.success) {
          alert('Schedule saved!');
          window.location.reload();
        } else {
          alert('Failed to save: ' + (j.error || 'Unknown error'));
        }
      });
    };

    function deleteSchedule(idx) {
      if (!confirm('Delete this schedule?')) return;
      fetch('/schedules/delete', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({index: idx})
      })
      .then(r => r.json())
      .then(j => {
        if (j.success) {
          loadSavedSchedules();
        } else {
          alert('Failed to delete: ' + (j.error || 'Unknown error'));
        }
      });
    }

    function loadSavedSchedules() {
      fetch('/schedules/list', { credentials: 'include' })
        .then(r => r.json())
        .then(schedules => {
          let html = '';
          if (schedules.length === 0) {
            html = '<div style="color:#888;">No schedules saved.</div>';
          } else {
            html = `<table style="width:100%;border-collapse:collapse;">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Time</th>
                  <th>Days</th>
                  <th>Relay</th>
                  <th>State</th>
                  <th>Active</th>
                  <th>Delete</th>
                </tr>
              </thead>
              <tbody>`;
            schedules.forEach((s, idx) => {
html += `<tr>
  <td>${s.name || ''}</td>
  <td>${
    (typeof s.startHour === 'number' && typeof s.startMinute === 'number')
      ? String(s.startHour).padStart(2, '0') + ':' + String(s.startMinute).padStart(2, '0')
      : '--:--'
  }${
    (typeof s.endHour === 'number' && typeof s.endMinute === 'number' && s.endHour >= 0 && s.endMinute >= 0)
      ? ' - ' + String(s.endHour).padStart(2, '0') + ':' + String(s.endMinute).padStart(2, '0')
      : ''
  }
  <div style="font-size:0.95em;color:#888;">
    ${
      (typeof s.startHour === 'number' && typeof s.startMinute === 'number')
        ? `ON at ${String(s.startHour).padStart(2, '0')}:${String(s.startMinute).padStart(2, '0')}`
        : ''
    }
    ${
      (typeof s.endHour === 'number' && typeof s.endMinute === 'number' && s.endHour >= 0 && s.endMinute >= 0)
        ? `, OFF at ${String(s.endHour).padStart(2, '0')}:${String(s.endMinute).padStart(2, '0')}`
        : ''
    }
  </div>
  </td>
  <td>${s.days && s.days.map((d,i)=>d?['S','M','T','W','T','F','S'][i]:'').filter(Boolean).join(' ')}</td>
  <td><span class="material-icons" style="color:#fbc02d;vertical-align:middle;">lightbulb</span> Relay ${s.relayNum}</td>
  <td>${s.state ? '<span style="color:#27ae60;font-weight:600;">ON</span>' : '<span style="color:#e74c3c;font-weight:600;">OFF</span>'}</td>
  <td>${s.active ? '<span style="color:#2563eb;font-weight:600;">Yes</span>' : 'No'}</td>
  <td><button class="delete-btn" onclick="deleteSchedule(${idx})">Delete</button></td>
</tr>`;
            });
            html += '</tbody></table>';
          }
          document.getElementById('savedSchedules').innerHTML = html;
        });
    }
    window.onload = function() {
      loadSavedSchedules();
    };
  </script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }
    JsonArray arr = doc["schedules"];
    if (!arr || arr.size() < 1) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"No schedules provided\"}");
      return;
    }
    bool anyAdded = false;
    for (JsonObject s : arr) {
  if (scheduleCount >= MAX_SCHEDULES) break;
  schedules[scheduleCount].active = s["active"];
  // Parse start time
  String startTime = s["startTime"] | "07:00";
  int startHour = 0, startMinute = 0;
  sscanf(startTime.c_str(), "%d:%d", &startHour, &startMinute);
  schedules[scheduleCount].startHour = startHour;
  schedules[scheduleCount].startMinute = startMinute;
  // Parse end time
  String endTime = s["endTime"] | "";
  int endHour = -1, endMinute = -1;
  if (endTime.length() > 0) {
    sscanf(endTime.c_str(), "%d:%d", &endHour, &endMinute);
  }
  schedules[scheduleCount].endHour = endHour;
  schedules[scheduleCount].endMinute = endMinute;
  schedules[scheduleCount].relayNum = s["relayNum"];
  for (int d = 0; d < 7; d++) schedules[scheduleCount].days[d] = s["days"][d];
  schedules[scheduleCount].repeat = s["repeat"];
  schedules[scheduleCount].name = s["name"] | "";
  scheduleCount++;
  anyAdded = true;
}
    if (!anyAdded) {
      server.send(400, "application/json", "{\"success\":false,\"error\":\"Max schedules reached\"}");
      return;
    }
    saveSchedulesToEEPROM();
    server.send(200, "application/json", "{\"success\":true}");
  }
}

void checkSchedules() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  for (int i = 0; i < scheduleCount; i++) {
    if (schedules[i].active && schedules[i].days[timeinfo.tm_wday]) {
      // Turn ON at start time
      if (timeinfo.tm_hour == schedules[i].startHour &&
          timeinfo.tm_min == schedules[i].startMinute &&
          timeinfo.tm_sec < 10) {
        int relayIdx = schedules[i].relayNum - 1;
        if (relayIdx >= 0 && relayIdx < RELAY_COUNT) {
          relayStates[relayIdx] = true;
          digitalWrite(relayPins[relayIdx], HIGH);
          addLog("Schedule ON: Relay " + String(schedules[i].relayNum));
          recordRelayEvent(schedules[i].relayNum, true, "schedule");
        }
      }
      // Turn OFF at end time (if set)
      if (schedules[i].endHour >= 0 && schedules[i].endMinute >= 0 &&
          timeinfo.tm_hour == schedules[i].endHour &&
          timeinfo.tm_min == schedules[i].endMinute &&
          timeinfo.tm_sec < 10) {
        int relayIdx = schedules[i].relayNum - 1;
        if (relayIdx >= 0 && relayIdx < RELAY_COUNT) {
          relayStates[relayIdx] = false;
          digitalWrite(relayPins[relayIdx], LOW);
          addLog("Schedule OFF: Relay " + String(schedules[i].relayNum));
          recordRelayEvent(schedules[i].relayNum, false, "schedule");
        }
      }
    }
  }
}

void handleSchedulesList() {
  if (requireLogin()) return;
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < scheduleCount; i++) {
    JsonObject s = arr.createNestedObject();
    s["name"] = schedules[i].name;
    s["startHour"] = schedules[i].startHour;
    s["startMinute"] = schedules[i].startMinute;
    s["endHour"] = schedules[i].endHour;
    s["endMinute"] = schedules[i].endMinute;
    s["relayNum"] = schedules[i].relayNum;
    s["state"] = schedules[i].state;
    JsonArray days = s.createNestedArray("days");
    for (int d = 0; d < 7; d++) days.add(schedules[i].days[d]);
    s["active"] = schedules[i].active;
    s["repeat"] = schedules[i].repeat;
  }
  String json;
  serializeJson(arr, json);
  server.send(200, "application/json", json);
}

void handleScheduleDelete() {
  if (requireLogin()) return;
  DynamicJsonDocument doc(128);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  int idx = doc["index"];
  if (idx < 0 || idx >= scheduleCount) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid schedule index\"}");
    return;
  }
  // Shift schedules left to delete
  for (int i = idx; i < scheduleCount - 1; i++) {
    schedules[i] = schedules[i + 1];
  }
  scheduleCount--;
  server.send(200, "application/json", "{\"success\":true}");
}

void saveSchedulesToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int addr = 200;
  EEPROM.write(addr++, scheduleCount);
  for (int i = 0; i < scheduleCount; i++) {
    EEPROM.write(addr++, schedules[i].active);
    EEPROM.write(addr++, schedules[i].startHour);
    EEPROM.write(addr++, schedules[i].startMinute);
    EEPROM.write(addr++, schedules[i].endHour);
    EEPROM.write(addr++, schedules[i].endMinute);
    EEPROM.write(addr++, schedules[i].relayNum);
    EEPROM.write(addr++, schedules[i].state);
    EEPROM.write(addr++, schedules[i].repeat);
    for (int d = 0; d < 7; d++) EEPROM.write(addr++, schedules[i].days[d]);
    for (int c = 0; c < 15; c++) {
      char ch = (c < schedules[i].name.length()) ? schedules[i].name[c] : 0;
      EEPROM.write(addr++, ch);
    }
  }
  EEPROM.commit();
  EEPROM.end();
}
  
void loadSchedulesFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int addr = 200;
  scheduleCount = EEPROM.read(addr++);
  if (scheduleCount > MAX_SCHEDULES) scheduleCount = 0;
  for (int i = 0; i < scheduleCount; i++) {
    schedules[i].active = EEPROM.read(addr++);
    schedules[i].startHour = EEPROM.read(addr++);
    schedules[i].startMinute = EEPROM.read(addr++);
    schedules[i].endHour = EEPROM.read(addr++);
    schedules[i].endMinute = EEPROM.read(addr++);
    schedules[i].relayNum = EEPROM.read(addr++);
    schedules[i].state = EEPROM.read(addr++);
    schedules[i].repeat = EEPROM.read(addr++);
    for (int d = 0; d < 7; d++) schedules[i].days[d] = EEPROM.read(addr++);
    char nameBuf[16] = {0};
    for (int c = 0; c < 15; c++) nameBuf[c] = EEPROM.read(addr++);
    schedules[i].name = String(nameBuf);
  }
  EEPROM.end();
}

void setup() {
  Serial.begin(115200);
  
  delay(1000);
  esp_task_wdt_init(10, true); // 10 second WDT
  esp_task_wdt_add(NULL); 
  // Initialize pins
  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }
  
  pinMode(STATUS_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize file system
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS. Formatting...");
    if (!SPIFFS.format()) {
      Serial.println("SPIFFS formatting failed");
    } else {
      Serial.println("SPIFFS formatted successfully");
      if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS after formatting");
      } else {
        Serial.println("SPIFFS mounted successfully after formatting");
      }
    }
  }
  
  Serial.println("Listing SPIFFS files:");
  File root = SPIFFS.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
  } else {
    File file = root.openNextFile();
    while (file) {
      Serial.print("  FILE: ");
      Serial.println(file.name());
      file = root.openNextFile();
    }
  }
  esp_log_level_set("*", ESP_LOG_ERROR); 
  // Load configuration
  loadCredentials();
  loadRelayStates();
  loadSceneStates();
  
  // Connect to WiFi
  connectWiFiStatic();
  
  // Initialize time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Initialize sensors
  dht.begin();
  
  // Set up web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings/credentials", HTTP_POST, handleSettingsCredentials);
  server.on("/settings/scenes", HTTP_POST, handleSettingsScenes);
  server.on("/system/restart", HTTP_GET, handleSystemRestart);
  server.on("/sensor/data", HTTP_GET, handleSensorData);
  server.on("/resetpass", HTTP_GET, handleResetPass);
  server.on("/resetpass", HTTP_POST, handleResetPassPost);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relayStatus", HTTP_GET, handleRelayStatus);
  server.on("/scene", HTTP_GET, handleScene);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/logs", HTTP_GET, handleSimpleLogs);
  server.on("/api/jarvis/relay", HTTP_GET, handleJarvisRelay);
  server.on("/ota", HTTP_GET, handleOTAWeb);
  server.on("/update", HTTP_POST, handleOTAFinish, handleOTAUpdate);
  server.on("/systeminfo", HTTP_GET, handleSystemInfo);
  server.on("/deviceStatus", HTTP_GET, handleDeviceStatus);
  server.on("/schedules", HTTP_GET, handleSchedules);
  server.on("/schedules", HTTP_POST, handleSchedules);
  server.on("/wifiStatus", HTTP_GET, handleWiFiStatus);
  server.on("/routines", HTTP_GET, handleRoutinesGet);
  server.on("/routines", HTTP_POST, handleRoutinesPost);
  server.on("/schedules/list", HTTP_GET, handleSchedulesList);
  server.on("/schedules/delete", HTTP_POST, handleScheduleDelete);
  server.onNotFound(handleNotFound);
  
  server.begin();
  
  // Set up mDNS for easy access
  if (MDNS.begin("home")) {
    DEBUG_INFO(WIFI, "mDNS responder started");
  }
loadSchedulesFromEEPROM();
  // Check for birthday
  checkBirthday();
  
  // Set up schedules
  setupSchedules();
    currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  dailyTempAverage[0] = currentTemp;
  dailyHumAverage[0] = currentHum;
  dailyTempAverage[1] = currentTemp; // For demo: yesterday = today
  dailyHumAverage[1] = currentHum;   // For demo: yesterday = today
  
  addRoutine("Wake Up Lights", "07:00", 1, true);
  addLog("System initialized");
}

void loop() {
  server.handleClient();
  handleButtonPress();

  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorRead > SENSOR_READ_INTERVAL) {
    lastSensorRead = currentMillis;

    // Read DHT sensor
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();

    if (!isnan(newTemp) && !isnan(newHum)) {
      currentTemp = newTemp;
      currentHum = newHum;
      currentTemp = newTemp;
      currentHum = newHum;
      tempSum += newTemp;
      humSum += newHum;
      sampleCount++;
      // Add to data history
      if (dataCount < MAX_DATA_POINTS) {
        time_t now;
        time(&now);
        dataPoints[dataCount].timestamp = now;
        dataPoints[dataCount].temperature = currentTemp;
        dataPoints[dataCount].humidity = currentHum;
        dataCount++;
      } else {
        // Shift data when buffer is full
        for (int i = 0; i < MAX_DATA_POINTS - 1; i++) {
          dataPoints[i] = dataPoints[i + 1];
        }
        time_t now;
        time(&now);
        dataPoints[MAX_DATA_POINTS - 1].timestamp = now;
        dataPoints[MAX_DATA_POINTS - 1].temperature = currentTemp;
        dataPoints[MAX_DATA_POINTS - 1].humidity = currentHum;
      }
    }
  }

  checkSchedules();
  checkRoutines();
  updateDailyTemperature();

  // WiFi reconnect logic
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFiStatic();
  }

  esp_task_wdt_reset();
  delay(10); // Small delay to prevent CPU hogging
}
