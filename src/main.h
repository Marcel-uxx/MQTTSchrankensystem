#ifndef MAIN_H
#define MAIN_H

// MQTT
void setupMqtt();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
const char * boolToString(uint8_t i);

// Funktionen OLED-Display
void initOLEDdisplay();

// Funktionen MPU
void initMPU();

// Funktionen Servo
void initServo();

// WiFi
void setupWifi();

// Ampelfunktionen
void tick(Ampel* sm);
void autoRequestGreen(Ampel* sm);
void printCurrentAmpelState();

void initRedLight();
void initYellowLight();
void initGreenLight();
void initBttn();

void turnOnRedLight();
void turnOffRedLight();
void turnOnYellowLight();
void turnOffYellowLight();
void turnOnGreenLight();
void turnOffGreenLight();

void initSchranke();
void openSchranke();
void closeSchranke();
void wait();

void toggleSchranke(Ampel* sm);
void checkBeschleunigung();
void sendRequest();
void isShaked();
void publishStateOnChange();
void publishEvent(const char* msg);
void onButtonInterrupt();
void startWritingOnDisplay();

#endif
