#include "firebase.h"
#include <RTClib.h>
#include "config.h"

#include <WiFi.h>
#include <Firebase_ESP_Client.h>

#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

extern RTC_DS3231 rtc;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool firebaseConnected = false;
unsigned long lastFirebaseCheck = 0;

void firebaseInit()
{
    Serial.println();
    Serial.println("==============================");
    Serial.println("Initializing Firebase...");
    Serial.println("==============================");

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected.");
        Serial.println("Firebase skipped.");

        firebaseConnected = false;
        return;
    }

    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    config.token_status_callback = tokenStatusCallback;
    config.timeout.serverResponse = 20 * 1000;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    Serial.println("Firebase started.");
    Serial.println("Waiting for authentication...");
}

void firebaseLoop()
{
    if (millis() - lastFirebaseCheck < 1000)
        return;

    lastFirebaseCheck = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        firebaseConnected = false;
        return;
    }

    if (Firebase.ready())
    {
        if (!firebaseConnected)
        {
            firebaseConnected = true;

            Serial.println();
            Serial.println("==============================");
            Serial.println("FIREBASE CONNECTED");
            Serial.println("==============================");

            updateStatus(true);
        }
    }
    else
    {
        if (firebaseConnected)
        {
            firebaseConnected = false;
            Serial.println("Firebase disconnected.");
        }
    }
}

bool firebaseReady()
{
    return firebaseConnected && Firebase.ready();
}

void updateStatus(bool online)
{
    if (!firebaseReady())
        return;

    Firebase.RTDB.setBool(
        &fbdo,
        "/MedicineBox/Status/Online",
        online
    );

    Firebase.RTDB.setInt(
        &fbdo,
        "/MedicineBox/Status/Uptime",
        millis()
    );

    Firebase.RTDB.setString(
        &fbdo,
        "/MedicineBox/Status/Device",
        "ESP32 Smart Medicine Box"
    );
}

void uploadLog(
    String event,
    String hash,
    String previousHash,
    String authenticatedUser,
    String scheduleOwner
)
{
    if (!firebaseReady())
    {
        Serial.println("Firebase unavailable. Log kept locally.");
        return;
    }

    FirebaseJson json;

    json.set("event", event);
    json.set("hash", hash);
    json.set("previousHash", previousHash);
    json.set("authenticatedUser", authenticatedUser);
    json.set("scheduleOwner", scheduleOwner);
    json.set("device", "ESP32 Smart Medicine Box");

    {
        DateTime now = rtc.now();

        char buffer[25];

        sprintf(
            buffer,
            "%02d/%02d/%04d %02d:%02d:%02d",
            now.day(),
            now.month(),
            now.year(),
            now.hour(),
            now.minute(),
            now.second()
        );

        json.set("time", String(buffer));
    }

    if (Firebase.RTDB.pushJSON(
            &fbdo,
            "/MedicineBox/Logs",
            &json))
    {
        Serial.println("Firebase Forensic Log Uploaded");
    }
    else
    {
        Serial.print("Firebase Log Error: ");
        Serial.println(fbdo.errorReason());
    }
}

void updateMedicineStatus(String status)
{
    if (!firebaseReady())
    {
        Serial.println("Firebase unavailable.");
        return;
    }

    if (Firebase.RTDB.setString(
            &fbdo,
            "/MedicineBox/Status/MedicineStatus",
            status))
    {
        Serial.print("Medicine Status: ");
        Serial.println(status);
    }
    else
    {
        Serial.print("Medicine Status Error: ");
        Serial.println(fbdo.errorReason());
    }
}
