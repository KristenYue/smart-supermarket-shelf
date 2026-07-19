#include <DFRobot_HX711_I2C.h>

DFRobot_HX711_I2C scale;

void setup() {
  Serial.begin(115200);
  delay(1000);
  while (!scale.begin()) {
    Serial.println("HX711 initialization failed; check power and I2C wiring");
    delay(1000);
  }
  scale.setCalWeight(100);
  scale.setThreshold(30);
  Serial.print("Calibration value: ");
  Serial.println(scale.getCalibration());
}

void loop() {
  float grams = scale.readWeight();
  if (grams <= 0.5f) grams = 0.0f;
  Serial.print("Weight: ");
  Serial.print(grams, 1);
  Serial.println(" g");
  delay(1000);
}

