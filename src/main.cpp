#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

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

// Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // GMT+5:30
const int daylightOffset_sec = 0;
const int EEPROM_SIZE = 512;
const int RELAY_ADDR = 0;

// Global Objects
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiManager wifiManager;

// Global Variables
float temperature = 0.0;
float humidity = 0.0;
bool relayStates[8] = {false, false, false, false, false, false, false, false};
String logBuffer = "";
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 2000; // 2 seconds

// Function Declarations
void setupOTA();
void addLog(String entry);
void handleRoot();
void handleToggle();
void handleSensor();
void handleRelayStatus();
void handleLogs();
void handleNotFound();
void updateSensorData();
void saveRelayStates();
void loadRelayStates();
void setupWiFi();

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Initialize components
  dht.begin();
  EEPROM.begin(EEPROM_SIZE);
  SPIFFS.begin(true); // Initialize SPIFFS with format if needed

  // Load saved relay states
  loadRelayStates();

  // Initialize relay pins
  int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};
  for (int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }

  // Setup WiFi
  setupWiFi();

  // Configure time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Setup OTA
  setupOTA();

  // Setup Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/relayStatus", HTTP_GET, handleRelayStatus);
  server.on("/logs", HTTP_GET, handleLogs);
  server.onNotFound(handleNotFound);
  server.begin();

  // Start mDNS
  if (!MDNS.begin("iot-garage")) {
    Serial.println("Error setting up MDNS responder!");
  }

  Serial.println("HTTP server started");
  digitalWrite(STATUS_LED, HIGH);
  addLog("System initialized");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  // Update sensor data periodically
  if (millis() - lastSensorRead >= sensorInterval) {
    updateSensorData();
    lastSensorRead = millis();
  }
}

// OTA Setup Function
void setupOTA() {
  ArduinoOTA.setHostname("iot-garage-controller");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
      SPIFFS.end();
    }
    addLog("OTA Update Started: " + type);
    digitalWrite(STATUS_LED, LOW);
  });
  
  ArduinoOTA.onEnd([]() {
    addLog("OTA Update Finished");
    digitalWrite(STATUS_LED, HIGH);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) addLog("OTA Auth Failed");
    else if (error == OTA_BEGIN_ERROR) addLog("OTA Begin Failed");
    else if (error == OTA_CONNECT_ERROR) addLog("OTA Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) addLog("OTA Receive Failed");
    else if (error == OTA_END_ERROR) addLog("OTA End Failed");
    digitalWrite(STATUS_LED, HIGH);
  });
  
  ArduinoOTA.begin();
  addLog("OTA Update Service Ready");
}

// WiFi Setup Function
void setupWiFi() {
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(30);
  wifiManager.setDebugOutput(true);
  
  if (!wifiManager.autoConnect("HemaController")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }
  
  addLog("Connected to WiFi: " + WiFi.localIP().toString());
}

// Update Sensor Data
void updateSensorData() {
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();
  
  if (!isnan(newTemp)) temperature = newTemp;
  if (!isnan(newHum)) humidity = newHum;
  
  // Log significant changes
  static float lastLoggedTemp = 0, lastLoggedHum = 0;
  if (abs(temperature - lastLoggedTemp) > 0.5 || abs(humidity - lastLoggedHum) > 1.0) {
    addLog("Sensor Update - Temp: " + String(temperature) + "°C, Hum: " + String(humidity) + "%");
    lastLoggedTemp = temperature;
    lastLoggedHum = humidity;
  }
}

// Save/Load Relay States
void saveRelayStates() {
  for (int i = 0; i < 8; i++) {
    EEPROM.write(RELAY_ADDR + i, relayStates[i]);
  }
  EEPROM.commit();
}

void loadRelayStates() {
  for (int i = 0; i < 8; i++) {
    relayStates[i] = EEPROM.read(RELAY_ADDR + i);
  }
}

// Logging Function
void addLog(String entry) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    logBuffer += "[TimeError] " + entry + "\n";
    return;
  }
  
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  logBuffer += "[" + String(timestamp) + "] " + entry + "\n";
  
  // Keep log buffer manageable
  if (logBuffer.length() > 4096) {
    logBuffer = logBuffer.substring(logBuffer.length() - 4096);
  }
  
  Serial.println(entry);
}

// Web Server Handlers
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IoT Garage Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root {
      --primary: #00796B;
      --secondary: #00A5A8;
      --error: #F44336;
      --bg: #f5f5f5;
      --card-bg: #fff;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: #333; line-height: 1.6; }
    .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
    header { background: var(--primary); color: white; padding: 1rem; text-align: center; margin-bottom: 2rem; }
    .card { background: var(--card-bg); border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); padding: 1.5rem; margin-bottom: 2rem; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 2rem; }
    .sensor-display { display: flex; justify-content: space-between; }
    .sensor-value { font-size: 1.5rem; font-weight: bold; color: var(--primary); }
    .btn-group { display: flex; gap: 0.5rem; flex-wrap: wrap; margin: 1rem 0; }
    .btn { padding: 0.5rem 1rem; border: none; border-radius: 4px; cursor: pointer; }
    .btn-primary { background: var(--primary); color: white; }
    .btn-outline { background: transparent; border: 1px solid var(--primary); color: var(--primary); }
    .switch { position: relative; display: inline-block; width: 60px; height: 34px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background: #ccc; transition: .4s; border-radius: 34px; }
    .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background: var(--secondary); }
    input:checked + .slider:before { transform: translateX(26px); }
    .relay-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 1rem; }
    .relay-item { display: flex; flex-direction: column; align-items: center; padding: 1rem; }
    #chart-container { height: 400px; width: 100%; }
    #logs { background: #f9f9f9; padding: 1rem; border-radius: 4px; margin-top: 1rem; white-space: pre-wrap; display: none; }
    .ota-section { margin-top: 2rem; border-top: 1px solid #eee; padding-top: 1rem; }
  </style>
</head>
<body>
  <header>
    <h1>IoT Garage Dashboard</h1>
  </header>
  
  <div class="container">
    <div class="grid">
      <div class="card">
        <h2>Sensor Data</h2>
        <div class="sensor-display">
          <div>
            <div>Temperature</div>
            <div class="sensor-value" id="temperature">--°C</div>
          </div>
          <div>
            <div>Humidity</div>
            <div class="sensor-value" id="humidity">--%</div>
          </div>
        </div>
        
        <div class="btn-group">
          <button class="btn btn-primary active" data-range="live">Live</button>
          <button class="btn btn-outline" data-range="1h">1H</button>
          <button class="btn btn-outline" data-range="1d">1D</button>
          <button class="btn btn-outline" data-range="7d">7D</button>
        </div>
        
        <div id="chart-container">
          <canvas id="sensorChart"></canvas>
        </div>
      </div>
      
      <div class="card">
        <h2>Relay Controls</h2>
        <div class="relay-grid" id="relays">
          <!-- Relay switches will be inserted here -->
        </div>
      </div>
    </div>
    
    <div class="card">
      <h2>System Logs</h2>
      <button class="btn btn-primary" id="logBtn">Show Logs</button>
      <pre id="logs"></pre>
    </div>
    
    <div class="card ota-section">
      <h2>OTA Update</h2>
      <div id="ota-status">Ready for updates</div>
      <div id="ota-progress" style="margin-top: 1rem; display: none;">
        <progress value="0" max="100" style="width: 100%;"></progress>
        <div id="ota-progress-text">0%</div>
      </div>
    </div>
  </div>

  <script>
    // DOM Elements
    const tempEl = document.getElementById('temperature');
    const humEl = document.getElementById('humidity');
    const relaysEl = document.getElementById('relays');
    const logBtn = document.getElementById('logBtn');
    const logsEl = document.getElementById('logs');
    const otaStatus = document.getElementById('ota-status');
    const otaProgress = document.getElementById('ota-progress');
    const otaProgressText = document.getElementById('ota-progress-text');
    
    // Chart Initialization
    const ctx = document.getElementById('sensorChart').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        datasets: [{
          label: 'Temperature (°C)',
          data: [],
          borderColor: '#00796B',
          backgroundColor: 'rgba(0, 121, 107, 0.1)',
          tension: 0.4,
          pointRadius: 3
        }, {
          label: 'Humidity (%)',
          data: [],
          borderColor: '#00A5A8',
          backgroundColor: 'rgba(0, 165, 168, 0.1)',
          tension: 0.4,
          pointRadius: 3,
          hidden: true
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          x: {
            type: 'time',
            time: {
              tooltipFormat: 'MMM d, HH:mm:ss'
            }
          },
          y: {
            beginAtZero: false
          }
        }
      }
    });
    
    // WebSocket for real-time updates
    let socket;
    function initWebSocket() {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const wsUri = protocol + '//' + window.location.host + '/ws';
      socket = new WebSocket(wsUri);
      
      socket.onopen = function(e) {
        console.log('WebSocket connected');
      };
      
      socket.onmessage = function(event) {
        const data = JSON.parse(event.data);
        
        // Update sensor values
        if (data.temperature !== undefined) {
          tempEl.textContent = data.temperature.toFixed(1) + '°C';
        }
        if (data.humidity !== undefined) {
          humEl.textContent = data.humidity.toFixed(1) + '%';
        }
        
        // Update chart
        const now = new Date();
        if (chart.data.datasets[0].data.length > 100) {
          chart.data.datasets[0].data.shift();
          chart.data.datasets[1].data.shift();
        }
        chart.data.datasets[0].data.push({x: now, y: data.temperature});
        chart.data.datasets[1].data.push({x: now, y: data.humidity});
        chart.update();
      };
      
      socket.onclose = function(event) {
        console.log('WebSocket disconnected, reconnecting...');
        setTimeout(initWebSocket, 1000);
      };
      
      socket.onerror = function(error) {
        console.log('WebSocket error:', error);
      };
    }
    
    // Update relay switches
    function updateRelays() {
      fetch('/relayStatus')
        .then(res => res.json())
        .then(states => {
          let html = '';
          for (let i = 0; i < 8; i++) {
            html += `
              <div class="relay-item">
                <div>Relay ${i+1}</div>
                <label class="switch">
                  <input type="checkbox" ${states[i] ? 'checked' : ''} onchange="toggleRelay(${i})">
                  <span class="slider"></span>
                </label>
              </div>
            `;
          }
          relaysEl.innerHTML = html;
        });
    }
    
    // Toggle relay
    function toggleRelay(relayNum) {
      fetch(`/toggle?relay=${relayNum}`)
        .then(() => updateRelays());
    }
    
    // Toggle logs visibility
    logBtn.addEventListener('click', function() {
      if (logsEl.style.display === 'none' || !logsEl.style.display) {
        fetch('/logs')
          .then(res => res.text())
          .then(text => {
            logsEl.textContent = text;
            logsEl.style.display = 'block';
            logBtn.textContent = 'Hide Logs';
          });
      } else {
        logsEl.style.display = 'none';
        logBtn.textContent = 'Show Logs';
      }
    });
    
    // Time range buttons
    document.querySelectorAll('[data-range]').forEach(btn => {
      btn.addEventListener('click', function() {
        document.querySelectorAll('[data-range]').forEach(b => {
          b.classList.remove('active');
          b.classList.remove('btn-primary');
          b.classList.add('btn-outline');
        });
        this.classList.add('active');
        this.classList.add('btn-primary');
        this.classList.remove('btn-outline');
        
        // Here you would update the chart based on the selected time range
        // For now we'll just keep showing live data
      });
    });
    
    // Initialize
    updateRelays();
    initWebSocket();
    
    // OTA Update handling
    const otaSocket = new WebSocket('ws://' + window.location.host + '/ota');
    otaSocket.onmessage = function(event) {
      const data = JSON.parse(event.data);
      if (data.status) {
        otaStatus.textContent = data.status;
      }
      if (data.progress !== undefined) {
        otaProgress.style.display = 'block';
        document.querySelector('#ota-progress progress').value = data.progress;
        otaProgressText.textContent = data.progress + '%';
      }
    };
  </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleToggle() {
  if (server.hasArg("relay")) {
    int relay = server.arg("relay").toInt();
    if (relay >= 0 && relay < 8) {
      relayStates[relay] = !relayStates[relay];
      int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};
      digitalWrite(relayPins[relay], relayStates[relay] ? HIGH : LOW);
      saveRelayStates();
      addLog("Relay " + String(relay + 1) + (relayStates[relay] ? " ON" : " OFF"));
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSensor() {
  JsonDocument doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleRelayStatus() {
  JsonDocument doc;
  for (int i = 0; i < 8; i++) doc[i] = relayStates[i];
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleLogs() {
  server.send(200, "text/plain", logBuffer);
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