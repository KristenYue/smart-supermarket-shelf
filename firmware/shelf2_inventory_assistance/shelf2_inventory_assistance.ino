#include <Wire.h>
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <DFRobot_HX711_I2C.h>

#include "secrets.h"
#include "../core/include/smart_shelf/fixed_outbox.hpp"
#include "../core/include/smart_shelf/reconnect_backoff.hpp"
#include "../core/include/smart_shelf/config_version.hpp"
#include "../core/include/smart_shelf/shelf2_controller.hpp"

namespace Pins {
constexpr uint8_t sound = A0;
constexpr uint8_t light = A1;
constexpr uint8_t button = 4;
constexpr uint8_t led = 3;
constexpr uint8_t buzzer = 2;
}

WiFiClient wifiClient;
BearSSLClient tlsClient(wifiClient);
MqttClient mqttClient(tlsClient);
DFRobot_HX711_I2C scale;
smart_shelf::FixedOutbox<4, 96, 512> mqttOutbox;

String telemetryTopic;
String eventsTopic;
String alertsTopic;
String commandTopic;
String configTopic;
smart_shelf::ConfigVersion configVersion;

smart_shelf::Shelf2Thresholds thresholds;
using smart_shelf::ShelfState;

constexpr unsigned long SAMPLE_INTERVAL_MS = 100;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 5000;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

unsigned long lastSampleMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastButtonChangeMs = 0;

int minimumLight = 1023;
int maximumSound = 0;
float weightG = 0.0f;
bool buttonPressed = false;
bool previousRawButton = false;
bool buttonLatched = false;
bool scaleAvailable = false;
bool remoteActuatorOverride = false;
bool remoteActuatorOn = false;
ShelfState currentState = ShelfState::Normal;
ShelfState previousState = ShelfState::Normal;
unsigned long telemetrySequence = 0;
unsigned long bootId = 1;

unsigned long networkTime() {
  return WiFi.getTime();
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

void publishAlert(ShelfState state, bool active) {
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"schemaVersion\":\"2.0\",\"bootId\":" + String(bootId) +
                   ",\"timestamp\":" + String(networkTime()) +
                   ",\"type\":\"" + smart_shelf::to_string(state) +
                   "\",\"active\":" + String(active ? "true" : "false") + "}";
  publishJson(alertsTopic, payload);
}

void beep(uint16_t frequency, uint16_t onMs, uint16_t offMs, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    tone(Pins::buzzer, frequency);
    digitalWrite(Pins::led, HIGH);
    delay(onMs);
    noTone(Pins::buzzer);
    digitalWrite(Pins::led, LOW);
    if (i + 1 < count) delay(offMs);
  }
}

void applyStateActuator(ShelfState state) {
  if (remoteActuatorOverride) {
    noTone(Pins::buzzer);
    digitalWrite(Pins::led, remoteActuatorOn ? HIGH : LOW);
    return;
  }

  noTone(Pins::buzzer);
  digitalWrite(Pins::led, LOW);
  switch (state) {
    case ShelfState::AlertLight:
      digitalWrite(Pins::led, HIGH);
      break;
    case ShelfState::AlertSound:
      beep(1200, 120, 80, 1);
      break;
    case ShelfState::LowStock:
      beep(1000, 180, 0, 1);
      digitalWrite(Pins::led, HIGH);
      break;
    case ShelfState::OutOfStock:
      beep(800, 220, 120, 2);
      digitalWrite(Pins::led, HIGH);
      break;
    case ShelfState::ManualAlarm:
      beep(900, 220, 100, 3);
      digitalWrite(Pins::led, HIGH);
      break;
    default:
      break;
  }
}

void updateButton(unsigned long now) {
  const bool rawPressed = digitalRead(Pins::button) == LOW;
  if (rawPressed != previousRawButton) {
    previousRawButton = rawPressed;
    lastButtonChangeMs = now;
  }
  if (now - lastButtonChangeMs >= BUTTON_DEBOUNCE_MS && rawPressed != buttonPressed) {
    buttonPressed = rawPressed;
    if (buttonPressed) buttonLatched = true;
  }
}

void sampleSensors(unsigned long now) {
  updateButton(now);
  minimumLight = min(minimumLight, analogRead(Pins::light));
  maximumSound = max(maximumSound, analogRead(Pins::sound));
}

void publishTelemetry() {
  weightG = scaleAvailable ? scale.readWeight() : -1.0f;
  if (weightG >= 0.0f && weightG <= 0.5f) weightG = 0.0f;

  const smart_shelf::Shelf2SensorSnapshot snapshot{
      minimumLight, maximumSound, weightG, scaleAvailable, buttonLatched};
  currentState = smart_shelf::evaluate(snapshot, thresholds);
  if (currentState != previousState) {
    if (previousState != ShelfState::Normal) publishAlert(previousState, false);
    applyStateActuator(currentState);
    if (currentState != ShelfState::Normal) publishAlert(currentState, true);
    publishEvent("state_changed", String(smart_shelf::to_string(previousState)) + "->" +
                                      smart_shelf::to_string(currentState));
    previousState = currentState;
  }

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
                   ",\"weightG\":" + String(weightG, 1) +
                   ",\"soundRaw\":" + String(maximumSound) +
                   ",\"lightRaw\":" + String(minimumLight) +
                   ",\"buttonPressed\":" + String(buttonLatched ? "true" : "false") +
                   ",\"state\":\"" + smart_shelf::to_string(currentState) +
                   "\",\"uptimeMs\":" + String(millis()) +
                   ",\"scaleHealthy\":" + String(scaleAvailable ? "true" : "false") + "}";
  publishJson(telemetryTopic, payload);
  Serial.println(payload);

  minimumLight = 1023;
  maximumSound = 0;
  buttonLatched = false;
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
    String command = message;
    command.toUpperCase();
    if (command == "ON") {
      remoteActuatorOverride = true;
      remoteActuatorOn = true;
    } else if (command == "OFF") {
      remoteActuatorOverride = true;
      remoteActuatorOn = false;
    } else if (command == "AUTO") {
      remoteActuatorOverride = false;
    } else if (command == "FLASH") {
      for (uint8_t i = 0; i < 5; ++i) {
        digitalWrite(Pins::led, HIGH); delay(150);
        digitalWrite(Pins::led, LOW); delay(150);
      }
    } else if (command == "ACK") {
      buttonLatched = false;
    }
    applyStateActuator(currentState);
    publishEvent("command_applied", command);
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
    candidate.light_min = static_cast<int>(jsonNumber(message, "lightMin", candidate.light_min));
    candidate.sound_max = static_cast<int>(jsonNumber(message, "soundMax", candidate.sound_max));
    candidate.low_stock_g = jsonNumber(message, "lowStockG", candidate.low_stock_g);
    candidate.out_of_stock_g = jsonNumber(message, "outOfStockG", candidate.out_of_stock_g);
    if (smart_shelf::validate(candidate) == smart_shelf::ThresholdError::None) {
      thresholds = candidate;
      configVersion.effective = requestedVersion;
      publishEvent("configuration_updated", "effectiveVersion=" + String(configVersion.effective));
    } else {
      publishEvent("configuration_rejected", "invalid_thresholds;effectiveVersion=" + String(configVersion.effective));
    }
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
  publishEvent("connected", "Shelf 2 MQTT session established");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(Pins::light, INPUT);
  pinMode(Pins::sound, INPUT);
  pinMode(Pins::button, INPUT_PULLUP);
  pinMode(Pins::led, OUTPUT);
  pinMode(Pins::buzzer, OUTPUT);
  randomSeed(static_cast<unsigned long>(analogRead(Pins::light)) ^
             (static_cast<unsigned long>(analogRead(Pins::sound)) << 10) ^ micros());
  bootId = static_cast<unsigned long>(random(1L, 2147483647L));

  telemetryTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/telemetry";
  eventsTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/events";
  alertsTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/alerts";
  commandTopic = "smartshelf/cmd/" + String(AWS_CLIENT_ID);
  configTopic = "smartshelf/" + String(AWS_CLIENT_ID) + "/config";

  scaleAvailable = scale.begin();
  if (scaleAvailable) {
    scale.setCalWeight(100);
    scale.setThreshold(30);
  } else {
    Serial.println("Warning: HX711 not detected; weight will be reported as -1");
  }

  if (!ECCX08.begin()) {
    Serial.println("ATECC608A not detected");
    while (true) delay(1000);
  }
  ArduinoBearSSL.onGetTime(networkTime);
  tlsClient.setEccSlot(0, DEVICE_CERT);
  mqttClient.onMessage(handleMessage);
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
