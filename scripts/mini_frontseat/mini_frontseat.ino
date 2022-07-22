#include <IBusBM.h>

/*----------------------------------------------*/
//If true writes thrust commands to specified LED pins
//If false writes thrust commands to specified motor controller pins
bool TEST_MODE = false;
//If true sends data output to Serial port for use with Python real-time plotting scripts
bool DEBUG_MODE = false;
//Values to plot in Python, valid options are "euler","accelerometer","gyroscope","magnetometer"
char debug_type[] = "euler";   

//Pin assignments
const int anchorMotorPin = 7;

const int rightMotorPin = 8;
const int leftMotorPin = 9;

const int rightForwardLED = 10;
const int leftForwardLED = 11;
const int rightBackwardLED = 12;
const int leftBackwardLED = 13;

//Serial port assignments
IBusBM ibusRC;
HardwareSerial& ibusRCSerial = Serial2;  //RC receiver
HardwareSerial& debug = Serial;   //comms with navigation RPi/MOOS-IvP

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
int manualControl = 1;   //0 = manual control off, 1 = manual control on

const float THROTTLE_ZERO_THRESHOLD = 5;
int LIMIT = 32; //this is the PWM step limit, must be <= 65
//forward PWM values are 191-254 (63 steps), and then limited to 191 + LIMIT
//backward PWM values are 120-185 (65 steps), and then limited to 185 - LIMIT
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
  debug.begin(115200);
  while (!debug) {
    delay(1);
  }

}

const String PREFIX    = "PL";   // Device prefix: PL = PEARL
const String ID_MOTOR  = "MOT";  // Sentence ID for notifying MOOS-IvP of current motor commands

void loop(void)
{
  static uint8_t counter = 0;


  /*--------Handle manual control inputs from RC--------*/
  handleRC();
  /*--------Convert back to thrust percentage for report to MOOS-IvP--------*/
  reportThrust();     // if TEST_MODE is true then also commands LED brightness


  // only send data to serial port once in a while
  if (counter++ <= PRINT_EVERY_N_UPDATES) {
    return;
  }
  // reset the counter
  counter = 0;

  /*--------Generate NMEA strings for MOOS-IvP--------*/

  //Last motor commands NMEA string
  String PAYLOAD_MOTOR = String(leftSend) + "," + String(rightSend);
  String NMEA_MOTOR = generateNMEAString(PAYLOAD_MOTOR, PREFIX, ID_MOTOR);

  debug.println(NMEA_MOTOR);
}

String generateNMEAString(String payload, String prefix, String id) {
  String nmea = "";
  nmea = prefix + id + "," + payload;
  return "$" + nmea + "*";    // Prefixed with $
}


float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  // Re-maps a float number from one range to another.
  // That is, a value of fromLow would get mapped to toLow, a value of fromHigh to toHigh, values in-between to values in-between, etc.
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void handleRC() {
  MANUAL = readSwitch(manualCH, false);

  BACKWARD = readSwitch(drivemodeCH, false);

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
    if (!TEST_MODE) {
      analogWrite(leftMotorPin, curLeft);
      analogWrite(rightMotorPin, curRight);
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

void sendToPython(float* x, float* y, float* z) {
  byte* byteX  = (byte*)(x);
  byte* byteY  = (byte*)(y);
  byte* byteZ  = (byte*)(z);
  debug.write(byteX, 4);
  debug.write(byteY, 4);
  debug.write(byteZ, 4);
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
