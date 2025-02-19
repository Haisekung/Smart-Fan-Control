#include "WiFi.h"
#include <FirebaseESP32.h>
#include <LiquidCrystal_I2C.h>

// Wi-Fi
const char* ssid = "Haise";
const char* password = "0953067280";

// Firebase
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
FirebaseData firebaseData;
#define FIREBASE_HOST "https://smart-fan-control-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "lbV0zRvKUAhVUiNhsBPZwvyFXUMWILtLdCaO6n3K"

// Relay
const int relayPin = 5; // ใช้ Pin 5 สำหรับ IN ของ Relay
bool relayState = false; // สถานะของ Relay (ปิดเริ่มต้น)

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Timer for Firebase fetch
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 10 * 1000; // 10 วินาที

// ฟังก์ชันสำหรับการควบคุม Relay
void controlRelay(bool state) {
    relayState = state; // อัพเดตสถานะ
    digitalWrite(relayPin, state ? LOW : HIGH); // เปิดเมื่อ LOW, ปิดเมื่อ HIGH
}

// ฟังก์ชันเชื่อมต่อ WiFi
void connectToWiFi() {
    WiFi.begin(ssid, password);
    lcd.clear();
    lcd.print("Connecting WiFi...");
    Serial.println("Connecting WiFi...");

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
        Serial.println("WiFi Connected Successfully!");
        delay(2000);
    } else {
        Serial.println("Failed to connect WiFi. Restarting...");
        ESP.restart();
    }
}

// ฟังก์ชันดึงข้อมูลจาก Firebase
void fetchData() {
    Serial.println("Fetching data from Firebase...");
    if (Firebase.getJSON(firebaseData, "/averageData")) {
        // Debug: แสดงโครงสร้าง JSON ทั้งหมด
        Serial.println("Firebase JSON data:");
        Serial.println(firebaseData.jsonString());

        FirebaseJson json = firebaseData.jsonObject();
        FirebaseJsonData jsonData;

        // ดึงค่า avgTemp จาก AVG
        if (json.get(jsonData, "AVG/avgTemp")) {
            float avgTemp = jsonData.floatValue; // อุณหภูมิ
            bool newRelayState = avgTemp >= 30.0; // กำหนดสถานะใหม่ของ Relay

            // อัพเดตสถานะ Relay หากเปลี่ยนแปลง
            if (newRelayState != relayState) {
                controlRelay(newRelayState); // เรียกฟังก์ชันควบคุม Relay
            }

            // อัพเดตหน้าจอ LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "Temp: %.1f C", avgTemp);
            lcd.print(buffer);

            lcd.setCursor(0, 1);
            lcd.print("Relay: ");
            lcd.print(relayState ? "ON" : "OFF");

            Serial.printf("Temp: %.1f C, Relay: %s\n", avgTemp, relayState ? "ON" : "OFF");
        } else {
            Serial.println("avgTemp not found in Firebase");
            lcd.clear();
            lcd.print("Error: Firebase");
        }
    } else {
        Serial.printf("Firebase Error: %s\n", firebaseData.errorReason().c_str());
        lcd.clear();
        lcd.print("Firebase Error");
    }
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();

    pinMode(relayPin, OUTPUT);
    controlRelay(false); // เริ่มต้นให้ Relay ปิด (Active LOW)

    connectToWiFi();

    // ตั้งค่า Firebase
    firebaseConfig.host = FIREBASE_HOST;
    firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&firebaseConfig, &firebaseAuth);

    Serial.println("System initialized");
    fetchData(); // ดึงข้อมูลครั้งแรก
}

void loop() {
    // เช็คสถานะ WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Restarting...");
        for (int i = 5; i > 0; i--) {
            lcd.clear();
            lcd.print("WiFi Lost");
            lcd.setCursor(0, 1);
            lcd.printf("Reset in %d", i);
            delay(1000);
        }
        ESP.restart();
    }

    // ดึงข้อมูล Firebase ทุก 10 วินาที
    if (millis() - lastFetchTime >= fetchInterval) {
        fetchData();
        lastFetchTime = millis();
    }

    delay(1000);
}
