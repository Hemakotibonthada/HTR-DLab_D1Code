#include "HomeAutomation.h"
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>

// ===== ADVANCED DEVICE CONTROLLER =====

class AdvancedDeviceController {
private:
    std::vector<SmartDevice> devices;
    
    // Hardware instances
    std::vector<Servo> servos;
    Adafruit_NeoPixel* neoPixels = nullptr;
    
    // Device pins and configurations
    struct DeviceConfig {
        int relayPins[8] = {2, 15, 16, 17, 18, 19, 21, 22};
        int servoPins[4] = {25, 26, 27, 14};
        int pwmPins[8] = {32, 33, 34, 35, 36, 39, 23, 5};
        int neoPixelPin = 13;
        int neoPixelCount = 60;
        int buzzerPin = 12;
        int motorPins[4] = {4, 0, 1, 3}; // Motor driver pins
    } config;
    
    // PWM configuration
    const int pwmFreq = 5000;
    const int pwmResolution = 8;
    
    // Device status tracking
    unsigned long lastStatusUpdate = 0;
    const unsigned long statusUpdateInterval = 1000;
    
    // Energy consumption tracking
    std::map<String, float> deviceEnergyConsumption;
    
public:
    AdvancedDeviceController() {
        // Initialize NeoPixel strip
        neoPixels = new Adafruit_NeoPixel(config.neoPixelCount, config.neoPixelPin, NEO_GRB + NEO_KHZ800);
    }
    
    void begin() {
        Serial.println("Initializing Advanced Device Controller...");
        
        // Initialize relay outputs
        for (int i = 0; i < 8; i++) {
            pinMode(config.relayPins[i], OUTPUT);
            digitalWrite(config.relayPins[i], LOW);
            addDevice(RELAY, config.relayPins[i], "Relay_" + String(i + 1), "Room_" + String(i + 1));
        }
        
        // Initialize servo motors
        servos.resize(4);
        for (int i = 0; i < 4; i++) {
            servos[i].attach(config.servoPins[i]);
            servos[i].write(90); // Center position
            addDevice(SERVO, config.servoPins[i], "Servo_" + String(i + 1), "Zone_" + String(i + 1));
        }
        
        // Initialize PWM channels for LED dimming
        for (int i = 0; i < 8; i++) {
            ledcSetup(i, pwmFreq, pwmResolution);
            ledcAttachPin(config.pwmPins[i], i);
            ledcWrite(i, 0);
            addDevice(PWM_LED, config.pwmPins[i], "PWM_LED_" + String(i + 1), "Area_" + String(i + 1));
        }
        
        // Initialize NeoPixel strip
        neoPixels->begin();
        neoPixels->show();
        neoPixels->setBrightness(50);
        addDevice(RGB_LED, config.neoPixelPin, "RGB_Strip", "Living Room");
        
        // Initialize buzzer
        pinMode(config.buzzerPin, OUTPUT);
        addDevice(BUZZER, config.buzzerPin, "Alert_Buzzer", "Main");
        
        // Initialize motor controls
        for (int i = 0; i < 4; i++) {
            pinMode(config.motorPins[i], OUTPUT);
            digitalWrite(config.motorPins[i], LOW);
            addDevice(MOTOR, config.motorPins[i], "Motor_" + String(i + 1), "Utility_" + String(i + 1));
        }
        
        // Add virtual devices (controlled via other means)
        addVirtualDevice("air_purifier", "Air Purifier", "Living Room");
        addVirtualDevice("cooling_fan", "Cooling Fan", "Bedroom");
        addVirtualDevice("heater", "Heater", "Living Room");
        addVirtualDevice("dehumidifier", "Dehumidifier", "Bathroom");
        addVirtualDevice("irrigation_pump", "Irrigation Pump", "Garden");
        addVirtualDevice("garage_door", "Garage Door", "Garage");
        addVirtualDevice("security_camera", "Security Camera", "Entrance");
        addVirtualDevice("doorbell", "Smart Doorbell", "Entrance");
        
        Serial.println("Advanced Device Controller initialized successfully");
    }
    
    void addDevice(DeviceType type, int pin, String name, String room) {
        SmartDevice device;
        device.type = type;
        device.pin = pin;
        device.name = name;
        device.room = room;
        device.state = false;
        device.value = 0;
        device.autoMode = false;
        device.lastAction = millis();
        device.description = getDeviceTypeDescription(type);
        
        devices.push_back(device);
        deviceEnergyConsumption[name] = 0.0;
    }
    
    void addVirtualDevice(String id, String name, String room) {
        SmartDevice device;
        device.type = RELAY; // Default type for virtual devices
        device.pin = -1; // Virtual devices don't have physical pins
        device.name = name;
        device.room = room;
        device.state = false;
        device.value = 0;
        device.autoMode = false;
        device.lastAction = millis();
        device.description = "Virtual Device";
        device.id = id;
        
        devices.push_back(device);
        deviceEnergyConsumption[id] = 0.0;
    }
    
    bool controlDevice(String deviceId, bool state, int value = 0) {
        SmartDevice* device = getDevice(deviceId);
        if (!device) {
            Serial.println("Device not found: " + deviceId);
            return false;
        }
        
        bool success = false;
        
        switch (device->type) {
            case RELAY:
                if (device->pin >= 0) {
                    digitalWrite(device->pin, state ? HIGH : LOW);
                    success = true;
                } else {
                    // Handle virtual devices
                    success = controlVirtualDevice(deviceId, state, value);
                }
                break;
                
            case SERVO:
                if (value >= 0 && value <= 180) {
                    int servoIndex = getServoIndex(device->pin);
                    if (servoIndex >= 0) {
                        servos[servoIndex].write(value);
                        device->value = value;
                        success = true;
                    }
                }
                break;
                
            case PWM_LED:
                if (value >= 0 && value <= 255) {
                    int pwmChannel = getPWMChannel(device->pin);
                    if (pwmChannel >= 0) {
                        ledcWrite(pwmChannel, state ? value : 0);
                        device->value = value;
                        success = true;
                    }
                }
                break;
                
            case RGB_LED:
                success = controlRGBLED(state, value);
                break;
                
            case BUZZER:
                if (state) {
                    tone(device->pin, value > 0 ? value : 1000, 500);
                } else {
                    noTone(device->pin);
                }
                success = true;
                break;
                
            case MOTOR:
                digitalWrite(device->pin, state ? HIGH : LOW);
                success = true;
                break;
        }
        
        if (success) {
            device->state = state;
            device->lastAction = millis();
            
            // Log the action
            String action = device->name + (state ? " turned ON" : " turned OFF");
            if (value > 0) action += " (value: " + String(value) + ")";
            addLog(action);
            
            // Update energy consumption
            updateEnergyConsumption(deviceId, state);
            
            // Publish to MQTT if connected
            publishDeviceStatus(deviceId);
        }
        
        return success;
    }
    
    bool controlVirtualDevice(String deviceId, bool state, int value) {
        // Handle virtual devices that might be controlled via other protocols
        // This could integrate with smart plugs, Zigbee devices, etc.
        
        if (deviceId == "air_purifier") {
            // Send command to smart air purifier via WiFi/MQTT
            publishMQTTData("home/air_purifier/command", state ? "ON" : "OFF");
            return true;
        }
        
        if (deviceId == "garage_door") {
            // Control garage door opener
            if (state) {
                // Trigger garage door (usually a momentary signal)
                digitalWrite(2, HIGH); // Assuming relay 1 controls garage
                delay(500);
                digitalWrite(2, LOW);
            }
            return true;
        }
        
        if (deviceId == "security_camera") {
            // Enable/disable security camera recording
            String command = state ? "record_on" : "record_off";
            httpPost("http://camera-ip/api/command", "{\"action\":\"" + command + "\"}");
            return true;
        }
        
        return false;
    }
    
    bool controlRGBLED(bool state, int value) {
        if (!state) {
            // Turn off all LEDs
            neoPixels->clear();
            neoPixels->show();
            return true;
        }
        
        // Various RGB effects based on value
        switch (value) {
            case 1: // Solid white
                fillColor(neoPixels->Color(255, 255, 255));
                break;
            case 2: // Rainbow
                rainbowEffect();
                break;
            case 3: // Breathing effect
                breathingEffect(neoPixels->Color(0, 100, 255));
                break;
            case 4: // Party mode
                partyMode();
                break;
            case 5: // Alarm red
                fillColor(neoPixels->Color(255, 0, 0));
                break;
            default: // Default warm white
                fillColor(neoPixels->Color(255, 200, 100));
                break;
        }
        
        return true;
    }
    
    void fillColor(uint32_t color) {
        for (int i = 0; i < neoPixels->numPixels(); i++) {
            neoPixels->setPixelColor(i, color);
        }
        neoPixels->show();
    }
    
    void rainbowEffect() {
        static int rainbowIndex = 0;
        for (int i = 0; i < neoPixels->numPixels(); i++) {
            int pixelHue = rainbowIndex + (i * 65536L / neoPixels->numPixels());
            neoPixels->setPixelColor(i, neoPixels->gamma32(neoPixels->ColorHSV(pixelHue)));
        }
        neoPixels->show();
        rainbowIndex += 256;
        if (rainbowIndex >= 65536) rainbowIndex = 0;
    }
    
    void breathingEffect(uint32_t color) {
        static int brightness = 0;
        static int direction = 1;
        
        brightness += direction * 5;
        if (brightness >= 255) {
            brightness = 255;
            direction = -1;
        } else if (brightness <= 0) {
            brightness = 0;
            direction = 1;
        }
        
        neoPixels->setBrightness(brightness);
        fillColor(color);
    }
    
    void partyMode() {
        for (int i = 0; i < neoPixels->numPixels(); i++) {
            uint32_t color = neoPixels->Color(random(255), random(255), random(255));
            neoPixels->setPixelColor(i, color);
        }
        neoPixels->show();
    }
    
    SmartDevice* getDevice(String deviceId) {
        for (auto& device : devices) {
            if (device.name == deviceId || device.id == deviceId) {
                return &device;
            }
        }
        return nullptr;
    }
    
    String getAllDevicesJson() {
        DynamicJsonDocument doc(4096);
        JsonArray deviceArray = doc.createNestedArray("devices");
        
        for (const auto& device : devices) {
            JsonObject deviceObj = deviceArray.createNestedObject();
            deviceObj["id"] = device.id.length() > 0 ? device.id : device.name;
            deviceObj["name"] = device.name;
            deviceObj["type"] = getDeviceTypeName(device.type);
            deviceObj["room"] = device.room;
            deviceObj["state"] = device.state;
            deviceObj["value"] = device.value;
            deviceObj["pin"] = device.pin;
            deviceObj["auto_mode"] = device.autoMode;
            deviceObj["last_action"] = device.lastAction;
            deviceObj["description"] = device.description;
            deviceObj["energy_consumption"] = deviceEnergyConsumption[device.name];
            deviceObj["responsive"] = isDeviceResponding(device.name);
        }
        
        doc["count"] = devices.size();
        doc["last_update"] = millis();
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    void updateDeviceStatus() {
        if (millis() - lastStatusUpdate < statusUpdateInterval) return;
        
        // Update RGB LED effects if active
        for (auto& device : devices) {
            if (device.type == RGB_LED && device.state) {
                controlRGBLED(true, device.value);
            }
        }
        
        lastStatusUpdate = millis();
    }
    
    bool isDeviceResponding(String deviceId) {
        SmartDevice* device = getDevice(deviceId);
        if (!device) return false;
        
        // For physical devices, we can check if they respond to commands
        // For virtual devices, we might need to ping them
        if (device->pin < 0) {
            // Virtual device - check network connectivity or last response
            return checkVirtualDeviceHealth(deviceId);
        }
        
        return true; // Physical devices are assumed responsive if pin is valid
    }
    
    bool checkVirtualDeviceHealth(String deviceId) {
        // Implement health checks for virtual devices
        if (deviceId == "air_purifier") {
            // Ping smart air purifier
            return httpGet("http://air-purifier-ip/status").length() > 0;
        }
        
        if (deviceId == "security_camera") {
            // Check camera status
            return httpGet("http://camera-ip/api/status").length() > 0;
        }
        
        return true; // Default to healthy
    }
    
    void emergencyShutdown() {
        Serial.println("EMERGENCY SHUTDOWN ACTIVATED");
        
        // Turn off all non-essential devices
        for (auto& device : devices) {
            if (device.name.indexOf("alarm") < 0 && 
                device.name.indexOf("emergency") < 0 &&
                device.name.indexOf("security") < 0) {
                controlDevice(device.name, false);
            }
        }
        
        // Keep emergency lighting on
        controlDevice("RGB_Strip", true, 5); // Red alert lighting
    }
    
    void scheduleAction(String deviceId, unsigned long delayMs, bool state, int value = 0) {
        // Schedule a device action for later execution
        // This could be implemented with a queue or timer system
        // For now, we'll use a simple delay (not recommended for production)
        
        // TODO: Implement proper task scheduling with FreeRTOS or timer interrupts
        Serial.println("Scheduled action for " + deviceId + " in " + String(delayMs) + "ms");
    }
    
    // Scene management
    void activateScene(String sceneName) {
        Serial.println("Activating scene: " + sceneName);
        
        if (sceneName == "good_night") {
            controlDevice("Relay_1", false); // Living room lights off
            controlDevice("Relay_2", true);  // Bedroom night light on
            controlDevice("security_camera", true); // Enable security
            controlDevice("RGB_Strip", true, 3); // Breathing blue
        }
        else if (sceneName == "good_morning") {
            controlDevice("Relay_1", true);  // Living room lights on
            controlDevice("Relay_3", true);  // Kitchen lights on
            controlDevice("cooling_fan", false); // Turn off fan
            controlDevice("RGB_Strip", true, 1); // Bright white
        }
        else if (sceneName == "movie_mode") {
            controlDevice("Relay_1", false); // Dim main lights
            controlDevice("PWM_LED_1", true, 50); // Ambient lighting
            controlDevice("RGB_Strip", true, 3); // Breathing effect
        }
        else if (sceneName == "party_mode") {
            controlDevice("RGB_Strip", true, 4); // Party colors
            controlDevice("Relay_1", true);  // All lights on
            controlDevice("Relay_3", true);
        }
        else if (sceneName == "energy_save") {
            // Turn off non-essential devices
            for (auto& device : devices) {
                if (device.name.indexOf("LED") >= 0 || 
                    device.name.indexOf("Fan") >= 0) {
                    controlDevice(device.name, false);
                }
            }
        }
        
        addLog("Scene activated: " + sceneName);
    }
    
private:
    int getServoIndex(int pin) {
        for (int i = 0; i < 4; i++) {
            if (config.servoPins[i] == pin) return i;
        }
        return -1;
    }
    
    int getPWMChannel(int pin) {
        for (int i = 0; i < 8; i++) {
            if (config.pwmPins[i] == pin) return i;
        }
        return -1;
    }
    
    String getDeviceTypeName(DeviceType type) {
        switch (type) {
            case RELAY: return "Relay";
            case SERVO: return "Servo";
            case PWM_LED: return "PWM LED";
            case RGB_LED: return "RGB LED";
            case BUZZER: return "Buzzer";
            case MOTOR: return "Motor";
            case HEATER: return "Heater";
            case COOLER: return "Cooler";
            case FAN: return "Fan";
            default: return "Unknown";
        }
    }
    
    String getDeviceTypeDescription(DeviceType type) {
        switch (type) {
            case RELAY: return "On/Off Switch Control";
            case SERVO: return "Position Control (0-180°)";
            case PWM_LED: return "Brightness Control (0-255)";
            case RGB_LED: return "Color and Effect Control";
            case BUZZER: return "Audio Alert Device";
            case MOTOR: return "Motor Control";
            default: return "Smart Device";
        }
    }
    
    void updateEnergyConsumption(String deviceId, bool state) {
        // Estimate energy consumption based on device type and state
        SmartDevice* device = getDevice(deviceId);
        if (!device) return;
        
        float powerConsumption = 0.0; // Watts
        
        switch (device->type) {
            case RELAY: powerConsumption = state ? 100.0 : 0.0; break; // 100W bulb
            case PWM_LED: powerConsumption = state ? (device->value / 255.0) * 10.0 : 0.0; break;
            case RGB_LED: powerConsumption = state ? 30.0 : 0.0; break; // LED strip
            case SERVO: powerConsumption = 5.0; break; // Servo always consuming when active
            case MOTOR: powerConsumption = state ? 200.0 : 0.0; break; // Motor
            case FAN: powerConsumption = state ? 75.0 : 0.0; break;
            case HEATER: powerConsumption = state ? 1500.0 : 0.0; break;
            default: powerConsumption = state ? 50.0 : 0.0; break;
        }
        
        // Update cumulative energy consumption (simplified calculation)
        float timeHours = (millis() - device->lastAction) / 3600000.0;
        deviceEnergyConsumption[deviceId] += powerConsumption * timeHours / 1000.0; // kWh
    }
    
    void publishDeviceStatus(String deviceId) {
        if (!connectToMQTT()) return;
        
        SmartDevice* device = getDevice(deviceId);
        if (!device) return;
        
        DynamicJsonDocument doc(512);
        doc["device_id"] = deviceId;
        doc["state"] = device->state;
        doc["value"] = device->value;
        doc["timestamp"] = millis();
        
        String payload;
        serializeJson(doc, payload);
        
        String topic = "home/devices/" + deviceId + "/status";
        publishMQTTData(topic, payload);
    }
};

// Global instance
AdvancedDeviceController advancedDeviceController;
