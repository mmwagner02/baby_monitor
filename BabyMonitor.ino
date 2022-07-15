#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include "secret.h"


//#define DEBUG

WebServer server(80);

// Temperature Stuff
#define DHTTYPE DHT22 
#define DHTPIN  21
DHT_Unified dht(DHTPIN, DHTTYPE);
volatile double tempReading = 0;
volatile double humReading = 0;

// Sound Stuff
const int micPin = 39;
volatile bool micReading = false;
//#define USE_INTERRUPT_FOR_SOUND
#ifdef USE_INTERRUPT_FOR_SOUND
SemaphoreHandle_t soundSem;
void IRAM_ATTR onMic() {
  xSemaphoreGive(soundSem);
  micReading = true;
}
#endif

// Motion Stuff
const int motionPin = 14;
volatile bool motionReading = false;

// Light Stuff
const int photoResistor = A0;
volatile int photoResistorRead = 0;


const int led = 13;


SemaphoreHandle_t metricsSem;
TaskHandle_t metricsSampler;

void setup(void) {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif
  
  metricsSem = xSemaphoreCreateMutex();
  
  pinMode(led, OUTPUT);
  pinMode(photoResistor, INPUT);
  pinMode(motionPin, INPUT);
  pinMode(micPin, INPUT);
  #ifdef USE_INTERRUPT_FOR_SOUND
    soundSem = xSemaphoreCreateBinary();
    attachInterrupt(micPin, onMic, RISING);
  #endif  
  dht.begin();

  // Setup WiFi
  WiFi.config(local_IP, gateway, subnet);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  MDNS.begin("esp32");
  server.on("/", handleRoot);
  server.on("/metrics", handleMetrics);
  server.onNotFound(handleNotFound);
  server.begin();
  digitalWrite(led, 0); // turn the LED off
  
  xTaskCreatePinnedToCore(
    recordMeasurements,    /* Task function. */
    "RecordMeasurements",  /* name of task. */
    10000,                 /* Stack size of task */
    NULL,                  /* parameter of the task */
    2,                     /* priority of the task */
    &metricsSampler,       /* task handle */
    0                      /* pin task to core */
  );
}

void loop(void) {
  server.handleClient();
}
void recordMeasurements( void * pvParameters ) {
  while(true) {
    xSemaphoreTake(metricsSem, 5);
    
    photoResistorRead = analogRead(photoResistor);
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    tempReading = event.temperature;
    dht.humidity().getEvent(&event);
    humReading = event.relative_humidity;
  
    motionReading = digitalRead(motionPin);
    #ifndef USE_INTERRUPT_FOR_SOUND
    micReading = !digitalRead(micPin);
    #endif
    xSemaphoreGive(metricsSem);
    delay(10000);
  }
}

void handleMetrics() {
  digitalWrite(led, 1);
  xSemaphoreTake(metricsSem, 5);
  
  String message="";
  message += "# TYPE light gauge\n";
  message += "# HELP light the raw reading from the photoresitor 0-4095\n";
  message += "light ";
  message += photoResistorRead;
  message += "\n";

  if (isnan(tempReading)) {
    message += "# No temperature available\n";
  } else {
    message += "# TYPE temp gauge\n";
    message += "# HELP temp degrees celcius\n";
    message += "temp ";
    message += tempReading;
    message += "\n";
  }

  if (isnan(humReading)) {
    message += "# No humidity available\n";
  } else {
    message += "# TYPE humiduty gauge\n";
    message += "# HELP humidity relative percent\n";
    message += "humidity ";
    message += humReading ;
    message += "\n";
  }

  #ifdef USE_INTERRUPT_FOR_SOUND
  xSemaphoreTake(soundSem, 5);
  #endif
  message += "# TYPE sound gauge\n";
  message += "# HELP sound 0 for no sound, 1 for sound\n";
  message += "sound ";
  message += micReading;
  message += "\n";
  #ifdef USE_INTERRUPT_FOR_SOUND
  micReading = false;
  #endif
  
  message += "# TYPE motion gauge\n";
  message += "# HELP motion 0 for no motion, 1 for motion\n";
  message += "motion ";
  message += motionReading;
  message += "\n";
  
  server.send(200, "text/plain", message);
  digitalWrite(led, 0);
  xSemaphoreGive(metricsSem);
}
void handleRoot() {
  digitalWrite(led, 1);
  String message = "hello from esp32!";
  server.send(200, "text/plain", message);
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
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
  digitalWrite(led, 0);
}
