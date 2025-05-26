#include <WiFiManager.h>
#include <WebServer.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>

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
#define EEPROM_SIZE 512
#define USERNAME_ADDR 0
#define PASSWORD_ADDR 32
#define RELAY_ADDR 64

// Sensor Data Configuration
#define MAX_DATA_POINTS 3000

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// Global Variables
String savedUsername;
String savedPassword;
bool relayStates[8] = {false, false, false, false, false, false, false, false};
const int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};
String logBuffer = "";

struct SensorDataPoint {
  time_t timestamp;
  float temperature;
  float humidity;
};
SensorDataPoint dataPoints[MAX_DATA_POINTS];
int dataCount = 0;

float currentTemp = NAN;
float currentHum = NAN;

// Helper Functions

// Read INI-like config file from SPIFFS
String getINIValue(const String& filePath, const String& key, const String& defaultValue) {
  if (!SPIFFS.exists(filePath)) {
    return defaultValue;
  }
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

// Load network config and apply static IP if any
void loadConfig() {
  String staticIP = getINIValue("/config.ini", "static_ip", "");
  String gateway = getINIValue("/config.ini", "gateway", "");
  String subnet = getINIValue("/config.ini", "subnet", "");
  String dns = getINIValue("/config.ini", "dns", "");

  if (staticIP.length() > 0 && gateway.length() > 0 && subnet.length() > 0) {
    IPAddress ip, gw, sn, dn;
    ip.fromString(staticIP);
    gw.fromString(gateway);
    sn.fromString(subnet);
    dn.fromString(dns.length() > 0 ? dns : gateway); // Use gateway as DNS if not specified

    if (!WiFi.config(ip, gw, sn, dn)) {
      Serial.println("Failed to configure static IP");
    }
  }
}

// Append entry to log buffer with timestamp
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

// Load credentials from EEPROM or set default
void loadCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  char buf[32];
  
  // Read username
  for (int i = 0; i < 31; i++) {
    buf[i] = EEPROM.read(USERNAME_ADDR + i);
  }
  buf[31] = '\0';
  savedUsername = String(buf);

  // Read password
  for (int i = 0; i < 31; i++) {
    buf[i] = EEPROM.read(PASSWORD_ADDR + i);
  }
  buf[31] = '\0';
  savedPassword = String(buf);

  EEPROM.end();

  // Set default credentials if none found
  if (savedUsername.length() == 0 || savedPassword.length() == 0) {
    savedUsername = "admin";
    savedPassword = "admin";
  }
}

// Save credentials to EEPROM
void saveCredentials(const String& username, const String& password) {
  EEPROM.begin(EEPROM_SIZE);
  
  // Write username
  for (int i = 0; i < 31; i++) {
    EEPROM.write(USERNAME_ADDR + i, i < username.length() ? username[i] : 0);
  }
  
  // Write password
  for (int i = 0; i < 31; i++) {
    EEPROM.write(PASSWORD_ADDR + i, i < password.length() ? password[i] : 0);
  }
  
  EEPROM.commit();
  EEPROM.end();

  savedUsername = username;
  savedPassword = password;
  addLog("Credentials updated");
}

// Save relay states to EEPROM
void saveRelayStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 8; i++) {
    EEPROM.write(RELAY_ADDR + i, relayStates[i] ? 1 : 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

// Load relay states from EEPROM and update pins
void loadRelayStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 8; i++) {
    byte val = EEPROM.read(RELAY_ADDR + i);
    relayStates[i] = (val == 1);
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
  EEPROM.end();
}

// Base64 Authentication Functions

static const char base64_chars[] = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

inline bool isBase64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

String base64_decode(const String& input) {
  int in_len = input.length();
  int i = 0, j = 0, in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  String ret = "";

  while (in_len-- && (input[in_] != '=') && isBase64(input[in_])) {
    char_array_4[i++] = input[in_]; in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] = strchr(base64_chars, char_array_4[i]) - base64_chars;

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; i < 3; i++)
        ret += (char)char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++)
      char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] = strchr(base64_chars, char_array_4[j]) - base64_chars;

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; j < (i - 1); j++) ret += (char)char_array_3[j];
  }
  return ret;
}

// Basic Auth check
bool checkAuth() {
  if (!server.hasHeader("Authorization")) {
    server.sendHeader("WWW-Authenticate", "Basic realm=\"Login Required\"");
    server.send(401, "text/plain", "Unauthorized");
    return false;
  }
  
  String authHeader = server.header("Authorization");
  if (authHeader.startsWith("Basic ")) {
    String encoded = authHeader.substring(6);
    String decoded = base64_decode(encoded);
    int colonIndex = decoded.indexOf(':');
    
    if (colonIndex > 0) {
      String user = decoded.substring(0, colonIndex);
      String pass = decoded.substring(colonIndex + 1);
      
      if (user == savedUsername && pass == savedPassword) {
        return true;
      }
    }
  }
  
  server.sendHeader("WWW-Authenticate", "Basic realm=\"Login Required\"");
  server.send(401, "text/plain", "Unauthorized");
  return false;
}

// Web Handlers

void handleRoot() {
  if (!checkAuth()) return;

  // Force password change if default creds
  if (savedUsername == "admin" && savedPassword == "admin") {
    server.sendHeader("Location", "/changepass");
    server.send(302, "text/plain", "Redirecting to password change");
    return;
  }

  // Serve the complete dashboard page
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>IoT Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/luxon"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-luxon"></script>
<style>
  body { font-family: 'Segoe UI', sans-serif; margin:0; padding:0; background:#f0f7f7; color:#333;}
  header { background:#00796B; color:#fff; padding:1rem; text-align:center; font-size:1.8rem; font-weight:bold;}
  #relays { display:flex; flex-wrap:wrap; justify-content:center; gap:1rem; padding:1rem;}
  .relay-label { display:flex; flex-direction:column; align-items:center; font-size:0.9rem; color:#444; }
  .switch { position:relative; display:inline-block; width:60px; height:34px; }
  .switch input { opacity:0; width:0; height:0; }
  .slider { position:absolute; cursor:pointer; top:0; left:0; right:0; bottom:0; background:#ccc; transition:.4s; border-radius:34px;}
  .slider:before { position:absolute; content:""; height:26px; width:26px; left:4px; bottom:4px; background:#fff; transition:.4s; border-radius:50%; }
  input:checked + .slider { background:#00A5A8; }
  input:checked + .slider:before { transform: translateX(26px); }
  #chart-container { max-width:900px; height:400px; margin: 0 auto; padding: 1rem; }
  #controls { text-align:center; margin-bottom: 1rem; }
  button.filter-btn { background:#00796B; color:#fff; border:none; padding: 0.5rem 1rem; margin: 0 0.3rem; border-radius:4px; cursor:pointer; font-weight:bold;}
  button.filter-btn.active { background:#004d40; }
  #logs { background:#fff; padding:1rem; margin:1rem auto; max-width:900px; border:1px solid #ccc; white-space: pre-wrap; font-family: monospace; font-size: 0.9rem; height:150px; overflow-y:auto;}
  #logBtn { display:block; margin: 1rem auto; padding:0.5rem 1rem; background:#00796B; color:#fff; border:none; border-radius:5px; font-size:1rem; cursor:pointer; }
  #logBtn:hover { background:#00665A; }
  .dashboard-section { background:#fff; border-radius:8px; box-shadow:0 2px 5px rgba(0,0,0,0.1); margin:1rem; padding:1rem;}
  .current-readings { display:flex; justify-content:center; gap:2rem; margin-bottom:1rem;}
  .reading { text-align:center;}
  .reading-value { font-size:1.5rem; font-weight:bold; color:#00796B;}
  .reading-label { font-size:0.9rem; color:#666;}
</style>
</head>
<body>
<header>IoT Dashboard</header>

<div class="dashboard-section">
  <div class="current-readings">
    <div class="reading">
      <div class="reading-label">Temperature</div>
      <div class="reading-value" id="current-temp">--.- °C</div>
    </div>
    <div class="reading">
      <div class="reading-label">Humidity</div>
      <div class="reading-value" id="current-hum">--.- %</div>
    </div>
  </div>
</div>

<div class="dashboard-section">
  <h3 style="text-align:center;">Relay Controls</h3>
  <div id="relays"></div>
</div>

<div class="dashboard-section">
  <div id="controls">
    <button class="filter-btn active" data-filter="LIVE">LIVE</button>
    <button class="filter-btn" data-filter="1H">1H</button>
    <button class="filter-btn" data-filter="1D">1D</button>
    <button class="filter-btn" data-filter="1W">1W</button>
  </div>

  <div id="chart-container">
    <canvas id="sensorChart"></canvas>
  </div>
</div>

<button id="logBtn" onclick="showLogs()">Show Logs</button>
<pre id="logs"></pre>

<script>
  let chart;
  const data = {
    labels: [],
    datasets: [
      {
        label: 'Temperature (°C)',
        borderColor: '#FF6384',
        backgroundColor: '#FF6384',
        data: [],
        tension: 0.4,
        fill: false,
        pointRadius: 5,
        pointHoverRadius: 7,
        borderWidth: 2,
        yAxisID: 'y'
      },
      {
        label: 'Humidity (%)',
        borderColor: '#36A2EB',
        backgroundColor: '#36A2EB',
        data: [],
        tension: 0.4,
        fill: false,
        pointRadius: 5,
        pointHoverRadius: 7,
        borderWidth: 2,
        yAxisID: 'y1'
      }
    ]
  };

  const options = {
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { labels: { color: '#333', font: { size: 14 } } }
    },
    scales: {
      x: {
        type: 'time',
        time: { tooltipFormat: 'yyyy-MM-dd HH:mm:ss', displayFormats: { second: 'HH:mm:ss', minute:'HH:mm', hour:'HH:mm' } },
        title: { display: true, text: 'Time', color: '#555' },
        ticks: { color: '#666', maxRotation: 0, autoSkipPadding: 20 },
        grid: { display: false }
      },
      y: {
        type: 'linear',
        display: true,
        position: 'left',
        title: { display: true, text: 'Temperature (°C)', color: '#555' },
        ticks: { color: '#666' },
        grid: { display: false }
      },
      y1: {
        type: 'linear',
        display: true,
        position: 'right',
        title: { display: true, text: 'Humidity (%)', color: '#555' },
        ticks: { color: '#666' },
        grid: { display: false }
      }
    }
  };

  function createRelaysUI() {
    const relaysDiv = document.getElementById('relays');
    relaysDiv.innerHTML = '';
    for (let i = 1; i <= 8; i++) {
      const relayDiv = document.createElement('div');
      relayDiv.className = 'relay-label';
      relayDiv.innerHTML = `
        Relay ${i} <label class="switch"><input type="checkbox" id="relay${i}" /><span class="slider"></span></label>
      `;
      relaysDiv.appendChild(relayDiv);
      document.getElementById(`relay${i}`).addEventListener('change', e => toggleRelay(i, e.target.checked));
    }
  }

  function toggleRelay(relayNum, state) {
    fetch(`/relay?num=${relayNum}&state=${state ? 1 : 0}`).then(resp => resp.json()).then(data => {
      if (!data.success) alert('Failed to toggle relay');
    });
  }

  function fetchRelayStates() {
    fetch('/relaystates').then(resp => resp.json()).then(states => {
      for (let i = 1; i <= 8; i++) {
        document.getElementById(`relay${i}`).checked = states[i - 1];
      }
    });
  }

  function fetchCurrentReadings() {
    fetch('/current').then(resp => resp.json()).then(data => {
      document.getElementById('current-temp').textContent = data.temperature.toFixed(1) + ' °C';
      document.getElementById('current-hum').textContent = data.humidity.toFixed(1) + ' %';
    });
  }

  function fetchSensorData(filter='LIVE') {
    fetch(`/sensordata?filter=${filter}`).then(resp => resp.json()).then(points => {
      chart.data.labels = points.map(p => new Date(p.timestamp * 1000));
      chart.data.datasets[0].data = points.map(p => p.temperature);
      chart.data.datasets[1].data = points.map(p => p.humidity);
      chart.update();
    });
  }

  function showLogs() {
    fetch('/logs').then(resp => resp.text()).then(text => {
      const logsEl = document.getElementById('logs');
      logsEl.textContent = text;
      logsEl.scrollTop = logsEl.scrollHeight;
    });
  }

  document.querySelectorAll('.filter-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.filter-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      fetchSensorData(btn.getAttribute('data-filter'));
    });
  });

  window.onload = () => {
    createRelaysUI();
    fetchRelayStates();
    fetchCurrentReadings();
    
    chart = new Chart(document.getElementById('sensorChart').getContext('2d'), {
      type: 'line',
      data: data,
      options: options
    });
    
    fetchSensorData();
    
    // Set up periodic updates
    setInterval(() => {
      fetchRelayStates();
      fetchCurrentReadings();
      const activeFilter = document.querySelector('.filter-btn.active').getAttribute('data-filter');
      fetchSensorData(activeFilter);
    }, 10000);
  };
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleChangePassGet() {
  if (!checkAuth()) return;

  String html = R"rawliteral(
  <html><body>
  <h3>Change Username and Password</h3>
  <form method="POST" action="/changepass">
    New Username:<br><input name="username" required><br>
    New Password:<br><input name="password" type="password" required><br><br>
    <input type="submit" value="Save">
  </form>
  </body></html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleChangePassPost() {
  if (!checkAuth()) return;

  if (!server.hasArg("username") || !server.hasArg("password") ||
      server.arg("username").length() == 0 || server.arg("password").length() == 0) {
    server.send(400, "text/plain", "Username and password cannot be empty");
    return;
  }

  String newUser = server.arg("username");
  String newPass = server.arg("password");

  saveCredentials(newUser, newPass);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Credentials updated. Redirecting...");
}

void handleRelayToggle() {
  if (!checkAuth()) return;

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
  saveRelayStates();
  addLog("Relay " + String(num) + (relayStates[num - 1] ? " ON" : " OFF"));
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRelayStates() {
  if (!checkAuth()) return;

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
  if (!checkAuth()) return;

  StaticJsonDocument<64> jsonDoc;
  jsonDoc["temperature"] = currentTemp;
  jsonDoc["humidity"] = currentHum;
  String json;
  serializeJson(jsonDoc, json);
  server.send(200, "application/json", json);
}

void handleSensorData() {
  if (!checkAuth()) return;

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
  if (!checkAuth()) return;

  server.send(200, "text/plain", logBuffer);
}

void setup() {
  Serial.begin(115200);

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    addLog("Failed to mount SPIFFS");
  } else {
    // Load config file from SPIFFS
    loadConfig();
    addLog("Config file loaded from SPIFFS");
  }

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

  // Load configuration
  loadConfig();
  loadCredentials();
  loadRelayStates();

  // Connect to WiFi
  WiFiManager wm;
  if (!wm.autoConnect("IoT-Garage-AP")) {
    Serial.println("Failed to connect and hit timeout");
    ESP.restart();
  }

  Serial.println("Connected to WiFi");
  addLog("System started. Connected to WiFi: " + WiFi.localIP().toString());

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    addLog("Failed to obtain NTP time");
  }

  // Set up web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/changepass", HTTP_GET, handleChangePassGet);
  server.on("/changepass", HTTP_POST, handleChangePassPost);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relaystates", HTTP_GET, handleRelayStates);
  server.on("/current", HTTP_GET, handleCurrentReadings);
  server.on("/sensordata", HTTP_GET, handleSensorData);
  server.on("/logs", HTTP_GET, handleLogs);

  server.begin();
  addLog("HTTP server started");
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
}