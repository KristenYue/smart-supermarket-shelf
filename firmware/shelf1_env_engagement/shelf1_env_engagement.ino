#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <Arduino_MKRENV.h>

#include "secrets.h"
#include "../core/include/smart_shelf/fixed_outbox.hpp"
#include "../core/include/smart_shelf/reconnect_backoff.hpp"
#include "../core/include/smart_shelf/config_version.hpp"

namespace Pins {
constexpr uint8_t ultrasonic = 5;
constexpr uint8_t light = A1;
constexpr uint8_t led = LED_BUILTIN;
}

WiFiClient wifiClient;
BearSSLClient tlsClient(wifiClient);
MqttClient mqttClient(tlsClient);
smart_shelf::FixedOutbox<4, 96, 512> mqttOutbox;

String telemetryTopic;
String eventsTopic;
String alertsTopic;
String commandTopic;
String configTopic;
smart_shelf::ConfigVersion configVersion;

struct Thresholds {
  float tempMinC = 2.0f;
  float tempMaxC = 8.0f;
  float humidityMinPct = 30.0f;
  float humidityMaxPct = 70.0f;
  float customerDistanceCm = 80.0f;
  int lightChange = 60;
} thresholds;

constexpr unsigned long SAMPLE_INTERVAL_MS = 1000;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 5000;
constexpr unsigned long ENV_ALERT_DELAY_MS = 30000;
constexpr unsigned long PRESENCE_TIMEOUT_MS = 2000;
constexpr unsigned long INTERACTION_COOLDOWN_MS = 1500;

unsigned long lastSampleMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long environmentBadSinceMs = 0;
unsigned long lastPresenceSeenMs = 0;
unsigned long presenceStartedMs = 0;
unsigned long lastInteractionMs = 0;
unsigned long totalDwellTimeMs = 0;

bool environmentOutOfRange = false;
bool environmentAlertActive = false;
bool customerPresent = false;
bool remoteLedOverride = false;
bool remoteLedOn = false;
int interactionCount = 0;
int visitorCount = 0;
int lastLightValue = 0;
unsigned long telemetrySequence = 0;
unsigned long bootId = 1;

float temperatureC = NAN;
float humidityPct = NAN;
float pressureKPa = NAN;
float distanceCm = -1.0f;
int lightRaw = 0;

unsigned long networkTime() {
  return WiFi.getTime();
}

String jsonNumber(float value, uint8_t decimals) {
  return isnan(value) ? "null" : String(value, decimals);
}

String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);
  for (unsigned int i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c != '\r') {
      out += c;
    }
  }
  return out;
}

bool sendJson(const char *topic, const char *payload, bool retained, int qos) {
  if (!mqttClient.connected()) return false;
  mqttClient.beginMessage(topic, strlen(payload), retained, qos);
  mqttClient.print(payload);
  return mqttClient.endMessage() == 1;
}

void publishJson(const String &topic, const String &payload, bool retained = false) {
  if (sendJson(topic.c_str(), payload.c_str(), retained, 1)) return;
  const auto result = mqttOutbox.push(topic.c_str(), payload.c_str(), retained, 1);
  if (result == smart_shelf::EnqueueResult::RejectedOversize) {
    Serial.println("MQTT outbox rejected an oversized frame");
  }
}

void flushOutbox() {
  while (mqttClient.connected() && mqttOutbox.front() != nullptr) {
    const auto *frame = mqttOutbox.front();
    if (!sendJson(frame->topic.data(), frame->payload.data(), frame->retained, frame->qos)) {
      return;
    }
    mqttOutbox.pop();
  }
}

void publishEvent(const char *eventName, const String &detail) {
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"schemaVersion\":\"2.0\",\"bootId\":" + String(bootId) +
                   ",\"timestamp\":" + String(networkTime()) +
                   ",\"event\":\"" + eventName +
                   "\",\"detail\":\"" + jsonEscape(detail) + "\"}";
  publishJson(eventsTopic, payload);
}

void publishAlert(const char *type, const String &detail, bool active) {
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"schemaVersion\":\"2.0\",\"bootId\":" + String(bootId) +
                   ",\"timestamp\":" + String(networkTime()) +
                   ",\"type\":\"" + type +
                   "\",\"active\":" + String(active ? "true" : "false") +
                   ",\"detail\":\"" + jsonEscape(detail) + "\"}";
  publishJson(alertsTopic, payload);
}

float readDistanceCmOnce() {
  pinMode(Pins::ultrasonic, OUTPUT);
  digitalWrite(Pins::ultrasonic, LOW);
  delayMicroseconds(2);
  digitalWrite(Pins::ultrasonic, HIGH);
  delayMicroseconds(5);
  digitalWrite(Pins::ultrasonic, LOW);
  pinMode(Pins::ultrasonic, INPUT);

  const unsigned long durationUs = pulseIn(Pins::ultrasonic, HIGH, 30000UL);
  return durationUs == 0 ? -1.0f : durationUs / 58.0f;
}

float readDistanceCmAveraged() {
  float total = 0.0f;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    const float reading = readDistanceCmOnce();
    if (reading >= 2.0f && reading <= 350.0f) {
      total += reading;
      ++valid;
    }
    delay(25);
  }
  return valid == 0 ? -1.0f : total / valid;
}

void updateLed() {
  const bool ledOn = remoteLedOverride ? remoteLedOn : environmentAlertActive;
  digitalWrite(Pins::led, ledOn ? HIGH : LOW);
}

void processEnvironment(unsigned long now) {
  if (isnan(temperatureC) || isnan(humidityPct)) return;
  const bool bad = temperatureC < thresholds.tempMinC ||
                   temperatureC > thresholds.tempMaxC ||
                   humidityPct < thresholds.humidityMinPct ||
                   humidityPct > thresholds.humidityMaxPct;

  if (bad) {
    if (!environmentOutOfRange) {
      environmentOutOfRange = true;
      environmentBadSinceMs = now;
    }
    if (!environmentAlertActive && now - environmentBadSinceMs >= ENV_ALERT_DELAY_MS) {
      environmentAlertActive = true;
      updateLed();
      publishAlert("ALERT_ENV",
                   "Temperature or humidity remained outside the configured range",
                   true);
    }
  } else {
    environmentOutOfRange = false;
    environmentBadSinceMs = 0;
    if (environmentAlertActive) {
      environmentAlertActive = false;
      updateLed();
      publishAlert("ALERT_ENV", "Environment returned to normal", false);
      publishEvent("environment_recovered", "Temperature and humidity are in range");
    }
  }
}

void processEngagement(unsigned long now) {
  const bool nearShelf = distanceCm > 0 && distanceCm <= thresholds.customerDistanceCm;
  if (nearShelf) {
    lastPresenceSeenMs = now;
    if (!customerPresent) {
      customerPresent = true;
      ++visitorCount;
      presenceStartedMs = now;
      publishEvent("customer_arrived", "Customer entered the detection zone");
    }
  } else if (customerPresent && now - lastPresenceSeenMs > PRESENCE_TIMEOUT_MS) {
    customerPresent = false;
    const unsigned long visitMs = now - presenceStartedMs;
    totalDwellTimeMs += visitMs;
    publishEvent("customer_left", "dwellMs=" + String(visitMs));
  }

  const int lightDifference = abs(lightRaw - lastLightValue);
  if (customerPresent && lightDifference >= thresholds.lightChange &&
      now - lastInteractionMs >= INTERACTION_COOLDOWN_MS) {
    ++interactionCount;
    lastInteractionMs = now;
    publishEvent("product_interaction",
                 "count=" + String(interactionCount) + ",lightDifference=" + String(lightDifference));
  }
  lastLightValue = lightRaw;
}

void sampleSensors(unsigned long now) {
  temperatureC = ENV.readTemperature();
  humidityPct = ENV.readHumidity();
  pressureKPa = ENV.readPressure();
  distanceCm = readDistanceCmAveraged();
  lightRaw = analogRead(Pins::light);
  processEnvironment(now);
  processEngagement(now);
}

void publishTelemetry() {
  const unsigned long sequence = ++telemetrySequence;
  const unsigned long timestamp = networkTime();
  const bool timeSynchronized = timestamp >= 1609459200UL;
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"schemaVersion\":\"2.0\",\"messageId\":\"" +
                   String(AWS_CLIENT_ID) + ":" + String(bootId) + ":" + String(sequence) +
                   "\",\"bootId\":" + String(bootId) +
                   ",\"sequence\":" + String(sequence) +
                   ",\"timestamp\":" + String(timeSynchronized ? timestamp : 0) +
                   ",\"timeSynchronized\":" +
                   String(timeSynchronized ? "true" : "false") +
                   ",\"temperatureC\":" + jsonNumber(temperatureC, 2) +
                   ",\"humidityPct\":" + jsonNumber(humidityPct, 2) +
                   ",\"pressureKPa\":" + jsonNumber(pressureKPa, 2) +
                   ",\"distanceCm\":" + String(distanceCm, 2) +
                   ",\"lightRaw\":" + String(lightRaw) +
                   ",\"customerPresent\":" + String(customerPresent ? "true" : "false") +
                   ",\"totalDwellTimeSec\":" + String(totalDwellTimeMs / 1000UL) +
                   ",\"visitorCount\":" + String(visitorCount) +
                   ",\"interactionCount\":" + String(interactionCount) +
                   ",\"environmentAlert\":" + String(environmentAlertActive ? "true" : "false") + "}";
  publishJson(telemetryTopic, payload);
  Serial.println(payload);
}

float jsonNumber(const String &json, const char *key, float fallback) {
  const String marker = "\"" + String(key) + "\":";
  const int start = json.indexOf(marker);
  if (start < 0) return fallback;
  return json.substring(start + marker.length()).toFloat();
}

void handleMessage(int messageSize) {
  (void)messageSize;
  const String topic = mqttClient.messageTopic();
  String message;
  while (mqttClient.available()) message += static_cast<char>(mqttClient.read());
  message.trim();

  if (topic == commandTopic) {
    message.toUpperCase();
    if (message == "ON") {
      remoteLedOverride = true;
      remoteLedOn = true;
    } else if (message == "OFF") {
      remoteLedOverride = true;
      remoteLedOn = false;
    } else if (message == "AUTO") {
      remoteLedOverride = false;
    } else if (message == "FLASH") {
      for (uint8_t i = 0; i < 5; ++i) {
        digitalWrite(Pins::led, HIGH); delay(150);
        digitalWrite(Pins::led, LOW); delay(150);
      }
    }
    updateLed();
    publishEvent("command_applied", message);
    return;
  }

  if (topic == configTopic) {
    if (message == "GET") {
      publishEvent("configuration_status", "effectiveVersion=" + String(configVersion.effective));
      return;
    }
    uint32_t requestedVersion = 0;
    const auto decision = configVersion.check(message.c_str(), requestedVersion);
    if (decision != smart_shelf::VersionDecision::Apply) {
      publishEvent("configuration_rejected",
                   String(decision == smart_shelf::VersionDecision::Stale ? "stale_or_duplicate" : "invalid_version") +
                   ";effectiveVersion=" + String(configVersion.effective));
      return;
    }
    auto candidate = thresholds;
    candidate.tempMinC = jsonNumber(message, "tempMinC", candidate.tempMinC);
    candidate.tempMaxC = jsonNumber(message, "tempMaxC", candidate.tempMaxC);
    candidate.humidityMinPct = jsonNumber(message, "humidityMinPct", candidate.humidityMinPct);
    candidate.humidityMaxPct = jsonNumber(message, "humidityMaxPct", candidate.humidityMaxPct);
    candidate.customerDistanceCm = jsonNumber(message, "customerDistanceCm", candidate.customerDistanceCm);
    candidate.lightChange = static_cast<int>(jsonNumber(message, "lightChange", candidate.lightChange));
    if (!(candidate.tempMinC < candidate.tempMaxC &&
          candidate.humidityMinPct >= 0 && candidate.humidityMaxPct <= 100 &&
          candidate.humidityMinPct < candidate.humidityMaxPct &&
          candidate.customerDistanceCm > 0 && candidate.lightChange >= 0)) {
      publishEvent("configuration_rejected", "invalid_thresholds;effectiveVersion=" + String(configVersion.effective));
      return;
    }
    thresholds = candidate;
    configVersion.effective = requestedVersion;
    publishEvent("configuration_updated", "effectiveVersion=" + String(configVersion.effective));
  }
}

bool connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectMqtt() {
  if (mqttClient.connected()) return true;
  mqttClient.setId(AWS_CLIENT_ID);
  if (!mqttClient.connect(AWS_IOT_ENDPOINT, AWS_IOT_PORT)) {
    Serial.print("MQTT connection failed: ");
    Serial.println(mqttClient.connectError());
    return false;
  }
  mqttClient.subscribe(commandTopic);
  mqttClient.subscribe(configTopic);
  flushOutbox();
  publishEvent("connected", "Shelf 1 MQTT session established");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(Pins::led, OUTPUT);
  digitalWrite(Pins::led, LOW);
  randomSeed(static_cast<unsigned long>(analogRead(Pins::light)) ^ micros());
  bootId = static_cast<unsigned long>(random(1L, 2147483647L));

  telemetryTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/telemetry";
  eventsTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/events";
  alertsTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/alerts";
  commandTopic = "smartshelf/cmd/" + String(AWS_CLIENT_ID);
  configTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/config";

  if (!ENV.begin()) {
    Serial.println("MKR ENV Shield not detected");
    while (true) delay(1000);
  }
  if (!ECCX08.begin()) {
    Serial.println("ATECC608A not detected");
    while (true) delay(1000);
  }

  ArduinoBearSSL.onGetTime(networkTime);
  tlsClient.setEccSlot(0, DEVICE_CERT);
  mqttClient.onMessage(handleMessage);
  lastLightValue = analogRead(Pins::light);
}

void loop() {
  static smart_shelf::ReconnectBackoff reconnect;
  const unsigned long now = millis();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    sampleSensors(now);
  }
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }

  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    reconnect.connected();
    mqttClient.poll();
    flushOutbox();
  } else if (reconnect.ready(millis())) {
    if (connectWiFi() && networkTime() >= 1609459200UL && connectMqtt()) {
      reconnect.connected();
    } else {
      reconnect.failed(millis());
    }
  }
}
