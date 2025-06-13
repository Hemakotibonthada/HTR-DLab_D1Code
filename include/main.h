#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <WebServer.h>

// Extern variable declarations
extern String sessionToken;
extern WebServer server;
// Add other extern variable declarations as needed
extern String savedUsername;
extern String savedPassword;
extern String savedBirthday;
extern bool relayStates[8];
extern float currentTemp;
extern float currentHum;

// Function declarations
void handleLogin();
void handleLogout();
void handleRoot();
void handleChangePassGet();
void handleChangePassPost();
void handleResetPass();
void handleResetPassPost();
void handleRelayToggle();
void handleRelayStatus();
void handleSensor();
void handleSimpleLogs();
// Add other function declarations as needed

#endif