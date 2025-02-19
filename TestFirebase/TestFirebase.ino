#include "WiFi.h"
#include <FirebaseESP32.h>

// ข้อมูล WiFi
const char* ssid = "Haise";
const char* password = "0953067280";

// ข้อมูล Firebase
#define FIREBASE_HOST "https://smart-fan-control-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "lbV0zRvKUAhVUiNhsBPZwvyFXUMWILtLdCaO6n3K"

FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

// ตัวจับเวลา
unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 10 * 1000; // 10 วินาที

void setup() {
    Serial.begin(115200);
    // เชื่อมต่อ WiFi
    WiFi.begin(ssid, password);
    int countdown = 10;
    while (WiFi.status() != WL_CONNECTED && countdown > 0) {
        delay(1000);
        Serial.print(".");
        countdown--;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("WiFi connection failed");
        return;
    }

    // ตั้งค่า Firebase
    firebaseConfig.host = FIREBASE_HOST;
    firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;

    Firebase.begin(&firebaseConfig, &firebaseAuth);
    Firebase.reconnectWiFi(true);

    // ข้อความเริ่มต้น
    Serial.println("Ready to fetch data from Firebase!");
}

void loop() {
    unsigned long currentTime = millis();

    // ดึงข้อมูลทุกๆ 10 วินาที
    if (currentTime - lastFetchTime >= fetchInterval) {
        lastFetchTime = currentTime;
        fetchData();
    }
}

void fetchData() {
    Serial.println("Fetching data from Firebase...");

    // ดึงข้อมูลจาก Firebase
    if (Firebase.getJSON(firebaseData, "/averageData")) {
        Serial.println("Data fetched successfully!");

        // แสดงข้อมูล JSON
        Serial.println(firebaseData.jsonString());

        // แปลง JSON และดึงค่า avgTemp
        FirebaseJson json = firebaseData.jsonObject();
        FirebaseJsonData jsonData;
        if (json.get(jsonData, "avgTemp")) {
            float avgTemp = jsonData.floatValue;
            Serial.printf("Avg Temp: %.2f°C\n", avgTemp);
            
            // ทำงานตามค่า avgTemp เช่น เปิดหรือปิด Relay
            if (avgTemp >= 29.0) {
                Serial.println("Relay ON");
            } else if (avgTemp <= 27.0) {
                Serial.println("Relay OFF");
            }
        } else {
            Serial.println("Error: avgTemp not found in JSON!");
        }
    } else {
        Serial.println("Failed to fetch data from Firebase!");
        Serial.println(firebaseData.errorReason());
    }
}
