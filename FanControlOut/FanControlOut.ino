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

String BoxID = "1";

LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

const char* googleSheetsURL = "https://script.google.com/macros/s/AKfycbwm7LcyEQV4e4FACgBMcCVTP_SljAsTrLtlC5IRdM2gqrmXYd1Ln1wOagvRsQm6DFfmtw/exec";

String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "Unknown";
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
}

// ฟังก์ชันอัปเดต LCD
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

    updateLCD("Relay Control", "Relay: " + String(relayState ? "ON " : "OFF"));
}

void connectWiFi() {
    WiFi.begin(ssid, password);
    updateLCD("Connecting...", "WiFi...");
    Serial.println("[WiFi] Connecting...");
    int retry = 30;

    while (WiFi.status() != WL_CONNECTED && retry > 0) {
        updateLCD("Retrying WiFi...", "In " + String(retry) + " sec");
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

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Lost connection! Resetting in 5 sec...");
        for (int i = 5; i > 0; i--) {
            updateLCD("WiFi Lost!", "Reset in " + String(i) + " sec");
            delay(1000);
        }
        ESP.restart();
    }
}

void sendDataToFirebase(String relayStatus) {
    String currentTime = getCurrentTime();
    String path = "/relayData/" + BoxID;

    Serial.println("[Firebase] Sending relay data...");
    
    if (Firebase.setString(firebaseData, path + "/RelayStatus", relayStatus) &&
        Firebase.setString(firebaseData, path + "/Time", currentTime)) {
        Serial.println("[Firebase] Data sent successfully.");
    } else {
        Serial.printf("[Firebase] Error: %s\n", firebaseData.errorReason().c_str());
    }
}

void sendDataToGoogleSheets(String relayStatus, float temp, float avgHumidity) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String currDateTime = getCurrentTime();
        String currDate = currDateTime.substring(0, 10);
        String currTime = currDateTime.substring(11);

        String fullUrl = String(googleSheetsURL) +
                         "?sts=write" +
                         "&date=" + currDate +
                         "&time=" + currTime +
                         "&BoxID=" + BoxID +
                         "&relayStatus=" + relayStatus +
                         "&Temp=" + String(temp, 1) +
                         "&Humidity=" + String(avgHumidity, 1);

        Serial.println("[GoogleSheets] Sending data...");
        Serial.println("[GoogleSheets] URL: " + fullUrl);

        http.begin(fullUrl);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int httpCode = http.GET();
        
        if (httpCode > 0) {
            Serial.printf("[GoogleSheets] Response code: %d, Response: %s\n", httpCode, http.getString().c_str());
        } else {
            Serial.printf("[GoogleSheets] Failed to send data. HTTP error: %d\n", httpCode);
        }
        http.end();
    }
}

void fetchDataAndControl() {
    Serial.println("[AutoMode] Fetching data...");
    
    float avgTemp = -1.0;
    float avgHumidity = -1.0;
    
    if (Firebase.getFloat(firebaseData, "/averageData/AVG/avgTemp")) {
        avgTemp = firebaseData.floatData();
        Serial.printf("[AutoMode] AVG Temp: %.1f°C\n", avgTemp);
    }

    if (Firebase.getFloat(firebaseData, "/averageData/AVG/avgHumd")) {
        avgHumidity = firebaseData.floatData();
        Serial.printf("[AutoMode] AVG Humidity: %.1f%%\n", avgHumidity);
    }

    updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: " + String(relayState ? "ON" : "OFF"));

    if (avgTemp >= 30.0 && !relayState) {
        controlRelay(true);
    } else if (avgTemp <= 27.0 && relayState) {
        controlRelay(false);
    }

    sendDataToFirebase(relayState ? "ON" : "OFF");
    sendDataToGoogleSheets(relayState ? "ON" : "OFF", avgTemp, avgHumidity);

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

    connectWiFi();

    firebaseConfig.host = FIREBASE_HOST;
    firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&firebaseConfig, &firebaseAuth);

    fetchDataAndControl();
    lastActionTime = millis();
}

void loop() {
    checkWiFiConnection();
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
