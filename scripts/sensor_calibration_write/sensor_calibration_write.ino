#include "Adafruit_Sensor_Calibration.h"

// select either EEPROM or SPI FLASH storage:
#ifdef ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM
  Adafruit_Sensor_Calibration_EEPROM cal;
#else
  Adafruit_Sensor_Calibration_SDFat cal;
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  delay(100);
  Serial.println("Calibration filesys test");
  if (!cal.begin()) {
    Serial.println("Failed to initialize calibration helper");
    while (1) yield();
  }
  Serial.print("Has EEPROM: "); Serial.println(cal.hasEEPROM());
  Serial.print("Has FLASH: "); Serial.println(cal.hasFLASH());

  if (! cal.loadCalibration()) {
    Serial.println("No calibration loaded/found... will start with defaults");
  } else {
    Serial.println("Loaded existing calibration");
  }

  // in uTesla - updated by MNH on 3/21/2021 at 3pm after data analysis
  cal.mag_hardiron[0] = -6.7;
  cal.mag_hardiron[1] = -13.3;
  cal.mag_hardiron[2] = -103.65;

  // in uTesla
  cal.mag_softiron[0] = 1;
  cal.mag_softiron[1] = 0;
  cal.mag_softiron[2] = 0;  
  cal.mag_softiron[3] = 0;
  cal.mag_softiron[4] = 1;
  cal.mag_softiron[5] = 0;  
  cal.mag_softiron[6] = 0;
  cal.mag_softiron[7] = 0;
  cal.mag_softiron[8] = 1;  

  // in Radians/s
  cal.gyro_zerorate[0] = 0.015679916;
  cal.gyro_zerorate[1] = 0.008437045;
  cal.gyro_zerorate[2] = 0.00865618;

  // in m/s^2
  cal.accel_zerog[0] = 0.162939796;
  cal.accel_zerog[1] = -0.505682653;
  cal.accel_zerog[2] = 0.525580102;

//---------- Previous Calibrations -----------//
//LSM9DS1 calibrations with Uno
/*
  // in uTesla
  cal.mag_hardiron[0] = 16.51;
  cal.mag_hardiron[1] = 25.46;
  cal.mag_hardiron[2] = -9.25;

  // in uTesla
  cal.mag_softiron[0] = 1.030;
  cal.mag_softiron[1] = 0.028;
  cal.mag_softiron[2] = 0.011;  
  cal.mag_softiron[3] = 0.028;
  cal.mag_softiron[4] = 0.991;
  cal.mag_softiron[5] = 0.012;  
  cal.mag_softiron[6] = 0.011;
  cal.mag_softiron[7] = 0.012;
  cal.mag_softiron[8] = 0.981;  

  // in Radians/s
  cal.gyro_zerorate[0] = 0.025;
  cal.gyro_zerorate[1] = -0.01;
  cal.gyro_zerorate[2] = 0.015;

  cal.accel_zerog[0] = 0.0;
  cal.accel_zerog[1] = 0.27;
  cal.accel_zerog[2] = -0.15;
*/

//NXP calibrations with Uno
/*
  // in uTesla
  cal.mag_hardiron[0] = -3.76;
  cal.mag_hardiron[1] = -16.42;
  cal.mag_hardiron[2] = -60.45;

  // in uTesla
  cal.mag_softiron[0] = 0.976;
  cal.mag_softiron[1] = -0.028;
  cal.mag_softiron[2] = -0.013;  
  cal.mag_softiron[3] = -0.028;
  cal.mag_softiron[4] = 1.058;
  cal.mag_softiron[5] = -0.010;  
  cal.mag_softiron[6] = -0.013;
  cal.mag_softiron[7] = -0.010;
  cal.mag_softiron[8] = 0.970;  

  // in Radians/s
  cal.gyro_zerorate[0] = 0.01;
  cal.gyro_zerorate[1] = 0.008;
  cal.gyro_zerorate[2] = 0.02;

  cal.accel_zerog[0] = 0.0;
  cal.accel_zerog[1] = -0.3;
  cal.accel_zerog[2] = 0.2;
*/

//NXP Calibration with MEGA
/*
  // in uTesla
  cal.mag_hardiron[0] = 1.15;
  cal.mag_hardiron[1] = -18.26;
  cal.mag_hardiron[2] = -54.83;

  // in uTesla
  cal.mag_softiron[0] = 0.989;
  cal.mag_softiron[1] = -0.033;
  cal.mag_softiron[2] = -0.001;  
  cal.mag_softiron[3] = -0.033;
  cal.mag_softiron[4] = 1.049;
  cal.mag_softiron[5] = -0.009;  
  cal.mag_softiron[6] = -0.001;
  cal.mag_softiron[7] = -0.009;
  cal.mag_softiron[8] = 0.965;  

  // in Radians/s
  cal.gyro_zerorate[0] = 0.01;
  cal.gyro_zerorate[1] = 0.01;
  cal.gyro_zerorate[2] = 0.02;

  cal.accel_zerog[0] = 0.2;
  cal.accel_zerog[1] = -0.07;
  cal.accel_zerog[2] = 0.2;
*/


 
//NXP calibration with MEGA from Ethan
/*  
 // in uTesla
  cal.mag_hardiron[0] = 2.85;
  cal.mag_hardiron[1] = -29.00;
  cal.mag_hardiron[2] = -92.00;

  // in uTesla
  cal.mag_softiron[0] = 1;
  cal.mag_softiron[1] = 0;
  cal.mag_softiron[2] = 0;  
  cal.mag_softiron[3] = 0;
  cal.mag_softiron[4] = 1;
  cal.mag_softiron[5] = 0;  
  cal.mag_softiron[6] = 0;
  cal.mag_softiron[7] = 0;
  cal.mag_softiron[8] = 1;  

  // in Radians/s
  cal.gyro_zerorate[0] = 0.01;
  cal.gyro_zerorate[1] = 0.0074;
  cal.gyro_zerorate[2] = 0.0189;

  cal.accel_zerog[0] = 0.05;
  cal.accel_zerog[1] = -0.52;
  cal.accel_zerog[2] = 0.25;
*/

  if (! cal.saveCalibration()) {
    Serial.println("**WARNING** Couldn't save calibration");
  } else {
    Serial.println("Wrote calibration");    
  }

  cal.printSavedCalibration();
}

void loop() {

}
