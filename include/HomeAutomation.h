#ifndef HOME_AUTOMATION_H
#define HOME_AUTOMATION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>

// Global function declarations
void addLog(const String& entry);

// ===== ADVANCED FEATURE DEFINITIONS =====

// Sensor Types
enum SensorType {
  TEMPERATURE,
  HUMIDITY,
  PRESSURE,
  AIR_QUALITY,
  MOTION,
  LIGHT,
  SOUND,
  VIBRATION,
  DOOR_WINDOW,
  SMOKE,
  GAS,
  WATER_LEVEL,
  SOIL_MOISTURE,
  UV_INDEX,
  ENERGY_CONSUMPTION
};

// Device Types
enum DeviceType {
  RELAY,
  SERVO,
  PWM_LED,
  RGB_LED,
  BUZZER,
  OLED_DISPLAY,
  MOTOR,
  HEATER,
  COOLER,
  FAN,
  PUMP,
  VALVE
};

// Automation Trigger Types
enum TriggerType {
  TIME_BASED,
  SENSOR_BASED,
  GEOFENCE_BASED,
  WEATHER_BASED,
  EVENT_BASED,
  VOICE_COMMAND,
  MANUAL,
  PRESENCE_BASED
};

// Security Levels
enum SecurityLevel {
  NONE,
  BASIC,
  MODERATE,
  HIGH_SECURITY,
  MAXIMUM
};

// ===== STRUCTURES =====

struct SensorData {
  SensorType type;
  float value;
  String unit;
  unsigned long timestamp;
  bool isValid;
  float minThreshold;
  float maxThreshold;
  String location;
};

struct SmartDevice {
  String id;
  DeviceType type;
  int pin;
  String name;
  String room;
  bool state;
  int value; // For PWM, servo position, etc.
  bool autoMode;
  unsigned long lastAction;
  String description;
};

struct AutomationRule {
  String id;
  String name;
  TriggerType trigger;
  String condition; // JSON string with conditions
  String action;    // JSON string with actions
  bool enabled;
  unsigned long lastTriggered;
  int priority;
  String schedule; // Cron-like expression
};

struct WeatherData {
  float temperature;
  float humidity;
  int pressure;
  String description;
  String icon;
  int uvIndex;
  float windSpeed;
  String windDirection;
  int visibility;
  unsigned long sunrise;
  unsigned long sunset;
  unsigned long lastUpdate;
};

struct EnergyData {
  float voltage;
  float current;
  float power;
  float energy; // kWh
  float cost;
  unsigned long timestamp;
};

struct UserProfile {
  String username;
  String email;
  SecurityLevel accessLevel;
  std::vector<String> allowedRooms;
  std::vector<String> allowedDevices;
  bool voiceEnabled;
  String preferences; // JSON string
};

struct GeofenceZone {
  String name;
  float latitude;
  float longitude;
  float radius; // in meters
  bool isHome;
  std::vector<String> enterActions;
  std::vector<String> exitActions;
};

struct VoiceCommand {
  String phrase;
  String action;
  std::vector<String> parameters;
  bool requiresConfirmation;
  SecurityLevel requiredLevel;
};

// ===== ADVANCED CLASSES =====

class SmartSensor {
private:
  SensorData data;
  std::vector<float> history;
  int maxHistorySize;
  String id;
  SensorType sensorType;
  String location;
  String unit;
  bool valid;
  unsigned long timestamp;
  float minThreshold;
  float maxThreshold;
  
public:
  SmartSensor(SensorType type, String location);
  void updateValue(float value);
  float getValue();
  float getAverage(int samples = 10);
  bool isThresholdExceeded();
  String getStatusJson();
  void calibrate(float offset);
  float predictNextValue(); // Simple linear prediction
  
  // Additional methods needed by the advanced classes
  void setId(String deviceId) { id = deviceId; }
  String getId() { return id; }
  SensorType getType() { return sensorType; }
  String getLocation() { return location; }
  String getUnit() { return unit; }
  bool isValid() { return valid; }
  unsigned long getTimestamp() { return timestamp; }
  void setThresholds(float minThresh, float maxThresh) { 
    minThreshold = minThresh; 
    maxThreshold = maxThresh; 
  }
  int getHistorySize() { return history.size(); }
  String getTrend() { return "stable"; } // Simplified
  float getPredictionConfidence() { return 0.8; } // Simplified
};

class SmartDeviceController {
private:
  std::vector<SmartDevice> devices;
  
public:
  void addDevice(DeviceType type, int pin, String name, String room);
  bool controlDevice(String deviceId, bool state, int value = 0);
  SmartDevice* getDevice(String deviceId);
  String getAllDevicesJson();
  void updateDeviceStatus();
  bool isDeviceResponding(String deviceId);
  void emergencyShutdown(); // Add missing method
};

class AutomationEngine {
private:
  std::vector<AutomationRule> rules;
  std::vector<SensorData> sensorReadings;
  
public:
  void addRule(AutomationRule rule);
  void removeRule(String ruleId);
  void evaluateRules();
  bool checkCondition(String condition, std::map<String, float> sensorValues);
  void executeAction(String action);
  String getRulesJson();
  void loadRulesFromEEPROM();
  void saveRulesToEEPROM();
};

class SecurityManager {
private:
  std::vector<UserProfile> users;
  std::map<String, int> loginAttempts;
  std::vector<String> blockedIPs;
  SecurityLevel currentLevel;
  
public:
  bool authenticateUser(String username, String password);
  bool hasPermission(String username, String resource);
  void blockIP(String ip, unsigned long duration);
  bool isIPBlocked(String ip);
  void logSecurityEvent(String event, String details);
  SecurityLevel getSecurityLevel();
  void setSecurityLevel(SecurityLevel level);
  String generateSessionToken();
  bool validateSessionToken(String token);
};

class EnergyMonitor {
private:
  std::vector<EnergyData> history;
  float dailyUsage;
  float monthlyUsage;
  float costPerKWh;
  
public:
  void updateReading(float voltage, float current);
  float getCurrentPower();
  float getDailyUsage();
  float getEstimatedMonthlyCost();
  String getUsageReport();
  void resetDailyUsage();
  bool isUsageAbnormal();
};

class WeatherStation {
private:
  WeatherData currentWeather;
  std::vector<WeatherData> forecast;
  String apiKey;
  String city;
  
public:
  void setApiKey(String key);
  void setCity(String cityName);
  bool updateWeather();
  WeatherData getCurrentWeather();
  std::vector<WeatherData> getForecast(int days = 5);
  String getWeatherJson();
  bool isRainExpected();
  bool isTemperatureExtreme();
};

class VoiceAssistant {
private:
  std::vector<VoiceCommand> commands;
  bool enabled;
  String wakeWord;
  
public:
  void addCommand(VoiceCommand command);
  bool processCommand(String input);
  void setWakeWord(String word);
  bool isListening();
  void enableVoice(bool enable);
  String getResponseForCommand(String command);
  void speakResponse(String text);
};

class GeofencingManager {
private:
  std::vector<GeofenceZone> zones;
  float currentLat, currentLon;
  
public:
  void addZone(GeofenceZone zone);
  void updateLocation(float lat, float lon);
  std::vector<String> checkZoneEntry();
  std::vector<String> checkZoneExit();
  bool isInHomeZone();
  float distanceToHome();
};

class MachineLearning {
private:
  std::map<String, std::vector<float>> trainingData;
  std::map<String, std::vector<float>> patterns;
  
public:
  void addTrainingData(String feature, float value);
  float predict(String feature, std::vector<float> inputs);
  void trainModel(String feature);
  bool detectAnomaly(String feature, float value);
  String getInsights();
  void optimizeEnergyUsage();
};

// ===== GLOBAL INSTANCES =====
extern SmartDeviceController deviceController;
extern AutomationEngine automationEngine;
extern SecurityManager securityManager;
extern EnergyMonitor energyMonitor;
extern WeatherStation weatherStation;
extern VoiceAssistant voiceAssistant;
extern GeofencingManager geofencing;
extern MachineLearning mlEngine;

// ===== UTILITY FUNCTIONS =====
String formatTimestamp(unsigned long timestamp);
float calculateDistance(float lat1, float lon1, float lat2, float lon2);
String encryptData(String data, String key);
String decryptData(String data, String key);
bool isValidEmail(String email);
String generateUUID();
void notifyUser(String message, String channel = "web");
void sendPushNotification(String title, String message);
bool connectToMQTT();
void publishMQTTData(String topic, String payload);
String httpPost(String url, String payload);
String httpGet(String url);

// ===== CONSTANTS =====
#define MAX_SENSORS 32
#define MAX_DEVICES 32
#define MAX_AUTOMATION_RULES 50
#define MAX_USERS 10
#define MAX_HISTORY_POINTS 1000
#define ENERGY_SAMPLE_INTERVAL 1000
#define WEATHER_UPDATE_INTERVAL 600000
#define GEOFENCE_CHECK_INTERVAL 30000
#define ML_TRAINING_THRESHOLD 100

#endif // HOME_AUTOMATION_H
