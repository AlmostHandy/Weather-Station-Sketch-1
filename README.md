# Weather-Station-Sketch-1
Arduino Uno R3 weather station test with Adafruit VEML7700, Adafruit BME280, Adafruit TCA9548A, and Sparkfun VEML6075

Weather Station powered by Arduino Uno R3. Temperature, humidity, and barometric pressure measured with BME280. UVA, UVB, and UV Index measured with VEML6075. LUX and White Light measured with VEML7700. 

VEML7700 and VEML6075 have the same i2C address and it cannot be changed so this project uses the TCA9548A i2C multiplexer.

Output to Serial Monitor at 115.2k Baud
