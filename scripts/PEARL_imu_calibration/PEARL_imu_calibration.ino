#include <Wire.h>
#include <Adafruit_Sensor_Calibration.h>
#include <Adafruit_AHRS.h>
#include <IBusBM.h>

/*------------Pin Assignments------------*/
int rightMotorPin = 8;
int leftMotorPin = 9;

/*------------Mag Calibrations-----------*/
float MAG_CAL_X = 0.0;
float MAG_CAL_Y = 0.0;
float MAG_CAL_Z = 0.0;

/*---------Serial Port Assignments-------*/
IBusBM ibusRC;
HardwareSerial& ibusRCSerial = Serial2;
HardwareSerial& frontseat = Serial;

/*--------Setup IMU--------*/
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer;
#include "NXP_FXOS_FXAS.h"
// pick your filter! slower == better quality output
//Adafruit_NXPSensorFusion filter; // slowest
Adafruit_Madgwick filter;  // faster than NXP
//Adafruit_Mahony filter;  // fastest/smallest

/*----------Load IMU calibrations----------*/
#if defined(ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM)
  Adafruit_Sensor_Calibration_EEPROM cal;
#else
  Adafruit_Sensor_Calibration_SDFat cal;
#endif
/*-----------------------------------------*/

#define FILTER_UPDATE_RATE_HZ 50
#define PRINT_EVERY_N_UPDATES 20
uint32_t timestamp;

/*-----Setup IMU Variables-----*/
float roll, pitch, heading, new_heading;
float ax, ay, az;
float gx, gy, gz;
float mx, my, mz;

/*-----Setup Calibration Variables-----*/
const int cal_total = 10;
float XCalData[2*cal_total];
float YCalData[2*cal_total];
float ZCalData[2*cal_total];
float xSum,ySum,zSum;
float xCal,yCal,zCal;
float accelLimit = 0.05;
float gyroLimit = 0.5;
bool cal_finished = false;
bool NEW_CAL_STARTED = false;
bool NEW_CAL_WRITTEN = false;
int cal_counter = 0;

float oldCalX,oldCalY,oldCalZ;

/*--------Setup RC Variables--------*/
int rotateCH = 0;
int throttleCH = 2;
int turnCH = 9;    //*******
int calibrateCH = 4;
int drivemodeCH = 5;
//int sensorCH = 6;
//int writeCalCH = 7;

int sensorCH = 1;
int writeCalCH = 3;

int rotateVal = 0;
int throttleVal = 0;
int turnVal = 0;
bool CALIBRATE = false;
bool BACKWARD = false;
int sensorVal = 0;
bool WRITECAL = false;

/*--------Setup Motor Variables--------*/
const float THROTTLE_ZERO_THRESHOLD = 5;
int LIMIT = 32; //this is the PWM step limit, must be <= 65 for PEARL's motors
//forward PWM values are 191-254 (63 steps), which will be limited in the code to 191 + LIMIT
//backward PWM values are 120-185 (65 steps), which will be limited in the code to 185 - LIMIT
const float STILL = 188;
const float MIN_THROTTLE = 1;
const float LEFT_FORWARD_MIN = STILL + MIN_THROTTLE;
const float LEFT_BACKWARD_MIN = STILL - MIN_THROTTLE;
const float RIGHT_FORWARD_MIN = LEFT_FORWARD_MIN;
const float RIGHT_BACKWARD_MIN = LEFT_BACKWARD_MIN;
const float LEFT_FORWARD_MAX = LEFT_FORWARD_MIN + LIMIT;
const float LEFT_BACKWARD_MAX = LEFT_BACKWARD_MIN - LIMIT;
const float RIGHT_FORWARD_MAX = RIGHT_FORWARD_MIN + LIMIT;
const float RIGHT_BACKWARD_MAX = LEFT_BACKWARD_MIN - LIMIT;
const float TURN_SPEED = 10;

int curLeft = STILL;
int curRight = STILL;

int turnMax = 30;
int turnLeft;
int turnRight;
int throttleMap;

void setup(void) {
  /*-----Setup for RC Control-----*/
  ibusRC.begin(ibusRCSerial);

  /*-----Setup Motor Pins-----*/
  pinMode(rightMotorPin, OUTPUT);
  pinMode(leftMotorPin, OUTPUT);
  analogWrite(rightMotorPin, STILL);
  analogWrite(leftMotorPin, STILL);

  /*-----Setup for front seat comms-----*/
  frontseat.begin(115200);
  while (!frontseat) {
    delay(1);
  }

  /*-----Setup for IMU-----*/
  cal.begin();
  cal.loadCalibration();
  init_sensors();
  setup_sensors();
  filter.begin(FILTER_UPDATE_RATE_HZ);
  timestamp = millis();
  Wire.setClock(400000); //400KHz

}

void loop()
{
  static uint8_t counter = 0;

  if ((millis() - timestamp) < (1000 / FILTER_UPDATE_RATE_HZ)) {
    return;
  }
  timestamp = millis();

  updateIMU();
  handleRC();

  // Only send data to serial port once in a while
  if (counter++ <= PRINT_EVERY_N_UPDATES) {return;}
  counter = 0;

  if (!CALIBRATE) {
//    NEW_CAL_STARTED = false;
    frontseat.print("Sensor (SWC): ");
    if (sensorVal==0) {
      frontseat.print("Accelerometer");
      frontseat.println(" --> Place on flat surface, start calibration (SWD).");
    }
    else if (sensorVal==1) {
      frontseat.print("Gyroscope");
      frontseat.println(" --> Place on flat surface, start calibration (SWD).");
    }
    else if (sensorVal==2) {
      frontseat.print("Magnetometer");
      frontseat.println(" --> Start calibration (SWD), close serial monitor, and run MagCalPlot.py.");

    }
  }
  else {
    NEW_CAL_STARTED = true;
    handleCalibration();
  }

  // After exiting calibration mode, reset all variables for next calibration
  if (!CALIBRATE && NEW_CAL_STARTED) {
    NEW_CAL_STARTED = false;
    memset(XCalData,0,sizeof(XCalData));
    memset(YCalData,0,sizeof(YCalData));
    memset(ZCalData,0,sizeof(ZCalData));
    xSum=0;ySum=0;zSum=0;
    xCal=0;yCal=0;zCal=0;
    cal_counter = 0;
    if (!NEW_CAL_WRITTEN && sensorVal==0) {
      cal.accel_zerog[0] = oldCalX;
      cal.accel_zerog[1] = oldCalY;
      cal.accel_zerog[2] = oldCalZ;
      cal.saveCalibration();
    }
    else if (!NEW_CAL_WRITTEN && sensorVal==1) {
      cal.gyro_zerorate[0] = oldCalX;
      cal.gyro_zerorate[1] = oldCalY;
      cal.gyro_zerorate[2] = oldCalZ;
      cal.saveCalibration();
    }
    else if (!NEW_CAL_WRITTEN && sensorVal==2) {
      cal.mag_hardiron[0] = oldCalX;
      cal.mag_hardiron[1] = oldCalY;
      cal.mag_hardiron[2] = oldCalZ;
      cal.saveCalibration();
    }
    oldCalX=0;oldCalY=0;oldCalZ=0;
    NEW_CAL_WRITTEN = false;
  }

}

void handleCalibration() {
  if (sensorVal==0) {
    calibrateAccel();
    if (WRITECAL && !NEW_CAL_WRITTEN) {
      NEW_CAL_WRITTEN = true;
      cal.accel_zerog[0] = xCal;
      cal.accel_zerog[1] = yCal;
      cal.accel_zerog[2] = zCal;
      if (!cal.saveCalibration())
        frontseat.println("**WARNING** Couldn't save calibration");
      else
        frontseat.println("Wrote accelerometer calibration");
    }
  }
  else if (sensorVal==1) {
    calibrateGyro();
    if (WRITECAL && !NEW_CAL_WRITTEN) {
      NEW_CAL_WRITTEN = true;
      cal.gyro_zerorate[0] = xCal * SENSORS_DPS_TO_RADS;
      cal.gyro_zerorate[1] = yCal * SENSORS_DPS_TO_RADS;
      cal.gyro_zerorate[2] = zCal * SENSORS_DPS_TO_RADS;
      if (!cal.saveCalibration()) {
        frontseat.println("**WARNING** Couldn't save calibration");
      } else {
        frontseat.println("Wrote gyroscope calibration");
      }
    }
  }
  else if (sensorVal==2) {
    calibrateMag();
    if (WRITECAL && !NEW_CAL_WRITTEN) {
      NEW_CAL_WRITTEN = true;
      cal.mag_hardiron[0] = MAG_CAL_X;
      cal.mag_hardiron[1] = MAG_CAL_Y;
      cal.mag_hardiron[2] = MAG_CAL_Z;

      cal.mag_softiron[0] = 1.0;
      cal.mag_softiron[1] = 0.0;
      cal.mag_softiron[2] = 0.0;
      cal.mag_softiron[3] = 0.0;
      cal.mag_softiron[4] = 1.0;
      cal.mag_softiron[5] = 0.0;
      cal.mag_softiron[6] = 0.0;
      cal.mag_softiron[7] = 0.0;
      cal.mag_softiron[8] = 1.0;
      cal.saveCalibration();
    }
  }
}

void updateIMU() {
  // Read sensors
  sensors_event_t accel, gyro, mag;
  accelerometer->getEvent(&accel);
  gyroscope->getEvent(&gyro);
  magnetometer->getEvent(&mag);

  cal.calibrate(accel);
  cal.calibrate(gyro);
  cal.calibrate(mag);

  ax = accel.acceleration.x;
  ay = accel.acceleration.y;
  az = accel.acceleration.z;

  // Gyroscope needs to be converted from rad/s to degree/s
  gx = gyro.gyro.x * SENSORS_RADS_TO_DPS;
  gy = gyro.gyro.y * SENSORS_RADS_TO_DPS;
  gz = gyro.gyro.z * SENSORS_RADS_TO_DPS;
  
  mx = mag.magnetic.x;
  my = mag.magnetic.y;
  mz = mag.magnetic.z;

  // Update the sensor fusion filter
  filter.update(gx, gy, gz,
                ax, ay, az,
                mx, my, mz);

  roll = filter.getRoll();
  pitch = filter.getPitch();
  heading = filter.getYaw();

  // Fix error with how IMU heading angle is reported
  new_heading = mapFloat(heading, 0, 360, 360, 0);
  new_heading += 180;
  if (new_heading > 360.0)
    new_heading -= 360.0;
  if (new_heading < 0.0)
    new_heading += 360;
}

void calibrateAccel() {
  if (cal_counter==0) {
    NEW_CAL_WRITTEN = false;
    cal.loadCalibration();
    oldCalX = cal.accel_zerog[0];
    oldCalY = cal.accel_zerog[1];
    oldCalZ = cal.accel_zerog[2];
    cal.accel_zerog[0] = 0.0;
    cal.accel_zerog[1] = 0.0;
    cal.accel_zerog[2] = 0.0;
    cal.saveCalibration();
  }
  
  if (cal_counter<cal_total) {
    XCalData[cal_counter] = ax;
    YCalData[cal_counter] = ay;
    ZCalData[cal_counter] = az;
    xSum += ax;
    ySum += ay;
    zSum += az;
    cal_counter++;
  }
  else if (cal_counter<(2*cal_total)) {
    xSum += ax;
    ySum += ay;
    zSum += az;
    xSum -= XCalData[cal_counter-cal_total];
    ySum -= YCalData[cal_counter-cal_total];
    zSum -= ZCalData[cal_counter-cal_total];
    XCalData[cal_counter-cal_total] = ax;
    YCalData[cal_counter-cal_total] = ay;
    ZCalData[cal_counter-cal_total] = az;
    xCal = xSum/float(cal_total);
    yCal = ySum/float(cal_total);
    zCal = zSum/float(cal_total)-9.8;
    cal_counter++;
  }
  else {
    cal_counter = cal_total;
  }

  float xNew = ax - xCal;
  float yNew = ay - yCal;
  float zNew = az - zCal;

  if (!WRITECAL && !NEW_CAL_WRITTEN) {
    frontseat.print("[X,Y,Z] (uncal): [");
    if (ax>=0) {frontseat.print(" ");}
    frontseat.print(ax);frontseat.print(",");
    if (ay>=0) {frontseat.print(" ");}
    frontseat.print(ay);frontseat.print(",");
    if (az<10) {frontseat.print(" ");}
    frontseat.print(az);frontseat.print("] ");
    frontseat.print("[X,Y,Z] (cal): [");
    if (xNew>=0) {frontseat.print(" ");}
    frontseat.print(xNew);frontseat.print(",");
    if (yNew>=0) {frontseat.print(" ");}
    frontseat.print(yNew);frontseat.print(",");
    if (zNew<10) {frontseat.print(" ");}
    frontseat.print(zNew);frontseat.print("] ");
    frontseat.print("Good Calibration: [");
    if (abs(xNew-0) < accelLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.print(",");
    if (abs(yNew-0) < accelLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.print(",");
    if (abs(zNew-9.8) < accelLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.println("]");
  }

  else if (!WRITECAL && NEW_CAL_WRITTEN) {
    frontseat.print("[X,Y,Z] (new calibration): [");
    if (ax>=0) {frontseat.print(" ");}
    frontseat.print(ax);frontseat.print(",");
    if (ay>=0) {frontseat.print(" ");}
    frontseat.print(ay);frontseat.print(",");
    if (az<10) {frontseat.print(" ");}
    frontseat.print(az);frontseat.println("] ");
  }
  
}

void calibrateGyro() {
  if (cal_counter==0) {
    NEW_CAL_WRITTEN = false;
    cal.loadCalibration();
    oldCalX = cal.gyro_zerorate[0];
    oldCalY = cal.gyro_zerorate[1];
    oldCalZ = cal.gyro_zerorate[2];
    cal.gyro_zerorate[0] = 0.0;
    cal.gyro_zerorate[1] = 0.0;
    cal.gyro_zerorate[2] = 0.0;
    cal.saveCalibration();
  }
  
  if (cal_counter<cal_total) {
    XCalData[cal_counter] = gx;
    YCalData[cal_counter] = gy;
    ZCalData[cal_counter] = gz;
    xSum += gx;
    ySum += gy;
    zSum += gz;
    cal_counter++;
  }
  else if (cal_counter<(2*cal_total)) {
    xSum += gx;
    ySum += gy;
    zSum += gz;
    xSum -= XCalData[cal_counter-cal_total];
    ySum -= YCalData[cal_counter-cal_total];
    zSum -= ZCalData[cal_counter-cal_total];
    XCalData[cal_counter-cal_total] = gx;
    YCalData[cal_counter-cal_total] = gy;
    ZCalData[cal_counter-cal_total] = gz;
    xCal = xSum/float(cal_total);
    yCal = ySum/float(cal_total);
    zCal = zSum/float(cal_total);
    cal_counter++;
  }
  else {
    cal_counter = cal_total;
  }

  float xNew = gx - xCal;
  float yNew = gy - yCal;
  float zNew = gz - zCal;

  if (!WRITECAL && !NEW_CAL_WRITTEN) {
    frontseat.print("[X,Y,Z] (uncal): [");
    if (gx>=0) {frontseat.print(" ");}
    frontseat.print(gx);frontseat.print(",");
    if (gy>=0) {frontseat.print(" ");}
    frontseat.print(gy);frontseat.print(",");
    if (gz>=0) {frontseat.print(" ");}
    frontseat.print(gz);frontseat.print("] ");
    frontseat.print("[X,Y,Z] (cal): [");
    if (xNew>=0) {frontseat.print(" ");}
    frontseat.print(xNew);frontseat.print(",");
    if (yNew>=0) {frontseat.print(" ");}
    frontseat.print(yNew);frontseat.print(",");
    if (zNew>=0) {frontseat.print(" ");}
    frontseat.print(zNew);frontseat.print("] ");
    frontseat.print("Good Calibration: [");
    if (abs(xNew-0) < gyroLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.print(",");
    if (abs(yNew-0) < gyroLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.print(",");
    if (abs(zNew-0) < gyroLimit) {frontseat.print("YES");}
    else {frontseat.print("NO");}
    frontseat.println("]");
  }
  else if (!WRITECAL && NEW_CAL_WRITTEN) {
    frontseat.print("[X,Y,Z] (new calibration): [");
    if (gx>=0) {frontseat.print(" ");}
    frontseat.print(gx);frontseat.print(",");
    if (gy>=0) {frontseat.print(" ");}
    frontseat.print(gy);frontseat.print(",");
    if (gz<10) {frontseat.print(" ");}
    frontseat.print(gz);frontseat.println("] ");
  }

}

void calibrateMag() {
  if (cal_counter==0) {
    NEW_CAL_WRITTEN = false;
    cal.loadCalibration();
    oldCalX = cal.mag_hardiron[0];
    oldCalY = cal.mag_hardiron[1];
    oldCalZ = cal.mag_hardiron[2];
    cal.mag_hardiron[0] = 0.0;
    cal.mag_hardiron[1] = 0.0;
    cal.mag_hardiron[2] = 0.0;
    cal.saveCalibration();
  }

  sendToPython(&mx, &my, &mz);
  
  cal_counter++;  
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  // Re-maps a float number from one range to another.
  // That is, a value of fromLow would get mapped to toLow, a value of fromHigh to toHigh, values in-between to values in-between, etc.
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void handleRC() {

  BACKWARD = readSwitch2(drivemodeCH, false);

  sensorVal = readSwitch3(sensorCH, 0);
  CALIBRATE = readSwitch2(calibrateCH, false);
  WRITECAL = readSwitch2(writeCalCH, false);

  rotateVal = readChannel(rotateCH, -100, 100, 0);
  throttleVal = readChannel(throttleCH, -100, 100, 0);

  /*--------Handle turn commands--------*/
  turnVal = readChannel(turnCH, -100, 100, 0);
  if (turnVal > 0) {
    turnRight = map(turnVal, 1, 100, 0, turnMax);
    turnLeft = 0;
  }
  else if (turnVal < 0) {
    turnRight = 0;
    turnLeft = map(turnVal, -1, -100, 0, turnMax);
  }
  else {
    turnRight = 0;
    turnLeft = 0;
  }
  /*------------------------------------*/

  if (rotateVal > 0) {
    curLeft = map(rotateVal, 1, 100, LEFT_FORWARD_MIN, LEFT_FORWARD_MAX);
    curRight = map(rotateVal, 1, 100, RIGHT_BACKWARD_MIN, RIGHT_BACKWARD_MAX);
  }
  else if (rotateVal < 0) {
    curLeft = map(rotateVal, -1, -100, LEFT_BACKWARD_MIN, LEFT_BACKWARD_MAX);
    curRight = map(rotateVal, -1, -100, RIGHT_FORWARD_MIN, RIGHT_FORWARD_MAX);
  }
  else {
    if (!BACKWARD) {
      throttleMap = map(throttleVal, -95, 100, LEFT_FORWARD_MIN, LEFT_FORWARD_MAX);
      if (throttleMap >= LEFT_FORWARD_MIN) {
        curLeft = throttleMap - turnLeft;
        curRight = throttleMap - turnRight;
      }
      else {
        curLeft = STILL;
        curRight = STILL;
      }
    }
    else if (BACKWARD) {
      throttleMap = map(throttleVal, -95, 100, LEFT_BACKWARD_MIN, LEFT_BACKWARD_MAX);
      if (throttleMap <= LEFT_BACKWARD_MIN) {
        curLeft = throttleMap + turnLeft;
        curRight = throttleMap + turnRight;
      }
      else {
        curLeft = STILL;
        curRight = STILL;
      }
    }
  }
    analogWrite(leftMotorPin, curLeft);
    analogWrite(rightMotorPin, curRight);
}

int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  // Read the number of a given channel and convert to the range provided.
  // If the channel is off, return the default value
  uint16_t ch = ibusRC.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  if (ch==1999) {ch=2000;}
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

bool readSwitch2(byte channelInput, bool defaultValue) {
  // Read the 2-way switch and return a boolean value
  int intDefaultValue = (defaultValue) ? 100 : 0;
  int ch = readChannel(channelInput, 0, 100, intDefaultValue);
  return (ch > 50);
}

int readSwitch3(byte channelInput, int defaultValue) {
  // Read the 3-way switch and return an integer value
  int ch = readChannel(channelInput, 0, 2, defaultValue);
  return ch;
}


void sendToPython(float* x, float* y, float* z) {
  byte* byteX  = (byte*)(x);
  byte* byteY  = (byte*)(y);
  byte* byteZ  = (byte*)(z);
  frontseat.write(byteX, 4);
  frontseat.write(byteY, 4);
  frontseat.write(byteZ, 4);
}
