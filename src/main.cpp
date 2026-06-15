//For testing purposes use https://www.hivemq.com/demos/websocket-client/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Ampel.hpp"
#include "Driver.hpp"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>
#include <ESP32Servo.h>
#include <string.h>
#include "main.h"

// Ampel
Ampel Sm;
unsigned long ampelTime = 0;
const uint8_t redLed1 = 19;
const uint8_t redLed2 = 4;
const uint8_t yellowLed1 = 18;
const uint8_t yellowLed2 = 2;
const uint8_t greenLed1 = 17;
const uint8_t greenLed2 = 15;

const uint8_t bttn = 34;

bool ampel = false;
bool schranke = false;
bool leveraged = false;

// OLED-Bildschirm
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// MPU6050
#define MPU6050_ADRESS 0x68
Adafruit_MPU6050 mpu;
const uint8_t SDA_PIN = 21;
const uint8_t SCL_PIN = 22;

// ServoMotor
Servo servo;
const uint8_t servo_PIN = 23;
const int freq = 1000;

// WiFi- & PubSubClient
WiFiClient wificlient;
PubSubClient client(wificlient);

const char* ssid = "Wokwi-GUEST";  // "Wokwi-GUEST" für WokwiSimulator
const char* wifi_pass = "";
const char* mqtt_server = "broker.hivemq.com";
const uint16_t mqtt_port = 1883;
const char* mqtt_user = "";   //später für TLS
const char* mqtt_passwd = "";   //später für TLS


// MQTT
const char * REQUEST_TOPIC = "schranke/1/request";  
const char * STATE_TOPIC = "schranke/1/state";  
const char * EVENT_TOPIC = "schranke/1/event";  
volatile bool buttonInterruptFlag = false;  //Button Flag



void setup() {
  Serial.begin(9600);
  Serial.println("Booting ESP32...");
  Ampel_ctor(&Sm);
  Ampel_start(&Sm);
  setupMqtt();
  setupWifi();
  initOLEDdisplay();
  initMPU();
  initServo();
  initBttn();
  initSchranke();
  attachInterrupt(digitalPinToInterrupt(bttn), onButtonInterrupt, CHANGE); // sendet ("Request Ampel" / "Request Schranke"), wenn Taster gedrückt
  delay(1500);
  Serial.println("Booting ESP32 successfully :)");
}

void loop() {
  publishStateOnChange();
  checkBeschleunigung();  // überprüft Beschleunigungssensor auf Bewegung
  if(!client.connected()) {
    reconnect();
  }
  client.loop();
  if(buttonInterruptFlag) {
    sendRequest();
    buttonInterruptFlag = false;
  }
  toggleSchranke(&Sm);  // geht in State Schranke, wenn Bedingung erfüllt
  tick(&Sm);  // tick für Ampel
  autoRequestGreen(&Sm);
  //printCurrentAmpelState();
  isShaked(); // sendet ("Request Ampel" / "Request Schranke"), wenn Bewegung bei Beschleunigungssensor

}

void setupWifi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, wifi_pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected to WiFi!");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupMqtt() {
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void reconnect() {
  while(!client.connected()) {
    Serial.println();
    Serial.println("Attempting MQTT connection...");
    if(client.connect("arduinoclient")) {
      Serial.println("connected");
      client.publish("outTopic", "Hello World");
      client.subscribe(REQUEST_TOPIC);
    } else {
      Serial.print("failed, rc-");
      Serial.print(client.state());
      Serial.println("Try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {  // holt MQTT-Nachricht
  Serial.print("Message arived [");
  Serial.print(topic);
  Serial.print("] : ");
  if(strcmp(topic, REQUEST_TOPIC) != 0) {
    Serial.println("Message ignored - wrong topic");
    return;
  }
  char msg[16];
  if(length >= sizeof(msg)) {
    length = sizeof(msg) -1;
  }
  for(unsigned int i = 0; i < length; i++) {
    msg[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  msg[length] = '\0';
  Serial.println();
  if(strcmp(msg, "ampel") == 0) {
    ampel = true;
    Serial.println("Flag ampel = true");
  } else if(strcmp(msg, "schranke") == 0){
    schranke = true;
    Serial.println("Flag schranke = true");
  } else {
    Serial.println("Unerwartete Nachricht: ");
    Serial.print(msg);
  }
}

void toggleSchranke(Ampel* sm) {  // geht in State TrafficSchranke, wenn Bedingungen passen
  if(schranke && sm -> state_id == Ampel_StateId_TRAFFICLIGHTSTATERED) {
    Ampel_dispatch_event(sm, Ampel_EventId_SCHRANKE);
    schranke = false;
  } else if (ampel && sm -> state_id == Ampel_StateId_TRAFFICSCHRANKE) {
    Ampel_dispatch_event(sm, Ampel_EventId_AMPEL);
    ampel = false;
  }
}

void checkBeschleunigung() {    // setzt bei Veränderung des Beschleunigungssensors leveraged auf true/false
  sensors_event_t acc, gyro, temp;
  mpu.getEvent(&acc,&gyro,&temp);
  const float xMin = -9.0;
  const float xMax = 9.0;
  const float yMin = -9.0;
  const float yMax = 9.0;
  const float zMin = 9.3;
  const float zMax = 10.1;
  float x = acc.acceleration.x;
  float y = acc.acceleration.y;
  float z = acc.acceleration.z;
  if((x > xMin && x < xMax) && 
      (y > yMin && y < yMax) && 
      (z > zMin && z < zMax)) {
    leveraged = false;
  } else {
    leveraged = true;
  }
}

void onButtonInterrupt() {
  buttonInterruptFlag = true;
  //Serial.println("Schranke durch Button per MQTT angefragt...");
}

void sendRequest() {  // soll bei Tastendruck ("Request-Ampel" / "Request-Schranke") per MQTTs senden
  if(Sm.state_id != Ampel_StateId_TRAFFICSCHRANKE) {
    publishEvent("Request Schranke");
  } else if(Sm.state_id == Ampel_StateId_TRAFFICSCHRANKE) {
    publishEvent("Request Ampel");
  }
}

void isShaked() {
  if(leveraged) {
    sendRequest();
    leveraged = false;
  }
}

void publishEvent(const char* msg) {
  if(client.connected()) {
    client.publish(EVENT_TOPIC, msg);
    Serial.println(" MQTT Event gesendet: ");
    Serial.print(EVENT_TOPIC);
    Serial.print(" -> ");
    Serial.print(msg);
  } else {
    Serial.println("MQTT nicht verbunden, Event nicht gesendet.");
  }
}

void publishStateOnChange() {  // schaut, ob Zustand der Ampel geändert wurde und schickt an Topic den neuen State
  static Ampel_StateId lastState = (Ampel_StateId) -1;
  if(Sm.state_id != lastState) {
    lastState = Sm.state_id;

    const char* stateStr = Ampel_state_id_to_string(Sm.state_id);
    //Serial.println();
    //Serial.println("Neuer Zustand: ");
    //Serial.print(stateStr);
    startWritingOnDisplay();
    display.println("Neuer Zustand: ");
    display.println(stateStr);
    display.display();
    if(client.connected()) {
      client.publish(STATE_TOPIC, stateStr);
    }
  }
}

const char* boolToString(uint8_t i) {
  switch(i) {
    case 0: {
      return "LOW";
    }
    case 1: {
      return "HIGH";
    }
    default:
      return "-";
  }
}

void tick(Ampel* sm) {
  static unsigned long last = 0;
  unsigned long now = millis();
  while(now - last >= 1) {
    last += 1;
    Ampel_dispatch_event(sm, Ampel_EventId_TICK);
  }
}

void autoRequestGreen(Ampel* sm) {
  constexpr unsigned long MIN_RED_MS = 5000;
  if(sm -> state_id == Ampel_StateId_TRAFFICLIGHTSTATERED && ampelTime >= MIN_RED_MS) {
    Ampel_dispatch_event(sm, Ampel_EventId_REQUESTGREEN);
  }
}

void initOLEDdisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADRESS)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
}

void initMPU() {
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!mpu.begin(MPU6050_ADRESS)) {
    Serial.println("MPU6050 not found!");
    while(true) {
      delay(1000);
    }
  }
}

void initServo() {
  servo.setPeriodHertz(50);
  servo.attach(servo_PIN, 500, 2500);
}

void printCurrentAmpelState() {
  startWritingOnDisplay();
  switch(Sm.state_id) {
    case Ampel_StateId_TRAFFICLIGHTSTATERED: {
      //Serial.println("Current State: ");
      //Serial.print(Ampel_state_id_to_string(Sm.state_id));
      display.println("State : ");
      display.println(Ampel_state_id_to_string(Sm.state_id));
      display.display();
    }
    case Ampel_StateId_TRAFFICSCHRANKE: {
      //Serial.println("Current State: ");
      //Serial.print(Ampel_state_id_to_string(Sm.state_id));
      display.println("State: ");
      display.println(Ampel_state_id_to_string(Sm.state_id));
      display.display();
    }
    case Ampel_StateId_TRAFFICLIGHTSTATEREDYELLOW: {
      //Serial.println("Current State: ");
      //Serial.print(Ampel_state_id_to_string(Sm.state_id));
      display.println("State: ");
      display.println(Ampel_state_id_to_string(Sm.state_id));
      display.display();
    }
    case Ampel_StateId_TRAFFICLIGHTGREEN: {
      //Serial.println("Current State: ");
      //Serial.print(Ampel_state_id_to_string(Sm.state_id));
      display.println("State: ");
      display.println(Ampel_state_id_to_string(Sm.state_id));
      display.display();
    }
    case Ampel_StateId_TRAFFICLIGHTYELLOW: {
      //Serial.println("Current State: ");
      //Serial.print(Ampel_state_id_to_string(Sm.state_id));
      display.println("State: ");
      display.println(Ampel_state_id_to_string(Sm.state_id));
      display.display();
    }
    default: {
      Serial.println("Problem with displaying current Ampel State");
      display.println("No current State available");
      display.display();
      break;
    }
  }
}

void initRedLight() {
  pinMode(redLed1, OUTPUT);
  pinMode(redLed2, OUTPUT);
}

void initYellowLight() {
  pinMode(yellowLed1, OUTPUT);
  pinMode(yellowLed2, OUTPUT);
}

void initGreenLight() {
  pinMode(greenLed1, OUTPUT);
  pinMode(greenLed2, OUTPUT);
}

void initBttn() {
  pinMode(bttn, INPUT_PULLUP);
}

void turnOnGreenLight() {
  digitalWrite(greenLed1, HIGH);
  digitalWrite(greenLed2, HIGH);
}

void turnOffGreenLight() {
  digitalWrite(greenLed1, LOW);
  digitalWrite(greenLed2, LOW);
}

void turnOnYellowLight() {
  digitalWrite(yellowLed1, HIGH);
  digitalWrite(yellowLed2, HIGH);
}

void turnOffYellowLight() {
  digitalWrite(yellowLed1, LOW);
  digitalWrite(yellowLed2, LOW);
}

void turnOnRedLight() {
  digitalWrite(redLed1, HIGH);
  digitalWrite(redLed2, HIGH);
}

void turnOffRedLight() {
  digitalWrite(redLed1, LOW);
  digitalWrite(redLed2, LOW);
}

void initSchranke() {
  startWritingOnDisplay();
  display.println(F("Fahre Schranke in Ausgangsposition 'closed' "));
  display.display();
  Serial.println("Fahre Schranke in Ausgansposition 'closed' ");
  for(int i = 0; i < 91; i++) {
    servo.write(i);
  }
}

void openSchranke() {
  startWritingOnDisplay();
  display.println(F("Öffne Schranke..."));
  display.display();
  Serial.println("Öffne Schranke...");
  for(int i = 90; i >= 0; i--) {
    servo.write(i);
  }
}

void closeSchranke() {
  startWritingOnDisplay();
  display.println(F("Schließe Schranke..."));
  display.display();
  Serial.println("Schließe Schranke...");
  initSchranke();
}

void wait() {
  delay(4000);
}

void startWritingOnDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}
