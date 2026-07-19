#include <Wire.h>
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <DFRobot_HX711_I2C.h>

#include "secrets.h"

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

String telemetryTopic;
String eventsTopic;
String alertsTopic;
String commandTopic;
String configTopic;

struct Thresholds {
  int lightMin = 100;
  int soundMax = 150;
  float lowStockG = 25.0f;
  float outOfStockG = 1.0f;
} thresholds;

enum ShelfState {
  NORMAL,
  ALERT_LIGHT,
  ALERT_SOUND,
  LOW_STOCK,
  OUT_OF_STOCK,
  MANUAL_ALARM
};

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
ShelfState currentState = NORMAL;
ShelfState previousState = NORMAL;

unsigned long networkTime() {
  return WiFi.getTime();
}

const char *stateName(ShelfState state) {
  switch (state) {
    case ALERT_LIGHT: return "ALERT_LIGHT";
    case ALERT_SOUND: return "ALERT_SOUND";
    case LOW_STOCK: return "LOW_STOCK";
    case OUT_OF_STOCK: return "OUT_OF_STOCK";
    case MANUAL_ALARM: return "MANUAL_ALARM";
    default: return "NORMAL";
  }
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

void publishJson(const String &topic, const String &payload, bool retained = false) {
  if (!mqttClient.connected()) return;
  mqttClient.beginMessage(topic, payload.length(), retained, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
}

void publishEvent(const char *eventName, const String &detail) {
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"timestamp\":" + String(networkTime()) +
                   ",\"event\":\"" + eventName +
                   "\",\"detail\":\"" + jsonEscape(detail) + "\"}";
  publishJson(eventsTopic, payload);
}

void publishAlert(ShelfState state, bool active) {
  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"timestamp\":" + String(networkTime()) +
                   ",\"type\":\"" + stateName(state) +
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
    case ALERT_LIGHT:
      digitalWrite(Pins::led, HIGH);
      break;
    case ALERT_SOUND:
      beep(1200, 120, 80, 1);
      break;
    case LOW_STOCK:
      beep(1000, 180, 0, 1);
      digitalWrite(Pins::led, HIGH);
      break;
    case OUT_OF_STOCK:
      beep(800, 220, 120, 2);
      digitalWrite(Pins::led, HIGH);
      break;
    case MANUAL_ALARM:
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

ShelfState decideState() {
  // Immediate human requests and abnormal physical events take priority.
  if (buttonLatched) return MANUAL_ALARM;
  if (minimumLight < thresholds.lightMin) return ALERT_LIGHT;
  if (maximumSound > thresholds.soundMax) return ALERT_SOUND;
  if (scaleAvailable && weightG <= thresholds.outOfStockG) return OUT_OF_STOCK;
  if (scaleAvailable && weightG <= thresholds.lowStockG) return LOW_STOCK;
  return NORMAL;
}

void publishTelemetry() {
  weightG = scaleAvailable ? scale.readWeight() : -1.0f;
  if (weightG >= 0.0f && weightG <= 0.5f) weightG = 0.0f;

  currentState = decideState();
  if (currentState != previousState) {
    if (previousState != NORMAL) publishAlert(previousState, false);
    applyStateActuator(currentState);
    if (currentState != NORMAL) publishAlert(currentState, true);
    publishEvent("state_changed", String(stateName(previousState)) + "->" + stateName(currentState));
    previousState = currentState;
  }

  String payload = "{\"deviceID\":\"" + String(AWS_CLIENT_ID) +
                   "\",\"timestamp\":" + String(networkTime()) +
                   ",\"weightG\":" + String(weightG, 1) +
                   ",\"soundRaw\":" + String(maximumSound) +
                   ",\"lightRaw\":" + String(minimumLight) +
                   ",\"buttonPressed\":" + String(buttonLatched ? "true" : "false") +
                   ",\"state\":\"" + stateName(currentState) +
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
    thresholds.lightMin = static_cast<int>(jsonNumber(message, "lightMin", thresholds.lightMin));
    thresholds.soundMax = static_cast<int>(jsonNumber(message, "soundMax", thresholds.soundMax));
    thresholds.lowStockG = jsonNumber(message, "lowStockG", thresholds.lowStockG);
    thresholds.outOfStockG = jsonNumber(message, "outOfStockG", thresholds.outOfStockG);
    publishEvent("configuration_updated", "Shelf 2 thresholds updated");
  }
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.print("Connecting to Wi-Fi");
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    if (WiFi.begin(SECRET_SSID, SECRET_PASS) == WL_CONNECTED) {
      Serial.println(" connected");
      for (uint8_t timeAttempt = 0; timeAttempt < 20 && networkTime() < 1609459200UL; ++timeAttempt) {
        delay(500);
      }
      return true;
    }
    Serial.print('.');
    delay(2000);
  }
  Serial.println(" failed");
  return false;
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
  publishEvent("boot", "Shelf 2 connected");
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
  static unsigned long nextConnectionAttemptMs = 0;
  const unsigned long now = millis();

  if ((WiFi.status() != WL_CONNECTED || !mqttClient.connected()) && now >= nextConnectionAttemptMs) {
    if (!connectWiFi() || !connectMqtt()) nextConnectionAttemptMs = now + 5000;
  }
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;
  mqttClient.poll();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    sampleSensors(now);
  }
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
