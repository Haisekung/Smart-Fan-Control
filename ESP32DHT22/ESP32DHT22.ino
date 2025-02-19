#include "WiFi.h"
#include <HTTPClient.h>
#include <WebServer.h>  // ✅ เพิ่ม Web Server
#include "DHT.h"

#define DHTPIN 4
#define DHTTYPE DHT22

const char* ssid = "Tenda_B2D1B0";
const char* password = "most8475";
String Web_App_URL = "https://script.google.com/macros/s/AKfycbxG4NhkHVEz7NgBuwQxHJVstfGzdIUDvL0FHBDmGi2kGaqby-6nH4uPYRYIObvyf9uuEg/exec";

String Status_Read_Sensor = "";
float Temp = -99.9;
int Humd = -1;
String DeviceID = "2";
unsigned long lastSendTime = 0;
bool needNewReading = false;

DHT dht(DHTPIN, DHTTYPE);

// ✅ เพิ่ม Web Server ที่ Port 80
WebServer server(80);

// ✅ ฟังก์ชัน Reset ESP32 ผ่าน HTTP
void handleReset() {
    server.send(200, "text/plain", "ESP32 is resetting...");
    delay(1000);
    ESP.restart();
}

// ✅ ฟังก์ชันตั้งค่า Web Server
void setupWebServer() {
    server.on("/reset", handleReset);
    server.begin();
    Serial.println("[WebServer] Started on port 80");
}

bool countdown(int seconds, String reason) {
    Serial.println("====================================");
    Serial.println("[System] " + reason);
    for (int i = seconds; i > 0; i--) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[System] WiFi connected! Cancelling countdown.");
            return true;
        }
        Serial.printf("[System] Reset in %d seconds...\n", i);
        delay(1000);
    }
    return false;
}

void connectToWiFi() {
    Serial.println("[WiFi] Connecting to WiFi...");
    WiFi.begin(ssid, password);
    if (!countdown(30, "[WiFi] Connection failed! Restarting ESP32...")) {
        Serial.println("[System] Restarting ESP32...");
        ESP.restart();
    }

    Serial.println("[WiFi] WiFi connected successfully.");
    
    // ✅ แสดง IP Address ของ ESP32
    Serial.print("[WiFi] ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[System] [WiFi] Connection lost! Restarting ESP32 in 5 seconds...");
        countdown(5, "[WiFi] Lost connection! Restarting ESP32...");
        Serial.println("[System] Restarting ESP32...");
        ESP.restart();
    }
}

void readSensorData() {
    Serial.println("[Sensor] Reading data from DHT22...");
    float newHumd = dht.readHumidity();
    float newTemp = dht.readTemperature();

    if (isnan(newHumd) || isnan(newTemp)) {
        Serial.println("[Sensor] Failed to read data!");

        if (needNewReading) {
            Serial.println("[Sensor] Second failed attempt! Restarting ESP32...");
            ESP.restart();
        } else {
            Serial.println("[Sensor] Using last valid values (temporarily)...");
            needNewReading = true;
        }
    } else {
        needNewReading = false;
        Status_Read_Sensor = "Success";
        Humd = newHumd;
        Temp = newTemp;
        Serial.printf("[Sensor] Temp: %.2f°C, Humidity: %d%%\n", Temp, Humd);
    }
}

void sendDataToGoogleSheets() {
    if (WiFi.status() == WL_CONNECTED) {
        if (Temp == -99.9 || Humd == -1) {
            Serial.println("[WARNING] Invalid Temperature or Humidity! Skipping data send.");
            return;
        }

        HTTPClient http;
        String Send_Data_URL = Web_App_URL + "?sts=write&srs=" + Status_Read_Sensor +
                               "&temp=" + String(Temp, 2) + "&humd=" + String(Humd) +
                               "&deviceid=" + DeviceID;

        Serial.println("[HTTP] Sending request to: " + Send_Data_URL);
        http.begin(Send_Data_URL.c_str());
        int httpCode = http.GET();

        if (httpCode > 0) {
            Serial.printf("[HTTP] Response code: %d\n", httpCode);
        } else {
            Serial.printf("[HTTP] Request failed. Error code: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("[HTTP] WiFi not connected. Data not sent.");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[System] ESP32 Booting up...");
    WiFi.mode(WIFI_STA);
    dht.begin();
    connectToWiFi();
    setupWebServer();  // ✅ เรียกใช้ Web Server
}

void loop() {
    checkWiFiConnection();
    server.handleClient();  // ✅ คอยรอรับ HTTP Request

    Serial.println("[System] Starting new cycle...");
    readSensorData();
    sendDataToGoogleSheets();
    lastSendTime = millis();

    while (millis() - lastSendTime < 900000) {
        checkWiFiConnection();
        server.handleClient();  // ✅ คอยรอรับ HTTP Request ระหว่างรอ
        delay(5000);
    }
}
