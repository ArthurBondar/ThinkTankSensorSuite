/*
   Reading Temperature Data using Arduino Nano
   Date: Apr 30, 2018
*/

#include <OneWire.h>                // Library for One-Wire interface
#include <DallasTemperature.h>      // Library for DS18B20 temp. sensor
#include <SoftwareSerial.h>

//
//  DEFINITIONS
//
#define DELIM ":"
#define RX 3
#define TX 2
SoftwareSerial ESP(RX, TX);

#define ONE_WIRE_GPIO   4          // pins 4 to 11 are used to for one wire strips
#define ONE_WIRE_LENGTH 8          // The number of pins used by the bus
#define CALLIBRATION    0          // Used to callibrate the sensors (in C)
#define MAX_SENSORS     20         // Allowed maximum number of sensors
#define MESSAGE_DELAY   100        // Delay in ms between serial message

// Setting up the interface for OneWire communication
OneWire oneWireBus[]  = {
  OneWire(ONE_WIRE_GPIO),     // D4
  OneWire(ONE_WIRE_GPIO + 1), // D5
  OneWire(ONE_WIRE_GPIO + 2), // D6
  OneWire(ONE_WIRE_GPIO + 3), // D7
  OneWire(ONE_WIRE_GPIO + 4), // D8
  OneWire(ONE_WIRE_GPIO + 5), // D9
  OneWire(ONE_WIRE_GPIO + 6), // D10
  OneWire(ONE_WIRE_GPIO + 7), // D11
};
// Creating an instans of DallasTemperature Class with reference to OneWire interface
DallasTemperature Strip[] = {
  DallasTemperature(&oneWireBus[0]),
  DallasTemperature(&oneWireBus[1]),
  DallasTemperature(&oneWireBus[2]),
  DallasTemperature(&oneWireBus[3]),
  DallasTemperature(&oneWireBus[4]),
  DallasTemperature(&oneWireBus[5]),
  DallasTemperature(&oneWireBus[6]),
  DallasTemperature(&oneWireBus[7])
};
// Sensor structure with address and value
struct oneWire_struct {
  byte address[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  float value = -127.0;
  uint8_t string = 0;
};

//
//  MAIN LOOP
//
void setup()
{
  // communication
  Serial.begin(115200); // DEBUG
  ESP.begin(9600);
  Serial.println("--- START ---");
}

void loop()
{
  // Creating memory for Sensors - static
  oneWire_struct TempSensor[MAX_SENSORS];
  // Getting number of sensors
  uint8_t oneWire_count = TempSensors_init();
  // Creating structure to hold the sensors
  if (oneWire_count <= MAX_SENSORS)
  {
    // Create a pointer to pass in the full array of structures
    oneWire_struct *pOneWire = &TempSensor[0];
    // Getting temperature
    TempSensors_getTemp(&pOneWire);
    // Display current sensor readings and addresses
    PrintValues(TempSensor, oneWire_count);
  }
}

//
//  FUNCTIONS
//
// Initialized oneWire sensors + gets the number of sensors
uint8_t TempSensors_init ()
{
  uint8_t _count = 0;
  for (int i = 0; i < 8; i++)
  {
    Strip[i].begin();                   // initializing the sensors
    _count += Strip[i].getDeviceCount();// getting number of sensors
    Strip[i].requestTemperatures();     // requesting data
  }
  return _count;
}

// Reading one sensor
// Modifing pointer for one sensor with address and value
void TempSensors_getTemp(oneWire_struct **_sensor)
{
  // Loop from strip 0 to 7 from sensor 0 to 80
  for (uint8_t i = 0, sen = 0; i < 8 && sen < MAX_SENSORS; i++)
  {
    // While loop to find all the sensors on the strip
    while (oneWireBus[i].search((*_sensor + sen)->address))
    {
      (*_sensor + sen)->value = Strip[i].getTempC((*_sensor + sen)->address);
      (*_sensor + sen)->value += CALLIBRATION;
      (*_sensor + sen)->string = i + 1;
      sen++;  // Sensor found on that strip inc. the index
    }
  }
}

// Sending values to esp over software-serial, hrdwr serial for debug
// Format ADDR:STRING:VALUE:CRC8
void PrintValues(oneWire_struct* TempSensor, uint8_t count)
{
  char buff[50], temp[10];
  byte CRC;

  // Sending sensor count
  sprintf(buff, "count%s%d", DELIM, count);
  Serial.println(buff);
  ESP.println(buff);
  delay(MESSAGE_DELAY);

  // Sending sensor address and value
  // Format-> ADDR:STRING:VALUE
  for (uint8_t i = 0; i < count; i++)
  {
    sprintf(buff, "");
    // getting address
    for (uint8_t d = 0; d < 8; d++) {
      sprintf(temp, "%02X", TempSensor[i].address[d]);
      strcat(buff, temp);
    }
    // string
    sprintf(temp, "%s%d%s", DELIM, TempSensor[i].string, DELIM);
    strcat(buff, temp);
    // sensor value
    dtostrf(TempSensor[i].value, 2, 2, temp);
    strcat(buff, temp);
    // Calculating and appending CRC
    CRC = OneWire::crc8((byte*)buff, strlen(buff));
    sprintf(temp, "%s%d", DELIM, CRC);
    strcat(buff, temp);
    // Printing
    Serial.println(buff);
    ESP.println(buff);
    // Delay between serial messages
    delay(MESSAGE_DELAY);
  }
}
