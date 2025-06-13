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

// ----------- CONFIGURABLE SECTION -----------

// --- Pin Definitions ---
#define RELAY_COUNT 8
const int relayPins[RELAY_COUNT] = {2, 15, 16, 17, 18, 19, 21, 22};
#define DHTPIN 4
#define DHTTYPE DHT11
#define STATUS_LED 23
#define BUTTON_PIN 5  // For physical button control

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

// Global Variables
String savedUsername, savedPassword, savedBirthday, sessionToken = "";
bool relayStates[RELAY_COUNT] = {false};
String logBuffer = "";
float currentTemp = NAN, currentHum = NAN;
unsigned long lastSensorRead = 0;
bool buttonPressed = false;
unsigned long lastButtonPress = 0;

struct SensorDataPoint {
  time_t timestamp;
  float temperature;
  float humidity;
};
SensorDataPoint dataPoints[MAX_DATA_POINTS];
int dataCount = 0;

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
  // TEMPORARY: Always return true to bypass login
  return true;
  
  // Original login check code (commented out)
  /*
  if (!server.hasHeader("Cookie")) {
    Serial.println("[DEBUG] No Cookie header received");
    return false;
  }
  String cookie = server.header("Cookie");
  Serial.println("[DEBUG] Cookie header: " + cookie);
  int idx = cookie.indexOf("ESPSESSIONID=");
  if (idx == -1) {
    Serial.println("[DEBUG] ESPSESSIONID not found in cookie");
    return false;
  }
  int start = idx + strlen("ESPSESSIONID=");
  int end = cookie.indexOf(';', start);
  String token = (end == -1) ? cookie.substring(start) : cookie.substring(start, end);
  token.trim();
  sessionToken.trim();
  Serial.println("[DEBUG] Cookie token: [" + token + "] | Session: [" + sessionToken + "]");
  Serial.printf("[DEBUG] Comparing cookie token [%s] with sessionToken [%s]\n", token.c_str(), sessionToken.c_str());
  return token == sessionToken;
  */
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
  String ssid = stripQuotes(getINIValue("/config.ini", "wifi_ssid", ""));
  String pass = stripQuotes(getINIValue("/config.ini", "wifi_password", ""));
  String staticIP = getINIValue("/config.ini", "static_ip", "");
  String gatewayStr = getINIValue("/config.ini", "gateway", "");
  String subnetStr = getINIValue("/config.ini", "subnet", "");
  String dnsStr = getINIValue("/config.ini", "dns", "");

  if (ssid == "" || pass == "" || staticIP == "" || gatewayStr == "" || subnetStr == "") {
    Serial.println("Missing WiFi/static IP config in config.ini");
    while (1) delay(1000);
  }

  IPAddress ip, gw, sn, dn;
  ip.fromString(staticIP);
  gw.fromString(gatewayStr);
  sn.fromString(subnetStr);
  dn.fromString(dnsStr.length() > 0 ? dnsStr : gatewayStr);

  WiFi.mode(WIFI_STA);
  WiFi.config(ip, gw, sn, dn);

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Connecting to WiFi (static IP)...");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500); 
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    addLog("Connected to WiFi (static IP): " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect to WiFi with static IP.");
    addLog("Failed to connect to WiFi with static IP.");
    ESP.restart();
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
    body { min-height: 100vh; background: linear-gradient(135deg, #00c6ff 0%, #0072ff 100%);
      display: flex; align-items: center; justify-content: center; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; }
    .login-container { background: #fff; border-radius: 24px; box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18); padding: 2.8rem 2.2rem 2.2rem 2.2rem; width: 100%; max-width: 370px; }
    .login-title { font-size: 2.2rem; font-weight: 800; color: #0072ff; margin-bottom: 1.7rem; letter-spacing: 1.5px; text-align: center; }
    .input-group { margin-bottom: 1.3rem; }
    .input-group label { display: block; margin-bottom: 0.5rem; color: #0072ff; font-weight: 600; }
    .input-group input { width: 100%; padding: 0.8rem 1.1rem; border: none; border-radius: 8px; font-size: 1.08rem; background: #f0f7fa; }
    .login-btn { width: 100%; padding: 0.9rem; background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%); color: #fff; border: none; border-radius: 8px; font-size: 1.15rem; font-weight: 700; cursor: pointer; margin-top: 0.7rem; }
    .footer { margin-top: 2.2rem; text-align: center; color: #aaa; font-size: 1rem; }
    .forgot-link { color: #0072ff; text-decoration: underline; font-size: 1rem; font-weight: 500; margin-top: 1.2rem; display: inline-block; }
    .error-message { color: #e74c3c; background: #fff0f0; border-radius: 6px; padding: 0.6rem 1.1rem; margin-top: 1.1rem; text-align: center; font-weight: 600; display: %ERROR_DISPLAY%; }
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

    if (username == savedUsername && password == savedPassword) {
      sessionToken = generateSessionToken();
      Serial.println("[DEBUG] New sessionToken: " + sessionToken);

      // Set the session cookie here!
      server.sendHeader("Set-Cookie", "ESPSESSIONID=" + sessionToken + "; Path=/; HttpOnly");
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "Redirecting to dashboard...");
      delay(100);
      addLog("User logged in");
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
    body { min-height: 100vh; background: linear-gradient(135deg, #00c6ff 0%, #0072ff 100%);
      display: flex; align-items: center; justify-content: center; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; }
    .login-container { background: #fff; border-radius: 24px; box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18); padding: 2.8rem 2.2rem 2.2rem 2.2rem; width: 100%; max-width: 370px; }
    .login-title { font-size: 2.2rem; font-weight: 800; color: #0072ff; margin-bottom: 1.7rem; letter-spacing: 1.5px; text-align: center; }
    .input-group { margin-bottom: 1.3rem; }
    .input-group label { display: block; margin-bottom: 0.5rem; color: #0072ff; font-weight: 600; }
    .input-group input { width: 100%; padding: 0.8rem 1.1rem; border: none; border-radius: 8px; font-size: 1.08rem; background: #f0f7fa; }
    .login-btn { width: 100%; padding: 0.9rem; background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%); color: #fff; border: none; border-radius: 8px; font-size: 1.15rem; font-weight: 700; cursor: pointer; margin-top: 0.7rem; }
    .footer { margin-top: 2.2rem; text-align: center; color: #aaa; font-size: 1rem; }
    .forgot-link { color: #0072ff; text-decoration: underline; font-size: 1rem; font-weight: 500; margin-top: 1.2rem; display: inline-block; }
    .error-message { color: #e74c3c; background: #fff0f0; border-radius: 6px; padding: 0.6rem 1.1rem; margin-top: 1.1rem; text-align: center; font-weight: 600; display: block; }
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
</body>
</html>
)rawliteral";
      server.send(200, "text/html", html);
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
  Serial.println("[DEBUG] handleRoot called");
  if (requireLogin()) {
    Serial.println("[DEBUG] Not logged in, redirecting to login");
    return;
  }
  
  Serial.println("[DEBUG] Serving inline dashboard HTML");
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Smart Home Control</title>
  <link href="https://fonts.googleapis.com/css?family=Segoe+UI:400,700&display=swap" rel="stylesheet">
  <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
  <style>
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: #f4f7fb;
      margin: 0;
      padding: 0;
      color: #222;
    }
    .container {
      max-width: 1200px;
      margin: 30px auto;
      padding: 0 20px;
    }
    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 24px 0 10px 0;
    }
    .header-title {
      font-size: 2.2rem;
      font-weight: 700;
      letter-spacing: 1px;
    }
    .status-dot {
      width: 12px;
      height: 12px;
      background: #2ecc40;
      border-radius: 50%;
      display: inline-block;
      margin-right: 8px;
      vertical-align: middle;
    }
    .tabs {
      display: flex;
      gap: 12px;
      margin-bottom: 24px;
      overflow-x: auto;
      padding-bottom: 5px;
    }
    .tab {
      background: #fff;
      border-radius: 8px 8px 0 0;
      padding: 10px 28px;
      font-weight: 600;
      color: #0072ff;
      cursor: pointer;
      border: none;
      outline: none;
      font-size: 1.1rem;
      box-shadow: 0 2px 8px rgba(0,0,0,0.04);
      transition: background 0.2s, color 0.2s;
      white-space: nowrap;
    }
    .tab.active, .tab:hover {
      background: #eaf3ff;
      color: #222;
    }
    .cards {
      display: flex;
      flex-wrap: wrap;
      gap: 24px;
      margin-bottom: 24px;
    }
    .card {
      background: #fff;
      border-radius: 16px;
      box-shadow: 0 2px 12px rgba(0,0,0,0.07);
      padding: 28px 24px 20px 24px;
      flex: 1 1 260px;
      min-width: 260px;
      max-width: 340px;
      display: flex;
      flex-direction: column;
      align-items: flex-start;
      position: relative;
    }
    .card .material-icons {
      font-size: 2.2rem;
      color: #0072ff;
      margin-bottom: 10px;
    }
    .card-title {
      font-size: 1.1rem;
      font-weight: 600;
      margin-bottom: 8px;
      color: #444;
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .card-value {
      font-size: 2.1rem;
      font-weight: 700;
      margin-bottom: 10px;
      color: #222;
    }
    .slider {
      width: 100%;
      margin: 10px 0 0 0;
    }
    .slider input[type=range] {
      width: 100%;
      accent-color: #0072ff;
      height: 4px;
      border-radius: 2px;
      background: #eaf3ff;
      outline: none;
      margin: 0;
    }
    .slider-labels {
      display: flex;
      justify-content: space-between;
      font-size: 0.95rem;
      color: #888;
      margin-top: 2px;
    }
    .toggle-switch {
      position: relative;
      display: inline-block;
      width: 48px;
      height: 28px;
      margin-left: 10px;
      vertical-align: middle;
    }
    .toggle-switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }
    .slider-toggle {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background: #ccc;
      transition: .4s;
      border-radius: 28px;
    }
    .slider-toggle:before {
      position: absolute;
      content: "";
      height: 20px;
      width: 20px;
      left: 4px;
      bottom: 4px;
      background: #fff;
      transition: .4s;
      border-radius: 50%;
      box-shadow: 0 2px 6px rgba(0,0,0,0.08);
    }
    .toggle-switch input:checked + .slider-toggle {
      background: #0072ff;
    }
    .toggle-switch input:checked + .slider-toggle:before {
      transform: translateX(20px);
    }
    .card-row {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-bottom: 8px;
    }
    .btn {
      background: #0072ff;
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 10px 22px;
      font-size: 1rem;
      font-weight: 600;
      cursor: pointer;
      margin-top: 8px;
      transition: background 0.2s;
    }
    .btn:active, .btn:hover {
      background: #005fcc;
    }
    .btn.locked {
      background: #e74c3c;
    }
    .btn.unlocked {
      background: #2ecc40;
    }
    .status-label {
      font-size: 0.98rem;
      color: #888;
      margin-left: 8px;
    }
    .scene-cards {
      display: flex;
      gap: 18px;
      margin-top: 18px;
      flex-wrap: wrap;
    }
    .scene-btn {
      flex: 1 1 200px;
      min-width: 120px;
      background: #f6f8fc;
      border-radius: 12px;
      padding: 18px 10px;
      text-align: center;
      font-weight: 600;
      font-size: 1.1rem;
      color: #fff;
      cursor: pointer;
      transition: transform 0.2s, box-shadow 0.2s;
      border: none;
      margin-bottom: 8px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 8px;
    }
    .scene-btn:hover {
      transform: translateY(-3px);
      box-shadow: 0 6px 16px rgba(0,0,0,0.15);
    }
    .scene-btn .material-icons {
      font-size: 1.8rem;
      margin-bottom: 6px;
    }
    .scene-btn.night { background: #3b3b98; }
    .scene-btn.morning { background: #f6b93b; color: #fff; }
    .scene-btn.movie { background: #6a89cc; }
    .scene-btn.away { background: #38ada9; }
    .scene-btn.all-on { background: #4CAF50; }
    .scene-btn.all-off { background: #607D8B; }
    .room-tab { display: none; }
    .room-tab:first-of-type { display: flex; }
    @media (max-width: 900px) {
      .cards { flex-direction: column; }
      .scene-cards { flex-direction: row; overflow-x: auto; padding-bottom: 15px; }
      .scene-btn { min-width: 150px; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="header-title">
        <span class="status-dot"></span> Smart Home Control
      </div>
      <div>
        <button class="btn" onclick="window.location.href='/settings'">Settings</button>
        <button class="btn" onclick="window.location.href='/logout'">Logout</button>
      </div>
    </div>
    <div class="tabs" id="roomTabs">
      <!-- Room tabs will be generated dynamically -->
    </div>
    <div id="roomContainers">
      <!-- Room card containers will be generated dynamically -->
    </div>
    <div class="scene-cards" id="sceneCards">
      <!-- Scene buttons will be generated dynamically -->
    </div>
  </div>
  <script>
    // Room definitions that match your configured rooms
    const rooms = [
      {name: "Living Room", id: "living"},
      {name: "Bedroom", id: "bedroom"},
      {name: "Kitchen", id: "kitchen"},
      {name: "Bathroom", id: "bathroom"},
      {name: "Garage", id: "garage"},
      {name: "Porch", id: "porch"},
      {name: "Study", id: "study"}
    ];
    
    // Scene definitions that match your configured scenes
    const scenes = [
      {name: "Good Night", icon: "nights_stay", class: "night"},
      {name: "Good Morning", icon: "wb_sunny", class: "morning"},
      {name: "Movie Mode", icon: "movie", class: "movie"},
      {name: "Away Mode", icon: "not_listed_location", class: "away"},
      {name: "All On", icon: "power", class: "all-on"},
      {name: "All Off", icon: "power_off", class: "all-off"}
    ];
    
    // Build tabs dynamically
    function buildTabs() {
      let tabsHtml = '';
      rooms.forEach((room, index) => {
        tabsHtml += `<button class="tab ${index === 0 ? 'active' : ''}" 
                      onclick="showRoom('${room.id}')">${room.name}</button>`;
      });
      document.getElementById('roomTabs').innerHTML = tabsHtml;
    }
    
    // Build room containers dynamically
    function buildRoomContainers() {
      let containersHtml = '';
      
      rooms.forEach((room, index) => {
        // Create cards for each room
        containersHtml += `
          <div id="room-${room.id}" class="cards room-tab" ${index === 0 ? '' : 'style="display:none"'}>
            <div class="card">
              <span class="material-icons">wb_incandescent</span>
              <div class="card-title">Main Light
                <label class="toggle-switch">
                  <input type="checkbox" id="${room.id}-mainLight" onchange="toggleRelay(${index*2}, this.checked)">
                  <span class="slider-toggle"></span>
                </label>
              </div>
            </div>
            <div class="card">
              <span class="material-icons">highlight</span>
              <div class="card-title">Accent Light
                <label class="toggle-switch">
                  <input type="checkbox" id="${room.id}-accentLight" onchange="toggleRelay(${index*2+1}, this.checked)">
                  <span class="slider-toggle"></span>
                </label>
              </div>
            </div>
            <div class="card">
              <span class="material-icons">thermostat</span>
              <div class="card-title">Temperature</div>
              <div class="card-value" id="${room.id}-temp">--°C</div>
              <div class="status-label" id="${room.id}-hum">Humidity: --%</div>
            </div>
          </div>`;
      });
      
      document.getElementById('roomContainers').innerHTML = containersHtml;
    }
    
    // Build scene buttons
    function buildSceneButtons() {
      let scenesHtml = '';
      scenes.forEach((scene, index) => {
        scenesHtml += `
          <button class="scene-btn ${scene.class}" onclick="applyScene(${index})">
            <span class="material-icons">${scene.icon}</span>
            ${scene.name}
          </button>`;
      });
      document.getElementById('sceneCards').innerHTML = scenesHtml;
    }
    
    // Switch active room tab
    function showRoom(roomId) {
      // Hide all room tabs
      document.querySelectorAll('.room-tab').forEach(tab => {
        tab.style.display = 'none';
      });
      
      // Show the selected room tab
      document.getElementById('room-' + roomId).style.display = 'flex';
      
      // Update active tab styling
      document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.remove('active');
      });
      event.currentTarget.classList.add('active');
    }
    
    // Toggle a relay
    function toggleRelay(relayNum, state) {
      fetch(`/relay?num=${relayNum+1}&state=${state ? 1 : 0}`, { credentials: 'include' })
        .then(resp => resp.json())
        .then(data => {
          if (!data.success) {
            alert('Failed to toggle relay');
            // Revert the checkbox state
            event.target.checked = !state;
          }
        })
        .catch(err => {
          alert('Network error: ' + err.message);
          // Revert the checkbox state
          event.target.checked = !state;
        });
    }
    
    // Apply a scene
    function applyScene(sceneId) {
      fetch(`/scene?idx=${sceneId}`, { credentials: 'include' })
        .then(resp => resp.json())
        .then(data => {
          if (data.success) {
            fetchRelayStates(); // Update UI after scene is applied
          } else {
            alert('Failed to apply scene');
          }
        })
        .catch(err => {
          alert('Network error: ' + err.message);
        });
    }
    
    // Fetch all relay states
    function fetchRelayStates() {
      fetch('/relayStatus', { credentials: 'include' })
        .then(resp => resp.json())
        .then(data => {
          // Update all toggles based on relay states
          rooms.forEach((room, roomIndex) => {
            const mainLightIndex = roomIndex * 2;
            const accentLightIndex = roomIndex * 2 + 1;
            
            if (mainLightIndex < data.length) {
              document.getElementById(`${room.id}-mainLight`).checked = data[mainLightIndex];
            }
            if (accentLightIndex < data.length) {
              document.getElementById(`${room.id}-accentLight`).checked = data[accentLightIndex];
            }
          });
        })
        .catch(err => console.error('Error fetching relay states:', err));
    }
    
    // Fetch sensor data
    function fetchSensorData() {
      fetch('/sensor', { credentials: 'include' })
        .then(resp => resp.json())
        .then(data => {
          // Format temperature and humidity values
          const temp = data.temperature ? data.temperature.toFixed(1) + '°C' : '--°C';
          const hum = data.humidity ? 'Humidity: ' + data.humidity.toFixed(0) + '%' : 'Humidity: --%';
          
          // Update all room temperature displays with the same values
          rooms.forEach(room => {
            document.getElementById(`${room.id}-temp`).textContent = temp;
            document.getElementById(`${room.id}-hum`).textContent = hum;
          });
        })
        .catch(err => console.error('Error fetching sensor data:', err));
    }
    
    // Initialize UI
    buildTabs();
    buildRoomContainers();
    buildSceneButtons();
    
    // Fetch initial data
    fetchRelayStates();
    fetchSensorData();
    
    // Set up polling
    setInterval(fetchRelayStates, 5000);
    setInterval(fetchSensorData, 10000);
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
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
    body { background: #f5f7fa; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .settings-container { background: #fff; border-radius: 10px; box-shadow: 0 10px 30px rgba(0,0,0,0.1); width: 100%; max-width: 600px; padding: 40px; }
    .settings-title { color: #2c3e50; font-size: 22px; font-weight: 600; margin-bottom: 30px; text-align: center; }
    .settings-tabs { display: flex; border-bottom: 1px solid #eee; margin-bottom: 20px; }
    .settings-tab { padding: 10px 20px; cursor: pointer; font-weight: 500; color: #7f8c8d; }
    .settings-tab.active { color: #0072ff; border-bottom: 2px solid #0072ff; }
    .settings-content { display: none; }
    .settings-content.active { display: block; }
    .input-group { margin-bottom: 20px; }
    .input-group label { display: block; margin-bottom: 8px; color: #2c3e50; font-size: 14px; font-weight: 500; }
    .input-group input, .input-group select { width: 100%; padding: 12px 15px; border: 1px solid #e0e0e0; border-radius: 6px; font-size: 14px; transition: border-color 0.3s; }
    .input-group input:focus, .input-group select:focus { outline: none; border-color: #3498db; }
    .settings-button { width: 100%; padding: 12px; background-color: #3498db; color: white; border: none; border-radius: 6px; font-size: 16px; font-weight: 500; cursor: pointer; transition: background-color 0.3s; }
    .settings-button:hover { background-color: #2980b9; }
    .error-message { color: #e74c3c; font-size: 14px; margin-top: 15px; display: none; }
    .success-message { color: #27ae60; font-size: 14px; margin-top: 15px; display: none; }
    .scene-config { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 20px; }
    .scene-item { flex: 1 1 200px; background: #f8f9fa; padding: 15px; border-radius: 8px; }
    .scene-item h3 { margin-top: 0; color: #2c3e50; }
    .scene-toggle { display: flex; align-items: center; margin-bottom: 8px; }
    .scene-toggle label { margin-left: 8px; }
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
        <div id="sensorChart" style="width:100%; height:200px; background:#f8f9fa;"></div>
      </div>
    </div>
  </div>
  
  <script>
    function showTab(tabId) {
      document.querySelectorAll('.settings-tab').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.settings-content').forEach(content => content.classList.remove('active'));
      document.querySelector(`.settings-tab[onclick="showTab('${tabId}')"]`).classList.add('active');
      document.getElementById(tabId).classList.add('active');
    }
    
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
    
    // Load sensor data for chart
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
    
    JsonArray sceneArray = doc["scenes"];
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

void handleSensorData() {
  if (requireLogin()) return;
  
  DynamicJsonDocument doc(4096);
  JsonArray data = doc.createNestedArray("data");
  
  // Get current time
  time_t now;
  time(&now);
  
  // Send last 24 hours of data
  for (int i = 0; i < dataCount; i++) {
    if (difftime(now, dataPoints[i].timestamp) <= 86400) { // 24 hours
      JsonObject point = data.createNestedObject();
      point["time"] = dataPoints[i].timestamp;
      point["temp"] = dataPoints[i].temperature;
      point["hum"] = dataPoints[i].humidity;
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
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
    body { min-height: 100vh; background: linear-gradient(135deg, #00c6ff 0%, #0072ff 100%);
      display: flex; align-items: center; justify-content: center; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; }
    .reset-container { background: #fff; border-radius: 24px; box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18); padding: 2.8rem 2.2rem 2.2rem 2.2rem; width: 100%; max-width: 370px; }
    .reset-title { font-size: 2.2rem; font-weight: 800; color: #0072ff; margin-bottom: 1.7rem; letter-spacing: 1.5px; text-align: center; }
    .input-group { margin-bottom: 1.3rem; }
    .input-group label { display: block; margin-bottom: 0.5rem; color: #0072ff; font-weight: 600; }
    .input-group input { width: 100%; padding: 0.8rem 1.1rem; border: none; border-radius: 8px; font-size: 1.08rem; background: #f0f7fa; }
    .reset-btn { width: 100%; padding: 0.9rem; background: linear-gradient(90deg, #00c6ff 60%, #0072ff 100%); color: #fff; border: none; border-radius: 8px; font-size: 1.15rem; font-weight: 700; cursor: pointer; margin-top: 0.7rem; }
    .footer { margin-top: 2.2rem; text-align: center; color: #aaa; font-size: 1rem; }
    .error-message { color: #e74c3c; background: #fff0f0; border-radius: 6px; padding: 0.6rem 1.1rem; margin-top: 1.1rem; text-align: center; font-weight: 600; display: none; }
    .success-message { color: #27ae60; background: #f0fff4; border-radius: 6px; padding: 0.6rem 1.1rem; margin-top: 1.1rem; text-align: center; font-weight: 600; display: none; }
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
      })
      .catch(error => {
        successEl.style.display = 'none';
        errorEl.textContent = 'Network error';
        errorEl.style.display = 'block';
      });
    }
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleResetPassPost() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  deserializeJson(doc, body);
  
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
  
  if (num < 1 || num > RELAY_COUNT) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay number\"}");
    return;
  }
  
  relayStates[num - 1] = (state == 1);
  digitalWrite(relayPins[num - 1], relayStates[num - 1] ? HIGH : LOW);
  saveRelayStates();
  addLog("Relay " + String(num) + (relayStates[num - 1] ? " ON" : " OFF"));
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRelayStatus() {
  if (requireLogin()) return;
  
  StaticJsonDocument<256> doc;
  for (int i = 0; i < RELAY_COUNT; i++) doc[i] = relayStates[i];
  
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
  
  StaticJsonDocument<256> doc;
  doc["success"] = true;
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
  
  StaticJsonDocument<128> doc;
  doc["temperature"] = currentTemp;
  doc["humidity"] = currentHum;
  
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
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleOTAUpdate() {
  if (requireLogin()) return;
  
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", upload.filename.c_str());
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

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "File Not Found");
  }
}

// ----------- Setup and Loop -----------

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH); // Turn on status LED
  
  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW); // Ensure all relays are off initially
  }
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize file system
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    while (1) delay(1000);
  }
  
  // Initialize EEPROM and load data
  EEPROM.begin(EEPROM_SIZE);
  dht.begin();
  loadCredentials();
  loadRelayStates();
  loadSceneStates();
  
  // Connect to WiFi
  connectWiFiStatic();
  
  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Set up server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/resetpass", HTTP_GET, handleResetPass);
  server.on("/resetpass", HTTP_POST, handleResetPassPost);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings/credentials", HTTP_POST, handleSettingsCredentials);
  server.on("/settings/scenes", HTTP_POST, handleSettingsScenes);
  server.on("/system/restart", HTTP_POST, handleSystemRestart);
  server.on("/sensor/data", HTTP_GET, handleSensorData);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relayStatus", HTTP_GET, handleRelayStatus);
  server.on("/scene", HTTP_GET, handleScene);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/logs", HTTP_GET, handleSimpleLogs);
  server.on("/api/jarvis/relay", HTTP_GET, handleJarvisRelay);
  server.on("/update", HTTP_POST, handleOTAFinish, handleOTAUpdate);
  server.onNotFound(handleNotFound);
  
  server.on("/debug/headers", HTTP_GET, []() {
    String headers;
    int n = server.headers();
    for (int i = 0; i < n; i++) {
      headers += server.headerName(i) + ": " + server.header(i) + "\n";
    }
    server.send(200, "text/plain", headers);
  });
  
  server.on("/testcookie", HTTP_GET, []() {
    if (server.hasHeader("Cookie")) {
      Serial.println("[DEBUG] /testcookie Cookie: " + server.header("Cookie"));
      server.send(200, "text/plain", "Cookie: " + server.header("Cookie"));
    } else {
      server.send(200, "text/plain", "No cookie received");
    }
  });
  
  server.begin();
  Serial.println("HTTP server started");
  
  // mDNS setup
  if (!MDNS.begin("esp32")) {
    Serial.println("Error starting mDNS");
  } else {
    Serial.println("mDNS responder started: http://esp32.local/");
  }
  
  // Initial log entries
  addLog("System initialized");
  addLog("Firmware version: 2.0.0");
  addLog("MAC address: " + WiFi.macAddress());
  
  // Check for birthday
  checkBirthday();
  
  // Turn off status LED after successful setup
  digitalWrite(STATUS_LED, LOW);
}
void loop() {
  server.handleClient();
  // Read sensor data periodically
  unsigned long now = millis();
  if (now - lastSensorRead > SENSOR_READ_INTERVAL) {
    lastSensorRead = now;
    
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    
    if (!isnan(temp) && !isnan(hum)) {
      currentTemp = temp;
      currentHum = hum;
      
      // Store data point if we have space
      if (dataCount < MAX_DATA_POINTS) {
        time_t nowTime;
        time(&nowTime);
        dataPoints[dataCount++] = {nowTime, temp, hum};
      }
      
      Serial.printf("Temp: %.2f°C, Hum: %.2f%%\n", temp, hum);
    } else {
      Serial.println("Failed to read from DHT sensor");
    }
  }
  
  // Check birthday once per hour
  static unsigned long lastBirthdayCheck = 0;
  if (now - lastBirthdayCheck > 3600000) { // 1 hour
    lastBirthdayCheck = now;
    checkBirthday();
  }
}