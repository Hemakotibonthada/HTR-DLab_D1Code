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

float temperature;
float humidity;
bool relayStates[8] = {false, false, false, false, false, false, false, false};

const int EEPROM_SIZE = 512;
const int RELAY_ADDR = 0;
String logBuffer = "";

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

void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Telugu IoT Garage Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/luxon"></script>
  <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-luxon"></script>
  <style>
    body {
      font-family: 'Segoe UI', sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f0f7f7;
      color: #333;
    }

    header {
      background-color: #00796B;
      color: white;
      padding: 1rem;
      text-align: center;
      font-size: 1.8rem;
      font-weight: bold;
    }

    #relays {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: 1rem;
      padding: 1rem;
    }

    .switch {
      position: relative;
      display: inline-block;
      width: 60px;
      height: 34px;
    }

    .switch input {
      opacity: 0;
      width: 0;
      height: 0;
    }

    .slider {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0;
      right: 0; bottom: 0;
      background-color: #ccc;
      transition: .4s;
      border-radius: 34px;
    }

    .slider:before {
      position: absolute;
      content: "";
      height: 26px; width: 26px;
      left: 4px; bottom: 4px;
      background-color: white;
      transition: .4s;
      border-radius: 50%;
    }

    input:checked + .slider {
      background-color: #00A5A8;
    }

    input:checked + .slider:before {
      transform: translateX(26px);
    }

    .relay-label {
      display: flex;
      flex-direction: column;
      align-items: center;
      font-size: 0.9rem;
      color: #444;
    }

    #chart-container {
      padding: 1rem;
      margin: 0 auto;
      max-width: 900px;
      height: 400px;
    }

    #logs {
      background: #ffffff;
      padding: 1rem;
      margin: 1rem auto;
      max-width: 900px;
      border: 1px solid #ccc;
      white-space: pre-wrap;
      font-family: monospace;
      font-size: 0.9rem;
    }

    #logBtn {
      display: block;
      margin: 1rem auto;
      padding: 0.5rem 1rem;
      background-color: #00796B;
      color: white;
      border: none;
      border-radius: 5px;
      font-size: 1rem;
      cursor: pointer;
    }

    #logBtn:hover {
      background-color: #00665A;
    }
  </style>
</head>
<body>

  <header>Telugu IoT Garage Dashboard</header>

  <div id="relays">
    <!-- Switches will be populated here -->
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
        pointRadius: 4,
        pointHoverRadius: 6
      }]
    };

    function initChart() {
      const ctx = document.getElementById('sensorChart').getContext('2d');
      chart = new Chart(ctx, {
        type: 'line',
        data: data,
        options: {
          responsive: true,
          maintainAspectRatio: false,
          interaction: {
            mode: 'index',
            intersect: false
          },
          plugins: {
            legend: {
              labels: {
                color: '#333',
                font: {
                  size: 14
                }
              }
            }
          },
          scales: {
            x: {
              type: 'time',
              time: {
                displayFormats: {
                  second: 'HH:mm:ss'
                },
                tooltipFormat: 'yyyy-MM-dd HH:mm:ss'
              },
              title: {
                display: true,
                text: 'Time',
                color: '#555'
              },
              ticks: {
                color: '#666'
              },
              grid: {
                color: '#e0e0e0'
              }
            },
            y: {
              beginAtZero: true,
              title: {
                display: true,
                text: 'Temperature (°C)',
                color: '#555'
              },
              ticks: {
                color: '#666'
              },
              grid: {
                color: '#e0e0e0'
              }
            }
          }
        }
      });
    }

    function updateUI() {
      fetch('/relayStatus')
        .then(res => res.json())
        .then(states => {
          let html = '';
          for (let i = 0; i < 8; i++) {
            html += `
              <label class="relay-label">
                Relay ${i + 1}
                <label class="switch">
                  <input type="checkbox" ${states[i] ? 'checked' : ''} onclick="toggleRelay(${i})">
                  <span class="slider"></span>
                </label>
              </label>
            `;
          }
          document.getElementById('relays').innerHTML = html;
        });

      fetch('/sensor')
        .then(res => res.json())
        .then(dataPoint => {
          const now = new Date();
          data.labels.push(now);
          data.datasets[0].data.push({ x: now, y: dataPoint.temperature });

          if (data.labels.length > 30) {
            data.labels.shift();
            data.datasets[0].data.shift();
          }

          chart.update();
        });
    }

    function toggleRelay(id) {
      fetch('/toggle?relay=' + id).then(updateUI);
    }

    function showLogs() {
      fetch('/logs')
        .then(res => res.text())
        .then(text => {
          document.getElementById('logs').innerText = text;
        });
    }

    initChart();
    setInterval(updateUI, 5000);
    updateUI();
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
      EEPROM.write(RELAY_ADDR + relay, relayStates[relay]);
      EEPROM.commit();
      addLog("Relay " + String(relay + 1) + (relayStates[relay] ? " ON" : " OFF"));
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSensor() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  StaticJsonDocument<100> doc;
  doc["temperature"] = isnan(temperature) ? 0.0 : temperature;
  doc["humidity"] = isnan(humidity) ? 0.0 : humidity;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleRelayStatus() {
  StaticJsonDocument<200> doc;
  for (int i = 0; i < 8; i++) doc[i] = relayStates[i];
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleLogs() {
  server.send(200, "text/plain", logBuffer);
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  dht.begin();
  EEPROM.begin(EEPROM_SIZE);
  SPIFFS.begin();
  loadConfig();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  int relayPins[8] = {RELAY1, RELAY2, RELAY3, RELAY4, RELAY5, RELAY6, RELAY7, RELAY8};
  for (int i = 0; i < 8; i++) {
    pinMode(relayPins[i], OUTPUT);
    relayStates[i] = EEPROM.read(RELAY_ADDR + i);
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  bool res = wm.autoConnect("HemaController");
  if (!res) {
    Serial.println("Failed to connect. Restarting...");
    delay(3000);
    ESP.restart();
  }

  addLog("Connected to WiFi: " + WiFi.localIP().toString());
  digitalWrite(STATUS_LED, HIGH);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/relayStatus", HTTP_GET, handleRelayStatus);
  server.on("/logs", HTTP_GET, handleLogs);
  server.begin();
}

void loop() {
  server.handleClient();
}
