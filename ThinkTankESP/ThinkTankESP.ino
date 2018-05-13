/*
   Think Tank ESP Communication
   Author: Benjamin Sabean
   Date: May 1, 2018
*/

// MQTT, HTTP & Soft AP
#include <AERClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// Display
#include <ESP_SSD1306.h>    // Modification of Adafruit_SSD1306 for ESP8266 compatibility
#include <Adafruit_GFX.h>   // Needs a little change in original Adafruit library (See README.txt file)
#include <SPI.h>            // For SPI comm (needed for not getting compile error)
#include <Wire.h>           // For I2C comm

#include <OneWire.h>        // Used only for crc8 algorithm (no object instantiated)

#define DEVICE_ID 8         // This device's unique ID as seen in the database
#define DEBUG false         // Flag to determine if debug info is displayed 

// Constants definition
#define BUFFSIZE 80                   // Size for all character buffers
#define TIMEOUT 60                    // Timeout for putting ESP into soft AP mode
#define AP_SSID  "Think Tank"         // Name of soft-AP network
#define DELIM ":"                     // Delimeter for serial communication
#define SUBSCRIPTION "8/CONTROL/TIME" // MQTT topic to change sample time

// Pin defines
#define BUTTON 14         // Interrupt to trigger soft AP mode
#define STATUS_LIGHT 13   // Light to indicate that HTTP server is running
#define OLED_RESET  16    // Pin 15 -RESET digital signal

/* Wifi setup */
char ssid[BUFFSIZE];       // Wi-Fi SSID
char password[BUFFSIZE];   // Wi-Fi password
char ip[BUFFSIZE] = "192.168.4.1";  // Default soft-AP ip address
char msg[BUFFSIZE] = "";  // Container for serial message
char topic[BUFFSIZE];     // The full MQTT topic
char ch;                  // Current character in serial buffer
char sAddr[BUFFSIZE], sVal[BUFFSIZE];   // Topic & data buffers for MQTT
char buf[BUFFSIZE];    // Temporary buffer for building messages on display
String tmp = "";       // Temproary buffer to build full topic name
bool apMode = false;   // Flag to dermine current mode of operation
bool corrupt = false;  // Flag to determine if serial buffer is corrupt

/* Create Library Object password */
AERClient aerServer(DEVICE_ID);
ESP8266WebServer server(80);

ESP_SSD1306 display(OLED_RESET); // FOR I2C

//------------------------------------------------------
// Get a string from EEPROM
// @param startAddr the starting address of the string
//            in EEPROM
// @return char* the current string at the address given
//            by startAddr
//-------------------------------------------------------
char* getString(int startAddr) {
  char str[BUFFSIZE];
  memset(str, NULL, BUFFSIZE);

  for (int i = 0; i < BUFFSIZE; i ++) {
    str[i] = char(EEPROM.read(startAddr + i));
  }
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
void callback(char* topic, byte* payload, unsigned int length) {
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

//------------------------------------------------------
// Write to the SSD1306 OLED display
// @param header A string to be printed in the header
//               section of the display
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
  digitalWrite(STATUS_LIGHT, LOW);
  //delay(500);
#if DEBUG
  Serial.println("Restarting");
#endif
  ESP.restart();
}

//------------------------------------------------------
// Page to enter new Wi-Fi credentials
// @param none
// @return void
//-------------------------------------------------------
void handleRoot() {
  String htmlmsg;
  if (server.hasArg("SSID") && server.hasArg("PASSWORD")) {
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
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

//------------------------------------------------------
// Hardware interrupt triggered by button press. Starts
// soft AP mode
// @param none
// @return void
//-------------------------------------------------------
void btnHandler() {
  digitalWrite(STATUS_LIGHT, HIGH);

  if (!apMode) {
    WiFi.disconnect(true);
    //delay(500);

    // AP SERVER
#if DEBUG
    Serial.println("Setting soft-AP");
#endif
    WiFi.mode(WIFI_AP_STA);

    for (int i = 0; i < TIMEOUT; i++) {
      if (WiFi.softAP(AP_SSID)) {
        break;
      }
      delay(10);
    }
#if DEBUG
    Serial.println("Ready");
#endif

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
#if DEBUG
    Serial.println("HTTP server started");
#endif

    // Stop AERClient from reconnecting to Wi-Fi so that HTTP server
    // (in soft AP mode) will be more responsive when Wi-Fi credentials
    // are incorrect.
    aerServer.disableReconnect();
    apMode = true;
  }
  else {
#if DEBUG
    Serial.println("Already in soft AP mode!");
#endif
  }
}

void setup()
{
  // Set a callback function for MQTT
  void (*pCallback)(char*, byte*, unsigned int);
  pCallback = &callback;

  // Set pins and baud rate
  Serial.begin(9600);
  delay(10);  // Wait for serial port to connect
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LIGHT, OUTPUT);
#if DEBUG
  Serial.println("\nStart");
#endif

  // SSD1306 OLED display init
  display.begin(SSD1306_SWITCHCAPVCC);
  display.display();
  delay(2000);           // Display logo
  display.clearDisplay();

  // Empty ssid and password buffers
  memset(ssid, NULL, BUFFSIZE);
  memset(password, NULL, BUFFSIZE);

  // Start communication with EEPROM
  EEPROM.begin(512);
  strcpy(ssid, getString(0));
  strcpy(password, getString(BUFFSIZE));

  // Initialization and connection to WiFi
  writeToDisplay("Connecting", ssid);
  if (aerServer.init(ssid, password)) {
    writeToDisplay("Connected!", ssid);
    aerServer.subscribe(SUBSCRIPTION, pCallback);
#if DEBUG
    Serial.println("Connected!");
    aerServer.debug();
#endif
  }
  else {
#if DEBUG
    Serial.println("Connection timed out");
    Serial.print("SSID: ");    Serial.println(ssid);
    Serial.print("Password: ");    Serial.println(password);
#endif
    writeToDisplay("Timed out", ssid);
    attachInterrupt(digitalPinToInterrupt(BUTTON), btnHandler, FALLING);
  }
  Serial.readString();   // Empty Serial buffer
}

void loop()
{
  if (apMode) {
    char buf[BUFFSIZE];
    String tmp = "";
    memset(buf, NULL, BUFFSIZE);
    tmp = String(AP_SSID) + "\n" + String(ip);
    tmp.toCharArray(buf, BUFFSIZE);
    writeToDisplay("Soft-AP", buf);

    server.handleClient();
  }
  else {
    // Publish Data
    while (Serial.available()) {
      corrupt = false;
      delay(10);
      memset(msg, NULL, BUFFSIZE);

      for (int i = 0; i < BUFFSIZE; i ++) {  // Get Address
        ch = Serial.read();
        if (String(ch) == String(DELIM)) {  // Check for delimeter
          break;
        }
        msg[i] = ch;
        delay(10);           // Wait for Serial buffer
        if (Serial.peek() == -1) {   // Check for missing data
          memset(buf, NULL, BUFFSIZE);
          tmp = String(ssid) + "\n" + "Corrupt Data";
          tmp.toCharArray(buf, BUFFSIZE);
          writeToDisplay("Status", buf);
          corrupt = true;
#if DEBUG
          Serial.println("Data Missing");
#endif
          break;
        }
      }
      strcpy(sAddr, msg);

      memset(msg, NULL, BUFFSIZE);
      for (int i = 0; i < BUFFSIZE; i ++) {  // Get Data
        ch = Serial.read();
        if (String(ch) == String("\n")) {
          break;
        }
        msg[i] = ch;
        delay(10);           // Wait for Serial buffer
        if (Serial.peek() == -1) {
          memset(buf, NULL, BUFFSIZE);
          tmp = String(ssid) + "\n" + "Corrupt";
          tmp.toCharArray(buf, BUFFSIZE);
          writeToDisplay("Status", buf);
          corrupt = true;
#if DEBUG
          Serial.println("Data Missing");
#endif
          break;
        }
      }
      strcpy(sVal, msg);
      if (!corrupt) {
        memset(buf, NULL, BUFFSIZE);
        tmp = String(ssid) + "\n" + "Sending";
        tmp.toCharArray(buf, BUFFSIZE);
        writeToDisplay("Status", buf);
        if (sAddr[0] == '[') {         // Data
          memset(topic, NULL, BUFFSIZE);
          strcat(topic, "DATA/");
          strcat(topic, sAddr);
#if DEBUG
          Serial.println(topic);
          Serial.println(sVal);
#endif
          if (aerServer.publish(topic, sVal)) {
            memset(buf, NULL, BUFFSIZE);
            tmp = String(ssid) + "\n" + "Sent";
            tmp.toCharArray(buf, BUFFSIZE);
            writeToDisplay("Status", buf);
#if DEBUG
            Serial.println("Msg Sent!");
#endif
          }
          else {
            memset(buf, NULL, BUFFSIZE);
            tmp = String(ssid) + "\n" + "Failed";
            tmp.toCharArray(buf, BUFFSIZE);
            writeToDisplay("Status", buf);
#if DEBUG
            Serial.println("Msg Failed!");
#endif
          }
        }
        else {                         // Runtime Info
          memset(topic, NULL, BUFFSIZE);
          strcat(topic, "System/");
          strcat(topic, sAddr);
#if DEBUG
          Serial.println(topic);
          Serial.println(sVal);
#endif
          if (aerServer.publish(topic, sVal)) {
            memset(buf, NULL, BUFFSIZE);
            tmp = String(ssid) + "\n" + "Sent";
            tmp.toCharArray(buf, BUFFSIZE);
            writeToDisplay("Status", buf);
#if DEBUG
            Serial.println("Msg Sent!");
#endif
          }
          else {
            memset(buf, NULL, BUFFSIZE);
            tmp = String(ssid) + "\n" + "Failed";
            tmp.toCharArray(buf, BUFFSIZE);
            writeToDisplay("Status", buf);
#if DEBUG
            Serial.println("Msg Failed!");
#endif
          }
        }
        if (!digitalRead(BUTTON)) {
          Serial.readString();
          btnHandler();
        }
      }
    }
  }
  if (!digitalRead(BUTTON)) {
    Serial.readString();
    btnHandler();
  }
  aerServer.loop();
  delay(100);
}
