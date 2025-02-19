#include "WiFi.h"
#include <HTTPClient.h>
#include <FirebaseESP32.h>
#include <LiquidCrystal_I2C.h>
#include <time.h> // ไลบรารีสำหรับการดึงเวลาปัจจุบันจาก NTP Server

// Wi-Fi Configuration
const char* ssid = "Haise";
const char* password = "0953067280";

// Firebase Configuration
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
FirebaseData firebaseData;
#define FIREBASE_HOST "https://smart-fan-control-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "lbV0zRvKUAhVUiNhsBPZwvyFXUMWILtLdCaO6n3K"

// Google Sheets Web App URL
String Web_App_URL = "https://script.google.com/macros/s/AKfycbxG4NhkHVEz7NgBuwQxHJVstfGzdIUDvL0FHBDmGi2kGaqby-6nH4uPYRYIObvyf9uuEg/exec";

// Relay Configuration
const int relayPin = 5;
bool relayState = false;
String RelayStatus = "OFF";

// Box Configuration
String BoxID = "1";

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Timer Configuration
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 10 * 1000; // 10 seconds

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600; // GMT+7 (Time Zone)
const int daylightOffset_sec = 0;

// ฟังก์ชันเข้ารหัส URL
String encodeURIComponent(String value) {
    String encoded = "";
    char c;
    char buf[4];
    for (unsigned int i = 0; i < value.length(); i++) {
        c = value.charAt(i);
        if (isalnum(c)) {
            encoded += c;
        } else {
            sprintf(buf, "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

// ฟังก์ชันดึงเวลาปัจจุบันในรูปแบบ ISO 8601
String getCurrentTime() {
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[NTP] Failed to obtain time.");
        return "Unknown";
    }
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
}

// ฟังก์ชันควบคุม Relay
void controlRelay(bool state) {
    relayState = state;
    digitalWrite(relayPin, state ? LOW : HIGH);
    RelayStatus = state ? "ON" : "OFF";
}

// ฟังก์ชันเชื่อมต่อ Wi-Fi
void connectToWiFi() {
    WiFi.begin(ssid, password);
    lcd.clear();
    lcd.print("Connecting WiFi...");
    Serial.println("[WiFi] Connecting...");

    int retry = 30; // 30 วินาที
    while (WiFi.status() != WL_CONNECTED && retry > 0) {
        delay(1000);
        lcd.setCursor(0, 1);
        lcd.printf("Retry: %2d", retry--);
        Serial.printf("Retrying... %d seconds left\n", retry);
    }

    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.print("WiFi Connected");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP());
        Serial.println("[WiFi] Connected successfully.");
        delay(2000);
    } else {
        Serial.println("[WiFi] Connection failed. Restarting...");
        ESP.restart();
    }
}

// ฟังก์ชันส่งข้อมูลไปยัง Firebase
void sendDataToFirebase(String relayStatus, String boxID) {
    if (WiFi.status() == WL_CONNECTED) {
        String path = "/relayData/" + boxID;
        String currentTime = getCurrentTime(); // เวลาปัจจุบัน

        if (Firebase.setString(firebaseData, path + "/RelayStatus", relayStatus) &&
            Firebase.setString(firebaseData, path + "/Time", currentTime)) {
            Serial.println("[Firebase] Data sent successfully.");
        } else {
            Serial.printf("[Firebase] Failed to send data: %s\n", firebaseData.errorReason().c_str());
        }
    } else {
        Serial.println("[Firebase] WiFi not connected.");
    }
}

// ฟังก์ชันส่งข้อมูลไปยัง Google Sheets
void sendDataToGoogleSheets(String relayStatus, String boxID) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String currDate = getCurrentTime().substring(0, 10); // YYYY-MM-DD
        String currTime = getCurrentTime().substring(11);    // HH:MM:SS

        String Send_Data_URL = Web_App_URL + "?date=" + encodeURIComponent(currDate) + 
                               "&time=" + encodeURIComponent(currTime) + 
                               "&relayStatus=" + encodeURIComponent(relayStatus) + 
                               "&BoxID=" + encodeURIComponent(boxID);

        Serial.println("[HTTP] Sending request to: " + Send_Data_URL);
        http.begin(Send_Data_URL.c_str());
        int httpCode = http.GET();

        if (httpCode > 0) {
            Serial.printf("[HTTP] Response code: %d\n", httpCode);
            Serial.println("[HTTP] Response: " + http.getString());
        } else {
            Serial.printf("[HTTP] Request failed. Error code: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("[HTTP] WiFi not connected.");
    }
}

// ฟังก์ชันดึงข้อมูลจาก Firebase
void fetchData() {
    Serial.println("[Firebase] Fetching data...");
    if (Firebase.getJSON(firebaseData, "/averageData")) {
        Serial.println("[Firebase] Data received.");
        FirebaseJson json = firebaseData.jsonObject();
        FirebaseJsonData jsonData;

        if (json.get(jsonData, "AVG/avgTemp")) {
            float avgTemp = jsonData.floatValue;
            bool newRelayState = avgTemp >= 30.0;
            if (avgTemp <= 28.0) newRelayState = false;

            if (newRelayState != relayState) {
                controlRelay(newRelayState);
            }

            // อัพเดตหน้าจอ LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.printf("Temp: %.1f C", avgTemp);
            lcd.setCursor(0, 1);
            lcd.print("Relay: ");
            lcd.print(RelayStatus);

            Serial.printf("Temp: %.1f C, Relay: %s\n", avgTemp, RelayStatus.c_str());

            // ส่งข้อมูลไปยัง Firebase และ Google Sheets
            sendDataToFirebase(RelayStatus, BoxID);
            sendDataToGoogleSheets(RelayStatus, BoxID);
        } else {
            Serial.println("[Firebase] avgTemp not found.");
            lcd.clear();
            lcd.print("Error: Firebase");
        }
    } else {
        Serial.printf("[Firebase] Error: %s\n", firebaseData.errorReason().c_str());
        lcd.clear();
        lcd.print("Firebase Error");
    }
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();

    pinMode(relayPin, OUTPUT);
    controlRelay(false); // ตั้งค่าเริ่มต้นเป็น OFF

    connectToWiFi();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    firebaseConfig.host = FIREBASE_HOST;
    firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&firebaseConfig, &firebaseAuth);

    Serial.println("[System] Initialized.");
    fetchData();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected. Restarting...");
        ESP.restart();
    }

    if (millis() - lastFetchTime >= fetchInterval) {
        fetchData();
        lastFetchTime = millis();
    }

    delay(1000);
}
