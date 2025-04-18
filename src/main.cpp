#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

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
  String timestamp = String(millis() / 1000);
  logBuffer += "[" + timestamp + "s] " + entry + "\n";
  Serial.println(entry);
}

void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>HT Research Labs</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
      body { font-family: Arial; text-align: center; background: #f2f2f2; }
      h2 { color: #333; }
      button {
        width: 80px; height: 50px; margin: 10px;
        font-size: 16px; border-radius: 8px; border: none;
      }
      .on { background: #4CAF50; color: white; }
      .off { background: #f44336; color: white; }
    </style>
  </head>
  <body>
    <h2>HT Research Labs</h2>
    <div id="relays"></div>
    <canvas id="sensorChart" width="400" height="200"></canvas><br>
    <button onclick="showLogs()">Logs</button>
    <pre id="logs" style="text-align:left;"></pre>

    <script>
      let chart;
      const data = { labels: [], datasets: [
        { label: 'Temperature (°C)', borderColor: 'red', data: [], fill: false },
        { label: 'Humidity (%)', borderColor: 'blue', data: [], fill: false }
      ]};

      function initChart() {
        const ctx = document.getElementById('sensorChart').getContext('2d');
        chart = new Chart(ctx, {
          type: 'line', data,
          options: { responsive: true, scales: {
            x: { title: { display: true, text: 'Time (s)' } },
            y: { beginAtZero: true, title: { display: true, text: 'Value' } }
          }}
        });
      }

      function updateUI() {
        fetch('/relayStatus').then(res => res.json()).then(states => {
          let html = '';
          for (let i = 0; i < 8; i++) {
            html += `<button class="${states[i] ? 'on' : 'off'}" onclick="toggleRelay(${i})">Relay ${i + 1}</button>`;
          }
          document.getElementById('relays').innerHTML = html;
        });

        fetch('/sensor').then(res => res.json()).then(data => {
          const timestamp = (Date.now() / 1000).toFixed(1);
          chart.data.labels.push(timestamp);
          chart.data.datasets[0].data.push(data.temperature);
          chart.data.datasets[1].data.push(data.humidity);
          if (chart.data.labels.length > 20) {
            chart.data.labels.shift();
            chart.data.datasets[0].data.shift();
            chart.data.datasets[1].data.shift();
          }
          chart.update();
        });
      }

      function toggleRelay(id) {
        fetch('/toggle?relay=' + id).then(updateUI);
      }

      function showLogs() {
        fetch('/logs').then(res => res.text()).then(text => {
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
      int pin = RELAY1 + (relay == 0 ? 0 : relay == 1 ? 13 : relay == 2 ? 14 : relay == 3 ? 15 : relay == 4 ? 16 : relay == 5 ? 17 : relay == 6 ? 19 : 20); // Adjust pin logic based on mapping
      digitalWrite(pin, relayStates[relay] ? HIGH : LOW);
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