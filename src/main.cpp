#include <WiFiManager.h>
#include <WebServer.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <Crypto.h>
#include <AES.h>
#include <GCM.h>
#include <ElegantOTA.h>
#include <FS.h>

// Pin Definitions
#define RELAY1 2
#define RELAY2 15
#define RELAY3 16
#define RELAY4 17
#define RELAY5 18
#define RELAY6 19
#define RELAY7 21
#define RELAY8 22
#define DHTPIN 4
#define DHTTYPE DHT11
#define STATUS_LED 23

// Time configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // GMT+5:30
const int daylightOffset_sec = 0;

// EEPROM Configuration
#define EEPROM_SIZE 1024
#define CONFIG_MAGIC 0x5AA5
#define CONFIG_VERSION 2

// Security Configuration
#define AES_KEY_SIZE 32
#define GCM_TAG_SIZE 16
#define GCM_IV_SIZE 12

// Session timeout (30 minutes)
#define SESSION_TIMEOUT 1800000

// Sensor Data Configuration
#define MAX_DATA_POINTS 3000

// User roles
enum UserRole {
  ROLE_VIEWER = 0,
  ROLE_ADMIN = 1
};

// Configuration structure
struct Config {
  uint16_t magic;
  uint8_t version;
  char username[32];
  char encryptedPassword[48]; // AES-GCM encrypted (32 + 16 tag)
  char iv[GCM_IV_SIZE];
  char birthday[16];
  uint8_t relayStates[8];
  UserRole role;
  char wifiSSID[32];
  char wifiPassword[64];
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
  size_t encryptedPasswordLen; // Add this field

  // Add these fields to your Config struct if not already present:
  char deviceName[32];
  float timezone;
  uint16_t otaPort;
  char otaPassword[32];
  float tempOffset;
  float humidityOffset;
  uint32_t updateInterval;
};

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiManager wifiManager;

// Global Variables
Config config;
String sessionToken = "";
UserRole currentUserRole = ROLE_VIEWER;
unsigned long lastActivityTime = 0;
String logBuffer = "";
const int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};
bool relayStates[8] = {false, false, false, false, false, false, false, false};

struct SensorDataPoint {
  time_t timestamp;
  float temperature;
  float humidity;
};
SensorDataPoint dataPoints[MAX_DATA_POINTS];
int dataCount = 0;

float currentTemp = NAN;
float currentHum = NAN;

// Crypto objects
GCM<AES256> gcm;
byte aesKey[AES_KEY_SIZE] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

// Helper Functions
void addLog(const String& entry) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
  logBuffer += "[" + String(timestamp) + "] " + entry + "\n";
  Serial.println(entry);
}

String encryptPassword(const String& password) {
  if (password.length() == 0) return "";

  for (int i = 0; i < GCM_IV_SIZE; i++) {
    config.iv[i] = random(256);
  }

  gcm.setKey(aesKey, AES_KEY_SIZE);
  gcm.setIV((byte*)config.iv, GCM_IV_SIZE);

  size_t ciphertextSize = password.length() + GCM_TAG_SIZE;
  byte ciphertext[ciphertextSize];

  gcm.encrypt((byte*)ciphertext, (byte*)password.c_str(), password.length());

  memcpy(config.encryptedPassword, ciphertext, ciphertextSize);
  config.encryptedPasswordLen = ciphertextSize; // Store length

  return String((char*)ciphertext, ciphertextSize);
}

String decryptPassword() {
  if (config.encryptedPasswordLen == 0) return "";

  gcm.setKey(aesKey, AES_KEY_SIZE);
  gcm.setIV((byte*)config.iv, GCM_IV_SIZE);

  size_t plaintextSize = config.encryptedPasswordLen - GCM_TAG_SIZE;
  byte plaintext[plaintextSize + 1];
  gcm.decrypt(plaintext, (byte*)config.encryptedPassword, config.encryptedPasswordLen);
  plaintext[plaintextSize] = 0; // Null-terminate

  return String((char*)plaintext);
}

void saveConfig();  // <-- Add this line before loadConfig()

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  EEPROM.end();

  // Check if config is valid
  if (config.magic != CONFIG_MAGIC || config.version != CONFIG_VERSION) {
    // Initialize default config
    memset(&config, 0, sizeof(config));
    config.magic = CONFIG_MAGIC;
    config.version = CONFIG_VERSION;
    strcpy(config.username, "admin");
    encryptPassword("admin");
    strcpy(config.birthday, "2000-01-01");
    config.role = ROLE_ADMIN;
    
    // Initialize relay states
    for (int i = 0; i < 8; i++) {
      config.relayStates[i] = 0;
    }
    
    saveConfig();
    addLog("Initialized new configuration");
  }
  
  // Load relay states
  for (int i = 0; i < 8; i++) {
    relayStates[i] = config.relayStates[i];
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
  
  addLog("Configuration loaded");
}

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
  addLog("Configuration saved");
}

void factoryReset() {
  // Clear EEPROM
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  
  // Clear SPIFFS
  SPIFFS.format();
  
  // Reset WiFi settings
  WiFi.disconnect(true);
  delay(1000);
  
  // Reboot
  ESP.restart();
}

String generateSessionToken() {
  String token = "";
  for (int i = 0; i < 32; i++) {
    token += char(random(65, 90));
  }
  return token;
}

bool isAuthenticated() {
  if (millis() - lastActivityTime > SESSION_TIMEOUT) {
    sessionToken = "";
    addLog("Session expired due to inactivity");
    return false;
  }
  
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("ESPSESSIONID=" + sessionToken) != -1) {
      lastActivityTime = millis();
      return true;
    }
  }
  return false;
}

bool hasAdminPrivileges() {
  return isAuthenticated() && currentUserRole == ROLE_ADMIN;
}

void connectWiFi() {
  // Try static IP first if configured
  if (config.staticIP != INADDR_NONE) {
    WiFi.config(config.staticIP, config.gateway, config.subnet, config.dns);
  }
  
  if (strlen(config.wifiSSID) > 0) {
    WiFi.begin(config.wifiSSID, config.wifiPassword);
    Serial.print("Connecting to WiFi...");
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500);
      Serial.print(".");
      retry++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
      addLog("Connected to WiFi: " + String(config.wifiSSID) + " IP: " + WiFi.localIP().toString());
      return;
    }
  }
  
  // Fall back to WiFiManager if no configured WiFi or connection failed
  wifiManager.setTimeout(180);
  if (!wifiManager.autoConnect("IoT_Device_AP")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  
  // Save the new WiFi credentials
  strncpy(config.wifiSSID, WiFi.SSID().c_str(), sizeof(config.wifiSSID));
  strncpy(config.wifiPassword, WiFi.psk().c_str(), sizeof(config.wifiPassword));
  saveConfig();
  
  Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
  addLog("Connected to WiFi via WiFiManager: " + WiFi.SSID() + " IP: " + WiFi.localIP().toString());
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
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

// Add this function to parse config.ini
void loadConfigFromINI() {
    File file = SPIFFS.open("/config.ini", "r");
    if (!file) {
        addLog("No config.ini found, using defaults");
        return;
    }
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("#") || line.startsWith(";") || line.length() == 0) continue;
        int sep = line.indexOf('=');
        if (sep == -1) continue;
        String key = line.substring(0, sep);
        String value = line.substring(sep + 1);
        key.trim(); value.trim();

        if (key == "static_ip") config.staticIP.fromString(value);
        else if (key == "gateway") config.gateway.fromString(value);
        else if (key == "subnet") config.subnet.fromString(value);
        else if (key == "dns") config.dns.fromString(value);
        else if (key == "wifi_ssid") strncpy(config.wifiSSID, value.c_str(), sizeof(config.wifiSSID));
        else if (key == "wifi_password") strncpy(config.wifiPassword, value.c_str(), sizeof(config.wifiPassword));
        else if (key == "device_name") strncpy(config.deviceName, value.c_str(), sizeof(config.deviceName));
        else if (key == "timezone") config.timezone = value.toFloat();
        else if (key == "ota_port") config.otaPort = value.toInt();
        else if (key == "ota_password") strncpy(config.otaPassword, value.c_str(), sizeof(config.otaPassword));
        else if (key == "temp_offset") config.tempOffset = value.toFloat();
        else if (key == "humidity_offset") config.humidityOffset = value.toFloat();
        else if (key == "update_interval") config.updateInterval = value.toInt();
        else if (key == "username") strncpy(config.username, value.c_str(), sizeof(config.username));
        else if (key == "role") config.role = (value == "admin") ? ROLE_ADMIN : ROLE_VIEWER;
        // Add more keys as needed
    }
    file.close();
    addLog("Loaded config.ini from SPIFFS");
}

// Web Handlers
void handleLoginGet() {
  if (isAuthenticated()) {
    server.sendHeader("Location", "/");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }
  
  if (!handleFileRead("/login.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleLoginPost() {
  if (server.hasArg("username") && server.hasArg("password")) {
    String username = server.arg("username");
    String password = server.arg("password");
    String decryptedPassword = decryptPassword();
    
    if (username == config.username && password == decryptedPassword) {
      sessionToken = generateSessionToken();
      lastActivityTime = millis();
      currentUserRole = config.role;
      
      String cookie = "ESPSESSIONID=" + sessionToken + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800";
      server.sendHeader("Set-Cookie", cookie);
      server.sendHeader("Location", "/");
      server.sendHeader("Cache-Control", "no-cache");
      server.send(302);
      addLog("User logged in: " + username);
      return;
    }
  }
  
  server.sendHeader("Location", "/login?error=1");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302);
}

void handleLogout() {
  sessionToken = "";
  currentUserRole = ROLE_VIEWER;
  String cookie = "ESPSESSIONID=; HttpOnly; SameSite=Strict; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
  server.sendHeader("Set-Cookie", cookie);
  server.sendHeader("Location", "/login");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302);
  addLog("User logged out");
}

void handleRoot() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  // Redirect to password change if using default credentials
  String decryptedPassword = decryptPassword();
  if (String(config.username) == "admin" && decryptedPassword == "admin") {
    server.sendHeader("Location", "/changepass");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  if (!handleFileRead("/index.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleChangePassGet() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  if (!handleFileRead("/changepass.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleChangePassPost() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  if (!server.hasArg("currentPassword") || !server.hasArg("newPassword")) {
    server.send(400, "text/plain", "Missing fields");
    return;
  }

  String currentPass = server.arg("currentPassword");
  String newPass = server.arg("newPassword");
  String decryptedPassword = decryptPassword();

  if (currentPass != decryptedPassword) {
    server.send(401, "text/plain", "Current password is incorrect");
    return;
  }

  if (newPass.length() < 8) {
    server.send(400, "text/plain", "New password must be at least 8 characters");
    return;
  }

  encryptPassword(newPass);
  saveConfig();
  
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302);
  addLog("Password changed for user: " + String(config.username));
}

void handleWiFiConfigGet() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!handleFileRead("/wificonfig.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleWiFiConfigPost() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (server.hasArg("ssid") && server.hasArg("password")) {
    strncpy(config.wifiSSID, server.arg("ssid").c_str(), sizeof(config.wifiSSID));
    strncpy(config.wifiPassword, server.arg("password").c_str(), sizeof(config.wifiPassword));
    
    if (server.hasArg("static_ip") && server.hasArg("gateway") && server.hasArg("subnet")) {
      config.staticIP.fromString(server.arg("static_ip"));
      config.gateway.fromString(server.arg("gateway"));
      config.subnet.fromString(server.arg("subnet"));
      
      if (server.hasArg("dns")) {
        config.dns.fromString(server.arg("dns"));
      } else {
        config.dns = config.gateway;
      }
    } else {
      config.staticIP = INADDR_NONE;
    }
    
    saveConfig();
    
    // Reconnect WiFi with new settings
    WiFi.disconnect();
    delay(1000);
    connectWiFi();
    
    server.sendHeader("Location", "/wificonfig?success=1");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    addLog("WiFi configuration updated");
  } else {
    server.send(400, "text/plain", "Missing SSID or password");
  }
}

void handleWiFiScan() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(1024);
  JsonArray networks = doc.to<JsonArray>();
  
  for (int i = 0; i < n; ++i) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = WiFi.encryptionType(i);
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleUserConfigGet() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!handleFileRead("/userconfig.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleUserConfigPost() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (server.hasArg("username") && server.hasArg("password") && server.hasArg("role")) {
    strncpy(config.username, server.arg("username").c_str(), sizeof(config.username));
    encryptPassword(server.arg("password"));
    config.role = (UserRole)server.arg("role").toInt();
    
    if (server.hasArg("birthday")) {
      strncpy(config.birthday, server.arg("birthday").c_str(), sizeof(config.birthday));
    }
    
    saveConfig();
    
    server.sendHeader("Location", "/userconfig?success=1");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    addLog("User configuration updated");
  } else {
    server.send(400, "text/plain", "Missing required fields");
  }
}

void handleRelayToggle() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  if (!server.hasArg("num") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }
  
  int num = server.arg("num").toInt();
  int state = server.arg("state").toInt();
  
  if (num < 1 || num > 8) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay number\"}");
    return;
  }
  
  relayStates[num - 1] = (state == 1);
  digitalWrite(relayPins[num - 1], relayStates[num - 1] ? HIGH : LOW);
  config.relayStates[num - 1] = relayStates[num - 1];
  saveConfig();
  
  addLog("Relay " + String(num) + (relayStates[num - 1] ? " ON" : " OFF"));
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRelayStates() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  StaticJsonDocument<64> jsonDoc;
  JsonArray arr = jsonDoc.to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    arr.add(relayStates[i]);
  }
  String json;
  serializeJson(arr, json);
  server.send(200, "application/json", json);
}

void handleCurrentReadings() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  StaticJsonDocument<64> jsonDoc;
  jsonDoc["temperature"] = currentTemp;
  jsonDoc["humidity"] = currentHum;
  String json;
  serializeJson(jsonDoc, json);
  server.send(200, "application/json", json);
}

void handleSensorData() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  String filter = server.hasArg("filter") ? server.arg("filter") : "LIVE";
  time_t now;
  time(&now);

  time_t fromTime;
  if (filter == "LIVE") {
    fromTime = now - 300; // last 5 minutes
  } else if (filter == "1H") {
    fromTime = now - 3600;
  } else if (filter == "1D") {
    fromTime = now - 86400;
  } else if (filter == "1W") {
    fromTime = now - 604800;
  } else {
    fromTime = now - 300;
  }

  DynamicJsonDocument doc(12000);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < dataCount; i++) {
    if (dataPoints[i].timestamp >= fromTime) {
      JsonObject obj = arr.createNestedObject();
      obj["timestamp"] = dataPoints[i].timestamp;
      obj["temperature"] = dataPoints[i].temperature;
      obj["humidity"] = dataPoints[i].humidity;
    }
  }
  
  String json;
  serializeJson(arr, json);
  server.send(200, "application/json", json);
}

void handleLogs() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    return;
  }

  server.send(200, "text/plain", logBuffer);
}

void handleResetPassGet() {
  if (!handleFileRead("/resetpass.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleResetPassPost() {
  if (!server.hasArg("username") || !server.hasArg("birthday") || !server.hasArg("password")) {
    server.send(400, "text/plain", "Missing fields");
    return;
  }
  
  String username = server.arg("username");
  String birthday = server.arg("birthday");
  String newPass = server.arg("password");
  
  if (username == config.username && birthday == config.birthday && newPass.length() >= 8) {
    encryptPassword(newPass);
    saveConfig();
    
    server.sendHeader("Location", "/login");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302);
    addLog("Password reset via birthday verification");
  } else {
    server.send(401, "text/plain", "Invalid details or password too short (min 8 chars)");
  }
}

void handleFactoryReset() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  server.send(200, "text/plain", "Factory reset initiated. Device will restart.");
  addLog("Factory reset initiated by admin");
  delay(1000);
  factoryReset();
}

void handleOTAUpdate() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.setDebugOutput(true);
    Serial.printf("Update: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
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
    Serial.setDebugOutput(false);
  }
  yield();
}

void handleOTAPage() {
  if (!hasAdminPrivileges()) {
    server.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!handleFileRead("/ota.html")) {
    server.send(404, "text/plain", "File not found");
  }
}

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "File not found");
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize relay pins
  for (int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Initialize DHT sensor
  dht.begin();

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    addLog("Failed to mount SPIFFS");
    delay(1000);
    ESP.restart();
  }

    // 1. Load EEPROM config first (default/fallback)
    loadConfig();

    // 2. Then load config.ini (which should override EEPROM)
    loadConfigFromINI();

    // 3. Save config to EEPROM so it's persistent
    saveConfig();

  connectWiFi();

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  int retry = 0;
  const int maxRetries = 10;
  while (!getLocalTime(&timeinfo) && retry < maxRetries) {
    Serial.println("Waiting for NTP time sync...");
    delay(1000);
    retry++;
  }
  if (retry == maxRetries) {
    Serial.println("Failed to obtain time");
    addLog("Failed to obtain NTP time");
  }

  // Set up web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLoginGet);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/changepass", HTTP_GET, handleChangePassGet);
  server.on("/changepass", HTTP_POST, handleChangePassPost);
  server.on("/wificonfig", HTTP_GET, handleWiFiConfigGet);
  server.on("/wificonfig", HTTP_POST, handleWiFiConfigPost);
  server.on("/wifiscan", HTTP_GET, handleWiFiScan);
  server.on("/userconfig", HTTP_GET, handleUserConfigGet);
  server.on("/userconfig", HTTP_POST, handleUserConfigPost);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relaystates", HTTP_GET, handleRelayStates);
  server.on("/current", HTTP_GET, handleCurrentReadings);
  server.on("/sensordata", HTTP_GET, handleSensorData);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/resetpass", HTTP_GET, handleResetPassGet);
  server.on("/resetpass", HTTP_POST, handleResetPassPost);
  server.on("/factoryreset", HTTP_POST, handleFactoryReset);
  server.on("/ota", HTTP_GET, handleOTAPage);
  server.on("/update", HTTP_POST, 
    []() { server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); ESP.restart(); },
    handleOTAUpdate
  );
  server.onNotFound(handleNotFound);

  server.begin();
  addLog("HTTP server started");

  // Start ElegantOTA
  ElegantOTA.begin(&server);    // For ESP32
}

void loop() {
  server.handleClient();

  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 5000) {
    lastSensorRead = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      currentTemp = t;
      currentHum = h;

      time_t now;
      time(&now);

      // Store data point
      if (dataCount < MAX_DATA_POINTS) {
        dataPoints[dataCount].timestamp = now;
        dataPoints[dataCount].temperature = currentTemp;
        dataPoints[dataCount].humidity = currentHum;
        dataCount++;
      } else {
        // Shift array left by one to free last slot
        memmove(&dataPoints[0], &dataPoints[1], sizeof(SensorDataPoint) * (MAX_DATA_POINTS - 1));
        dataPoints[MAX_DATA_POINTS - 1].timestamp = now;
        dataPoints[MAX_DATA_POINTS - 1].temperature = currentTemp;
        dataPoints[MAX_DATA_POINTS - 1].humidity = currentHum;
      }

      addLog("Temperature: " + String(currentTemp, 1) + "°C, Humidity: " + String(currentHum, 1) + "%");

      // Blink status LED
      digitalWrite(STATUS_LED, HIGH);
      delay(100);
      digitalWrite(STATUS_LED, LOW);
    } else {
      addLog("Failed to read from DHT sensor");
    }
  }

  // Watchdog reset
  esp_task_wdt_reset();
}