#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

#ifndef DEVICE_KEY
#define DEVICE_KEY ""
#endif

// My server
const char *API_URL = "http://iot-ckthompson.mexicocentral.cloudapp.azure.com";

// My MQTT name
const char *MQTT_NAME = "pinkgirl";

// Class broker
const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long RETRY_EVERY_MS = 5000;
const int DHT_PIN = 15;
const unsigned long SEND_EVERY_MS = 10000;
const int LED_PIN = 2;

DHTesp dht;
WiFiClient net;
PubSubClient mqtt(net);

String statusTopic;    // ecse3038/pinkgirl/status
String settingsTopic;  // ecse3038/pinkgirl/fan/settings
String stateTopic;     // ecse3038/pinkgirl/fan/state

unsigned long lastSend = 0;
unsigned long lastAttempt = 0;

String fanMode = "";          // Empty until first settings arrive (B4: fan is off)
float maxHumidity = NAN;
float lastHumidity = NAN;
String reportedState = "";

bool connectWiFi() {
  Serial.print("Connecting to Wokwi-GUEST");
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println("\nNo Wi-Fi after 15 s. Retrying in 5 s.");
      return false;
    }
    delay(250);
    Serial.print(".");
  }

  Serial.println(" connected");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  return true;
}

// B1: Every 10 s POST {"mac", "humidity"} with X-API-Key
void sendReading(float humidity) {
  char body[96];
  snprintf(body, sizeof(body), "{\"mac\": \"%s\", \"humidity\": %.1f}",
           WiFi.macAddress().c_str(), humidity);

  HTTPClient http;
  http.begin(String(API_URL) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  
  int status = http.POST(body);
  Serial.print("POST /readings ");
  Serial.print(body);
  Serial.print(" -> ");
  Serial.println(status);

  if (status == 401) {
    Serial.println("401 Unauthorized: Register MAC via POST /fans and put key in secrets.h");
  }
  http.end();
}

// B4 & B5: Evaluate LED state and publish retained only when state changes
void updateFan() {
  bool turnOn = false;

  if (fanMode == "on") {
    turnOn = true;
  } else if (fanMode == "off") {
    turnOn = false;
  } else if (fanMode == "auto") {
    if (!isnan(maxHumidity) && !isnan(lastHumidity) && lastHumidity > maxHumidity) {
      turnOn = true;
    }
  }

  digitalWrite(LED_PIN, turnOn ? HIGH : LOW);
  String state = turnOn ? "on" : "off";

  if (state != reportedState && mqtt.connected()) {
    mqtt.publish(stateTopic.c_str(), state.c_str(), true);
    reportedState = state;
    Serial.println("Published fan/state: " + state);
  }
}

// B3: Parse JSON settings, refuse invalid messages, react immediately (B4)
void onMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  if (String(topic) == settingsTopic) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
      Serial.println("Invalid JSON ignored");
      return;
    }

    if (!doc["mode"].is<String>() || !doc["max_humidity"].is<float>()) {
      Serial.println("Missing fields ignored");
      return;
    }

    String newMode = doc["mode"].as<String>();
    if (newMode != "auto" && newMode != "on" && newMode != "off") {
      Serial.println("Invalid mode ignored");
      return;
    }

    fanMode = newMode;
    maxHumidity = doc["max_humidity"].as<float>();
    Serial.print("Applied settings -> mode: ");
    Serial.print(fanMode);
    Serial.print(", max_humidity: ");
    Serial.println(maxHumidity);

    updateFan(); // React immediately
  }
}

// B2: Connect with retained Last Will "offline", publish retained "online"
bool connectMqtt() {
  String clientId = "esp32-" + String(MQTT_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  
  bool ok = mqtt.connect(
    clientId.c_str(),
    statusTopic.c_str(),
    1,
    true,
    "offline"
  );

  if (!ok) {
    Serial.print("MQTT failed, state: ");
    Serial.println(mqtt.state());
    return false;
  }

  Serial.println("Connected to broker");
  mqtt.publish(statusTopic.c_str(), "online", true);
  mqtt.subscribe(settingsTopic.c_str());

  reportedState = "";
  return true;
}

void setup() {
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Off until first settings arrive

  String base = "ecse3038/" + String(MQTT_NAME);
  statusTopic = base + "/status";
  settingsTopic = base + "/fan/settings";
  stateTopic = base + "/fan/state";

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(15);

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= RETRY_EVERY_MS) {
      lastAttempt = millis();
      connectWiFi();
    }
    return;
  }

  if (mqtt.connected()) {
    mqtt.loop();
  } else if (millis() - lastAttempt >= RETRY_EVERY_MS) {
    lastAttempt = millis();
    connectMqtt();
  }

  if (millis() - lastSend < SEND_EVERY_MS) {
    return;
  }
  lastSend = millis();

  float humidity = dht.getHumidity();
  if (isnan(humidity)) {
    Serial.println("No reading from DHT22");
    return;
  }

  lastHumidity = humidity;
  sendReading(humidity);
  updateFan();
}