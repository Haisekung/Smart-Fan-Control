#include "WiFi.h"
#include <HTTPClient.h>
#include <FirebaseESP32.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// Wi-Fi Configuration
const char* ssid = "Haise";
const char* password = "0953067280";

// Firebase Configuration
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
FirebaseData firebaseData;
#define FIREBASE_HOST "https://smart-fan-control-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "lbV0zRvKUAhVUiNhsBPZwvyFXUMWILtLdCaO6n3K"

// Relay Configuration
const int relayPin = 5;
const int manualButtonPin = 4;
bool relayState = false; // Current relay state
bool manualMode = false; // Current manual mode state
unsigned long lastActionTime = 0; // Last action timestamp
unsigned long manualStartTime = 0; // Manual mode start time
const unsigned long interval = 15 * 60 * 1000; // 15 minutes interval

// Box Configuration
String BoxID = "2"; // Box ID without spaces

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// Google Sheets Web App URL
const char* googleSheetsURL = "https://script.google.com/macros/s/AKfycbwm7LcyEQV4e4FACgBMcCVTP_SljAsTrLtlC5IRdM2gqrmXYd1Ln1wOagvRsQm6DFfmtw/exec";

// Function to get current time
String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[Time] Failed to obtain time.");
        return "Unknown";
    }
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
}

// Function to encode URL parameters
String urlEncode(const String &value) {
    String encoded = "";
    for (char c : value) {
        if (isalnum(c)) {
            encoded += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

// Function to send data to Google Sheets
void sendDataToGoogleSheets(String relayStatus, float temp, float humidity) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String currDateTime = getCurrentTime();
        String currDate = currDateTime.substring(0, 10);
        String currTime = currDateTime.substring(11);

        // URL Encode parameters
        String fullUrl = String(googleSheetsURL) +
                         "?sts=write" +
                         "&date=" + urlEncode(currDate) +
                         "&time=" + urlEncode(currTime) +
                         "&BoxID=" + urlEncode(BoxID) +
                         "&relayStatus=" + urlEncode(relayStatus) +
                         "&Temp=" + urlEncode(String(temp, 1)) +
                         "&Humidity=" + urlEncode(String(humidity, 1));

        Serial.println("[GoogleSheets] Sending data...");
        Serial.println("[GoogleSheets] URL: " + fullUrl); // Debug URL in Serial Monitor

        http.begin(fullUrl);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int httpCode = http.GET();

        if (httpCode > 0) {
            Serial.printf("[GoogleSheets] Response code: %d\n", httpCode);
            String payload = http.getString();
            Serial.println("[GoogleSheets] Response: " + payload);
        } else {
            Serial.printf("[GoogleSheets] Request failed. Error code: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("[GoogleSheets] WiFi not connected.");
    }
}

// Function to control Relay
void controlRelay(bool state) {
    if (relayState != state) {
        relayState = state;
        digitalWrite(relayPin, state ? LOW : HIGH); // LOW = ON, HIGH = OFF
        Serial.printf("[Relay] State changed to %s\n", state ? "ON" : "OFF");
    }
}

// Function to update LCD display
void updateLCD(String line1, String line2) {
    static String prevLine1 = "";
    static String prevLine2 = "";

    if (line1 != prevLine1 || line2 != prevLine2) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(line1);
        lcd.setCursor(0, 1);
        lcd.print(line2);
        prevLine1 = line1;
        prevLine2 = line2;
    }
}

// Function to fetch data from Firebase and control Relay
void fetchDataAndControl() {
    Serial.println("[FetchData] Fetching data from Firebase...");
    String pathRelay = "/relayData/1/RelayStatus";
    String pathAvgTemp = "/averageData/AVG/avgTemp";
    String pathAvgHumidity = "/averageData/AVG/avgHumd";

    // Fetch RelayStatus
    String relayStatus = "";
    if (Firebase.getString(firebaseData, pathRelay)) {
        relayStatus = firebaseData.stringData();
        Serial.printf("[FetchData] RelayStatus: %s\n", relayStatus.c_str());
    } else {
        Serial.printf("[FetchData] Failed to fetch RelayStatus: %s\n", firebaseData.errorReason().c_str());
        return;
    }

    // Fetch avgTemp and avgHumidity
    float avgTemp = Firebase.getFloat(firebaseData, pathAvgTemp) ? firebaseData.floatData() : -1.0;
    float avgHumidity = Firebase.getFloat(firebaseData, pathAvgHumidity) ? firebaseData.floatData() : -1.0;

    if (avgTemp == -1.0 || avgHumidity == -1.0) {
        Serial.println("[FetchData] Failed to fetch Temp or Humidity.");
        return;
    }

    Serial.printf("[FetchData] AVG Temp: %.2f, AVG Humidity: %.2f\n", avgTemp, avgHumidity);

    // Control relay based on conditions
    if (avgTemp <= 27.0) {
        controlRelay(false);
        updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: OFF");
    } else if (relayStatus == "OFF") {
        controlRelay(true);
        updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: ON");
    } else if (relayStatus == "ON") {
        controlRelay(false);
        updateLCD("Temp: " + String(avgTemp, 1) + "C", "Relay: OFF");
    }

    // Send data to Google Sheets
    String relayStateStr = relayState ? "ON" : "OFF";
    sendDataToGoogleSheets(relayStateStr, avgTemp, avgHumidity);
}

// Function to handle WiFi reconnection
void checkWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected. Starting countdown to reset...");
        for (int i = 5; i > 0; i--) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("WiFi Lost!");
            lcd.setCursor(0, 1);
            lcd.printf("Reset in %d sec", i);
            Serial.printf("[WiFi] Reset in %d seconds...\n", i);
            delay(1000);
        }
        ESP.restart();
    }
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    lcd.clear();
    lcd.print("Connecting WiFi...");
    Serial.println("[WiFi] Connecting...");

    int retry = 30;
    while (WiFi.status() != WL_CONNECTED && retry > 0) {
        delay(1000);
        lcd.setCursor(0, 1);
        lcd.printf("Retry: %2d", retry--);
        Serial.printf("[WiFi] Retrying... %d seconds left\n", retry);
    }

    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.print("WiFi Connected");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP());
        Serial.println("[WiFi] Connected successfully.");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        delay(2000);
    } else {
        lcd.clear();
        lcd.print("WiFi Failed!");
        Serial.println("[WiFi] Connection failed. Restarting...");
        ESP.restart();
    }
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
    Serial.println("[System] Initialized.");

    // Initial fetch and action
    fetchDataAndControl();
    lastActionTime = millis();
}

void loop() {
    unsigned long currentTime = millis();
    static bool lastButtonState = HIGH;
    bool buttonState = digitalRead(manualButtonPin);

    // Manual Mode
    if (buttonState == LOW && lastButtonState == HIGH) {
        manualMode = true;
        manualStartTime = currentTime; // Record start time of manual mode
        controlRelay(!relayState);    // Toggle relay
        updateLCD("Manual Mode", relayState ? "Relay: ON" : "Relay: OFF");
        Serial.printf("[ManualMode] Relay toggled to %s\n", relayState ? "ON" : "OFF");
    }

    lastButtonState = buttonState;

    // Exit Manual Mode after 5 seconds of inactivity
    if (manualMode && (currentTime - manualStartTime >= 5000)) {
        // Countdown on LCD
        for (int i = 5; i > 0; i--) {
            updateLCD("Back to Auto Mode", "In " + String(i) + " sec");
            delay(1000);
        }

        manualMode = false; // Return to Auto Mode
        Serial.println("[ManualMode] Returning to Auto Mode");
        fetchDataAndControl();        // Fetch data from Firebase and act
        lastActionTime = millis();    // Reset action time for Auto Mode
    }

    // Auto Mode: Fetch and act every 15 minutes
    if (!manualMode && (currentTime - lastActionTime >= interval)) {
        lastActionTime = currentTime;
        fetchDataAndControl();
    }

    // Check WiFi
    checkWiFi();

    delay(100);
}