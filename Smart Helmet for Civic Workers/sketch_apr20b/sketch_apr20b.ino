#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "Vishi";
const char* password = "9005885311";

// Web server endpoint (Base station)
const char* serverName = "http://192.168.163.217:5000/endpoint"; // Change to your base station URL

// Unique identifier for this helmet
const char* HELMET_ID = "helmet_01";

// Sensor pins
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ9_PIN 34  // Analog gas sensor pin

// Output pins
#define BUZZER_PIN 25
#define LED_PIN 26
#define STATUS_LED_PIN 2  // Built-in LED for connectivity status

// Thresholds - Adjusted values
#define TEMP_THRESHOLD 50      // Celsius
#define GAS_THRESHOLD 4200     // Adjusted based on your environment readings (3500-4000)
#define ACCEL_THRESHOLD 1.5    // G-force

// I2C pins for MPU6050
#define SDA_PIN 21
#define SCL_PIN 22

// Connectivity variables
bool wifiConnected = false;
bool serverConnected = false;
unsigned long lastConnectionAttempt = 0;
const unsigned long CONNECTION_RETRY_INTERVAL = 30000; // 30 seconds
int failedConnectionAttempts = 0;

// Sensor status tracking
bool mpuInitialized = false;

// Objects
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;

// Function declarations
bool connectToWiFi();
bool sendDataToServer(float temperature, int gasValue, float totalG, bool danger);
void handleSensorFailure(const char* sensorName);
void blinkLED(int pin, int times, int delayMs);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n===== Helmet Safety System Starting =====");
  
  // Configure pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  // Initial LED state
  digitalWrite(STATUS_LED_PIN, LOW);
  
  // Start I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize sensors
  Serial.println("Initializing sensors...");
  
  // DHT sensor
  dht.begin();
  delay(2000); // Give time for DHT to stabilize
  
  // MQ9 Gas Sensor needs warmup
  Serial.println("Warming up gas sensor (MQ9)...");
  // Let the sensor warm up for a bit
  for (int i = 0; i < 10; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(500);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nGas sensor warm-up complete");
  
  // MPU6050 with Adafruit library
  Serial.println("Initializing MPU6050...");
  
  // Try to initialize MPU6050
  int mpuAttempts = 0;
  while (!mpu.begin() && mpuAttempts < 5) {
    Serial.println("Failed to find MPU6050 chip, retrying...");
    handleSensorFailure("MPU6050");
    delay(1000);
    mpuAttempts++;
  }
  
  if (mpuAttempts >= 5) {
    Serial.println("Could not find a valid MPU6050 sensor, continuing without it");
    mpuInitialized = false;
  } else {
    // Configure MPU6050
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 initialized successfully!");
    mpuInitialized = true;
  }
  
  blinkLED(STATUS_LED_PIN, 3, 200); // Setup complete indicator
  
  // Connect to WiFi
  connectToWiFi();
}

void loop() {
  // Check WiFi connection periodically
  unsigned long currentMillis = millis();
  if (!wifiConnected && (currentMillis - lastConnectionAttempt > CONNECTION_RETRY_INTERVAL)) {
    connectToWiFi();
    lastConnectionAttempt = currentMillis;
  }
  
  // Read temperature
  float temperature = dht.readTemperature();
  
  // Check if temperature reading failed
  if (isnan(temperature)) {
    Serial.println("Failed to read temperature!");
    temperature = -999; // Error indicator
    handleSensorFailure("DHT22");
  }
  
  // Read gas sensor
  int gasValue = analogRead(MQ9_PIN);
  Serial.printf("Raw gas value: %d\n", gasValue);
  
  // Read accelerometer values with Adafruit library
  float totalG = 0;
  
  if (mpuInitialized) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    // Calculate total acceleration in g-forces (9.8 m/s^2 = 1g)
    float accX = a.acceleration.x / 9.81;
    float accY = a.acceleration.y / 9.81;
    float accZ = a.acceleration.z / 9.81;
    
    // Total G-force
    totalG = sqrt(accX * accX + accY * accY + accZ * accZ);
    
    Serial.printf("Accelerometer: X:%.2f, Y:%.2f, Z:%.2f g\n", accX, accY, accZ);
    Serial.printf("Total G-force: %.2f\n", totalG);
  } else {
    Serial.println("MPU6050 not available for reading");
  }
  
  // Print sensor values
  Serial.printf("Temp: %.2f°C | Gas: %d | G-force: %.2f\n", temperature, gasValue, totalG);
  
  // Danger check
  bool danger = false;
  
  // Check temperature (if valid)
  if (temperature != -999 && temperature > TEMP_THRESHOLD) {
    danger = true;
    Serial.println("⚠ DANGER: High temperature detected!");
  }
  
  // Check gas - with adjusted threshold
  if (gasValue > GAS_THRESHOLD) {
    danger = true;
    Serial.println("⚠ DANGER: Gas detected!");
  }
  
  // Check acceleration
  if (mpuInitialized && totalG > ACCEL_THRESHOLD) {
    danger = true;
    Serial.println("⚠ DANGER: High impact detected!");
  }
  
  // Activate alerts if in danger
  if (danger) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    Serial.println("⚠ DANGER ALERTS ACTIVATED! ⚠");
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
  }
  
  // Send to server
  if (wifiConnected) {
    serverConnected = sendDataToServer(temperature, gasValue, totalG, danger);
    
    if (serverConnected) {
      digitalWrite(STATUS_LED_PIN, HIGH); // Keep LED on when connected
      failedConnectionAttempts = 0; // Reset counter on successful connection
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
      failedConnectionAttempts++;
      
      // If we've failed to connect multiple times, try to reconnect WiFi
      if (failedConnectionAttempts >= 3) {
        Serial.println("Multiple server connection failures - reconnecting WiFi...");
        WiFi.disconnect();
        wifiConnected = false;
        failedConnectionAttempts = 0;
      }
    }
  } else {
    blinkLED(STATUS_LED_PIN, 1, 500); // Blink to indicate no WiFi
  }
  
  delay(5000);  // Send every 5 seconds
}

bool connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return true;
  }
  
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  // Wait for connection with timeout
  int connectionTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && connectionTimeout < 20) {
    delay(500);
    Serial.print(".");
    connectionTimeout++;
    blinkLED(STATUS_LED_PIN, 1, 100);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    return true;
  } else {
    Serial.println("\nWiFi connection failed!");
    wifiConnected = false;
    return false;
  }
}

bool sendDataToServer(float temperature, int gasValue, float totalG, bool danger) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected to WiFi. Cannot send data.");
    return false;
  }
  
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON document
  StaticJsonDocument<256> doc;
  doc["helmet_id"] = HELMET_ID;
  
  // Only add valid temperature
  if (temperature != -999) {
    doc["temperature"] = temperature;
  } else {
    doc["temperature"] = nullptr;
  }
  
  doc["gas"] = gasValue;
  doc["g_force"] = totalG;
  doc["danger"] = danger;
  
  // Serialize JSON to string
  String payload;
  serializeJson(doc, payload);
  
  // Send the request
  int httpResponseCode = http.POST(payload);
  
  Serial.print("HTTP Response: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode >= 200 && httpResponseCode < 300) {
    String response = http.getString();
    Serial.println("Server response: " + response);
    http.end();
    return true;
  } else if (httpResponseCode == 405) {
    Serial.println("ERROR: Method Not Allowed (405). Check if the server endpoint is correct.");
    http.end();
    return false;
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

void handleSensorFailure(const char* sensorName) {
  Serial.print("SENSOR FAILURE: ");
  Serial.println(sensorName);
  
  // Notify with beeps
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void blinkLED(int pin, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(delayMs);
    digitalWrite(pin, LOW);
    delay(delayMs);
  }
}