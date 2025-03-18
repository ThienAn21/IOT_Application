
// Import required libraries
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "Wire.h"
#include "DHT20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Thingsboard.h"
#include "Arduino_MQTT_Client.h"

// Replace with your network credentials
constexpr char WIFI_SSID[] = "Redmi Note 11";
constexpr char WIFI_PASSWORD[] = "1234567890";

constexpr char TOKEN[] = "st9575uaTqBbHZofKnGA";

constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;


// Set LED GPIO
// const int ledPin = 13;

// Stores LED state
// String ledState;

// Create AsyncWebServer object on port 80
// AsyncWebServer server(80);


DHT20 dht20;

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

// Replaces placeholder with LED state value
// String processor(const String& var){
//   Serial.println(var);
//   if(var == "STATE"){
//     if(digitalRead(ledPin)){
//       ledState = "ON";
//     }
//     else{
//       ledState = "OFF";
//     }
//     Serial.print(ledState);
//     return ledState;
//   }
//   return String();
// }

bool wifi_connected = false;

// Task to handle Wi-Fi connection
void wifiTask(void *pvParameters) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    Serial.println("Connecting to WiFi..");
  }
  wifi_connected = true;
  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());
  vTaskDelete(NULL);  // Delete the task when done
}

void reconnectWifiTask(void *pvParameters) {
  while(1){
    vTaskDelay(1000 / portTICK_PERIOD_MS);
      // Check to ensure we have done init connect
    if (wifi_connected == true) {
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED) {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          Serial.println("Connecting again to WiFi..");
        }
      }
    }
  }
}

void connectThingboardTask(void *pvParameters) {
  while(1){
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (WiFi.status() == WL_CONNECTED) {
      if (!tb.connected()) {
        Serial.print("Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
          Serial.println("Failed to connect");
        }
      }
    }
  }
}

// Task to handle server
// void serverTask(void *pvParameters) {
//   // Initialize SPIFFS
//   if(!SPIFFS.begin(true)){
//     Serial.println("An Error has occurred while mounting SPIFFS");
//     vTaskDelete(NULL);  // Delete the task if SPIFFS initialization fails
//   }

//   // Route for root / web page
//   server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send(SPIFFS, "/index.html", String(), false, processor);
//   });
  
//   // Route to load style.css file
//   server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send(SPIFFS, "/style.css", "text/css");
//   });

//   // Route to set GPIO to HIGH
//   server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
//     digitalWrite(ledPin, HIGH);    
//     request->send(SPIFFS, "/index.html", String(), false, processor);
//   });
  
//   // Route to set GPIO to LOW
//   server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
//     digitalWrite(ledPin, LOW);    
//     request->send(SPIFFS, "/index.html", String(), false, processor);
//   });

//   // Start server
//   server.begin();
//   vTaskDelete(NULL);  // Delete the task when done
// }

void HelloWorldTask(void *pvParameters) {
  while(1) {
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    Serial.println("Hello World");
  }
  vTaskDelete(NULL);  // Delete the task when done
}

void DHT20Task(void *pvParameters) {
  while(1) {
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    if (millis() - dht20.lastRead() >= 2000) {
      int status = dht20.read();
      float temperature = dht20.getTemperature();
      float humidity = dht20.getHumidity();

      Serial.print("DHT20 Temperature: ");
      Serial.print(temperature, 1);
      Serial.println(" °C");

      Serial.print("DHT20 Humidity: ");
      Serial.print(humidity, 1);
      Serial.println(" %");

      Serial.print("Status: ");
      switch (status) {
          case DHT20_OK:
              Serial.println("OK");
              if (WiFi.status() == WL_CONNECTED && tb.connected()){
                tb.sendTelemetryData("temperature", temperature);
                tb.sendTelemetryData("humidity", humidity);
                Serial.println("Sent to server done");
              }
              break;
          case DHT20_ERROR_CHECKSUM:
              Serial.println("Checksum error");
              break;
          case DHT20_ERROR_CONNECT:
              Serial.println("Connect error");
              break;
          case DHT20_MISSING_BYTES:
              Serial.println("Missing bytes");
              break;
          case DHT20_ERROR_BYTES_ALL_ZERO:
              Serial.println("All bytes read zero");
              break;
          case DHT20_ERROR_READ_TIMEOUT:
              Serial.println("Read time out");
              break;
          case DHT20_ERROR_LASTREAD:
              Serial.println("Read too fast");
              break;
          default:
              Serial.println("Unknown error");
              break;
      }
      Serial.println();
    }
  }
  vTaskDelete(NULL);  // Delete the task when done
}

void setup(){
  Serial.begin(115200);
  delay(5000);
  Wire.begin();  // Initialize I2C

  if (!dht20.begin()) {
      Serial.println("Failed to initialize DHT20 sensor!");
  }
  else {
    Serial.println("DHT20 sensor initialized.");
  }

  xTaskCreate(HelloWorldTask, "HelloWorldTask", 4096, NULL, 1, NULL);
  xTaskCreate(wifiTask, "WiFiTask", 4096, NULL, 1, NULL);
  xTaskCreate(reconnectWifiTask, "reconnectWifiTask ", 8192, NULL, 1, NULL);
  xTaskCreate(connectThingboardTask, "connectThingboardTask ", 8192, NULL, 1, NULL);
  xTaskCreate(DHT20Task, "DHT20Task", 8192, NULL, 1, NULL);
}
 
void loop(){
  // Nothing to do here, FreeRTOS tasks handle the work
}