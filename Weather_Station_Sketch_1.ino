#include "Adafruit_VEML7700.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SparkFun_VEML6075_Arduino_Library.h>

#define TCAADDR 0x70

Adafruit_VEML7700 veml;
Adafruit_BME280 bme;
VEML6075 uv;

unsigned long delayTime;

void tcaselect(uint8_t i) {
  if (i > 7) return;

  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println(F("BME280 VEML7700 VEML6075 Test"));

  unsigned status;

  status = bme.begin();
  Wire.begin();
  if (!status) {
    Serial.println("Unable to communicate with BME280");
    while (1) delay(10);
  }
  tcaselect(1);
  if (uv.begin() == false)
  {
    Serial.println("Unable to communicate with VEML6075.");
    while (1)
      ;
  }
  tcaselect(0);
  if (!veml.begin()) {
    Serial.println("Unable to communicate with VEML7700");
    while (1);
  }

  veml.setGain(VEML7700_GAIN_1_8);
  veml.setIntegrationTime(VEML7700_IT_800MS);

}


void loop() {
  Serial.print("Temperature = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");

  Serial.print("Pressure = ");
  Serial.print((bme.readPressure() / 100.0F) * 0.03);
  Serial.println(" inHg");

  Serial.print("Humidity = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");
  delay(500);
  tcaselect(1);
  Serial.println(String(uv.uva()) + ", " + String(uv.uvb()) + ", " + String(uv.index()));
  delay(500);
  tcaselect(0);
  Serial.print("Lux: "); Serial.println(veml.readLux());
  Serial.print("White: "); Serial.println(veml.readWhite());

  Serial.println();

  delay(5000);
}
