#include "HomeAutomation.h"
#include <time.h>

// ===== AI-POWERED AUTOMATION ENGINE =====

class AIAutomationEngine {
private:
    std::vector<AutomationRule> rules;
    std::vector<SensorData> sensorHistory;
    std::map<String, float> userPreferences;
    std::map<String, std::vector<float>> behaviorPatterns;
    std::map<String, unsigned long> lastExecution;
    
    // Learning parameters
    float learningRate = 0.01;
    int minSamplesForLearning = 50;
    float confidenceThreshold = 0.75;
    
    // Prediction models (simplified)
    struct PredictionModel {
        std::vector<float> weights;
        float bias;
        float accuracy;
        String feature;
    };
    
    std::map<String, PredictionModel> models;
    
    // Pattern recognition
    struct Pattern {
        String name;
        std::vector<float> signature;
        float confidence;
        String action;
        unsigned long lastDetected;
    };
    
    std::vector<Pattern> recognizedPatterns;
    
public:
    AIAutomationEngine() {
        initializeDefaultRules();
        initializePredictionModels();
    }
    
    void begin() {
        Serial.println("Initializing AI Automation Engine...");
        loadRulesFromEEPROM();
        loadUserPreferences();
        Serial.println("AI Automation Engine initialized successfully");
    }
    
    void initializeDefaultRules() {
        // Energy optimization rule
        AutomationRule energyRule;
        energyRule.id = "energy_opt_001";
        energyRule.name = "Smart Energy Optimization";
        energyRule.trigger = SENSOR_BASED;
        energyRule.condition = "{\"type\":\"and\",\"conditions\":[{\"sensor\":\"LDR\",\"operator\":\">\",\"value\":800},{\"sensor\":\"PIR\",\"operator\":\"==\",\"value\":0}]}";
        energyRule.action = "{\"devices\":[{\"id\":\"indoor_lights\",\"state\":false}]}";
        energyRule.enabled = true;
        energyRule.priority = 1;
        rules.push_back(energyRule);
        
        // Comfort control rule
        AutomationRule comfortRule;
        comfortRule.id = "comfort_001";
        comfortRule.name = "Smart Climate Control";
        comfortRule.trigger = SENSOR_BASED;
        comfortRule.condition = "{\"type\":\"or\",\"conditions\":[{\"sensor\":\"BME280_TEMP\",\"operator\":\">\",\"value\":28},{\"sensor\":\"BME280_HUM\",\"operator\":\">\",\"value\":70}]}";
        comfortRule.action = "{\"devices\":[{\"id\":\"cooling_fan\",\"state\":true}]}";
        comfortRule.enabled = true;
        comfortRule.priority = 2;
        rules.push_back(comfortRule);
        
        // Security rule
        AutomationRule securityRule;
        securityRule.id = "security_001";
        securityRule.name = "Motion Detection Response";
        securityRule.trigger = SENSOR_BASED;
        securityRule.condition = "{\"type\":\"and\",\"conditions\":[{\"sensor\":\"PIR\",\"operator\":\"==\",\"value\":1},{\"time\":\"night\"}]}";
        securityRule.action = "{\"devices\":[{\"id\":\"security_lights\",\"state\":true},{\"id\":\"security_camera\",\"state\":true}]}";
        securityRule.enabled = true;
        securityRule.priority = 3;
        rules.push_back(securityRule);
        
        // Adaptive learning rule
        AutomationRule learningRule;
        learningRule.id = "learning_001";
        learningRule.name = "User Behavior Learning";
        learningRule.trigger = EVENT_BASED;
        learningRule.condition = "{\"type\":\"user_action\",\"learn\":true}";
        learningRule.action = "{\"type\":\"learn_pattern\"}";
        learningRule.enabled = true;
        learningRule.priority = 5;
        rules.push_back(learningRule);
    }
    
    void initializePredictionModels() {
        // Temperature prediction model
        PredictionModel tempModel;
        tempModel.weights = {0.5, 0.3, 0.2}; // Previous temp, humidity, time of day
        tempModel.bias = 0.0;
        tempModel.accuracy = 0.0;
        tempModel.feature = "temperature";
        models["temperature"] = tempModel;
        
        // Occupancy prediction model
        PredictionModel occupancyModel;
        occupancyModel.weights = {0.6, 0.2, 0.2}; // Time patterns, motion history, day of week
        occupancyModel.bias = 0.0;
        occupancyModel.accuracy = 0.0;
        occupancyModel.feature = "occupancy";
        models["occupancy"] = occupancyModel;
        
        // Energy usage prediction model
        PredictionModel energyModel;
        energyModel.weights = {0.4, 0.3, 0.2, 0.1}; // Device usage, weather, time, occupancy
        energyModel.bias = 0.0;
        energyModel.accuracy = 0.0;
        energyModel.feature = "energy_usage";
        models["energy_usage"] = energyModel;
    }
    
    void addRule(AutomationRule rule) {
        rules.push_back(rule);
        saveRulesToEEPROM();
    }
    
    void removeRule(String ruleId) {
        rules.erase(std::remove_if(rules.begin(), rules.end(),
            [ruleId](const AutomationRule& rule) { return rule.id == ruleId; }),
            rules.end());
        saveRulesToEEPROM();
    }
    
    void evaluateRules() {
        // Get current sensor values
        std::map<String, float> currentSensorValues = getCurrentSensorValues();
        
        // Sort rules by priority
        std::sort(rules.begin(), rules.end(), 
            [](const AutomationRule& a, const AutomationRule& b) {
                return a.priority < b.priority;
            });
        
        for (auto& rule : rules) {
            if (!rule.enabled) continue;
            
            // Check if rule should be throttled
            if (shouldThrottleRule(rule.id)) continue;
            
            bool conditionMet = false;
            
            switch (rule.trigger) {
                case SENSOR_BASED:
                    conditionMet = checkSensorCondition(rule.condition, currentSensorValues);
                    break;
                case TIME_BASED:
                    conditionMet = checkTimeCondition(rule.condition);
                    break;
                case WEATHER_BASED:
                    conditionMet = checkWeatherCondition(rule.condition);
                    break;
                case PRESENCE_BASED:
                    conditionMet = checkPresenceCondition(rule.condition);
                    break;
                case EVENT_BASED:
                    conditionMet = checkEventCondition(rule.condition);
                    break;
            }
            
            if (conditionMet) {
                float confidence = calculateActionConfidence(rule, currentSensorValues);
                
                if (confidence > confidenceThreshold) {
                    executeAction(rule.action);
                    rule.lastTriggered = millis();
                    lastExecution[rule.id] = millis();
                    
                    // Learn from successful execution
                    learnFromExecution(rule, currentSensorValues, true);
                    
                    Serial.println("Rule executed: " + rule.name + " (confidence: " + String(confidence) + ")");
                    addLog("AI Rule: " + rule.name + " executed with " + String(confidence * 100) + "% confidence");
                }
            }
        }
        
        // Perform predictive analysis
        performPredictiveAnalysis();
        
        // Update behavior patterns
        updateBehaviorPatterns();
        
        // Check for anomalies
        detectAnomalies(currentSensorValues);
    }
    
    bool checkSensorCondition(String condition, std::map<String, float> sensorValues) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, condition);
        
        if (doc["type"] == "and") {
            JsonArray conditions = doc["conditions"];
            for (JsonObject cond : conditions) {
                if (!evaluateSingleCondition(cond, sensorValues)) {
                    return false;
                }
            }
            return true;
        }
        else if (doc["type"] == "or") {
            JsonArray conditions = doc["conditions"];
            for (JsonObject cond : conditions) {
                if (evaluateSingleCondition(cond, sensorValues)) {
                    return true;
                }
            }
            return false;
        }
        else {
            return evaluateSingleCondition(doc.as<JsonObject>(), sensorValues);
        }
    }
    
    bool evaluateSingleCondition(JsonObject condition, std::map<String, float> sensorValues) {
        String sensor = condition["sensor"];
        String op = condition["operator"];
        float value = condition["value"];
        
        if (sensorValues.find(sensor) == sensorValues.end()) {
            return false;
        }
        
        float sensorValue = sensorValues[sensor];
        
        if (op == ">") return sensorValue > value;
        if (op == "<") return sensorValue < value;
        if (op == ">=") return sensorValue >= value;
        if (op == "<=") return sensorValue <= value;
        if (op == "==") return abs(sensorValue - value) < 0.01;
        if (op == "!=") return abs(sensorValue - value) >= 0.01;
        
        return false;
    }
    
    bool checkTimeCondition(String condition) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, condition);
        
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        
        if (doc.containsKey("hour")) {
            int targetHour = doc["hour"];
            return timeinfo->tm_hour == targetHour;
        }
        
        if (doc.containsKey("time_range")) {
            String startTime = doc["time_range"]["start"];
            String endTime = doc["time_range"]["end"];
            return isTimeInRange(timeinfo, startTime, endTime);
        }
        
        if (doc.containsKey("day_of_week")) {
            int targetDay = doc["day_of_week"];
            return timeinfo->tm_wday == targetDay;
        }
        
        return false;
    }
    
    bool checkWeatherCondition(String condition) {
        // Get current weather data
        WeatherData weather = weatherStation.getCurrentWeather();
        
        DynamicJsonDocument doc(512);
        deserializeJson(doc, condition);
        
        if (doc.containsKey("temperature")) {
            float targetTemp = doc["temperature"];
            String op = doc["temp_operator"] | ">";
            if (op == ">") return weather.temperature > targetTemp;
            if (op == "<") return weather.temperature < targetTemp;
        }
        
        if (doc.containsKey("condition")) {
            String targetCondition = doc["condition"];
            return weather.description.indexOf(targetCondition) >= 0;
        }
        
        return false;
    }
    
    bool checkPresenceCondition(String condition) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, condition);
        
        if (doc["type"] == "home") {
            return geofencing.isInHomeZone();
        }
        
        if (doc["type"] == "away") {
            return !geofencing.isInHomeZone();
        }
        
        if (doc["type"] == "room_occupancy") {
            String room = doc["room"];
            return checkRoomOccupancy(room);
        }
        
        return false;
    }
    
    bool checkEventCondition(String condition) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, condition);
        
        if (doc["type"] == "user_action") {
            // This would be triggered by user interactions
            return false; // Placeholder
        }
        
        return false;
    }
    
    void executeAction(String action) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, action);
        
        if (doc.containsKey("devices")) {
            JsonArray devices = doc["devices"];
            for (JsonObject device : devices) {
                String deviceId = device["id"];
                bool state = device["state"];
                int value = device["value"] | 0;
                
                // Note: Device control functionality would be implemented here
                // advancedDeviceController.controlDevice(deviceId, state, value);
                addLog("AI Action: Control device " + deviceId + " state: " + String(state));
            }
        }
        
        if (doc.containsKey("scene")) {
            String sceneName = doc["scene"];
            // Note: Scene activation functionality would be implemented here
            // advancedDeviceController.activateScene(sceneName);
            addLog("AI Action: Activate scene " + sceneName);
        }
        
        if (doc.containsKey("notification")) {
            String message = doc["notification"]["message"];
            String type = doc["notification"]["type"];
            notifyUser(message, type);
        }
        
        if (doc["type"] == "learn_pattern") {
            // Trigger pattern learning
            learnCurrentPattern();
        }
    }
    
    float calculateActionConfidence(AutomationRule& rule, std::map<String, float> sensorValues) {
        float baseConfidence = 0.7; // Base confidence for rule-based actions
        
        // Adjust confidence based on historical success
        String ruleId = rule.id;
        if (behaviorPatterns.find(ruleId) != behaviorPatterns.end()) {
            std::vector<float>& history = behaviorPatterns[ruleId];
            if (history.size() > 5) {
                float successRate = calculateSuccessRate(history);
                baseConfidence = (baseConfidence + successRate) / 2.0;
            }
        }
        
        // Adjust confidence based on sensor data quality
        float dataQuality = calculateDataQuality(sensorValues);
        baseConfidence *= dataQuality;
        
        // Adjust confidence based on time since last execution
        if (lastExecution.find(rule.id) != lastExecution.end()) {
            unsigned long timeSinceLastExecution = millis() - lastExecution[rule.id];
            if (timeSinceLastExecution < 60000) { // Less than 1 minute
                baseConfidence *= 0.5; // Reduce confidence for frequent executions
            }
        }
        
        // Adjust confidence based on user preferences
        float preferenceScore = calculatePreferenceScore(rule.action);
        baseConfidence = (baseConfidence + preferenceScore) / 2.0;
        
        return constrain(baseConfidence, 0.0, 1.0);
    }
    
    void performPredictiveAnalysis() {
        // Predict future sensor values
        for (auto& model : models) {
            std::vector<float> prediction = predictNextValues(model.first);
            
            // Use predictions to trigger proactive actions
            if (model.first == "temperature" && prediction.size() > 0) {
                float predictedTemp = prediction[0];
                if (predictedTemp > 30.0) {
                    // Proactively turn on cooling
                    Serial.println("AI Prediction: High temperature expected, pre-cooling");
                    // Note: Predictive device control would be implemented here
                    // advancedDeviceController.controlDevice("cooling_fan", true);
                    addLog("AI Prediction: Temperature rising, would activate cooling");
                }
            }
            
            if (model.first == "occupancy" && prediction.size() > 0) {
                float occupancyProb = prediction[0];
                if (occupancyProb > 0.8) {
                    // Prepare home for arrival
                    Serial.println("AI Prediction: Occupancy expected, preparing home");
                    // Note: Predictive scene activation would be implemented here
                    // advancedDeviceController.activateScene("welcome_home");
                    addLog("AI Prediction: User arrival detected, would activate welcome scene");
                }
            }
        }
    }
    
    std::vector<float> predictNextValues(String feature) {
        if (models.find(feature) == models.end()) {
            return {};
        }
        
        PredictionModel& model = models[feature];
        std::vector<float> inputs = getFeatureInputs(feature);
        
        if (inputs.size() != model.weights.size()) {
            return {};
        }
        
        float prediction = model.bias;
        for (size_t i = 0; i < inputs.size(); i++) {
            prediction += inputs[i] * model.weights[i];
        }
        
        // Apply activation function (sigmoid for probabilities)
        if (feature == "occupancy") {
            prediction = 1.0 / (1.0 + exp(-prediction));
        }
        
        return {prediction};
    }
    
    std::vector<float> getFeatureInputs(String feature) {
        std::vector<float> inputs;
        std::map<String, float> currentSensorValues = getCurrentSensorValues();
        
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        
        if (feature == "temperature") {
            inputs.push_back(currentSensorValues["BME280_TEMP"]);
            inputs.push_back(currentSensorValues["BME280_HUM"]);
            inputs.push_back(timeinfo->tm_hour / 24.0); // Normalize hour
        }
        else if (feature == "occupancy") {
            inputs.push_back(timeinfo->tm_hour / 24.0);
            inputs.push_back(currentSensorValues["PIR"]);
            inputs.push_back(timeinfo->tm_wday / 7.0); // Day of week
        }
        else if (feature == "energy_usage") {
            inputs.push_back(timeinfo->tm_hour / 24.0);
            inputs.push_back(currentSensorValues["BME280_TEMP"]);
            inputs.push_back(currentSensorValues["PIR"]);
            inputs.push_back(getCurrentDeviceCount());
        }
        
        return inputs;
    }
    
    void updateBehaviorPatterns() {
        // Learn from user interactions and device usage patterns
        std::map<String, float> currentSensorValues = getCurrentSensorValues();
        
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        
        // Create a pattern signature
        std::vector<float> signature;
        signature.push_back(timeinfo->tm_hour);
        signature.push_back(timeinfo->tm_wday);
        signature.push_back(currentSensorValues["BME280_TEMP"]);
        signature.push_back(currentSensorValues["PIR"]);
        
        // Check if this pattern matches any known patterns
        bool patternFound = false;
        for (auto& pattern : recognizedPatterns) {
            if (patternsMatch(signature, pattern.signature, 0.8)) {
                pattern.lastDetected = millis();
                pattern.confidence += 0.01;
                pattern.confidence = constrain(pattern.confidence, 0.0, 1.0);
                patternFound = true;
                break;
            }
        }
        
        if (!patternFound && shouldCreateNewPattern(signature)) {
            Pattern newPattern;
            newPattern.name = "Pattern_" + String(recognizedPatterns.size() + 1);
            newPattern.signature = signature;
            newPattern.confidence = 0.1;
            newPattern.action = inferActionFromPattern(signature);
            newPattern.lastDetected = millis();
            recognizedPatterns.push_back(newPattern);
        }
    }
    
    void detectAnomalies(std::map<String, float> sensorValues) {
        for (auto& sensor : sensorValues) {
            String sensorName = sensor.first;
            float value = sensor.second;
            
            // Get historical average and standard deviation
            std::vector<float> history = getSensorHistory(sensorName);
            if (history.size() < 10) continue;
            
            float mean = calculateMean(history);
            float stdDev = calculateStandardDeviation(history, mean);
            
            // Check if current value is an anomaly (beyond 2 standard deviations)
            float zScore = abs(value - mean) / stdDev;
            if (zScore > 2.0) {
                String anomalyMsg = "Anomaly detected: " + sensorName + " = " + String(value) + 
                                  " (expected: " + String(mean) + " ± " + String(stdDev) + ")";
                Serial.println(anomalyMsg);
                
                // Take appropriate action based on anomaly type
                handleAnomaly(sensorName, value, mean);
            }
        }
    }
    
    void handleAnomaly(String sensorName, float value, float expectedValue) {
        if (sensorName == "BME280_TEMP" && value > expectedValue + 10) {
            // Sudden temperature spike - possible fire or equipment malfunction
            notifyUser("ALERT: Unusual temperature spike detected!", "critical");
            // Note: Anomaly response would be implemented here
            // advancedDeviceController.controlDevice("cooling_fan", true);
            addLog("AI Anomaly: High temperature detected, would activate cooling");
        }
        
        if (sensorName == "MQ135" && value > expectedValue + 100) {
            // Air quality anomaly
            notifyUser("Air quality alert: Consider ventilation", "warning");
            // Note: Air quality response would be implemented here
            // advancedDeviceController.controlDevice("air_purifier", true);
            addLog("AI Anomaly: Poor air quality detected, would activate purifier");
        }
        
        if (sensorName == "PIR" && value != expectedValue) {
            // Unexpected motion (security concern)
            if (!geofencing.isInHomeZone()) {
                notifyUser("Motion detected while away from home!", "security");
                // Note: Security response would be implemented here
                // advancedDeviceController.controlDevice("security_camera", true);
                addLog("AI Anomaly: Intrusion detected, would activate security systems");
            }
        }
    }
    
    void learnFromExecution(AutomationRule& rule, std::map<String, float> sensorValues, bool successful) {
        String ruleId = rule.id;
        
        // Update success/failure history
        if (behaviorPatterns.find(ruleId) == behaviorPatterns.end()) {
            behaviorPatterns[ruleId] = std::vector<float>();
        }
        
        behaviorPatterns[ruleId].push_back(successful ? 1.0 : 0.0);
        
        // Keep only recent history
        if (behaviorPatterns[ruleId].size() > 100) {
            behaviorPatterns[ruleId].erase(behaviorPatterns[ruleId].begin());
        }
        
        // Update prediction models based on execution results
        updatePredictionModels(sensorValues, successful);
    }
    
    void updatePredictionModels(std::map<String, float> sensorValues, bool successful) {
        // Simple online learning algorithm
        for (auto& model : models) {
            std::vector<float> inputs = getFeatureInputs(model.first);
            if (inputs.empty()) continue;
            
            float target = successful ? 1.0 : 0.0;
            std::vector<float> prediction = predictNextValues(model.first);
            if (prediction.empty()) continue;
            
            float error = target - prediction[0];
            
            // Update weights using gradient descent
            for (size_t i = 0; i < model.second.weights.size() && i < inputs.size(); i++) {
                model.second.weights[i] += learningRate * error * inputs[i];
            }
            model.second.bias += learningRate * error;
        }
    }
    
    void learnCurrentPattern() {
        // Learn from current context and user actions
        std::map<String, float> currentSensorValues = getCurrentSensorValues();
        
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        
        // Create learning signature
        std::vector<float> learnSignature;
        learnSignature.push_back(timeinfo->tm_hour);
        learnSignature.push_back(currentSensorValues["BME280_TEMP"]);
        learnSignature.push_back(currentSensorValues["PIR"]);
        
        // Store for future pattern recognition
        String patternKey = "learned_" + String(millis());
        behaviorPatterns[patternKey] = learnSignature;
        
        Serial.println("Learning pattern from current context");
    }
    
    String getAIInsights() {
        DynamicJsonDocument doc(2048);
        
        // Prediction insights
        JsonArray predictions = doc.createNestedArray("predictions");
        for (auto& model : models) {
            std::vector<float> pred = predictNextValues(model.first);
            if (!pred.empty()) {
                JsonObject predObj = predictions.createNestedObject();
                predObj["feature"] = model.first;
                predObj["prediction"] = pred[0];
                predObj["confidence"] = model.second.accuracy;
            }
        }
        
        // Pattern insights
        JsonArray patterns = doc.createNestedArray("patterns");
        for (auto& pattern : recognizedPatterns) {
            if (pattern.confidence > 0.5) {
                JsonObject patObj = patterns.createNestedObject();
                patObj["name"] = pattern.name;
                patObj["confidence"] = pattern.confidence;
                patObj["last_detected"] = pattern.lastDetected;
            }
        }
        
        // Optimization suggestions
        JsonArray suggestions = doc.createNestedArray("suggestions");
        std::vector<String> opts = generateOptimizationSuggestions();
        for (const String& suggestion : opts) {
            suggestions.add(suggestion);
        }
        
        // System health
        doc["system_health"]["rules_active"] = getActiveRulesCount();
        doc["system_health"]["learning_progress"] = getLearningProgress();
        doc["system_health"]["prediction_accuracy"] = getOverallPredictionAccuracy();
        
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    std::vector<String> generateOptimizationSuggestions() {
        std::vector<String> suggestions;
        
        // Analyze energy usage patterns
        float avgEnergyUsage = energyMonitor.getDailyUsage();
        if (avgEnergyUsage > 15.0) { // kWh threshold
            suggestions.push_back("Consider implementing more aggressive energy-saving schedules");
        }
        
        // Analyze comfort vs efficiency
        std::map<String, float> sensorValues = getCurrentSensorValues();
        if (sensorValues["BME280_TEMP"] < 20 && isDeviceActive("heater")) {
            suggestions.push_back("Heating efficiency could be improved with better insulation");
        }
        
        // Security optimization
        if (recognizedPatterns.size() > 10) {
            suggestions.push_back("Your routine is well-learned. Consider enabling fully autonomous mode");
        }
        
        return suggestions;
    }
    
private:
    std::map<String, float> getCurrentSensorValues() {
        std::map<String, float> values;
        
        // Get sensor values from the sensor manager
        // This would interface with the actual sensor manager
        values["BME280_TEMP"] = 25.0;    // Placeholder
        values["BME280_HUM"] = 60.0;     // Placeholder
        values["BME280_PRESS"] = 1013.0; // Placeholder
        values["PIR"] = 0.0;             // Placeholder
        values["LDR"] = 500.0;           // Placeholder
        values["MQ135"] = 100.0;         // Placeholder
        
        return values;
    }
    
    bool shouldThrottleRule(String ruleId) {
        if (lastExecution.find(ruleId) == lastExecution.end()) {
            return false;
        }
        
        unsigned long timeSinceLastExecution = millis() - lastExecution[ruleId];
        return timeSinceLastExecution < 30000; // 30 second minimum between executions
    }
    
    bool isTimeInRange(struct tm* timeinfo, String startTime, String endTime) {
        // Parse time strings and check if current time is in range
        // Implementation details would parse "HH:MM" format
        return true; // Placeholder
    }
    
    bool checkRoomOccupancy(String room) {
        // Check if specified room is occupied based on sensors
        return false; // Placeholder
    }
    
    float calculateSuccessRate(std::vector<float>& history) {
        if (history.empty()) return 0.5;
        
        float sum = 0;
        for (float value : history) {
            sum += value;
        }
        return sum / history.size();
    }
    
    float calculateDataQuality(std::map<String, float> sensorValues) {
        // Calculate data quality based on sensor reliability, age, etc.
        return 0.9; // Placeholder
    }
    
    float calculatePreferenceScore(String action) {
        // Calculate how well this action aligns with user preferences
        return 0.8; // Placeholder
    }
    
    bool patternsMatch(std::vector<float>& pattern1, std::vector<float>& pattern2, float threshold) {
        if (pattern1.size() != pattern2.size()) return false;
        
        float similarity = 0;
        for (size_t i = 0; i < pattern1.size(); i++) {
            similarity += 1.0 - abs(pattern1[i] - pattern2[i]) / max(pattern1[i], pattern2[i]);
        }
        similarity /= pattern1.size();
        
        return similarity >= threshold;
    }
    
    bool shouldCreateNewPattern(std::vector<float>& signature) {
        // Only create new patterns if we have enough variation
        return recognizedPatterns.size() < 20;
    }
    
    String inferActionFromPattern(std::vector<float>& signature) {
        // Infer what action should be taken based on pattern signature
        return "{\"type\":\"learned_action\"}";
    }
    
    std::vector<float> getSensorHistory(String sensorName) {
        std::vector<float> history;
        // Get historical sensor data
        return history; // Placeholder
    }
    
    float calculateMean(std::vector<float>& data) {
        float sum = 0;
        for (float value : data) {
            sum += value;
        }
        return sum / data.size();
    }
    
    float calculateStandardDeviation(std::vector<float>& data, float mean) {
        float variance = 0;
        for (float value : data) {
            variance += pow(value - mean, 2);
        }
        variance /= data.size();
        return sqrt(variance);
    }
    
    float getCurrentDeviceCount() {
        // Count currently active devices
        return 5.0; // Placeholder
    }
    
    bool isDeviceActive(String deviceId) {
        // Check if device is currently active
        return false; // Placeholder
    }
    
    int getActiveRulesCount() {
        int count = 0;
        for (auto& rule : rules) {
            if (rule.enabled) count++;
        }
        return count;
    }
    
    float getLearningProgress() {
        // Calculate overall learning progress
        return min(behaviorPatterns.size() / 50.0, 1.0);
    }
    
    float getOverallPredictionAccuracy() {
        float totalAccuracy = 0;
        int modelCount = 0;
        for (auto& model : models) {
            totalAccuracy += model.second.accuracy;
            modelCount++;
        }
        return modelCount > 0 ? totalAccuracy / modelCount : 0.0;
    }
    
    void saveRulesToEEPROM() {
        // Save automation rules to EEPROM
        Serial.println("Saving automation rules to EEPROM");
    }
    
    void loadRulesFromEEPROM() {
        // Load automation rules from EEPROM
        Serial.println("Loading automation rules from EEPROM");
    }
    
    void loadUserPreferences() {
        // Load user preferences from storage
        userPreferences["temperature_comfort"] = 23.0;
        userPreferences["humidity_comfort"] = 50.0;
        userPreferences["lighting_brightness"] = 80.0;
        userPreferences["energy_saving_priority"] = 0.7;
    }
};

// Global instance
AIAutomationEngine aiEngine;
