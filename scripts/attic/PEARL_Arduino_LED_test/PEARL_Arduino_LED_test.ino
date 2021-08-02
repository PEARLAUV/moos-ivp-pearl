#include <Wire.h>
#include <Adafruit_Sensor_Calibration.h>
#include <Adafruit_AHRS.h>
  
Adafruit_Sensor *accelerometer, *gyroscope, *magnetometer;

#include "NXP_FXOS_FXAS.h"  // NXP 9-DoF breakout

// pick your filter! slower == better quality output
//Adafruit_NXPSensorFusion filter; // slowest
Adafruit_Madgwick filter;  // faster than NXP
//Adafruit_Mahony filter;  // fastest/smalleset

#if defined(ADAFRUIT_SENSOR_CALIBRATION_USE_EEPROM)
  Adafruit_Sensor_Calibration_EEPROM cal;
#else
  Adafruit_Sensor_Calibration_SDFat cal;
#endif

#define FILTER_UPDATE_RATE_HZ 50
#define PRINT_EVERY_N_UPDATES 5

uint32_t timestamp;

const byte numChars = 32;
char receivedChars[numChars];
char tempChars[numChars];

char nmeaHeader[numChars] = {0};
float thrustLeft = 0.0;
float thrustRight = 0.0;

boolean newData = false;
 
void setup(void) 
{
  Serial.begin(115200);
  
  /* Load calibration */
  cal.begin();
  cal.loadCalibration();

  init_sensors();
  setup_sensors();
  filter.begin(FILTER_UPDATE_RATE_HZ);
  timestamp = millis();

  Wire.setClock(400000); // 400KHz

  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
}

const String PREFIX    = "PL";   // Device prefix: PL = PEARL
const String ID_EULER  = "IMU";  // Sentence ID for Euler angles generated from sensor fusion filter
 
void loop(void) 
{
  static uint8_t counter = 0;
  /*------Read IMU data and package in NMEA sentence--------*/
  
  float roll, pitch, heading;
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

  // only print the calculated output once in a while
  if (counter++ <= PRINT_EVERY_N_UPDATES) {
    return;
  }
  // reset the counter
  counter = 0;

  roll = filter.getRoll();
  pitch = filter.getPitch();
  heading = filter.getYaw();

  String PAYLOAD_EULER = String(0) + "," + String(heading) + "," + String(pitch) + "," + String(roll); 

  String NMEA_EULER = generateNMEAString(PAYLOAD_EULER, PREFIX, ID_EULER);

  Serial.println(NMEA_EULER);
                       

  /*-------Read NMEA sentence from serial port and parse into thrust values-------*/

  recvWithStartEndMarkers();
  if (newData == true) {
    strcpy(tempChars, receivedChars);
    parseNMEA();
    newData = false;
    if (thrustLeft > 20.0) {
      digitalWrite(8, HIGH);
    }
    else {
      digitalWrite(8, LOW);
    }
    if (thrustRight > 20.0) {
      digitalWrite(9, HIGH);
    }
    else {
      digitalWrite(9, LOW);
    }
  }

}

String generateNMEAString(String payload, String prefix, String id)
{
  String nmea = "";
  if (prefix.length() != 2)
    return nmea; // ("Bad prefix [" + prefix + "], must be 2 character long.");
  if (id.length() != 3)
    return nmea; // ("Bad ID [" + id + "], must be 3 character long.");
  nmea = prefix + id + "," + payload;
  return "$" + nmea + "*";    // Prefixed with $
}

void recvWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    char startMarker = '$';
    char endMarker = '*';
    char rc;
 
    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();

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

void parseNMEA() {      // split the data into its parts

    char * strtokIndx; // this is used by strtok() as an index

    strtokIndx = strtok(tempChars,",");      // get the first part - the string
    strcpy(nmeaHeader, strtokIndx); // copy it to nmeaHeader

    strtokIndx = strtok(NULL,",");
    thrustLeft = atof(strtokIndx);     // convert the second entry to the left thrust percentage command

    strtokIndx = strtok(NULL,",");
    thrustRight = atof(strtokIndx);     // convert the third entry to the right thrust percentage command
}
