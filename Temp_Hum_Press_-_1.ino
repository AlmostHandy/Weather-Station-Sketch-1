

/*
The AlmostHandy Weather Station BME280 Test Sketch
You can find all related files at https://github.com/AlmostHandy

Original Code written by 
Jay Ham
Colorado State University
jay.ham@colostate.edu
You can find his original code at https://gist.github.com/ProfJayHam/7792782

Weather station data logger Program
Reads and logs all primary weather variables, including:
Air Temperature, Ground Temperature (at 4 inch depth), Humidity, Barometric Pressure, Wind Speed, 
Wind Direction, Lux Meter, UV Meter, Lightning Detection, and Precipitation. 
Samples sensors every 10 seconds and stores 5 minute statistics on sd card
Dewpoint temperaure, the wind vector, and the battery voltage are calculated & logged. 

Main Components:
Arduino Uno R3
Adafruit #1141 Datalogger shield
Sparkfun #SEN-15089 VEML6075 UV Sensor
Adafruit #4162 VEML7700 Lux Sensor
Adafruit #2652 BME280 Temp/Humi/Press Sensor
Sparkfun #SEN-15441 AS3935 Lightning Sensor
Adafruit #381 DS18B20 Waterproof Temp Sensor
Sparkfun #SEN-15901 Anemometer, Wind Vane, and Rain Guage
 
Sources:
code for anemometer modified from sections of arduwind project, https://code.google.com/p/arduwind/
as written by Thierry Brunet de Courssou
The code for the wind vector calculation was adapted from that used by the National Climatic Data Center 
http://www.intellovations.com/2011/01/16/wind-observation-calculations-in-fortran-and-python/

============================
 Anemometer notes
============================
Description:
Measures wind speed and direction and output to serial monitor

The AlmostHandy Weather Station uses the Sparkfun Weather Meter Kit. It includes
a cup anemometer, wind vane, and tipping bucket rain guage.

Circuits for ESD protection, and to debounce the anemometer and rain gauge have been built on
a section of Adafruit Bakelite Universal Protoboard (#2670) and installed as a backpack on the 
Datalogger shield. It was created with Fritzing, and the files are available at https://github.com/AlmostHandy

Debounce Circuit:
The the pulse from the cup anemometer and the Rain Guage must be debounced. The debounce circuit from
Professor Ham's weather station was modified for use with the Argent Data Systems devices.
Components: 10K pullup resistor to 5V,
2.2 uF capacitor (poly film) across the reed switch
330 ohm series resistor to digital input port 

Wiring:
Wind Vane to A2 
Anemometer Pulse to D2, interrupt 0
The rain gauges signal is debounced with the same circuit used 
for the anemometer and connected to D3, intterupt 1

======================================
i2C Sensor Notes
Adafruit #2899 VEML6070 UV Sensor
Adafruit #4162 VEML7700 Lux Sensor
Adafruit #2652 BME280 Temp/Humi/Press Sensor
Sparkfun #SEN-15441 AS3935 Lightning Sensor
======================================
All sensors connect to headers on the proto board. 

Red: VCC (5 V)
Blk: Ground
Blue: Data
Green: SCK

======================================
*/
#include <Wire.h>
#include <OneWire.h>
#include <Adafruit_BME280.h>
//#include <SparkFun_AS3935.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_VEML6070.h>
#include <math.h>  // need to calc wind vector
#include "RTClib.h"
#include <SdFat.h>
#include "Statistic.h"
#include <Adafruit_ADS1015.h>
#include <MemoryFree.h>
//#include <avr/pgmspace.h>

// ===== user defined variables ==========================
#define REQUEST_RATE 5000 // in milliseconds - sample rate, 5000 default
#define LOG_RATE 5 // in minutes, interval to calc stats and store on SD

#define vanePin 2                // analog pin for wind vane output, A2 default
const float WindTo_mph = 1.49;   // Davis = 2.25, Inspeed = 2.5, Novalynx = 1.25; unique
const byte dataPin =  4;         // SHTxx serial data
const byte sclkPin =  5; 

const int led_1_Pin =  6;       // main sample loop indicator, green
const int led_2_Pin =  7;       // SD card error indicator, red 
int led_1_State = LOW; 
int led_2_State = LOW;

// user defined filename and header
char filename[] = "LogAD_00.csv";  // filename must be 6 char 2 zeros
//char header[] = "ID,Unixtime,Datetime,avgTemp,avgTdew,samRH,avgSpd,avgDir,sdevDir,vectDir,vectSpd,vectn,avgRs";
//const char header[] PROGMEM = "ID,Unix,time,avgT,avgdew,RH,avgSpd,avgDir,sdvDir,vDir,vSpd,vn,avgRs,battV";
 
//#define aref_voltage 3300

#define ECHO_TO_SERIAL 0 // echo data to serial port, set to 0 to stop all serial.print cmds

// ===== end user defined variables =====

unsigned long lastupdate = 0;  // timer value when last  update was done
uint32_t timer = 0;            // a local timer

// Anemometer section

unsigned long PulseTimeNow = 0; // Time stamp (in millisecons) for pulse triggering the interrupt

float WindSpeed_mps, WindSpeed_mph,WindSpeed_cnt,WindSpeed_rpm,WindSpeed_Hz;
volatile unsigned long PulseTimeLast = 0; // Time stamp of the previous pulse
volatile unsigned long PulseTimeInterval = 0;; // Time interval since last pulse

volatile unsigned long PulsesCumulatedTime = 0; // Time Interval since last wind speed computation 
volatile unsigned long PulsesNbr = 0;           // Number of pulses since last wind speed computation 
volatile unsigned long LastPulseTimeInterval = 1000000000;
volatile unsigned long MinPulseTimeInterval = 1000000000;
float MaxWind = 0.0; // Max wind speed 
float WindGust = 0.0;

// vane
  unsigned int WindDirection = 0; // value from the ADC
  float DirectionVolt = 0.0; // voltage read from the vane output

// wind vector
float rx,ry,rrdd;
float r_dir,r_spd;
long n_rwd;

// RH and temp sensor
Adafruit_BME280 bme; // I2C
Sensirion sht = Sensirion(dataPin, sclkPin);
unsigned int rawData;
float temperature;
float humidity;
float dewpoint;
byte measActive = false;
byte measType = TEMP;

Adafruit_ADS1115 ads1015; // initalize 16bit ADC
int16_t solar_raw;    // raw result from ADC connected to Radiometer
float solar_volt, solar_Wm2;

// Raingauge, tipping bucket
volatile unsigned long RainNbr = 0;           // Number of pulses since last rain tip
unsigned long totRain = 0;

//coef for volt divider on Vin/battery, board specific
const float voltdivide = 4.09; 
double battVolt ;
double aux;
double Vcc;

// RTC clock
RTC_DS1307 RTC;

//SD Card
SdFat sd;
SdFile logfile;
const int chipSelect = 10; //  CS  for SD card data logging shield

//Statistics
// define statistic object for each variable
Statistic TStats; // Temperature, C
Statistic DStats; // dewpoint, C
Statistic UStats; // windspeed, m/s
Statistic CStats; // wind direction, deg
Statistic xStats; // wind vector, x
Statistic yStats; // wind vector, y
Statistic RStats; // Global Irradiance - pyranometer, W/m2
Statistic AStats; // auxillary data
float avgTemp; 
float avgTdew; 
float avgSpd;
float avgDir;
float sdDir;
float totrx,totry;
float avgRs;
float minSpd;
float maxSpd;
float avgAux; 

// Flow control
int FirstLoop = 0;
int RecNum = 0;
byte output_flg_on = false; // goes high when all data ready to report
int oldminute = 99;

void setup()
{
  Serial.begin(9600);
   // Anemometer Interrupt setup 
  // anemometer is assigned to interrupt 0 on digital pin D2
  attachInterrupt(0, AnemometerPulse, FALLING);
  PulseTimeLast = micros();
 
  // rain gauge is assigned to interrupt 1 on digital pin D3
  attachInterrupt(1, RainPulse, FALLING);
 
  Wire.begin();
  RTC.begin();
  
  ads1015.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  
  ads1015.begin();
  
   // see if the card is present and can be initialized:
  if (!sd.begin(chipSelect,SPI_HALF_SPEED)) {
    #if ECHO_TO_SERIAL
      Serial.print("SD card not detected or initalized");
    #endif  
    digitalWrite(led_2_Pin, HIGH);
    sd.initErrorHalt();
    return;
    }
  
  // create a new file name for each reset/start
    for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i/10 + '0';
    filename[7] = i%10 + '0';
    if (! sd.exists(filename)) {
       break;  // leave the loop!
       }
    } 
    if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
    #if ECHO_TO_SERIAL
      Serial.print("SD file open failed");
    #endif  
    digitalWrite(led_2_Pin, HIGH);
    sd.errorHalt();
    
  }
  
  //print header
  logfile.println(F("ID,Unix,time,avgT,RH,avgdew,avgRs,avgSpd,vDir,sdvDir,rain,maxSpd,minSpd,gust,battV,vSpd,avgDir,vn,aux"));
  
  // close the file:
  logfile.close();
 
  //clear data arrays
   TStats.clear();
   DStats.clear();
   UStats.clear();
   CStats.clear();
   xStats.clear();
   yStats.clear();
   RStats.clear();
   //A1Stats.clear();
   AStats.clear();
   
 pinMode(led_1_Pin, OUTPUT);
 pinMode(led_2_Pin, OUTPUT);
  
 //analogReference(EXTERNAL); 
  
#if ECHO_TO_SERIAL 
 Serial.print(F("setup done"));
#endif
} // end setup

// -- MAIN LOOP
void loop()  
{  
  DateTime now;
  now = RTC.now();
  int j = 0;
  
//----------------------------------------------------------------------  
// Sample at REQUEST_RATE, default = 5 seconds
//----------------------------------------------------------------------
  if ( ( millis()-lastupdate ) > REQUEST_RATE )
    {
  
      lastupdate = millis();
  timer = lastupdate;
  j++;
  
        AnemometerLoop();   // Get Anemometer data 
        
        solar_raw = ads1015.readADC_Differential_0_1(); // Get Radiometer data
        solar_volt = solar_raw*0.015625;
        solar_Wm2 = solar_volt*mVTo_Wm2;
        if (solar_Wm2 <0.5) solar_Wm2=0.0;
        RStats.add(solar_Wm2);
        
        measActive = true; // Start RH/T measurement, SHT15
        measType = TEMP;
        sht.meas(TEMP, &rawData, NONBLOCK);        // Start temp measurement
  if (FirstLoop <= 2) { return; } // Discard first sets of data to make sure you get clean data
 
       //blink LED every other pass through main loop
       if (led_1_State == LOW)
         led_1_State = HIGH;
       else
         led_1_State = LOW;
       digitalWrite(led_1_Pin, led_1_State);  //flash Green LED on and off every 5 s
    }
   
   RHTdataCheck() ;  // check RH/T data 
   
   Vcc = readVcc();  // get rail voltage
   battVolt = analogRead(0)*4.09/1023.0*Vcc;  // read Vin on A0 for battery 
   aux = analogRead(3)*0.25/1023.0*Vcc; // read apogee pyranometer to cal photodiode
   
   AStats.add(aux);
   
   if (output_flg_on == true) {  // if RH/T conversion complete
      TStats.add(temperature);
      DStats.add(dewpoint);
      printData();
      output_flg_on=false;
      }
  
//----------------------------------------------------------------------  
// Log data on SD card at LOG_RATE, default = 5 min intervals
//----------------------------------------------------------------------  

  if ( (now.minute() % LOG_RATE)==0  & now.minute() != oldminute & measActive == false) {
    oldminute=now.minute();
    calcStat();
    windvector();
    
    logData();    // send data to sd card
   
  }

}  // end main loop


void AnemometerLoop ()    // START Anemometer section
{
  WindSpeed_Hz=1000000.0*PulsesNbr/PulsesCumulatedTime; 
        WindSpeed_mph=WindSpeed_Hz*WindTo_mph;
        WindSpeed_mps=WindSpeed_Hz*WindTo_mph*0.44704 ;
        WindSpeed_rpm = PulsesNbr/(PulsesCumulatedTime/1E6)*60;
        if (WindTo_mph == 1.25) WindSpeed_rpm = WindSpeed_rpm/3; 
        WindSpeed_cnt= 1.0*PulsesNbr;
  MaxWind       = 1000000*WindTo_mph*0.44704/MinPulseTimeInterval; // Determine wind gust (i.e the smallest pulse interval between Pachube updates)
       
        PulsesCumulatedTime = 0;
  PulsesNbr = 0;
  WindGust = MaxWind;
  MaxWind = 0;

       if (WindTo_mph==2.5) {   //Inspeed e-vane
        DirectionVolt= analogRead (vanePin) * (5.0 / 1024.0);   
        if (DirectionVolt < 0.25) DirectionVolt=0.25;
        if (DirectionVolt > 4.75) DirectionVolt=4.75;
        WindDirection = (DirectionVolt-0.25)*360/4.5;  
        } 
        else { //Davis or Novalynx
  DirectionVolt = analogRead (vanePin); 
        WindDirection = (DirectionVolt / 1024.0) * 360.0; 
         
        }

        if (WindDirection > 360 ) { WindDirection = WindDirection - 360; }
        if (WindDirection < 0 ) { WindDirection = WindDirection + 360; }

        //wind vector calculations
        rrdd=(WindDirection+180)*3.14156/180;  // add 180 deg conver to rad
        rx=WindSpeed_mps*sin(rrdd);
        ry=WindSpeed_mps*cos(rrdd);
        
        //add to statistic vector
        UStats.add(WindSpeed_mps);
        CStats.add(WindDirection);
        xStats.add(rx);
        yStats.add(ry);
        
        if (FirstLoop <= 2) {
    FirstLoop = FirstLoop + 1;
  }
}

void AnemometerPulse() 
{
  noInterrupts();             // disable global interrupts
  PulseTimeNow = micros();   // Micros() is more precise to compute pulse width that millis();
  PulseTimeInterval = PulseTimeNow - PulseTimeLast;
  PulseTimeLast = PulseTimeNow;
  PulsesCumulatedTime = PulsesCumulatedTime + PulseTimeInterval;
  PulsesNbr++;

  if ( PulseTimeInterval < LastPulseTimeInterval )   // faster wind speed == shortest pulse interval
  { 
    MinPulseTimeInterval = PulseTimeInterval;
    LastPulseTimeInterval = MinPulseTimeInterval;
  }

  interrupts();              // Re-enable Interrupts
}

void RainPulse() 
{
  noInterrupts();             // disable global interrupts
  RainNbr++;
  interrupts();              // Re-enable Interrupts
}

void RHTdataCheck() {

  if (measActive && sht.measRdy()) {           // Check measurement status
    if (measType == TEMP) {                    // Process temp or humi?
      measType = HUMI;
      temperature = sht.calcTemp(rawData);     // Convert raw sensor data
      sht.meas(HUMI, &rawData, NONBLOCK);      // Start humi measurement
    } else {
      measActive = false;
      humidity = sht.calcHumi(rawData, temperature); // Convert raw sensor data
      dewpoint = sht.calcDewpoint(humidity, temperature);
      output_flg_on=true;
    }
  }
}

void printData() {
 #if ECHO_TO_SERIAL
 DateTime now = RTC.now();       
 Serial.println("-----------------------------  ");
 Serial.print(now.year(), DEC); Serial.print('-');
 Serial.print(now.month(), DEC); Serial.print('-');
 Serial.print(now.day(), DEC); Serial.print(' ');
 printDigits(now.hour()); Serial.print(':');
 printDigits(now.minute());Serial.print(':');
 printDigits(now.second());
 Serial.println();
 Serial.print(F("WindSpeed_mps: ")); Serial.println(WindSpeed_mps, 2);
 Serial.print(F("WindGust_mps: ")); Serial.println(WindGust, 2);
 Serial.print(F("WindSpeed_rpm: ")); Serial.println(WindSpeed_rpm, 1);
 Serial.print(F("WindDirection: ")); Serial.println(WindDirection);
 Serial.print(F("Temperature  : "));  Serial.println(temperature);
 Serial.print(F("Rel. Humidity: ")); Serial.println(humidity);
 Serial.print(F("Dewpoint Temp: ")); Serial.println(dewpoint);
 Serial.print(F("Global Irrad : ")); Serial.println(solar_Wm2);
 Serial.print(F("batt         : ")); Serial.println(battVolt);
 Serial.print(F("Aux          : ")); Serial.println(aux);
 Serial.print("freeMemory()=");   // report free SRAM
 Serial.println(freeMemory());
 #endif
}

void calcStat() {
  //Averages
   avgTemp = TStats.average();
   avgTdew = DStats.average();
   avgSpd = UStats.average();
   avgDir = CStats.average();
   //Std dev of wind direction
   sdDir = CStats.pop_stdev();
   //Sums for wind vector
   totrx = xStats.sum();
   totry = yStats.sum();
   n_rwd = xStats.count();
   //Radiation
   avgRs = RStats.average();
   //rain
   totRain= RainNbr;
   //wind gust
   maxSpd=UStats.maximum();
   minSpd=UStats.minimum();
   avgAux=AStats.average();
  //clear data arrays
   TStats.clear();
   DStats.clear();
   UStats.clear();
   CStats.clear();
   xStats.clear();
   yStats.clear();
   RStats.clear();
   //A1Stats.clear();
   AStats.clear();
   
   // Zero Rain counter
   RainNbr = 0;
   // reset wind gust
   MinPulseTimeInterval = 1000000000;
   LastPulseTimeInterval = 1000000000;
   
}

void windvector() {

  if (totry == 0.0) {
     r_dir = 0; }
    else {
     r_dir = atan(totrx/totry); // avg r_wind dir
     }
     r_dir = r_dir / (3.14156/180); // convert radians back to degrees 
  
   if (totrx*totry < 0) {
     if (totrx < 0){
                r_dir = 180 + r_dir;}
            else{
                r_dir = 360 + r_dir;}
   }           
   else {
      if (totrx > 0){r_dir = 180 + r_dir;}
   }             

   r_spd = sqrt((totrx*totrx + totry*totry)/(n_rwd*n_rwd));

   if ((r_dir == 0) & (r_spd != 0)) {r_dir = 360;}

   if (r_spd == 0)  r_dir = 0;

} // end wind vector


void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
 // Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void logDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
// logfile.print(":");
  if(digits < 10)
    logfile.print('0');
  logfile.print(digits);
}

void logData() {
// write to logfile on SD card
   if (logfile.open(filename, O_RDWR | O_APPEND))
    {
   DateTime now = RTC.now();   
   logfile.print(RecNum,DEC);
   logfile.print(F(", ")); 
   logfile.print(now.unixtime(), DEC);
   logfile.print(F(", ")); 
   logfile.print(now.year(), DEC);
   logfile.print(F("-"));
   logDigits(now.month());
   logfile.print(F("-"));
   logDigits(now.day());
   logfile.print(F(" "));
   logDigits(now.hour()); 
   logfile.print(F(":"));
   logDigits(now.minute());
   logfile.print(F(":"));
   logDigits(now.second());
   logfile.print(F(", "));   
   logfile.print(avgTemp);
   logfile.print(F(", "));    
   logfile.print(humidity);
   logfile.print(F(", ")); 
   logfile.print(avgTdew);
   logfile.print(F(", ")); 
   logfile.print(avgRs);
   logfile.print(F(", "));  
   logfile.print(avgSpd);
   logfile.print(F(", "));  
   logfile.print(r_dir);
   logfile.print(", ");      
   logfile.print(sdDir);
   logfile.print(F(", "));    
   logfile.print(totRain);
   logfile.print(F(", ")); 
   logfile.print(maxSpd);
   logfile.print(F(", "));
   logfile.print(minSpd);
   logfile.print(F(", "));
   logfile.print(WindGust);
   logfile.print(F(", "));
   logfile.print(battVolt);
   logfile.print(F(", "));
   logfile.print(r_spd);
   logfile.print(F(", "));    
   logfile.print(avgDir);
   logfile.print(F(", "));
   logfile.print(n_rwd);
   logfile.print(F(", "));    
   logfile.print(avgAux);
   logfile.println(F(""));
   logfile.close();
    RecNum++;
  }
  else
  {
    Serial.println("Couldn't open log file");
    digitalWrite(led_2_Pin, HIGH);
  }
}

long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  
 
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring
 
  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both
 
  long result = (high<<8) | low;
 
  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}
