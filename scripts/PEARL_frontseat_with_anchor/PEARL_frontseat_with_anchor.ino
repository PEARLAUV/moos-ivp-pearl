#include <Wire.h>
#include <Adafruit_Sensor_Calibration.h>
#include <Adafruit_AHRS.h>
#include <IBusBM.h>

/*----------------------------------------------*/
//If true writes thrust commands to specified LED pins
//If false writes thrust commands to specified motor controller pins
bool TEST_MODE = false;
//If true sends data output to Serial port for use with Python real-time plotting scripts
bool DEBUG_MODE = false;

//Pin assignments
int anchorMotorPin = 7;

int rightMotorPin = 8;
int leftMotorPin = 9;

int rightForwardLED = 10;
int leftForwardLED = 11;
int rightBackwardLED = 12;
int leftBackwardLED = 13;

//Serial port assignments
IBusBM ibusRC;
HardwareSerial& ibusRCSerial = Serial2;  //RC receiver
HardwareSerial& moos = Serial;   //comms with navigation RPi/MOOS-IvP

/*----------Setup IMU and sensor fusion----------*/
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer;

#include "NXP_FXOS_FXAS.h"  // NXP 9-DoF breakout

// pick your filter! slower == better quality output
//Adafruit_NXPSensorFusion filter; // slowest
Adafruit_Madgwick filter;  // faster than NXP
//Adafruit_Mahony filter;  // fastest/smalleset

/*----------Load IMU calibrations----------*/
#if defined(ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM)
Adafruit_Sensor_Calibration_EEPROM cal;
#else
Adafruit_Sensor_Calibration_SDFat cal;
#endif
/*-----------------------------------------*/

#define FILTER_UPDATE_RATE_HZ 50
#define PRINT_EVERY_N_UPDATES 5

uint32_t timestamp;
const byte numChars = 32;
char receivedChars[numChars];
char tempChars[numChars];
char nmeaHeader[numChars] = {0};
float thrustLeft = 0.0;
float thrustRight = 0.0;
int curLeft = 188.0;
int curRight = 188.0;
float leftSend = 0.0;
float rightSend = 0.0;
boolean newData = false;
int manualControl = 0;   //0 = manual control off, 1 = manual control on

//anchor constants
const int anchor_stop = 1;
const int anchor_up = 0;
const int anchor_down = 2;

const float THROTTLE_ZERO_THRESHOLD = 5;
int LIMIT_THRUST = 32; //this is the PWM step limit, must be <= 65
//forward PWM values are 191-254 (63 steps), and then limited to 191 + LIMIT_THRUST
//backward PWM values are 120-185 (65 steps), and then limited to 185 - LIMIT_THRUST
int LIMIT_ANCHOR = 32;
const float STILL = 188;
const float MIN_THROTTLE = 1;
const float LEFT_FORWARD_MIN = STILL + MIN_THROTTLE;
const float LEFT_BACKWARD_MIN = STILL - MIN_THROTTLE;
const float RIGHT_FORWARD_MIN = LEFT_FORWARD_MIN;
const float RIGHT_BACKWARD_MIN = LEFT_BACKWARD_MIN;
const float LEFT_FORWARD_MAX = LEFT_FORWARD_MIN + LIMIT_THRUST;
const float LEFT_BACKWARD_MAX = LEFT_BACKWARD_MIN - LIMIT_THRUST;
const float RIGHT_FORWARD_MAX = RIGHT_FORWARD_MIN + LIMIT_THRUST;
const float RIGHT_BACKWARD_MAX = LEFT_BACKWARD_MIN - LIMIT_THRUST;
const float TURN_SPEED = 10;

//Anchor Constants
const float ANCHOR_UP_MAX = STILL + LIMIT_ANCHOR;
const float ANCHOR_DOWN_MAX = STILL - LIMIT_ANCHOR;

int curAnchor = STILL;

//RC variables
int rotateCH = 0;
int rotateVal = 0;

int throttleCH = 2;
int throttleVal = 0;

int turnCH = 3;
int turnVal = 0;

int manualCH = 4;
bool MANUAL;

int drivemodeCH = 5;
bool BACKWARD;

int anchorCH = 7;
int ANCHOR_STATE;

int turnMax = 30;
int turnLeft;
int turnRight;
int throttleMap;

void setup(void)
{
  /*------Setup for RC control--------*/
  ibusRC.begin(ibusRCSerial);

  /*------Setup motor pins--------*/
  if (!TEST_MODE) {
    pinMode(rightMotorPin, OUTPUT);
    pinMode(leftMotorPin, OUTPUT);
    analogWrite(rightMotorPin, STILL);
    analogWrite(leftMotorPin, STILL);

    //Anchor
    pinMode(anchorMotorPin, OUTPUT);
    analogWrite(anchorMotorPin, STILL);
  }

  /*------Setup test LED pins--------*/
  if (TEST_MODE) {
    pinMode(rightForwardLED, OUTPUT);
    pinMode(leftForwardLED, OUTPUT);
    pinMode(rightBackwardLED, OUTPUT);
    pinMode(leftBackwardLED, OUTPUT);
    digitalWrite(rightForwardLED, LOW);
    digitalWrite(leftForwardLED, LOW);
    digitalWrite(rightBackwardLED, LOW);
    digitalWrite(leftBackwardLED, LOW);
  }

  /*------Setup for front seat comms--------*/
  moos.begin(115200);
  while (!moos) {
    delay(1);
  }

  /*------Setup for IMU--------*/
  cal.begin();
  cal.loadCalibration();
  init_sensors();
  setup_sensors();
  filter.begin(FILTER_UPDATE_RATE_HZ);
  timestamp = millis();
  Wire.setClock(400000); // 400KHz

}

const String PREFIX    = "PL";   // Device prefix: PL = PEARL
const String ID_EULER  = "IMU";  // Sentence ID for Euler angles generated from sensor fusion filter
const String ID_RAW    = "RAW";  // Sentence ID for raw IMU readings
const String ID_MOTOR  = "MOT";  // Sentence ID for notifying MOOS-IvP of current motor commands

//Anchor
const String ID_ANCHOR = "ANC";   // Semtemce ID for notifying MOOS-IvP of current anchor status

void loop(void)
{
  static uint8_t counter = 0;

  /*------Read IMU data and package in NMEA sentence--------*/
  float roll, pitch, heading, new_heading;
  float ax, ay, az;
  float gx, gy, gz;
  float mx, my, mz;

  if ((millis() - timestamp) < (1000 / FILTER_UPDATE_RATE_HZ)) {
    return;
  }

  timestamp = millis();
  // Read the motion sensors
  sensors_event_t accel, gyro, mag;
  accelerometer->getEvent(&accel);
  gyroscope->getEvent(&gyro);
  magnetometer->getEvent(&mag);

  cal.calibrate(mag);
  cal.calibrate(accel);
  cal.calibrate(gyro);

  ax = accel.acceleration.x;
  ay = accel.acceleration.y;
  az = accel.acceleration.z;

  // Gyroscope needs to be converted from Rad/s to Degree/s
  gx = gyro.gyro.x * SENSORS_RADS_TO_DPS;
  gy = gyro.gyro.y * SENSORS_RADS_TO_DPS;
  gz = gyro.gyro.z * SENSORS_RADS_TO_DPS;

  mx = mag.magnetic.x;
  my = mag.magnetic.y;
  mz = mag.magnetic.z;

  // Update the SensorFusion filter
  filter.update(gx, gy, gz,
                ax, ay, az,
                mx, my, mz);

  roll = filter.getRoll();
  pitch = filter.getPitch();
  heading = filter.getYaw();

  // fixes error with how IMU heading angle is reported
  new_heading = mapFloat(heading, 0, 360, 360, 0);
  new_heading += 180;
  if (new_heading > 360.0)
    new_heading -= 360.0;
  if (new_heading < 0.0)
    new_heading += 360.0;

  /*--------Handle manual control inputs from RC--------*/
  handleRC();
  /*--------Read NMEA sentence from serial port--------*/
  readFromMOOS();
  /*--------Convert to PWM, and send to motor controllers-------*/
  commandThrust();
  /*--------Convert back to thrust percentage for report to MOOS-IvP--------*/
  reportThrust();     // if TEST_MODE is true then also commands LED brightness


  // only send data to serial port once in a while
  if (counter++ <= PRINT_EVERY_N_UPDATES) {
    return;
  }
  // reset the counter
  counter = 0;

  /*--------Generate NMEA strings for MOOS-IvP--------*/
  //Euler angle NMEA string
  String PAYLOAD_EULER = String(manualControl) + "," + String(new_heading) + "," + String(pitch) + "," + String(roll);
  String NMEA_EULER = generateNMEAString(PAYLOAD_EULER, PREFIX, ID_EULER);

  //Raw IMU data NMEA string
  String PAYLOAD_RAW = String(ax) + "," + String(ay) + "," + String(az) + "," +
                       String(gx) + "," + String(gy) + "," + String(gz) + "," +
                       String(mx) + "," + String(my) + "," + String(mz);
  String NMEA_RAW = generateNMEAString(PAYLOAD_RAW, PREFIX, ID_RAW);

  //Last motor commands NMEA string
  String PAYLOAD_MOTOR = String(leftSend) + "," + String(rightSend);
  String NMEA_MOTOR = generateNMEAString(PAYLOAD_MOTOR, PREFIX, ID_MOTOR);


  if (DEBUG_MODE) {
    float accx = accel.acceleration.x;
    float accy = accel.acceleration.y;
    float accz = accel.acceleration.z;
    float gyrox = gyro.gyro.x * SENSORS_RADS_TO_DPS;
    float gyroy = gyro.gyro.y * SENSORS_RADS_TO_DPS;
    float gyroz = gyro.gyro.z * SENSORS_RADS_TO_DPS;
    float magx = mag.magnetic.x;
    float magy = mag.magnetic.y;
    float magz = mag.magnetic.z;
    float HEADING = new_heading;
    float PITCH   = pitch;
    float ROLL    = roll;
//   sendToPython(&accx, &accy, &accz);
//   sendToPython(&gyrox, &gyroy, &gyroz);
//   sendToPython(&magx, &magy, &magz);
    sendToPython(&HEADING, &PITCH, &ROLL);
  }
  else {
    moos.println(NMEA_EULER);
    moos.println(NMEA_RAW);
    moos.println(NMEA_MOTOR);
    //Anchor status string 
  }

}

String generateNMEAString(String payload, String prefix, String id) {
  String nmea = "";
  nmea = prefix + id + "," + payload;
  return "$" + nmea + "*";    // Prefixed with $
}

void readFromMOOS() {
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '$';
  char endMarker = '*';
  char rc;

  while (moos.available() > 0 && newData == false) {
    rc = moos.read();

    if (recvInProgress == true) {
      if (rc != endMarker) {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= numChars) {
          ndx = numChars - 1;
        }
      }
      else {
        receivedChars[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }

    else if (rc == startMarker) {
      recvInProgress = true;
    }
  }
}

void parseNMEA() {
  // split the data into its parts
  char * strtokIndx; // this is used by strtok() as an index

  strtokIndx = strtok(tempChars, ",");     // get the first part - the string
  strcpy(nmeaHeader, strtokIndx); // copy it to nmeaHeader

  strtokIndx = strtok(NULL, ",");
  thrustLeft = atof(strtokIndx);     // convert the second entry to the left thrust percentage command

  strtokIndx = strtok(NULL, ",");
  thrustRight = atof(strtokIndx);     // convert the third entry to the right thrust percentage command

  //TODO: Add anchor commands to NMEA string parse
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  // Re-maps a float number from one range to another.
  // That is, a value of fromLow would get mapped to toLow, a value of fromHigh to toHigh, values in-between to values in-between, etc.
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void handleRC() {
  MANUAL = readSwitch(manualCH, false);
  if (MANUAL) {
    manualControl = 1;
  }
  else if (!MANUAL) {
    manualControl = 0;
  }

  BACKWARD = readSwitch(drivemodeCH, false);

  //Anchor
  ANCHOR_STATE = readSwitch3(anchorCH, 0);

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

  if (manualControl == 1) {
    curAnchor = STILL;
    if( ANCHOR_STATE == anchor_stop){ //Only throttle when anchor is not being moved
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
    }
    else{
      curLeft = STILL;
      curRight = STILL;
      if(ANCHOR_STATE == anchor_up){
        curAnchor = ANCHOR_UP_MAX;
      }
      else{
        curAnchor = ANCHOR_DOWN_MAX;
      }
    }
    if (!TEST_MODE) {
      analogWrite(leftMotorPin, curLeft);
      analogWrite(rightMotorPin, curRight);
      analogWrite(anchorMotorPin, curAnchor);
    }
  }
}

int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  // Read the number of a given channel and convert to the range provided.
  // If the channel is off, return the default value
  uint16_t ch = ibusRC.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

bool readSwitch(byte channelInput, bool defaultValue) {
  // Read the channel and return a boolean value
  int intDefaultValue = (defaultValue) ? 100 : 0;
  int ch = readChannel(channelInput, 0, 100, intDefaultValue);
  return (ch > 50);
}

//Anchor switch (3 state switch)
int readSwitch3(byte channelInput, int defaultValue) {
  // Read the 3-way switch and return an integer value
  int ch = readChannel(channelInput, 0, 2, defaultValue);
  return ch;
}

void sendToPython(float* x, float* y, float* z) {
  byte* byteX  = (byte*)(x);
  byte* byteY  = (byte*)(y);
  byte* byteZ  = (byte*)(z);
  moos.write(byteX, 4);
  moos.write(byteY, 4);
  moos.write(byteZ, 4);
}

void commandThrust() {
  if (newData == true) {
    strcpy(tempChars, receivedChars);
    parseNMEA();
    newData = false;
    if (manualControl == 0) {
      float leftVal, rightVal;
      // Map left thrust value to PWM
      if (thrustLeft > 0.05) {
        leftVal = mapFloat(thrustLeft, 0.0, 100.0, LEFT_FORWARD_MIN, LEFT_FORWARD_MAX);
      }
      else if (thrustLeft < 0.05) {
        leftVal = mapFloat(thrustLeft, -100.0, 0.0, LEFT_BACKWARD_MAX, LEFT_BACKWARD_MIN);
      }
      else {
        leftVal = STILL;
      }
      // Map right thrust value to PWM
      if (thrustRight > 0.05) {
        rightVal = mapFloat(thrustRight, 0.0, 100.0, RIGHT_FORWARD_MIN, RIGHT_FORWARD_MAX);
      }
      else if (thrustRight < 0.05) {
        rightVal = mapFloat(thrustRight, -100.0, 0.0, RIGHT_BACKWARD_MAX, RIGHT_BACKWARD_MIN);
      }
      else {
        rightVal = STILL;
      }
      curLeft = round(leftVal);
      curRight = round(rightVal);

      curAnchor = STILL;

      if (!TEST_MODE) {
        analogWrite(leftMotorPin, curLeft);
        analogWrite(rightMotorPin, curRight);
        analogWrite(anchorMotorPin, curAnchor);
      }
    }
  }
}

void reportThrust() {
  if (curLeft > 190) {
    leftSend = mapFloat(float(curLeft), LEFT_FORWARD_MIN, LEFT_FORWARD_MAX, 0.0, 100.0);
    if (TEST_MODE) {
      analogWrite(leftForwardLED, map(curLeft, LEFT_FORWARD_MIN, LEFT_FORWARD_MAX, 0, 255));
      analogWrite(leftBackwardLED, 0);
    }
  }
  else if (curLeft < 186) {
    leftSend = mapFloat(float(curLeft), LEFT_BACKWARD_MAX, LEFT_BACKWARD_MIN, -100.0, 0.0);
    if (TEST_MODE) {
      analogWrite(leftBackwardLED, map(curLeft, LEFT_BACKWARD_MIN, LEFT_BACKWARD_MAX, 0, 255));
      analogWrite(leftForwardLED, 0);
    }
  }
  else {
    leftSend = 0.0;
    if (TEST_MODE) {
      analogWrite(leftForwardLED, 0);
      analogWrite(leftBackwardLED, 0);
    }
  }
  // Map right thrust value to PWM
  if (curRight > 190) {
    rightSend = mapFloat(float(curRight), RIGHT_FORWARD_MIN, RIGHT_FORWARD_MAX, 0.0, 100.0);
    if (TEST_MODE) {
      analogWrite(rightForwardLED, map(curRight, RIGHT_FORWARD_MIN, RIGHT_FORWARD_MAX, 0, 255));
      analogWrite(rightBackwardLED, 0);
    }
  }
  else if (curRight < 186) {
    rightSend = mapFloat(float(curRight), RIGHT_BACKWARD_MAX, RIGHT_BACKWARD_MIN, -100.0, 0.0);
    if (TEST_MODE) {
      analogWrite(rightBackwardLED, map(curRight, RIGHT_BACKWARD_MIN, RIGHT_BACKWARD_MAX, 0, 255));
      analogWrite(rightForwardLED, 0);
    }
  }
  else {
    rightSend = 0.0;
    if (TEST_MODE) {
      analogWrite(rightForwardLED, 0);
      analogWrite(rightBackwardLED, 0);
    }
  }
}
