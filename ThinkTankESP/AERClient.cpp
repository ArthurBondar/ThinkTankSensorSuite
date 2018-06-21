#include "AERClient.h"            // Library for One-Wire interface

// Constructor that that stores Module ID
AERClient::AERClient(int ID)
{
  uint8_t mac[6];
  // Creating PubSubClient Object
  _client = new PubSubClient(mqtt_server, mqtt_port, _espClient);
  _client->setServer(mqtt_server, 1883);
  // Generating ID for MQTT broker
  this->_ID = ID;
  WiFi.macAddress(mac);
  this->clientName = "esp8266-" + macToStr(mac) + "-" + String(_ID);
  // Set autoconnect on power loss
  if (!WiFi.getAutoConnect())
    WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
}

// Destructor to de-allocate memory
AERClient::~AERClient()
{
  delete this->_client;
}

//
//  Establishes connection to WiFi access point with parameters provided
//  Connects to MQTT broker - aerlab.ddns.net
//
uint8_t AERClient::init(const char* ssid, const char* password)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  return WiFi.waitForConnectResult();
}

//
//  Establishes connection to WiFi AP with stored SSID parameters
//  Connects to MQTT broker - aerlab.ddns.net
//
uint8_t AERClient::init()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  return WiFi.waitForConnectResult();
}

//
//  Published Data to AERLab server
//  First parameter - Topic, second - Payload
//
bool AERClient::publish(String topic, String payload)
{
  bool _result = false;
  if (_client->connected() && WiFi.status() == WL_CONNECTED)
  {
    _client->loop();
    _result = _client->publish((String(_ID) + "/" + topic).c_str(), payload.c_str(), 2);
  }
  else if (!_client->connected())
  {
    reconnect();                    //check for MQTT Connection
    _client->loop();
  }
  return _result;
}

//
//  Subscribe to an MQTT topic
//  First parameter - topic, Second parameter - pointer to callback function
//
bool AERClient::subscribe(char* topic, void (*pCallback)(char*, byte*, unsigned int))
{
  if (_client->connected()) {
    _client->subscribe(topic);
    _client->setCallback(pCallback);
    _client->loop();
    return true;
  }
  else {
    reconnect();
    return false;
  }

}

//
//  Connect to Wi-Fi access point
//
bool AERClient::wifiConnected()
{
  if (WiFi.localIP().toString() == "0.0.0.0" || !WiFi.isConnected())
    return false;
  else
    return true;
}

//
//  Reconnecting to MQTT broker
//
void AERClient::reconnect()
{
  // Loop until we're reconnected with timeout
  for (int i = 0; i < mqtt_reconnect_retry; i++, delay(100))
  {
    if (_client->connect(clientName.c_str(), mqtt_user, mqtt_pswd))
      break;
  }
}

//
//  MAC address used as MQTT identification
//
String AERClient::macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

//
//  Used to Debug library operation
//  Requeires Serial.begin()
//
void AERClient::debug()
{
  Serial.println("\n------- AERClient DEBUG ------- ");
  Serial.print("WiFi IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Module Name: " + clientName);
  Serial.println("Connecting to: " + WiFi.SSID());
  Serial.println("With password: " + WiFi.psk());
  Serial.println("MQTT Status: " + String(_client->state()));
  Serial.println("------- WiFi DEBUG ------- ");
  WiFi.printDiag(Serial);
}

//
//  Dissconnect MQTT client from the server
//
void AERClient::disconnect()
{
  _client->disconnect();  // disconnect from MQTT Broker
  _espClient.stop();      // Stop TCP connection
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
}
