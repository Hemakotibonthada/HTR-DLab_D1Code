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
#define BIRTHDAY_ADDR 96 // after relay states

// Sensor Data Configuration
#define MAX_DATA_POINTS 3000

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// Global Variables
String savedUsername;
String savedPassword;
String savedBirthday;
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
String sessionToken = "";

// Helper Functions
String generateSessionToken() {
  String token = "";
  for (int i = 0; i < 16; i++) token += String(random(0, 16), HEX);
  return token;
}

bool isLoggedIn() {
  return true;
}

void requireLogin() {
  // No-op: login is disabled, always allow
}

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

String stripQuotes(const String& s) {
  if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) {
    return s.substring(1, s.length() - 1);
  }
  return s;
}

void connectStaticWiFi() {
  String ssid = stripQuotes(getINIValue("/config.ini", "wifi_ssid", ""));
  String pass = stripQuotes(getINIValue("/config.ini", "wifi_password", ""));
  String staticIP = getINIValue("/config.ini", "static_ip", "");
  String gateway = getINIValue("/config.ini", "gateway", "");
  String subnet = getINIValue("/config.ini", "subnet", "");
  String dns = getINIValue("/config.ini", "dns", "");

  if (ssid == "" || pass == "" || staticIP == "" || gateway == "" || subnet == "") {
    Serial.println("Missing WiFi/static IP config in config.ini");
    while (1) delay(1000);
  }

  IPAddress ip, gw, sn, dn;
  ip.fromString(staticIP);
  gw.fromString(gateway);
  sn.fromString(subnet);
  dn.fromString(dns.length() > 0 ? dns : gateway);

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
    while (1) delay(1000);
  }
}

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
    dn.fromString(dns.length() > 0 ? dns : gateway);

    if (!WiFi.config(ip, gw, sn, dn)) {
      Serial.println("Failed to configure static IP");
    }
  }
}

void loadCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  char buf[32];
  
  for (int i = 0; i < 31; i++) {
    buf[i] = EEPROM.read(USERNAME_ADDR + i);
  }
  buf[31] = '\0';
  savedUsername = String(buf);

  for (int i = 0; i < 31; i++) {
    buf[i] = EEPROM.read(PASSWORD_ADDR + i);
  }
  buf[31] = '\0';
  savedPassword = String(buf);

  for (int i = 0; i < 15; i++) {
    buf[i] = EEPROM.read(BIRTHDAY_ADDR + i);
  }
  buf[15] = '\0';
  savedBirthday = String(buf);

  EEPROM.end();

  if (savedUsername.length() == 0 || savedPassword.length() == 0) {
    savedUsername = "admin";
    savedPassword = "admin";
    savedBirthday = "2000-01-01";
  }
}

void saveCredentials(const String& username, const String& password, const String& birthday = "") {
  EEPROM.begin(EEPROM_SIZE);
  
  for (int i = 0; i < 31; i++) {
    EEPROM.write(USERNAME_ADDR + i, i < username.length() ? username[i] : 0);
  }
  
  for (int i = 0; i < 31; i++) {
    EEPROM.write(PASSWORD_ADDR + i, i < password.length() ? password[i] : 0);
  }

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
  for (int i = 0; i < 8; i++) {
    EEPROM.write(RELAY_ADDR + i, relayStates[i] ? 1 : 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadRelayStates() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 8; i++) {
    byte val = EEPROM.read(RELAY_ADDR + i);
    relayStates[i] = (val == 1);
    digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
  }
  EEPROM.end();
}

// Web Handlers
void handleLogin() {
  if (server.method() == HTTP_GET) {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>IoT Dashboard Login</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body {
      min-height: 100vh;
      background: linear-gradient(135deg, #00c6ff 0%, #0072ff 100%);
      display: flex;
      align-items: center;
      justify-content: center;
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      margin: 0;
      padding: 0;
      overflow: hidden;
    }
    .login-outer {
      perspective: 1200px;
      display: flex;
      align-items: center;
      justify-content: center;
      width: 100vw;
      height: 100vh;
    }
    .login-container {
      background: rgba(255,255,255,0.95);
      border-radius: 24px;
      box-shadow: 0 12px 32px 0 rgba(0,0,0,0.18), 0 1.5px 6px 0 rgba(0,0,0,0.10);
      padding: 2.8rem 2.2rem 2.2rem 2.2rem;
      width: 100%;
      max-width: 370px;
      animation: popIn 0.8s cubic-bezier(.68,-0.55,.27,1.55);
      transform-style: preserve-3d;
      position: relative;
      z-index: 2;
    }
    @keyframes popIn {
      0% { opacity: 0; transform: scale(0.7) rotateY(30deg);}
      80% { opacity: 1; transform: scale(1.05) rotateY(-5deg);}
      100% { opacity: 1; transform: scale(1) rotateY(0);}
    }
    .login-title {
      font-size: 2.2rem;
      font-weight: 800;
      color: #0072ff;
      margin-bottom: 1.7rem;
      letter-spacing: 1.5px;
      text-align: center;
      text-shadow: 0 2px 8px #b3e0ff, 0 1px 0 #fff;
    }
    .input-group {
      margin-bottom: 1.3rem;
      position: relative;
    }
    .input-group label {
      display: block;
      margin-bottom: 0.5rem;
      color: #0072ff;
      font-weight: 600;
      letter-spacing: 0.5px;
      text-shadow: 0 1px 0 #fff;
    }
    .input-group input {
      width: 100%;
      padding: 0.8rem 1.1rem;
      border: none;
      border-radius: 8px;
      font-size: 1.08rem;
      background: #f0f7fa;
      box-shadow: 0 2px 8px rgba(0,0,0,0.07);
      transition: box-shadow 0.2s, background 0.2s;
      outline: none;
      font-weight: 500;
      color: #222;
    }
    .input-group input:focus {
      background: #e3f0ff;
      box-shadow: 0 4px 16px rgba(0,114,255,0.13);
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
      box-shadow: 0 4px 16px rgba(0,114,255,0.13), 0 1.5px 6px 0 rgba(0,0,0,0.10);
      transition: background 0.2s, transform 0.1s;
      letter-spacing: 0.5px;
    }
    .login-btn:hover, .login-btn:focus {
      background: linear-gradient(90deg, #0072ff 60%, #00c6ff 100%);
      transform: translateY(-2px) scale(1.03);
      box-shadow: 0 8px 24px rgba(0,114,255,0.18);
    }
    .error-message {
      color: #e74c3c;
      background: #fff0f0;
      border-radius: 6px;
      padding: 0.6rem 1.1rem;
      margin-top: 1.1rem;
      text-align: center;
      display: none;
      font-weight: 600;
      box-shadow: 0 2px 8px rgba(231,76,60,0.07);
      animation: shake 0.4s;
    }
    @keyframes shake {
      0%, 100% { transform: translateX(0);}
      20%, 60% { transform: translateX(-8px);}
      40%, 80% { transform: translateX(8px);}
    }
    .footer {
      margin-top: 2.2rem;
      text-align: center;
      color: #aaa;
      font-size: 1rem;
      text-shadow: 0 1px 0 #fff;
    }
    .forgot-link {
      color: #0072ff;
      text-decoration: underline;
      font-size: 1rem;
      font-weight: 500;
      margin-top: 1.2rem;
      display: inline-block;
      transition: color 0.2s;
    }
    .forgot-link:hover {
      color: #00c6ff;
    }
    /* Glassmorphism effect */
    .login-container:before {
      content: "";
      position: absolute;
      top: -30px; left: -30px; right: -30px; bottom: -30px;
      background: linear-gradient(120deg, #00c6ff33 0%, #0072ff22 100%);
      border-radius: 32px;
      z-index: -1;
      filter: blur(18px);
      opacity: 0.7;
    }
    /* Decorative floating shapes */
    .bubble {
      position: absolute;
      border-radius: 50%;
      opacity: 0.18;
      z-index: 0;
      filter: blur(2px);
      animation: float 8s infinite ease-in-out alternate;
    }
    .bubble1 { width: 120px; height: 120px; background: #00c6ff; left: 5vw; top: 10vh; animation-delay: 0s;}
    .bubble2 { width: 80px; height: 80px; background: #0072ff; right: 8vw; top: 20vh; animation-delay: 2s;}
    .bubble3 { width: 100px; height: 100px; background: #00c6ff; left: 15vw; bottom: 10vh; animation-delay: 4s;}
    .bubble4 { width: 60px; height: 60px; background: #0072ff; right: 12vw; bottom: 8vh; animation-delay: 6s;}
    @keyframes float {
      0% { transform: translateY(0) scale(1);}
      100% { transform: translateY(-30px) scale(1.08);}
    }
  </style>
</head>
<body>
  <div class="login-outer">
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
        <div class="error-message" id="errorMsg">Invalid username or password</div>
        <div style="margin-top:1.2rem; text-align:center;">
          <a href="/resetpass" class="forgot-link">Forgot password?</a>
        </div>
        <div class="footer">©️ 2024 IoT Dashboard</div>
      </form>
    </div>
    <div class="bubble bubble1"></div>
    <div class="bubble bubble2"></div>
    <div class="bubble bubble3"></div>
    <div class="bubble bubble4"></div>
  </div>
 <script>
  document.getElementById('loginForm').addEventListener('submit', function(e) {
    e.preventDefault();
    const formData = new FormData(this);
    const errorMsg = document.getElementById('errorMsg');

    fetch('/login', {
      method: 'POST',
      body: formData,
      credentials: 'include'
    }).then(resp => resp.json())
    .then(data => {
      if (data.success) {
        window.location.href = '/';
      } else {
        errorMsg.style.display = 'block';
        setTimeout(() => {
          errorMsg.style.display = 'none';
        }, 2000);
      }
    }).catch(error => {
      console.error('Login error:', error);
      errorMsg.style.display = 'block';
      setTimeout(() => {
        errorMsg.style.display = 'none';
      }, 2000);
    });
  });
</script>
</body>
</html>
)rawliteral";
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(200, "text/html", html);
  } else if (server.method() == HTTP_POST) {
    String username = server.arg("username");
    String password = server.arg("password");
    if (username == savedUsername && password == savedPassword) {
      sessionToken = generateSessionToken();
      String cookie = "ESPSESSIONID=" + sessionToken + "; Path=/; HttpOnly; SameSite=None; Secure";
      server.sendHeader("Set-Cookie", cookie);
      server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
      server.sendHeader("Access-Control-Allow-Credentials", "true");
      server.send(200, "application/json", "{\"success\":true}");
      addLog("User logged in");
    } else {
      server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
      server.sendHeader("Access-Control-Allow-Credentials", "true");
      server.send(401, "application/json", "{\"success\":false}");
      addLog("Failed login attempt");
    }
  }
}

void handleLogout() {
  sessionToken = "";
  server.sendHeader("Set-Cookie", "ESPSESSIONID=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly");
  server.sendHeader("Location", "/login");
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(302, "text/plain", "Logged out");
}

void handleRoot() {
  requireLogin();

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
  header { background:#00796B; color:#fff; padding:1rem; text-align:center; font-size:1.8rem; font-weight:bold; position:relative;}
  .logout-btn {
    position: absolute; right: 1.5rem; top: 1.1rem;
    background: #e74c3c; color: #fff; border: none; border-radius: 5px;
    padding: 0.4rem 1.1rem; font-size: 1rem; font-weight: 500; cursor: pointer;
    transition: background 0.2s;
  }
  .logout-btn:hover { background: #c0392b; }
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
  .dashboard-section { background:#fff; border-radius:8px; box-shadow:0 2px 5px rgba(0,0,0,0.1); margin:1rem; padding: 1rem;}
  .current-readings { display:flex; justify-content:center; gap:2rem; margin-bottom:1rem;}
  .reading { text-align:center;}
  .reading-value { font-size:1.5rem; font-weight:bold; color:#00796B;}
  .reading-label { font-size:0.9rem; color:#666;}
  .header-btn {
    background: #e74c3c;
    color: #fff;
    border: none;
    border-radius: 5px;
    padding: 0.4rem 1.1rem;
    font-size: 1rem;
    font-weight: 500;
    cursor: pointer;
    margin-left: 0.5rem;
    transition: background 0.2s;
  }
  .header-btn:first-child {
    background: #3498db;
  }
  .header-btn:hover {
    filter: brightness(0.9);
  }
</style>
</head>
<body>
<header>
  IoT Dashboard
  <div style="position:absolute; right:1.5rem; top:1.1rem;">
    <button class="header-btn" onclick="window.location.href='/changepass'">Change Password</button>
    <button class="header-btn" onclick="window.location.href='/logout'">Logout</button>
  </div>
</header>
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
    fetch(`/relay?num=${relayNum}&state=${state ? 1 : 0}`, { credentials: 'include' })
      .then(resp => resp.json())
      .then(data => {
        if (!data.success) alert('Failed to toggle relay');
      });
  }
  function fetchRelayStates() {
    fetch('/relaystates', { credentials: 'include' })
      .then(resp => resp.json())
      .then(states => {
        for (let i = 1; i <= 8; i++) {
          document.getElementById(`relay${i}`).checked = states[i - 1];
        }
      });
  }
  function fetchCurrentReadings() {
    fetch('/current', { credentials: 'include' })
      .then(resp => resp.json())
      .then(data => {
        document.getElementById('current-temp').textContent = data.temperature.toFixed(1) + ' °C';
        document.getElementById('current-hum').textContent = data.humidity.toFixed(1) + ' %';
      });
  }
  function fetchSensorData(filter='LIVE') {
    fetch(`/sensordata?filter=${filter}`, { credentials: 'include' })
      .then(resp => resp.json())
      .then(points => {
        chart.data.labels = points.map(p => new Date(p.timestamp * 1000));
        chart.data.datasets[0].data = points.map(p => p.temperature);
        chart.data.datasets[1].data = points.map(p => p.humidity);
        chart.update();
      });
  }
  function showLogs() {
    fetch('/logs', { credentials: 'include' })
      .then(resp => resp.text())
      .then(text => {
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
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "text/html", html);
}

void handleChangePassGet() {
  requireLogin();
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Change Credentials</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { background: #f5f7fa; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .changepass-container { background: #fff; border-radius: 10px; box-shadow: 0 10px 30px rgba(0,0,0,0.1); width: 100%; max-width: 400px; padding: 40px; text-align: center; }
    .changepass-title { color: #2c3e50; font-size: 22px; font-weight: 600; margin-bottom: 30px; }
    .input-group { margin-bottom: 20px; text-align: left; }
    .input-group label { display: block; margin-bottom: 8px; color: #2c3e50; font-size: 14px; font-weight: 500; }
    .input-group input { width: 100%; padding: 12px 15px; border: 1px solid #e0e0e0; border-radius: 6px; font-size: 14px; transition: border-color 0.3s; }
    .input-group input:focus { outline: none; border-color: #3498db; }
    .changepass-button { width: 100%; padding: 12px; background-color: #3498db; color: white; border: none; border-radius: 6px; font-size: 16px; font-weight: 500; cursor: pointer; transition: background-color 0.3s; }
    .changepass-button:hover { background-color: #2980b9; }
    .footer { margin-top: 20px; color: #7f8c8d; font-size: 12px; }
    .error-message { color: #e74c3c; font-size: 14px; margin-top: 15px; display: none; animation: shake 0.5s; }
    @keyframes shake { 0%, 100% { transform: translateX(0); } 10%, 30%, 50%, 70%, 90% { transform: translateX(-5px); } 20%, 40%, 60%, 80% { transform: translateX(5px); } }
  </style>
</head>
<body>
  <div class="changepass-container">
    <div class="changepass-title">Change Username and Password</div>
    <form id="changepassForm" method="POST" action="/changepass">
      <div class="input-group">
        <label for="username">New Username</label>
        <input type="text" id="username" name="username" required>
      </div>
      <div class="input-group">
        <label for="password">New Password</label>
        <input type="password" id="password" name="password" required>
      </div>
      <button type="submit" class="changepass-button">Save</button>
      <div id="errorMessage" class="error-message">
        Username and password cannot be empty
      </div>
    </form>
    <div class="footer">
      ©️ 2024 IoT Dashboard
    </div>
  </div>
  <script>
    document.getElementById('changepassForm').addEventListener('submit', function(e) {
      e.preventDefault();
      const formData = new FormData(this);
      const errorMessage = document.getElementById('errorMessage');
      fetch('/changepass', {
        method: 'POST',
        body: formData,
        credentials: 'include'
      })
      .then(response => {
        if (response.redirected) {
          window.location.href = response.url;
        } else {
          errorMessage.style.display = 'block';
          setTimeout(() => {
            errorMessage.style.display = 'none';
          }, 3000);
        }
      })
      .catch(error => {
        errorMessage.style.display = 'block';
        setTimeout(() => {
          errorMessage.style.display = 'none';
        }, 3000);
      });
    });
  </script>
</body>
</html>
)rawliteral";
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "text/html", html);
}

void handleChangePassPost() {
  requireLogin();
  if (!server.hasArg("username") || !server.hasArg("password") ||
      server.arg("username").length() == 0 || server.arg("password").length() == 0) {
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(400, "text/plain", "Username and password cannot be empty");
    return;
  }
  String newUser = server.arg("username");
  String newPass = server.arg("password");
  saveCredentials(newUser, newPass);
  server.sendHeader("Location", "/");
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(302, "text/plain", "Credentials updated. Redirecting...");
}

void handleRelayToggle() {
  requireLogin();
  if (!server.hasArg("num") || !server.hasArg("state")) {
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
    return;
  }
  int num = server.arg("num").toInt();
  int state = server.arg("state").toInt();
  if (num < 1 || num > 8) {
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid relay number\"}");
    return;
  }
  relayStates[num - 1] = (state == 1);
  digitalWrite(relayPins[num - 1], relayStates[num - 1] ? HIGH : LOW);
  saveRelayStates();
  addLog("Relay " + String(num) + (relayStates[num - 1] ? " ON" : " OFF"));
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleRelayStates() {
  requireLogin();
  StaticJsonDocument<64> jsonDoc;
  JsonArray arr = jsonDoc.to<JsonArray>();
  for (int i = 0; i < 8; i++) {
    arr.add(relayStates[i]);
  }
  String json;
  serializeJson(arr, json);
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "application/json", json);
}

void handleCurrentReadings() {
  requireLogin();
  StaticJsonDocument<64> jsonDoc;
  jsonDoc["temperature"] = currentTemp;
  jsonDoc["humidity"] = currentHum;
  String json;
  serializeJson(jsonDoc, json);
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "application/json", json);
}

void handleSensorData() {
  requireLogin();
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
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "application/json", json);
}

void handleLogs() {
  requireLogin();
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "text/plain", logBuffer);
}

void handleResetPassGet() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Reset Password</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { background: #f5f7fa; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .resetpass-container { background: #fff; border-radius: 10px; box-shadow: 0 10px 30px rgba(0,0,0,0.1); width: 100%; max-width: 400px; padding: 40px; text-align: center; }
    .resetpass-title { color: #2c3e50; font-size: 22px; font-weight: 600; margin-bottom: 30px; }
    .input-group { margin-bottom: 20px; text-align: left; }
    .input-group label { display: block; margin-bottom: 8px; color: #2c3e50; font-size: 14px; font-weight: 500; }
    .input-group input { width: 100%; padding: 12px 15px; border: 1px solid #e0e0e0; border-radius: 6px; font-size: 14px; transition: border-color 0.3s; }
    .input-group input:focus { outline: none; border-color: #3498db; }
    .resetpass-button { width: 100%; padding: 12px; background-color: #3498db; color: white; border: none; border-radius: 6px; font-size: 16px; font-weight: 500; cursor: pointer; transition: background-color 0.3s; }
    .resetpass-button:hover { background-color: #2980b9; }
    .footer { margin-top: 20px; color: #7f8c8d; font-size: 12px; }
    .error-message { color: #e74c3c; font-size: 14px; margin-top: 15px; display: none; animation: shake 0.5s; }
    @keyframes shake { 0%, 100% { transform: translateX(0); } 10%, 30%, 50%, 70%, 90% { transform: translateX(-5px); } 20%, 40%, 60%, 80% { transform: translateX(5px); } }
  </style>
</head>
<body>
  <div class="resetpass-container">
    <div class="resetpass-title">Reset Password</div>
    <form id="resetpassForm" method="POST" action="/resetpass">
      <div class="input-group">
        <label for="username">Username</label>
        <input type="text" id="username" name="username" required>
      </div>
      <div class="input-group">
        <label for="birthday">Date of Birth (YYYY-MM-DD)</label>
        <input type="text" id="birthday" name="birthday" required>
      </div>
      <div class="input-group">
        <label for="password">New Password</label>
        <input type="password" id="password" name="password" required>
      </div>
      <button type="submit" class="resetpass-button">Reset Password</button>
      <div id="errorMessage" class="error-message">
        Invalid details. Please try again.
      </div>
    </form>
    <div class="footer">
      ©️ 2024 IoT Dashboard
    </div>
  </div>
  <script>
    document.getElementById('resetpassForm').addEventListener('submit', function(e) {
      e.preventDefault();
      const formData = new FormData(this);
      const errorMessage = document.getElementById('errorMessage');
      fetch('/resetpass', {
        method: 'POST',
        body: formData
      })
      .then(response => {
        if (response.redirected) {
          window.location.href = response.url;
        } else {
          errorMessage.style.display = 'block';
          setTimeout(() => {
            errorMessage.style.display = 'none';
          }, 3000);
        }
      })
      .catch(error => {
        errorMessage.style.display = 'block';
        setTimeout(() => {
          errorMessage.style.display = 'none';
        }, 3000);
      });
    });
  </script>
</body>
</html>
)rawliteral";
  server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
  server.sendHeader("Access-Control-Allow-Credentials", "true");
  server.send(200, "text/html", html);
}

void handleResetPassPost() {
  if (!server.hasArg("username") || !server.hasArg("birthday") || !server.hasArg("password")) {
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(400, "text/plain", "Missing fields");
    return;
  }
  String username = server.arg("username");
  String birthday = server.arg("birthday");
  String newPass = server.arg("password");
  if (username == savedUsername && birthday == savedBirthday && newPass.length() > 0) {
    saveCredentials(savedUsername, newPass, savedBirthday);
    server.sendHeader("Location", "/login");
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(302, "text/plain", "Password reset. Redirecting...");
    addLog("Password reset via birthday verification");
  } else {
    server.sendHeader("Access-Control-Allow-Origin", "https://www.htresearchlab.com");
    server.sendHeader("Access-Control-Allow-Credentials", "true");
    server.send(401, "text/plain", "Invalid details");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    addLog("Failed to mount SPIFFS");
  } else {
    Serial.println("SPIFFS mounted. Files:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.println(file.name());
        file = root.openNextFile();
    }
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

  // Connect to WiFi using ONLY static IP from config.ini
  connectStaticWiFi();

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
  server.on("/changepass", HTTP_GET, handleChangePassGet);
  server.on("/changepass", HTTP_POST, handleChangePassPost);
  server.on("/relay", HTTP_GET, handleRelayToggle);
  server.on("/relaystates", HTTP_GET, handleRelayStates);
  server.on("/current", HTTP_GET, handleCurrentReadings);
  server.on("/sensordata", HTTP_GET, handleSensorData);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/resetpass", HTTP_GET, handleResetPassGet);
  server.on("/resetpass", HTTP_POST, handleResetPassPost);

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