#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ===================== PINS =====================
#define SOIL_MOISTURE_PIN 32
#define RAIN_SENSOR_PIN   34

#define DHTPIN            13
#define DHTTYPE           DHT11

#define RELAY_PIN         26

#define LED_WIFI          15
#define LED_MQTT          4

// 4-IN-1 SENSOR UART
#define PH_RX_PIN         16
#define PH_TX_PIN         17

// ===================== OLED =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===================== OBJECTS =====================
DHT dht(DHTPIN, DHTTYPE);
HardwareSerial phSerial(2);

// ===================== VARIABLES =====================
float pHValue = 6.5;

float Nitrogen = 0;
float Phosphorus = 0;
float Potassium = 0;

float temperature = 0;
float humidity = 0;

int moistureRaw = 0;
int ldrRaw = 0;
int rainRaw = 0;

String rainStatus = "CLEAR";
String pumpStatus = "OFF";
String timeStatus = "DAY";

// OLED Pages
int page = 0;
unsigned long lastSwitch = 0;

// Random correction factors
float RN = 0;
float RP = 0;
float RK = 0;

// ===================== MQTT & WIFI =====================
#define PUBLISH_INTERVAL 5000
#define MQTT_BUFFER_SIZE 2048
#define WIFI_RETRY_DELAY 500
#define MQTT_RETRY_DELAY 5000

const char* WIFI_SSID    = "Arghya";
const char* WIFI_PASS    = "arghya@15";

const char* MQTT_HOST    = "94115c42cfdb4cafbaeab332ee285834.s1.eu.hivemq.cloud";
const int   MQTT_PORT    = 8883;
const char* MQTT_USER    = "Agri-Sense_admin";
const char* MQTT_PASS    = "@agri2026P";

const char* USER_EMAIL   = "arghyaroy1507@gmail.com";

String TOPIC_SENSORS;
String TOPIC_COMMANDS;

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublishTime = 0;
unsigned long lastMqttCheck = 0;

bool manualPumpOverride = false;
unsigned long pumpOverrideTime = 0;

// =====================================================
// FUNCTION PROTOTYPES
// =====================================================
void startupScreen();
void weatherPage();
void soilPage();
void npkPage();
void systemPage();

void connectWifi();
void connectMqtt();
void onMessageReceived(char* topic, byte* payload, unsigned int length);
void processTelemetry();

// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_MQTT, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_MQTT, LOW);

  dht.begin();

  // UART2 for 4-IN-1 SENSOR
  phSerial.begin(9600, SERIAL_8N1, PH_RX_PIN, PH_TX_PIN);

  // OLED START
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED");
    while (1);
  }

  startupScreen();

  TOPIC_SENSORS  = "agrisense/" + String(USER_EMAIL) + "/field_b/sensors";
  TOPIC_COMMANDS = "agrisense/" + String(USER_EMAIL) + "/field_b/commands";

  connectWifi();

  espClient.setInsecure();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(onMessageReceived);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
}

// =====================================================
// LOOP
// =====================================================
void loop() {

  if (!mqttClient.connected()) {
    connectMqtt();
  }

  mqttClient.loop();

  // ================= SENSOR READINGS =================
  moistureRaw = analogRead(SOIL_MOISTURE_PIN);
  rainRaw = analogRead(RAIN_SENSOR_PIN);

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

 // =====================================================
// SAFE UART SENSOR READING
// =====================================================
while (phSerial.available()) {

  byte buffer[20];

  int availableBytes = phSerial.available();

  // Prevent overflow
  if (availableBytes > 20) {
    availableBytes = 20;
  }

  int len = phSerial.readBytes(buffer, availableBytes);

  Serial.print("RAW: ");

  for (int i = 0; i < len; i++) {

    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

  // Example decoding
  if (len >= 6) {

    // pH
    pHValue = buffer[3] / 10.0;

    // Light/LDR
  // Original raw value
// Original UART light value
int rawLight = (buffer[4] << 8) | buffer[5];

// Convert 7500–14000 into 0–4000
ldrRaw = map(rawLight, 7500, 14000, 0, 4000);

// Safety limit
ldrRaw = constrain(ldrRaw, 0, 4000);
  }
}

  // =====================================================
  // LIGHT DETECTION
  // =====================================================
  if (ldrRaw > 3250) {
    timeStatus = "NIGHT";
  }
  else if (ldrRaw >= 2700 && ldrRaw <= 3250) {
    timeStatus = "MORNING";
  }
  else {
    timeStatus = "DAY";
  }

  // ================= RAIN STATUS =================
  if (rainRaw < 2000) {
    rainStatus = "RAIN";
  }
  else {
    rainStatus = "CLEAR";
  }

  // =====================================================
  // SMART PUMP CONTROL
  // =====================================================
  if (manualPumpOverride && (millis() - pumpOverrideTime > 60000)) {
    manualPumpOverride = false;
  }

  if (!manualPumpOverride) {

    if (moistureRaw > 500 || rainRaw > 2000 || (ldrRaw >= 2700 && ldrRaw <= 3250)) {

      digitalWrite(RELAY_PIN, LOW);
      pumpStatus = "ON";
    }
    else {

      digitalWrite(RELAY_PIN, HIGH);
      pumpStatus = "OFF";
    }
  }

  // ================= NPK CALCULATION =================
  Nitrogen = 92 - 7 * pow((pHValue - 6.5), 2) + RN;
  Phosphorus = 72 - 10 * pow((pHValue - 6.5), 2) + RP;
  Potassium = 88 - 6 * pow((pHValue - 6.5), 2) + RK;

  if (Nitrogen < 0) Nitrogen = 0;
  if (Phosphorus < 0) Phosphorus = 0;
  if (Potassium < 0) Potassium = 0;

  // ================= TELEMETRY =================
  unsigned long currentTime = millis();

  if (currentTime - lastPublishTime >= PUBLISH_INTERVAL) {

    lastPublishTime = currentTime;

    processTelemetry();

    Serial.println("=========================");
    Serial.print("pH: ");
    Serial.println(pHValue);

    Serial.print("LDR: ");
    Serial.println(ldrRaw);

    Serial.print("Time: ");
    Serial.println(timeStatus);

    Serial.print("Moisture: ");
    Serial.println(moistureRaw);

    Serial.print("Rain: ");
    Serial.println(rainStatus);

    Serial.print("Pump: ");
    Serial.println(pumpStatus);
  }

  // ================= OLED PAGE SWITCH =================
  if (millis() - lastSwitch > 3000) {

    page++;

    if (page > 3)
      page = 0;

    lastSwitch = millis();
  }

  display.clearDisplay();

  switch (page) {

    case 0:
      weatherPage();
      break;

    case 1:
      soilPage();
      break;

    case 2:
      npkPage();
      break;

    case 3:
      systemPage();
      break;
  }

  display.display();

  delay(100);
}

// =====================================================
// WIFI
// =====================================================
void connectWifi() {

  Serial.println("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {

    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(500);
    Serial.print(".");
  }

  digitalWrite(LED_WIFI, HIGH);

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());
}

// =====================================================
// MQTT
// =====================================================
void connectMqtt() {

  if (millis() - lastMqttCheck > MQTT_RETRY_DELAY) {

    lastMqttCheck = millis();

    String clientId = "AGRI_PRO_" + WiFi.macAddress();
    clientId.replace(":", "");

    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {

      digitalWrite(LED_MQTT, HIGH);

      mqttClient.subscribe(TOPIC_COMMANDS.c_str());

      Serial.println("MQTT Connected");
    }
    else {

      digitalWrite(LED_MQTT, LOW);

      Serial.println("MQTT Failed");
    }
  }
}

// =====================================================
// MQTT MESSAGE
// =====================================================
void onMessageReceived(char* topic, byte* payload, unsigned int length) {

  StaticJsonDocument<256> doc;

  String messageBody = "";

  for (unsigned int i = 0; i < length; i++) {
    messageBody += (char)payload[i];
  }

  deserializeJson(doc, messageBody);

  const char* action = doc["action"] | "NONE";

  if (strcmp(action, "PUMP_ON") == 0) {

    manualPumpOverride = true;
    pumpOverrideTime = millis();

    digitalWrite(RELAY_PIN, LOW);

    pumpStatus = "ON";
  }

  else if (strcmp(action, "PUMP_OFF") == 0) {

    manualPumpOverride = true;
    pumpOverrideTime = millis();

    digitalWrite(RELAY_PIN, HIGH);

    pumpStatus = "OFF";
  }
}

// =====================================================
// TELEMETRY
// =====================================================
void processTelemetry() {

  StaticJsonDocument<1200> doc;

  doc["user_email"] = USER_EMAIL;

  int moisturePercent = map(moistureRaw, 4095, 0, 0, 100);
  int rainPercent = map(rainRaw, 4095, 0, 0, 100);

  JsonObject soil = doc.createNestedObject("soil");

  soil["ph"] = pHValue;
  soil["temp"] = temperature;
  soil["moisture"] = moisturePercent;

  JsonObject npk = soil.createNestedObject("npk");

  npk["n"] = Nitrogen;
  npk["p"] = Phosphorus;
  npk["k"] = Potassium;

  JsonObject weather = doc.createNestedObject("weather");

  weather["temp"] = temperature;
  weather["humidity"] = humidity;
  weather["lightIntensity"] = ldrRaw;
  weather["rainLevel"] = rainPercent;

  JsonObject irrigation = doc.createNestedObject("irrigation");

  irrigation["pump"] = (pumpStatus == "ON") ? "ACTIVE" : "ONLINE";

  JsonObject hardware = doc.createNestedObject("hardware");

  hardware["pump"] = (pumpStatus == "ON") ? "ACTIVE" : "ONLINE";
  hardware["display"] = "ACTIVE";

  doc["node"] = "AgriSense_Pro_Node";
  doc["status"] = "HEALTHY";
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  char payloadBuffer[1200];

  serializeJson(doc, payloadBuffer);

  mqttClient.publish(TOPIC_SENSORS.c_str(), payloadBuffer);
}

// =====================================================
// OLED PAGES
// =====================================================
void startupScreen() {

  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(WHITE);

  display.setCursor(10, 10);
  display.println("AgriSense");

  display.setTextSize(1);

  display.setCursor(28, 38);
  display.println("SMART FARMING");

  display.setCursor(22, 52);
  display.println("Initializing...");

  display.display();

  delay(3000);
}

void weatherPage() {

  display.setTextSize(1);

  display.setCursor(30, 0);
  display.println("WEATHER");

  display.drawLine(0, 10, 128, 10, WHITE);

  display.setCursor(0, 16);
  display.print("Temp : ");
  display.print(temperature);
  display.println(" C");

  display.setCursor(0, 26);
  display.print("Hum  : ");
  display.print(humidity);
  display.println("%");

  display.setCursor(0, 36);
  display.print("Rain : ");
  display.println(rainStatus);

  display.setCursor(0, 46);
  display.print("LDR  : ");
  display.println(ldrRaw);

  display.setCursor(0, 56);
  display.print("Time : ");
  display.println(timeStatus);
}

void soilPage() {

  display.setTextSize(1);

  display.setCursor(35, 0);
  display.println("SOIL");

  display.drawLine(0, 10, 128, 10, WHITE);

  display.setCursor(0, 18);
  display.print("pH    : ");
  display.println(pHValue, 2);

  display.setCursor(0, 30);
  display.print("Moist : ");
  display.println(moistureRaw);

  display.setCursor(0, 42);
  display.print("Pump  : ");
  display.println(pumpStatus);

  display.setCursor(0, 54);
  display.print("Rain  : ");
  display.println(rainStatus);
}

void npkPage() {

  display.setTextSize(1);

  display.setCursor(40, 0);
  display.println("NPK");

  display.drawLine(0, 10, 128, 10, WHITE);

  display.setCursor(0, 18);
  display.print("N : ");
  display.println(Nitrogen, 1);

  display.setCursor(0, 32);
  display.print("P : ");
  display.println(Phosphorus, 1);

  display.setCursor(0, 46);
  display.print("K : ");
  display.println(Potassium, 1);
}

void systemPage() {

  display.setTextSize(1);

  display.setCursor(30, 0);
  display.println("SYSTEM");

  display.drawLine(0, 10, 128, 10, WHITE);

  display.setCursor(0, 16);
  display.print("Time : ");
  display.println(timeStatus);

  display.setCursor(0, 28);
  display.print("LDR  : ");
  display.println(ldrRaw);

  display.setCursor(0, 40);
  display.print("Rain : ");
  display.println(rainStatus);

  display.setCursor(0, 52);
  display.print("Pump : ");
  display.println(pumpStatus);
}
