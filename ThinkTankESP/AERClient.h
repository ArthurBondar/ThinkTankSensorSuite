/*
   Library to Interface with IoT server AERLab over MQTT
   Date: May 17, 2017
   Author: Arthur Bondar
   Email: arthur.bondar.1@gmail.com
*/


#ifndef AERClient_h
#define AERClient_h

#include <ESP8266WiFi.h>          // ESP WiFi Libarary
#include <PubSubClient.h>         // MQTT publisher/subscriber client 
#include "Arduino.h"

/*
    API for interfacing with the server
    Uses MQTT as underlining protocol
*/
class AERClient
{
  public:
    // Constructors
    AERClient(int);
    ~AERClient();
    // Methods
    uint8_t init();
    uint8_t init(const char*, const char*);
    bool publish(String, String);
    bool subscribe(char* , void (*pCallback)(char*, byte*, unsigned int));
    void debug();
    void disconnect();
    bool wifiConnected();
    // Parameters
    char* mqtt_user = "aerlab";
    char* mqtt_pswd = "server";
    char* mqtt_server = "aerlab.ddns.net";
    int mqtt_port = 1883;
    int _ID;
    int mqtt_reconnect_retry = 1;
  private:
    WiFiClient _espClient;          // Wifi library object
    PubSubClient* _client;          // MQTT library object
    String macToStr(const uint8_t* mac);
    void reconnect();
    String clientName;
};

#endif
