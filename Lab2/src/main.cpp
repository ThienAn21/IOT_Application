#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12

#define DHT20_EN 0

#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time.h"

// Cấu hình NTP server để lấy thời gian thực
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT+7 (Việt Nam)
const int   daylightOffset_sec = 0;
time_t startTime = 0;
time_t endTime = 0;

constexpr char WIFI_SSID[] = "Redmi Note 11";
constexpr char WIFI_PASSWORD[] = "1234567890";

constexpr char TOKEN[] = "MbuNbL6qHKy5kS5MfT3O";

constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

constexpr char SCHEDULE_ATTR[] = "scheduler";

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange;

constexpr int16_t telemetrySendInterval = 10000U;
uint32_t previousDataSend;

constexpr std::array<const char *, 2U> SHARED_ATTRIBUTES_LIST = {
  SCHEDULE_ATTR
};

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

DHT20 dht20;

//  Hàm lấy thời gian thực (Unix time)
time_t getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return 0;
    }
    return mktime(&timeinfo);
}

//  Hàm xử lý chuỗi nhận được từ ThingsBoard
void processSchedulerData(String data) {
    Serial.println(" Received data: " + data);

    // Loại bỏ ký tự 'S' đầu chuỗi
    if (data.startsWith("S")) {
        data = data.substring(1);
    }

    // Tạo JSON document
    StaticJsonDocument<256> jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, data);

    if (error) {
        Serial.println("JSON Parsing failed!");
    }

    // Lấy giá trị `startTime` & `endTime`
    startTime = jsonDoc["startTime"];
    endTime = jsonDoc["endTime"];

    Serial.print("Start Time: ");
    Serial.println(startTime);
    Serial.print("End Time: ");
    Serial.println(endTime);
}

bool isLedOnSched = false;

void SchedulerControlTask(void *pvParameters) {
  while(1) {
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // Lấy thời gian hiện tại
    time_t now = getCurrentTime();
    Serial.print("Current Time: ");
    Serial.println(now);

    // So sánh thời gian để bật/tắt LED
    if (now >= startTime && now < endTime) {
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
        isLedOnSched = true;
        tb.sendAttributeData("value", ledState);
        Serial.println("LED TURNED ON!");
    } else if (isLedOnSched) {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
        isLedOnSched = false;
        tb.sendAttributeData("value", ledState);
        Serial.println("LED TURNED OFF!");
    }
  }
}

RPC_Response setLedSwitchState(const RPC_Data &data) {
    Serial.println("Received Switch state");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    digitalWrite(LED_PIN, newState);
    return RPC_Response("setValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setValue", setLedSwitchState }
};

void processSharedAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), SCHEDULE_ATTR) == 0) {
      String data = it->value().as<String>();
      processSchedulerData(data);
    }
  }
}

const Shared_Attribute_Callback attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback attribute_shared_request_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());


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

void TbLoobTask(void *pvParameters) {
  while(1) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
    tb.loop();
  }
  vTaskDelete(NULL);  // Delete the task when done
}

void connectTBTask(void *pvParameters) {
  while(1){
    vTaskDelay(1000 / portTICK_PERIOD_MS);
      // Check to ensure we have done init connect
      if (!tb.connected()) {
        Serial.print("Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
          Serial.println("Failed to connect");
        }

        tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());

        Serial.println("Subscribing for RPC...");
        if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
          Serial.println("Failed to subscribe for RPC");
        }

        if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
          Serial.println("Failed to subscribe for shared attribute updates");
        }

        Serial.println("Subscribe done");

        if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
          Serial.println("Failed to request for shared attributes");
        }
      }
  }
}

void HelloWorldTask(void *pvParameters) {
  while(1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    Serial.println("Hello World");
  }
  vTaskDelete(NULL);  // Delete the task when done
}

void DHT20Task(void *pvParameters) {
  while(1) {
    vTaskDelay(5000 / portTICK_PERIOD_MS);
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

void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  pinMode(LED_PIN, OUTPUT);
  delay(1000);
  // InitWiFi();

#ifdef DHT20_EN
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!dht20.begin()) {
      Serial.println("Failed to initialize DHT20 sensor!");
  }
  else {
    xTaskCreate(DHT20Task, "DHT20Task", 8192, NULL, 1, NULL);
    Serial.println("DHT20 sensor initialized.");
  }
#endif

    // Đồng bộ thời gian từ NTP server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏳ Syncing time...");
  delay(2000); // Đợi đồng bộ

  xTaskCreate(HelloWorldTask, "HelloWorldTask", 4096, NULL, 1, NULL);
  xTaskCreate(wifiTask, "WiFiTask", 4096, NULL, 1, NULL);
  xTaskCreate(reconnectWifiTask, "reconnectWifiTask ", 8192, NULL, 1, NULL);
  xTaskCreate(connectTBTask, "connectTBTask ", 8192, NULL, 1, NULL);
  xTaskCreate(SchedulerControlTask, "SchedulerControlTask ", 8192, NULL, 1, NULL);
  xTaskCreate(TbLoobTask, "TbLoobTask", 4096, NULL, 1, NULL);
}

void loop() {
}
