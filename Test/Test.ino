#include "WiFi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include "DHT.h"
#include "esp_task_wdt.h"  // Watchdog Timer

#define DHTPIN 4
#define DHTTYPE DHT22
#define WDT_TIMEOUT 10  // ตั้งค่า Watchdog Timer ที่ 10 วินาที

const char* ssid = "Haise";
const char* password = "0953067280";

String Web_App_URL = "https://script.google.com/macros/s/...";
String Status_Read_Sensor = "";
float Temp = 0.0;
int Humd = 0;
String DeviceID = "2";

unsigned long lastSendTime = 0;
const unsigned long interval = 900000;  // 15 นาที
const unsigned long maxNoResponseTime = 900000;  // ถ้าไม่ส่งข้อมูล 15 นาทีเต็ม → รีเซ็ต
const float tempThreshold = 60.0;  // รีเซ็ต ESP32 ถ้าอุณหภูมิ >= 60°C
int dhtFailCount = 0;  // นับจำนวนครั้งที่อ่านค่า DHT22 ไม่ได้

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// ✅ ฟังก์ชัน Reset ESP32 ผ่าน HTTP
void handleReset() {
    Serial.println("[WebServer] Reset command received!");
    server.send(200, "text/plain", "ESP32 is resetting...");
    delay(1000);
    ESP.restart();
}

// ✅ ตั้งค่า Web Server
void setupWebServer() {
    server.on("/reset", handleReset);
    server.begin();
    Serial.println("[WebServer] Web Server started on port 80");
}

// ✅ เชื่อมต่อ WiFi
void connectToWiFi() {
    Serial.println("[WiFi] Connecting to WiFi...");
    WiFi.begin(ssid, password);

    int retry = 30;
    while (WiFi.status() != WL_CONNECTED && retry > 0) {
        Serial.printf("[WiFi] Retrying... %d sec\n", retry);
        delay(1000);
        retry--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] ESP32 IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[WiFi] Connection Failed! Restarting ESP32...");
        ESP.restart();
    }
}

// ✅ รีเซ็ต ESP32 ถ้าอุณหภูมิสูงเกิน 60°C
void checkESP32Temperature() {
    float espTemp = temperatureRead();
    Serial.printf("[ESP32] Temperature: %.2f°C\n", espTemp);

    if (espTemp >= tempThreshold) {
        Serial.println("[Warning] ESP32 Overheating! Restarting...");
        Status_Read_Sensor = "Reset ESP32 (Overheat)";
        sendDataToGoogleSheets();
        delay(1000);
        ESP.restart();
    }
}

// ✅ อ่านค่า DHT22 และป้องกันเซ็นเซอร์ค้าง
void readSensorData() {
    Serial.println("[Sensor] Reading data from DHT22...");
    float newHumd = dht.readHumidity();
    float newTemp = dht.readTemperature();

    if (isnan(newHumd) || isnan(newTemp)) {
        dhtFailCount++;
        Serial.printf("[Sensor] Failed to read data! Attempt %d\n", dhtFailCount);

        if (dhtFailCount >= 3) {
            Serial.println("[Sensor] DHT22 Failed 3 times! Restarting ESP32...");
            ESP.restart();
        }

        dht.begin();
        delay(2000);
        return;
    }

    dhtFailCount = 0;
    Status_Read_Sensor = "Success";
    Humd = newHumd;
    Temp = newTemp;
    Serial.printf("[Sensor] Temp: %.2f°C, Humidity: %d%%\n", Temp, Humd);
}

// ✅ ตั้งค่า Watchdog Timer (WDT) ตาม ESP-IDF 5.0+
void setupWatchdog() {
    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = WDT_TIMEOUT * 1000,  // แปลงเป็นมิลลิวินาที
        .idle_core_mask = (1 << CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0),
        .trigger_panic = true,
    };

    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(NULL);
}

// ✅ ตั้งค่า ESP32
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    dht.begin();
    connectToWiFi();
    setupWebServer();
    setupWatchdog();  // ✅ เรียกใช้ Watchdog Timer
}

// ✅ Loop หลัก
void loop() {
    esp_task_wdt_reset();  // รีเซ็ต Watchdog Timer

    checkESP32Temperature();
    server.handleClient();

    Serial.println("[System] Starting new cycle...");
    readSensorData();
    sendDataToGoogleSheets();
    lastSendTime = millis();

    while (millis() - lastSendTime < interval) {
        checkESP32Temperature();
        server.handleClient();
        esp_task_wdt_reset();
        delay(5000);
    }
}
