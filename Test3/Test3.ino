#include "WiFi.h"
#include <HTTPClient.h>
#include <FirebaseESP32.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

const char* ssid = "Tenda_B2D1B0";
const char* password = "most8475";

FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
FirebaseData firebaseData;
#define FIREBASE_HOST "https://smart-fan-control-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "lbV0zRvKUAhVUiNhsBPZwvyFXUMWILtLdCaO6n3K"

const int relayPin = 5;
const int manualButtonPin = 4;
bool relayState = false;
bool manualMode = false;
bool autoModeReady = false;
unsigned long lastActionTime = 0;
unsigned long manualStartTime = 0;
const unsigned long interval = 15 * 60 * 1000;

String BoxID = "3";

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

void updateLCD(const String& line1, const String& line2) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
}

void controlRelay(bool state) {
    relayState = state;
    digitalWrite(relayPin, relayState ? LOW : HIGH);
    Serial.printf("[Relay] State changed to %s\n", relayState ? "ON" : "OFF");

    if (!manualMode) {
        updateLCD("Temp: --.-C", "Relay: " + String(relayState ? "ON" : "OFF"));
    } else {
        updateLCD("Manual Mode", "Relay: " + String(relayState ? "ON" : "OFF"));
    }
}

void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected. Restarting in 5 sec...");
        for (int i = 5; i > 0; i--) {
            updateLCD("WiFi Lost!", "Reset in " + String(i) + " sec");
            delay(1000);
        }
        ESP.restart();
    }
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    updateLCD("Connecting...", "WiFi...");
    Serial.println("[WiFi] Connecting...");

    int retry = 30;
    while (WiFi.status() != WL_CONNECTED && retry > 0) {
        updateLCD("Retry WiFi...", "In " + String(retry) + " sec");
        Serial.printf("[WiFi] Retrying... %d sec\n", retry);
        delay(1000);
        retry--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        updateLCD("WiFi", "Connected!");
        Serial.println("[WiFi] Connected.");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    } else {
        updateLCD("WiFi Failed!", "Restarting...");
        Serial.println("[WiFi] Restarting...");
        delay(1000);
        ESP.restart();
    }
}

void fetchDataAndControl() {
    Serial.println("[AutoMode] Fetching data from Firebase...");

    String pathRelay = "/relayData/1/RelayStatus";  // FAN OUT Relay Status
    String pathAvgTemp = "/averageData/AVG/avgTemp"; // Temperature

    String relayStatus = "";
    if (Firebase.getString(firebaseData, pathRelay)) {
        relayStatus = firebaseData.stringData();
        Serial.printf("[FetchData] FAN OUT Relay Status: %s\n", relayStatus.c_str());
    } else {
        Serial.printf("[FetchData] Failed to fetch FAN OUT status: %s\n", firebaseData.errorReason().c_str());
        return;
    }

    float avgTemp = Firebase.getFloat(firebaseData, pathAvgTemp) ? firebaseData.floatData() : -1.0;

    if (avgTemp == -1.0) {
        Serial.println("[FetchData] Failed to fetch temperature.");
        return;
    }

    Serial.printf("[FetchData] AVG Temp: %.2f°C\n", avgTemp);

    // ** FAN IN เปิดเมื่อ FAN OUT เป็น "ON" และอุณหภูมิ ≥29°C **
    if (relayStatus == "ON" && avgTemp >= 29.0) {
        controlRelay(true);
        updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: ON");
    } 
    // ** ปิดเมื่ออุณหภูมิ ≤27°C **
    else if (avgTemp <= 27.0) {
        controlRelay(false);
        updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: OFF");
    }

    autoModeReady = true;
    Serial.println("[AutoMode] FetchData Completed.");
}

void restartAutoMode() {
    Serial.println("[ManualMode] Returning to Auto Mode...");

    for (int i = 5; i > 0; i--) {
        updateLCD("Back to Auto Mode", "In " + String(i) + " sec");
        delay(1000);
    }

    manualMode = false;
    fetchDataAndControl();
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();
    pinMode(relayPin, OUTPUT);
    pinMode(manualButtonPin, INPUT_PULLUP);
    controlRelay(false);

    connectToWiFi();

    firebaseConfig.host = FIREBASE_HOST;
    firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&firebaseConfig, &firebaseAuth);

    fetchDataAndControl();
    lastActionTime = millis();
}

void loop() {
    checkWiFi();
    unsigned long currentTime = millis();
    static bool lastButtonState = HIGH;
    bool buttonState = digitalRead(manualButtonPin);

    if (buttonState == LOW && lastButtonState == HIGH && autoModeReady) {
        manualMode = true;
        manualStartTime = millis();
        controlRelay(!relayState);
        updateLCD("Manual Mode", "Relay: " + String(relayState ? "ON" : "OFF"));
    }

    lastButtonState = buttonState;

    if (manualMode && (millis() - manualStartTime >= 5000)) {
        restartAutoMode();
    }

    if (!manualMode && (currentTime - lastActionTime >= interval)) {
        lastActionTime = currentTime;
        fetchDataAndControl();
    }

    delay(100);
}
