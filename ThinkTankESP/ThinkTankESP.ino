/*
   Think Tank ESP Communication
   Author: Benjamin Sabean
   Date: May 12, 2018
*/

#include <AERClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP_SSD1306.h>      // Modification of Adafruit_SSD1306 for ESP8266 compatibility
#include <Adafruit_GFX.h>     // Needs a little change in original Adafruit library (See README.txt file)
#include <SPI.h>              // For SPI comm (needed for not getting compile error)
#include <Wire.h>             // For I2C comm
#include <OneWire.h>          // Used only for crc8 algorithm (no object instantiated)
//#include <SoftwareSerial.h>   // Used for communication between ESP and Arduino
//
//  DEFINITIONS
//
#define DEVICE_ID     8       // This device's unique ID as seen in the database
#define BUFFSIZE      80      // Size for all character buffers
#define TIMEOUT       60      // Timeout for putting ESP into soft AP mode
#define AP_SSID       "ThinkTank" // Name of soft-AP network
#define DELIM         ":"     // Delimeter for serial communication
#define SUBSCRIPTION  "8/CONTROL/TIME" // MQTT topic to change sample time
// Pin defines
#define BUTTON        14      // Interrupt to trigger soft AP mode
#define STATUS_LIGHT  13      // Light to indicate that HTTP server is running
#define OLED_RESET    15      // Pin 15 -RESET digital signal
#define RX            2       // SoftSerial COM RX
#define TX            16      // SoftSerial COM TX
#define COUNT_EVERY   20      // Send device count every N messages
#define WIFI_HEADER     "   WiFi"
#define SENSORS_HEADER  "  Sensors"
#define APMODE_HEADER   "  APmode"

//  Wifi setup
char ssid[BUFFSIZE];          // Wi-Fi SSID
char password[BUFFSIZE];      // Wi-Fi password
char ip[BUFFSIZE] = "192.168.4.1";  // Default soft-AP ip address
char buf[BUFFSIZE];           // Temporary buffer for building messages on display
volatile bool apMode = false; // Flag to dermine current mode of operation
volatile bool prev_state = false;
volatile int loop_count = 0;

// Create Library Object password
AERClient aerServer(DEVICE_ID);  // publishing to IoT cloud
ESP8266WebServer server(80);     // webServer
//SoftwareSerial Arduino(RX, TX);  // for Serial
ESP_SSD1306 display(OLED_RESET); // for I2C

//
//  MAIN
//
void setup()
{
  // Erasing old wifi configs
  ESP.eraseConfig();

  // COM
  Serial.begin(9600);
  //Arduino.begin(9600);
  delay(100);  // Wait for serial port to connect
  Serial.println("\n--- START ---");

  // GPIO
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LIGHT, OUTPUT);

  // SSD1306 OLED display init
  display.begin(SSD1306_SWITCHCAPVCC);
  display.display();
  delay(2000);           // Display logo
  display.clearDisplay();

  // get SSID and password from EEPROM
  EEPROM.begin(512);
  strcpy(ssid, getString(0));
  strcpy(password, getString(BUFFSIZE));
    
  // Initialization and connection to WiFi
  sprintf(buf, "%s\nConnecting\n...", ssid);
  writeToDisplay(WIFI_HEADER, buf);

  // Attemp to connect to AP and print status
  printWiFiState(aerServer.init(ssid, password));

  // Set a callback function for MQTT
  void (*pCallback)(char*, byte*, unsigned int);
  pCallback = &callback;
  aerServer.subscribe(SUBSCRIPTION, pCallback);
}

void loop()
{
  char addr[BUFFSIZE], topic[BUFFSIZE], val[BUFFSIZE], *p;
  int _string = 0, count = 0;
  bool result;

  //
  //  WiFi connected, streaming Serial data
  //
  if (!apMode)
  {
    //Serial.println("WiFiConnected() => " + String(aerServer.wifiConnected()));
    /*
      if (aerServer.wifiConnected() != connected)
      {
      connected = aerServer.wifiConnected();
      if (connected) {
        sprintf(buf, "%s\n  ONLINE", ssid);
        writeToDisplay(WIFI_HEADER, buf);
        // Set a callback function for MQTT
        void (*pCallback)(char*, byte*, unsigned int);
        pCallback = &callback;
        aerServer.subscribe(SUBSCRIPTION, pCallback);
      } else {
        Serial.println("Connection timed out");
        Serial.print("SSID: ");     Serial.println(ssid);
        Serial.print("Password: "); Serial.println(password);
        sprintf(buf, "%s\n  OFFLINE", ssid);
        writeToDisplay(WIFI_HEADER, buf);
      }
      }
    */
    if (Serial.available() > 0)
    {
      // Reading incomming data
      readString(buf, sizeof(buf));
      Serial.println();
      Serial.println(buf);

      // Validating all sections + CRC
      if (validityCheck(buf) == 1)
      {
        Serial.println("Validation FAILED");
        return;   // Do not go further and post
      }
      // Breaking messsage down to sensor addres, string and value
      if (parseData(buf, &count, addr, &_string, val))
      {
        Serial.println("Parsing FAILED");
        return;
      }
      // Sending device count every 5 sensors
      loop_count++;
      if (loop_count > COUNT_EVERY)
      {
        loop_count = 0;
        // Sending device count
        sprintf(buf, "%d", count);
        result = aerServer.publish("System/DeviceCount", buf);
        if (!result) Serial.println("Send: FAILED");
        delay(10);
      }
      // Sending sensor value
      sprintf(topic, "Data/%s", addr);
      sprintf(buf, "%s%d", DELIM, _string);
      strcat(val, buf);
      Serial.print(topic); Serial.println(val);
      result = aerServer.publish(topic, val);
      if (!result) Serial.println("Send: FAILED");

      // Update display on every state change
      if (result != prev_state)
      {
        prev_state = result;  // save the new state
        printWiFiState(result);
      }
    }

  }
  //  AP Mode
  else
    server.handleClient();

  //
  //  AP Button was pressed
  //
  if (!digitalRead(BUTTON) && !apMode)
  {
    delay(10);
    Serial.println("\n\n\n -- Setting soft-AP -- ");
    // LED indicator ON
    digitalWrite(STATUS_LIGHT, HIGH);
    // Disconnect from MQTT
    aerServer.disconnect();
    // Disconnect from the router
    WiFi.disconnect(true);
    // Turn WiFi Off
    //ESP.eraseConfig();
    WiFi.mode(WIFI_OFF);
    // Setting AP station on
    WiFi.mode(WIFI_AP_STA);// WiFi.mode(WIFI_AP_STA);
    // Starting softAP
    if (!WiFi.softAP(AP_SSID))
      return;
    Serial.println("Soft AP Ready");

    // Handlers
    server.on("/", handleRoot);
    server.on("/success", handleSubmit);
    server.on("/inline", []() {
      server.send(200, "text/plain", "this works without need of authentification");
    });
    server.onNotFound(handleNotFound);

    //here the list of headers to be recorded
    const char * headerkeys[] = {
      "User-Agent", "Cookie"
    } ;
    size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
    //ask server to track these headers
    server.collectHeaders(headerkeys, headerkeyssize );
    server.begin();
    Serial.println("HTTP server started");

    // setting status True
    apMode = true;
    // Printing to the screen
    sprintf(buf, "%s\n%s\n", AP_SSID, ip);
    writeToDisplay(APMODE_HEADER, buf);
  }

  // must be < 125ms to read serial from arduino reliably
  delay(90);
}


//
//  FUNCTIONS
//
//------------------------------------------------------
// Get a string from EEPROM
// @param startAddr the starting address of the string in EEPROM
// @return char* the current string at the address given by startAddr
//-------------------------------------------------------
char* getString(int startAddr)
{
  char str[BUFFSIZE];
  memset(str, '\0', BUFFSIZE);

  for (int i = 0; i < BUFFSIZE; i ++)
    str[i] = char(EEPROM.read(startAddr + i));

  return str;
}

//------------------------------------------------------
// Function to be run anytime a message is recieved
// from MQTT broker
// @param topic The topic of the recieved message
// @param payload The revieved message
// @param length the lenght of the recieved message
// @return void
//-------------------------------------------------------
void callback(char* topic, byte * payload, unsigned int length) {
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

//------------------------------------------------------
// Write to the SSD1306 OLED display
// @param header A string to be printed in the header section of the display
// @param msg A string to displayed beneath th header
// @return void
//-------------------------------------------------------
void writeToDisplay(char* header, char* msg) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(header);
  display.println(msg);
  display.display();
}

//------------------------------------------------------
// Page to inform the user they successfully changed
// Wi-Fi credentials
// @param none
// @return void
//-------------------------------------------------------
void handleSubmit() {
  String content = "<html><body><H2>WiFi information updated!</H2><br>";
  server.send(200, "text/html", content);
  delay(5000);
  // Shutdown routine
  digitalWrite(STATUS_LIGHT, LOW);
  Serial.println("Restarting");
  WiFi.softAPdisconnect(); delay(500);
  WiFi.mode(WIFI_OFF); delay(500);
  ESP.restart();
}

//------------------------------------------------------
// Page to enter new Wi-Fi credentials
// @param none
// @return void
//-------------------------------------------------------
void handleRoot() {
  String htmlmsg;
  if (server.hasArg("SSID") && server.hasArg("PASSWORD"))
  {
    char newSSID[BUFFSIZE];
    char newPw[BUFFSIZE];
    memset(newSSID, NULL, BUFFSIZE);
    memset(newPw, NULL, BUFFSIZE);
    server.arg("SSID").toCharArray(newSSID, BUFFSIZE);
    server.arg("PASSWORD").toCharArray(newPw, BUFFSIZE);

    for (int i = 0; i < BUFFSIZE; i++) {
      EEPROM.write(i, newSSID[i]);              // Write SSID to EEPROM
      EEPROM.write((i + BUFFSIZE), newPw[i]);   // Write Password to EEPROM
    }
    EEPROM.commit();

    server.sendContent("HTTP/1.1 301 OK\r\nLocation: /success\r\nCache-Control: no-cache\r\n\r\n");
    return;
  }
  String content = "<html><body><form action='/' method='POST'>Please enter new SSID and password.<br>";
  content += "SSID:<input type='text' name='SSID' placeholder='SSID'><br>";
  content += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form>" + htmlmsg + "<br>";
  server.send(200, "text/html", content);
}

//------------------------------------------------------
// Page displayed on HTTP 404 not found error
// @param none
// @return void
//-------------------------------------------------------
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", message);
}

//------------------------------------------------------
// Hardware interrupt triggered by button press. Starts
// soft AP mode
// @param none
// @return void
//-------------------------------------------------------
void btnHandler()
{

}


//------------------------------------------------------
// Read string of characters from serial monitor
//-------------------------------------------------------
void readString (char* buff, int len)
{
  int i;
  // Delay to wait for all data to come in
  delay(40);
  for (i = 0; i < len && Serial.available(); i++)
    buff[i] = Serial.read();
  buff[i - 2] = '\0'; // Crop end of line [\n]
}

//------------------------------------------------------
// Extracts the last byte for CRC and checks with internal CRC8
// Return 0 if sucess, 1 if failed
// Message format: COUNT:ADDR:STRING:VALUE:CRC8
//-------------------------------------------------------
bool validityCheck(const char* buff)
{
  byte CRC_calc;
  char _buff[BUFFSIZE], CRC_received[BUFFSIZE], *p;

  // Copying the buffer to not mess with incomming data
  strcpy(_buff, buff);
  // Extracting COUNT
  if (strtok(_buff, DELIM) == NULL) return 1;
  // Extracting ADDR
  if (strtok(NULL, DELIM) == NULL) return 1;
  // Extracting STRING
  if (strtok(NULL, DELIM) == NULL) return 1;
  // Extracting VALUE
  if (strtok(NULL, DELIM) == NULL) return 1;
  // Extracting CRC8
  p = strtok(NULL, DELIM);
  if (p == NULL) return 1;
  strcpy(CRC_received, p);
  // Calculatring CRC8 (!without the extra CRC byte and one byte delimiter!)
  CRC_calc = OneWire::crc8((byte*)buff, strlen(buff) - 3);
  sprintf(_buff, "%02X", CRC_calc);
  // Check Equality
  if (strcmp(CRC_received, _buff))
    return 1;
  else
    return 0;
}

//------------------------------------------------------
// Breaks down incomming serial message to 3 sections
// Returns sensors count, address, string index and data
// Message format: COUNT:ADDR:STRING:VALUE:CRC8
//-------------------------------------------------------
bool parseData(char* msg, int* count, char* addr, int* st, char* val)
{
  // Getting sensors count
  char *p = strtok(msg, DELIM);
  if (p == NULL) return 1;
  sscanf(p, "%d", count);
  // Getting sensor address
  p = strtok(NULL, DELIM);
  if (p == NULL) return 1;
  strcpy(addr, p);
  // Getting sensor string index
  p = strtok(NULL, DELIM);
  if (p == NULL) return 1;
  sscanf(p, "%d", st);
  // Getting sensor value
  p = strtok(NULL, DELIM);
  if (p == NULL) return 1;
  strcpy(val, p);
  return 0;
}

//------------------------------------------------------
// Breaks down incomming serial message to 3 sections
// Returns sensors count, address, string index and data
// Message format: COUNT:ADDR:STRING:VALUE:CRC8
//-------------------------------------------------------
void printWiFiState(bool status)
{
  // ONLINE //
  if (status) {
    sprintf(buf, "%s\n  ONLINE", ssid);
    writeToDisplay(WIFI_HEADER, buf);
    // OFFLINE //
  } else {
    Serial.println("Connection timed out");
    sprintf(buf, "%s\n  OFFLINE", ssid);
    writeToDisplay(WIFI_HEADER, buf);
  }
}

