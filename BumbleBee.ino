#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "RestClient.h"
#include <DNSServer.h>
#include <WiFiManager.h>
#include <Ticker.h>

#define HTTP_DEBUG

typedef struct am2302 {
  float temp;
  float humidity;
}dht22;
dht22 read_dht();
dht22 dht_data;

char* proj_name = "Bumblebee";
const char* heartbeat_url = "http://192.168.0.13:9801/test";
const int heartbeat_interval = 10000; // 1000 means near 1 sec
int relay_num = 0;
int loopCount = 0;

Ticker ticker;
WiFiManager wifiManager;
ESP8266WebServer server(80);


void tick() {
  int state = digitalRead(BUILTIN_LED);
  digitalWrite(BUILTIN_LED, !state);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  display_clear();
  display_lcd("AP Mode", (char*)myWiFiManager->getConfigPortalSSID().c_str());

  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void init_wifi() {
  pinMode(BUILTIN_LED, OUTPUT);
  
  Serial.print("macAddress: ");
  Serial.print(WiFi.macAddress());
  Serial.println(" ");
  Serial.println("Connecting WiFi");
  display_clear();
  display_lcd("Connect to WiFi", "");
  ticker.attach(0.6, tick);
  wifiManager.setAPCallback(configModeCallback);

  wifiManager.setTimeout(120);
  if (!wifiManager.autoConnect()) {
    Serial.println("Connect Failed");
    reset_wifi();
  } else {
    Serial.println("");
    Serial.println("WiFi connected");
  
    Serial.print("WiFi RSSI: ");
    Serial.println(WiFi.RSSI());
    // print some informations for WiFi
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    display_clear();
    display_lcd("WiFi Connected", (char*)WiFi.localIP().toString().c_str());
  
    send_heartbeat();
  
    ticker.attach(0.1, tick);
    delay(1500);
    ticker.detach();
  }
}

void reset_wifi() {
  wifiManager.resetSettings();
  display_lcd("Connecting FAILED", "Clear WiFi Setup");
}

String macToStr(const uint8_t* mac){
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
    result += ':';
  }
  return result;
}

void setup() {
  Serial.begin(115200);
  Serial.print("\nStarting... ");
  Serial.println(proj_name);
  
  // LCD init
  init_lcd();
  display_lcd("Init", proj_name);
  
  // Connect Wifi
  init_wifi();

  // 온습도 센서 초기화
  init_dht();

  // 먼지센서 초기화
  init_dust();

  // 4채널 릴레이 초기화
  init_relay();
  
  conf_rest_server();
  server.begin();
  Serial.println("HTTP server started");
}

void conf_rest_server() {
  server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", proj_name);
  });
  server.on("/relay", HTTP_GET, get_relay);
  server.on("/reset", HTTP_GET, reset_wifi);
  server.on("/relay", HTTP_POST, put_relay);
  server.on("/dht", HTTP_GET, get_dht);
  server.onNotFound(handleNotFound);        // When a client requests an unknown URI (i.e. something other than "/"), call function "handleNotFound"

}
void get_relay() {
  String json_data = "";
  DynamicJsonDocument doc(200);

  for (int i=1; i<=2; i++) {
    doc["relay_" + String(i)] = get_relay_status(i);
  }
  serializeJson(doc, Serial);
  serializeJsonPretty(doc, json_data);
  server.send(200, "text/json", json_data); 
}

void put_relay() {
  if (server.hasArg("plain") == false){ //Check if body received
    server.send(200, "text/plain", "Body not received");
    return;
  }
  String body = server.arg("plain");
  Serial.println(body);

  StaticJsonDocument<200> doc;
  deserializeJson(doc, body);
 
  for (int i=1; i<=2; i++) {
    if (doc["relay_" + String(i)] == 0) {
      turn_off(i);
    }
    else {
      turn_on(i);
    }
  }
 
  server.send(200, "text/plain", body);
}


void get_dht() {
  collect_dht();
  dht_data = read_dht();
  float temp = dht_data.temp;
  float humidity = dht_data.humidity;
  String json_data = "";
  StaticJsonDocument<200> dht;
  dht["temp"] = temp;
  dht["humidity"] = humidity;

  serializeJson(dht, json_data);
  Serial.print("get_dht: ");
  Serial.println(json_data);
  server.send(200, "text/html", json_data);
}

void send_heartbeat() {
  Serial.print("heartbeat: "); 
  String json_data = "";
  StaticJsonDocument<200> doc;
  
  doc["local_ip"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["chip_id"] = ESP.getChipId();
  doc["flashchip_id"] =  ESP.getFlashChipId();
  doc["temp"] =  dht_data.temp;
  doc["humidity"] =  dht_data.humidity;
  doc["dust"] =  get_dust();
  for (int i=1; i<=2; i++) {
    doc["relay_" + String(i)] = get_relay_status(i);
  }
  
  serializeJson(doc, json_data);

  HTTPClient http;
  //http.begin("https://blog.zeroidle.com/", "65 13 7B 3B 5E 5A B2 CC 56 63 D5 D0 EE 79 57 65 A5 C9 97 FF");
  //http.begin("https://api.medusa.ml/bumblebee/", "94 CD 13 65 AB AD 8C 72 FA 05 A0 52 59 D6 93 C8 F0 E5 46 EF");
  http.begin(heartbeat_url);
  http.addHeader("Content-Type", "application/json; charset=utf-8");  //Specify content-type header
  
  int httpCode = http.POST(json_data);   //Send the request
  String payload = http.getString();                  //Get the response payload
  if (httpCode == 200) {
    Serial.print(payload);    //Print request response payload
  }
  else {
    Serial.println(httpCode);   //Print HTTP return code
  }
  
  http.end();  //Close connection
}


void loop() {
  static long heartbeat_tick = millis();
  static long dht_tick = millis();
  server.handleClient(); 
  gather_dust();
  

  if ((millis()-heartbeat_tick) >= heartbeat_interval) {
    loopCount = 0;
    send_heartbeat();
    display_clear();
    heartbeat_tick = millis();
  }
  
  if ((millis()-dht_tick) >= 2000) {
    collect_dht();
    dht22 dht_data = read_dht();
    char *line1 = (char*)malloc(sizeof(char)*20);
    char *line2 = (char*)malloc(sizeof(char)*20);

    sprintf(line1, "T:%.1f H:%.1f  ", dht_data.temp, dht_data.humidity);
    sprintf(line2, "D:%.1f R:%s %s", get_dust(), get_relay_status_tostring(1), get_relay_status_tostring(2));
    display_lcd(line1, line2);
    free(line1);
    free(line2);
    dht_tick = millis();
  }
  delay(100);
}

void handlerelay() {                          // If a POST request is made to URI /relay
  //turn(relay_num);
  Serial.println("relay");
  server.sendHeader("Location","/");        // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303);                         // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}
