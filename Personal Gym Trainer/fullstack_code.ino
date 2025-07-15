#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
using namespace ArduinoJson;

// ===== User Config =====
const char* ssid = "Vishi";
const char* password = "9005885311";

// EMG pins & window
const int EMG1_PIN = 34;
const int EMG2_PIN = 25;
const int windowSize = 20;

// Vibration (LEDC PWM)
const int VIBE_PIN = 32;
const int PWM_CHANNEL = 0;
const int PWM_FREQ = 5000;
const int PWM_RES_BITS = 8;

// MPU6050
Adafruit_MPU6050 mpu;

// Web server & SSE
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Buffers & state
int bufIdx = 0;
bool bufFull = false;
int emgBuf1[windowSize], emgBuf2[windowSize];
int repCount = 0;
unsigned long lastRepTime = 0;
const unsigned long maxRepTime = 3000; // ms

// Default settings (can be updated via API)
int emgThreshold = 2100;
int angleStartMin = 10;
int angleStartMax = 30;
int angleComplete = 80;
String currentExercise = "bicepCurl";

// Variables for exercise state tracking
bool inExercise = false;
bool repStarted = false;
bool repCompleted = false;
float currentAngle = 0.0;
unsigned long lastEmissionTime = 0;

// Function declarations
void sendSensorData();

// HTML content as a placeholder - you mentioned you don't need the website
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><body>Exercise Tracker API</body></html>)rawliteral";

void sendSensorData() {
    DynamicJsonDocument json(1024);
    json["emg1"] = emgBuf1[bufIdx > 0 ? bufIdx - 1 : windowSize - 1]; // Get most recent value
    json["emg2"] = emgBuf2[bufIdx > 0 ? bufIdx - 1 : windowSize - 1]; // Get most recent value
    json["angle"] = currentAngle;
    json["reps"] = repCount;
    if(repCount > 0 && lastRepTime > 0) {
      json["lastRepTime"] = lastRepTime;
    }
    
    String output;
    serializeJson(json, output);
    events.send(output.c_str(), "update");
}

void handleSettings(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.containsKey("emgThreshold")) {
    emgThreshold = jsonObj["emgThreshold"].as<int>();
  }
  if (jsonObj.containsKey("angleStartMin")) {
    angleStartMin = jsonObj["angleStartMin"].as<int>();
  }
  if (jsonObj.containsKey("angleStartMax")) {
    angleStartMax = jsonObj["angleStartMax"].as<int>();
  }
  if (jsonObj.containsKey("angleComplete")) {
    angleComplete = jsonObj["angleComplete"].as<int>();
  }
  if (jsonObj.containsKey("exercise")) {
    currentExercise = jsonObj["exercise"].as<String>();
  }

  Serial.println("Settings updated via API");
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void setup() {
  Serial.begin(115200);

  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize web server
  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId()) {
      Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
    }
  });
  server.addHandler(&events);

  // Serve the index.html file
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  // Handle GET request for settings
  server.on("/getSettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument json(256);
    json["emgThreshold"] = emgThreshold;
    json["angleStartMin"] = angleStartMin;
    json["angleStartMax"] = angleStartMax;
    json["angleComplete"] = angleComplete;
    json["exercise"] = currentExercise;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(json, *response);
    request->send(response);
  });

  // Handle POST request for settings
  AsyncCallbackJsonWebHandler *settingsHandler = new AsyncCallbackJsonWebHandler("/api/settings", handleSettings);
  server.addHandler(settingsHandler);

  server.begin();
  Serial.println("HTTP server started");

  // PWM Setup for vibration motor
  ledcAttach(VIBE_PIN, PWM_FREQ, PWM_RES_BITS);

  // Initialize lastRepTime
  lastRepTime = millis();
}

void loop() {
  // Read EMG and accelerometer data
  int emgVal1 = analogRead(EMG1_PIN);
  int emgVal2 = analogRead(EMG2_PIN);
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Store EMG values in buffers
  emgBuf1[bufIdx] = emgVal1;
  emgBuf2[bufIdx] = emgVal2;
  bufIdx++;
  if (bufIdx >= windowSize) {
    bufIdx = 0;
    bufFull = true;
  }

  // Calculate angle from accelerometer data (X and Z axes)
  float angle = atan2(a.acceleration.x, a.acceleration.z) * 180.0 / PI;
  currentAngle = angle;

  // Check if muscle activation exceeds threshold using raw EMG values
  bool muscleActive = (emgVal1 > emgThreshold || emgVal2 > emgThreshold);
  
  // State machine for rep counting
  if (!repStarted && muscleActive && angle >= angleStartMin && angle <= angleStartMax) {
    // Start of rep detected
    repStarted = true;
    ledcWrite(PWM_CHANNEL, 128); // Vibrate to indicate rep start
    Serial.println("Rep started");
  } 
  else if (repStarted && !repCompleted && angle >= angleComplete) {
    // Rep completion detected
    repCompleted = true;
    ledcWrite(PWM_CHANNEL, 255); // Stronger vibration for completion
    Serial.println("Rep completed");
  }
  else if (repStarted && repCompleted && angle <= angleStartMax) {
    // Reset for next rep
    repStarted = false;
    repCompleted = false;
    repCount++;
    unsigned long currentTime = millis();
    unsigned long repTime = currentTime - lastRepTime;
    Serial.printf("Rep #%d completed in %lu ms\n", repCount, repTime);
    lastRepTime = currentTime;
    ledcWrite(PWM_CHANNEL, 0); // Stop vibration
  }
  
  // Send data to clients periodically
  unsigned long currentTime = millis();
  if (currentTime - lastEmissionTime >= 33) { // ~30 Hz update rate
    lastEmissionTime = currentTime;
    sendSensorData();
  }
  
  // Print raw sensor values for debugging
  Serial.printf("EMG1: %d, EMG2: %d, Angle: %.2f\n", emgVal1, emgVal2, angle);
  
  delay(10); // Small delay to prevent overwhelming the processor
}