#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>

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

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // GMT+5:30
const int daylightOffset_sec = 0;

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

float temperature = 0;
float humidity = 0;
bool relayStates[8] = {false, false, false, false, false, false, false, false};

const int EEPROM_SIZE = 512;
const int RELAY_ADDR = 0;

String logBuffer = "";

struct SensorDataPoint {
  time_t timestamp;
  float temperature;
};

#define MAX_DATA_POINTS 1000
SensorDataPoint dataPoints[MAX_DATA_POINTS];
int dataCount = 0;

int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};

String getINIValue(String filePath, String key, String defaultValue) {
  File file = SPIFFS.open(filePath, "r");
  if (!file) return defaultValue;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    int separatorIndex = line.indexOf("=");
    if (separatorIndex != -1) {
      String lineKey = line.substring(0, separatorIndex);
      lineKey.trim();
      if (lineKey == key) {
        String value = line.substring(separatorIndex + 1);
        value.trim();
        return value;
      }
    }
  }
  return defaultValue;
}

void loadConfig() {
  String staticIP = getINIValue("/config.ini", "static_ip", "192.168.1.100");
  String gateway = getINIValue("/config.ini", "gateway", "192.168.1.1");
  String subnet = getINIValue("/config.ini", "subnet", "255.255.255.0");
  String dns = getINIValue("/config.ini", "dns", "8.8.8.8");

  IPAddress ip, gw, sn, dn;
  ip.fromString(staticIP);
  gw.fromString(gateway);
  sn.fromString(subnet);
  dn.fromString(dns);

  WiFi.config(ip, gw, sn, dn);
}

void addLog(String entry) {
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

void saveRelayStates() {
  for (int i = 0; i < 8; i++) {
    EEPROM.write(RELAY_ADDR + i, relayStates[i] ? 1 : 0);
  }
  EEPROM.commit();
}

void loadRelayStates() {
  for (int i = 0; i < 8; i++) {
    byte val = EEPROM.read(RELAY_ADDR + i);
    relayStates[i] = val == 1 ? true : false;
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Telugu IoT Garage Dashboard</title>
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
</style>
</head>
<body>
<header>Telugu IoT Garage Dashboard</header>

<div id="relays"></div>

<div id="controls">
  <button class="filter-btn active" data-filter="LIVE">LIVE</button>
  <button class="filter-btn" data-filter="1H">1H</button>
  <button class="filter-btn" data-filter="1D">1D</button>
  <button class="filter-btn" data-filter="1W">1W</button>
</div>

<div id="chart-container">
  <canvas id="sensorChart"></canvas>
</div>

<button id="logBtn" onclick="showLogs()">Show Logs</button>
<pre id="logs"></pre>

<script>
  let chart;
  const data = {
    labels: [],
    datasets: [{
      label: 'Temperature (°C)',
      borderColor: '#00A5A8',
      backgroundColor: '#00A5A8',
      data: [],
      tension: 0.4,
      fill: false,
      pointRadius: 5,
      pointHoverRadius: 7,
      borderWidth: 2,
      cubicInterpolationMode: 'monotone'
    }]
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
        grid: { display: false }  // NO GRID LINES on x-axis
      },
      y: {
        title: { display: true, text: 'Temperature (°C)', color: '#555' },
        ticks: { color: '#666' },
        grid: { display: false }  // NO GRID LINES on y-axis
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

  function fetchSensorData(filter='LIVE') {
    fetch(`/sensordata?filter=${filter}`).then(resp => resp.json()).then(points => {
      chart.data.labels = points.map(p => new Date(p.timestamp * 1000));
      chart.data.datasets[0].data = points.map(p => p.temperature);
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
    chart = new Chart(document.getElementById('sensorChart').getContext('2d'), {
      type: 'line',
      data: data,
      options: options
    });
    fetchSensorData();
    setInterval(() => {
      fetchRelayStates();
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

void handleRelayToggle() {
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
  StaticJsonDocument<64> jsonDoc;
  JsonArray arr = jsonDoc.to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    arr.add(relayStates[i]);
  }
  String json;
  serializeJson(arr, json);
  server.send(200, "application/json", json);
}

void handleSensorData() {
  String filter = server.hasArg("filter") ? server.arg("filter") : "LIVE";
  time_t now;
  time(&now);

  // Filter data points according to selected filter
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
    fromTime = now - 300; // default LIVE last 5 minutes
  }

  StaticJsonDocument<4096> jsonDoc;
  JsonArray arr = jsonDoc.to<JsonArray>();

  for (int i = 0; i < dataCount; i++) {
    if (dataPoints[i].timestamp >= fromTime) {
      JsonObject obj = arr.createNestedObject();
      obj["timestamp"] = dataPoints[i].timestamp;
      obj["temperature"] = dataPoints[i].temperature;
    }
  }
  String json;
  serializeJson(arr, json);
  server.send(200, "application/json", json);
}

void handleLogs() {
  server.send(200, "text/plain", logBuffer);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  for (int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  if(!SPIFFS.begin(true)){
    Serial.println("Failed to mount SPIFFS");
    return;
  }

  dht.begin();

  WiFiManager wifiManager;
  wifiManager.autoConnect("TeluguIoTGarage-AP");
  Serial.println("Connected to WiFi");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }

  loadRelayStates();
  addLog("ESP32 started and connected to WiFi");

  server.on("/", handleRoot);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relaystates", HTTP_GET, handleRelayStates);
  server.on("/sensordata", HTTP_GET, handleSensorData);
  server.on("/logs", HTTP_GET, handleLogs);
  server.begin();
}

void loop() {
  server.handleClient();

  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead > 5000) {
    lastSensorRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;

      time_t now;
      time(&now);

      if (dataCount < MAX_DATA_POINTS) {
        dataPoints[dataCount].timestamp = now;
        dataPoints[dataCount].temperature = temperature;
        dataCount++;
      } else {
        // Shift left and add new point at end
        for (int i = 1; i < MAX_DATA_POINTS; i++) {
          dataPoints[i-1] = dataPoints[i];
        }
        dataPoints[MAX_DATA_POINTS - 1].timestamp = now;
        dataPoints[MAX_DATA_POINTS - 1].temperature = temperature;
      }

      addLog("Temp: " + String(temperature, 1) + "C, Hum: " + String(humidity, 1) + "%");
      digitalWrite(STATUS_LED, HIGH);
      delay(100);
      digitalWrite(STATUS_LED, LOW);
    }
  }
}
