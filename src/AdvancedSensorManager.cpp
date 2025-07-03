#include "HomeAutomation.h"
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ===== ADVANCED SENSOR MANAGER =====

class AdvancedSensorManager {
private:
    // Sensor instances
    Adafruit_BME280 bme280;
    Adafruit_BMP280 bmp280;
    Adafruit_MPU6050 mpu6050;
    OneWire oneWire;
    DallasTemperature dallasTemp;
    
    // Sensor data storage
    std::vector<SmartSensor*> sensors;
    
    // Air quality pins and settings
    int mq135Pin = 35;      // Air quality sensor
    int mq2Pin = 34;        // Smoke/gas sensor
    int pirPin = 27;        // Motion sensor
    int lightSensorPin = 36; // Light sensor (LDR)
    int soundSensorPin = 39; // Sound sensor
    int waterLevelPin = 32;  // Water level sensor
    int soilMoisturePin = 33; // Soil moisture sensor
    
    // Calibration values
    float tempOffset = 0.0;
    float humidityOffset = 0.0;
    float pressureOffset = 0.0;
    
    // Thresholds
    float airQualityThreshold = 150.0;
    float motionThreshold = 500.0;
    float soundThreshold = 300.0;
    
    // Timing
    unsigned long lastSensorRead = 0;
    unsigned long sensorInterval = 5000; // 5 seconds
    
public:
    AdvancedSensorManager() : oneWire(25), dallasTemp(&oneWire) {}
    
    void begin() {
        Serial.println("Initializing Advanced Sensor Manager...");
        
        // Initialize I2C sensors
        if (!bme280.begin(0x76)) {
            Serial.println("Could not find BME280 sensor!");
        } else {
            Serial.println("BME280 sensor initialized");
            addSensor(TEMPERATURE, "BME280_TEMP", "Indoor");
            addSensor(HUMIDITY, "BME280_HUM", "Indoor");
            addSensor(PRESSURE, "BME280_PRESS", "Indoor");
        }
        
        if (!bmp280.begin(0x77)) {
            Serial.println("Could not find BMP280 sensor!");
        } else {
            Serial.println("BMP280 sensor initialized");
            addSensor(TEMPERATURE, "BMP280_TEMP", "Outdoor");
            addSensor(PRESSURE, "BMP280_PRESS", "Outdoor");
        }
        
        if (!mpu6050.begin()) {
            Serial.println("Could not find MPU6050 sensor!");
        } else {
            Serial.println("MPU6050 sensor initialized");
            mpu6050.setAccelerometerRange(MPU6050_RANGE_8_G);
            mpu6050.setGyroRange(MPU6050_RANGE_500_DEG);
            mpu6050.setFilterBandwidth(MPU6050_BAND_21_HZ);
            addSensor(VIBRATION, "MPU6050_ACCEL", "Device");
        }
        
        // Initialize Dallas temperature sensors
        dallasTemp.begin();
        int deviceCount = dallasTemp.getDeviceCount();
        Serial.printf("Found %d Dallas temperature sensors\n", deviceCount);
        
        for (int i = 0; i < deviceCount; i++) {
            String sensorId = "DS18B20_" + String(i);
            addSensor(TEMPERATURE, sensorId, "Zone_" + String(i + 1));
        }
        
        // Initialize analog sensors
        pinMode(mq135Pin, INPUT);
        pinMode(mq2Pin, INPUT);
        pinMode(pirPin, INPUT);
        pinMode(lightSensorPin, INPUT);
        pinMode(soundSensorPin, INPUT);
        pinMode(waterLevelPin, INPUT);
        pinMode(soilMoisturePin, INPUT);
        
        // Add analog sensors
        addSensor(AIR_QUALITY, "MQ135", "Living Room");
        addSensor(SMOKE, "MQ2", "Kitchen");
        addSensor(MOTION, "PIR", "Entrance");
        addSensor(LIGHT, "LDR", "Outdoor");
        addSensor(SOUND, "SOUND", "Living Room");
        addSensor(WATER_LEVEL, "WATER", "Tank");
        addSensor(SOIL_MOISTURE, "SOIL", "Garden");
        
        Serial.println("Advanced Sensor Manager initialized successfully");
    }
    
    void addSensor(SensorType type, String id, String location) {
        SmartSensor* sensor = new SmartSensor(type, location);
        sensor->setId(id);
        sensors.push_back(sensor);
    }
    
    void readAllSensors() {
        if (millis() - lastSensorRead < sensorInterval) return;
        
        // Read BME280
        if (bme280.begin()) {
            updateSensorValue("BME280_TEMP", bme280.readTemperature() + tempOffset);
            updateSensorValue("BME280_HUM", bme280.readHumidity() + humidityOffset);
            updateSensorValue("BME280_PRESS", bme280.readPressure() / 100.0F + pressureOffset);
        }
        
        // Read BMP280
        if (bmp280.begin()) {
            updateSensorValue("BMP280_TEMP", bmp280.readTemperature() + tempOffset);
            updateSensorValue("BMP280_PRESS", bmp280.readPressure() / 100.0F + pressureOffset);
        }
        
        // Read MPU6050
        if (mpu6050.begin()) {
            sensors_event_t a, g, temp;
            mpu6050.getEvent(&a, &g, &temp);
            float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                                  a.acceleration.y * a.acceleration.y + 
                                  a.acceleration.z * a.acceleration.z);
            updateSensorValue("MPU6050_ACCEL", totalAccel);
        }
        
        // Read Dallas temperature sensors
        dallasTemp.requestTemperatures();
        int deviceCount = dallasTemp.getDeviceCount();
        for (int i = 0; i < deviceCount; i++) {
            float temp = dallasTemp.getTempCByIndex(i);
            if (temp != DEVICE_DISCONNECTED_C) {
                String sensorId = "DS18B20_" + String(i);
                updateSensorValue(sensorId, temp + tempOffset);
            }
        }
        
        // Read analog sensors
        updateSensorValue("MQ135", analogRead(mq135Pin));
        updateSensorValue("MQ2", analogRead(mq2Pin));
        updateSensorValue("PIR", digitalRead(pirPin));
        updateSensorValue("LDR", analogRead(lightSensorPin));
        updateSensorValue("SOUND", analogRead(soundSensorPin));
        updateSensorValue("WATER", analogRead(waterLevelPin));
        updateSensorValue("SOIL", analogRead(soilMoisturePin));
        
        lastSensorRead = millis();
        
        // Check for alerts
        checkSensorAlerts();
    }
    
    void updateSensorValue(String sensorId, float value) {
        for (auto* sensor : sensors) {
            if (sensor->getId() == sensorId) {
                sensor->updateValue(value);
                break;
            }
        }
    }
    
    SmartSensor* getSensor(String sensorId) {
        for (auto* sensor : sensors) {
            if (sensor->getId() == sensorId) {
                return sensor;
            }
        }
        return nullptr;
    }
    
    String getAllSensorsJson() {
        DynamicJsonDocument doc(4096);
        JsonArray sensorArray = doc.createNestedArray("sensors");
        
        for (auto* sensor : sensors) {
            JsonObject sensorObj = sensorArray.createNestedObject();
            sensorObj["id"] = sensor->getId();
            sensorObj["type"] = getSensorTypeName(sensor->getType());
            sensorObj["location"] = sensor->getLocation();
            sensorObj["value"] = sensor->getValue();
            sensorObj["unit"] = sensor->getUnit();
            sensorObj["status"] = sensor->isValid() ? "OK" : "ERROR";
            sensorObj["timestamp"] = sensor->getTimestamp();
            sensorObj["average"] = sensor->getAverage();
            sensorObj["threshold_exceeded"] = sensor->isThresholdExceeded();
        }
        
        doc["count"] = sensors.size();
        doc["last_update"] = millis();
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    void checkSensorAlerts() {
        for (auto* sensor : sensors) {
            if (sensor->isThresholdExceeded()) {
                String alert = "ALERT: " + sensor->getId() + " exceeded threshold. Value: " + 
                              String(sensor->getValue()) + " " + sensor->getUnit();
                Serial.println(alert);
                notifyUser(alert, "alert");
                
                // Trigger automation based on sensor alerts
                triggerAutomationBySensor(sensor->getId(), sensor->getValue());
            }
        }
    }
    
    void triggerAutomationBySensor(String sensorId, float value) {
        // Air quality alert - turn on air purifier
        if (sensorId == "MQ135" && value > airQualityThreshold) {
            deviceController.controlDevice("air_purifier", true);
        }
        
        // Motion detected - turn on lights
        if (sensorId == "PIR" && value > 0) {
            deviceController.controlDevice("entrance_light", true);
        }
        
        // Low light - turn on outdoor lights
        if (sensorId == "LDR" && value < 200) {
            deviceController.controlDevice("outdoor_lights", true);
        }
        
        // High temperature - turn on cooling
        if (sensorId.indexOf("TEMP") >= 0 && value > 30.0) {
            deviceController.controlDevice("cooling_fan", true);
        }
        
        // Low temperature - turn on heating
        if (sensorId.indexOf("TEMP") >= 0 && value < 18.0) {
            deviceController.controlDevice("heater", true);
        }
        
        // High humidity - turn on dehumidifier
        if (sensorId == "BME280_HUM" && value > 70.0) {
            deviceController.controlDevice("dehumidifier", true);
        }
        
        // Low soil moisture - turn on irrigation
        if (sensorId == "SOIL" && value < 300) {
            deviceController.controlDevice("irrigation_pump", true);
        }
        
        // Water level low - send alert
        if (sensorId == "WATER" && value < 200) {
            notifyUser("Water tank level is low!", "critical");
        }
        
        // Smoke detected - trigger emergency protocol
        if (sensorId == "MQ2" && value > 400) {
            emergencyProtocol("SMOKE_DETECTED");
        }
    }
    
    void emergencyProtocol(String emergencyType) {
        Serial.println("EMERGENCY PROTOCOL ACTIVATED: " + emergencyType);
        
        if (emergencyType == "SMOKE_DETECTED") {
            // Turn off all electrical devices except emergency ones
            deviceController.emergencyShutdown();
            // Sound alarm
            deviceController.controlDevice("alarm_buzzer", true);
            // Send emergency notifications
            notifyUser("SMOKE DETECTED! Emergency protocol activated!", "emergency");
            sendPushNotification("EMERGENCY", "Smoke detected in your home!");
        }
    }
    
    void calibrateSensor(String sensorId, float offset) {
        SmartSensor* sensor = getSensor(sensorId);
        if (sensor) {
            sensor->calibrate(offset);
        }
    }
    
    void setThreshold(String sensorId, float minThreshold, float maxThreshold) {
        SmartSensor* sensor = getSensor(sensorId);
        if (sensor) {
            sensor->setThresholds(minThreshold, maxThreshold);
        }
    }
    
    String getSensorTypeName(SensorType type) {
        switch (type) {
            case TEMPERATURE: return "Temperature";
            case HUMIDITY: return "Humidity";
            case PRESSURE: return "Pressure";
            case AIR_QUALITY: return "Air Quality";
            case MOTION: return "Motion";
            case LIGHT: return "Light";
            case SOUND: return "Sound";
            case VIBRATION: return "Vibration";
            case SMOKE: return "Smoke";
            case WATER_LEVEL: return "Water Level";
            case SOIL_MOISTURE: return "Soil Moisture";
            default: return "Unknown";
        }
    }
    
    // Predictive analytics
    String getSensorPredictions() {
        DynamicJsonDocument doc(2048);
        JsonArray predictions = doc.createNestedArray("predictions");
        
        for (auto* sensor : sensors) {
            if (sensor->getHistorySize() >= 10) {
                JsonObject pred = predictions.createNestedObject();
                pred["sensor_id"] = sensor->getId();
                pred["current_value"] = sensor->getValue();
                pred["predicted_value"] = sensor->predictNextValue();
                pred["trend"] = sensor->getTrend(); // "increasing", "decreasing", "stable"
                pred["confidence"] = sensor->getPredictionConfidence();
            }
        }
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    // Energy efficiency recommendations
    String getEnergyRecommendations() {
        DynamicJsonDocument doc(1024);
        JsonArray recommendations = doc.createNestedArray("recommendations");
        
        SmartSensor* tempSensor = getSensor("BME280_TEMP");
        SmartSensor* lightSensor = getSensor("LDR");
        
        if (tempSensor && tempSensor->getValue() > 25.0) {
            JsonObject rec = recommendations.createNestedObject();
            rec["type"] = "cooling";
            rec["message"] = "Consider increasing AC temperature by 1°C to save energy";
            rec["potential_savings"] = "10-15%";
        }
        
        if (lightSensor && lightSensor->getValue() > 800) {
            JsonObject rec = recommendations.createNestedObject();
            rec["type"] = "lighting";
            rec["message"] = "Natural light is sufficient, consider turning off indoor lights";
            rec["potential_savings"] = "5-8%";
        }
        
        String result;
        serializeJson(doc, result);
        return result;
    }
};

// Global instance
AdvancedSensorManager sensorManager;
