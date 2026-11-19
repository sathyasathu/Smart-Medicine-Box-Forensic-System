#include <Wire.h>
#include <WiFi.h>
#include "config.h"
#include "firebase.h"
#include <Firebase_ESP_Client.h>
#include <Preferences.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "mbedtls/sha256.h"

// =====================================================
// SMART MEDICINE BOX - FORENSIC ACCESS CONTROL
// Existing hardware only
// =====================================================

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

// ---------------- RTC ----------------
RTC_DS3231 rtc;
bool rtcAvailable = false;

// ---------------- STORAGE ----------------
Preferences preferences;

// ---------------- PINS ----------------
#define SDA_PIN       21
#define SCL_PIN       22

#define GREEN_LED     19
#define RED_LED       23
#define BUZZER_PIN    18

// IMPORTANT: this is the REED pin used by your uploaded project.
#define REED          4

#define BTN_SET       25
#define BTN_NEXT      26

// ---------------- REMINDERS ----------------
#define TOTAL_REMINDERS 3

const char* reminderNames[TOTAL_REMINDERS] = {
    "MORNING",
    "AFTERNOON",
    "NIGHT"
};

int reminderHour[TOTAL_REMINDERS] = {8, 14, 20};
int reminderMinute[TOTAL_REMINDERS] = {0, 0, 0};
bool reminderEnabled[TOTAL_REMINDERS] = {true, true, true};

int selectedReminder = 0;
int reminderEditField = 0;       // 0 = hour, 1 = minute
bool reminderSettingMode = false;

// Prevent the same scheduled minute from triggering repeatedly.
int lastTriggeredDay = -1;
int lastTriggeredSlot = -1;

// ---------------- ACCESS CONTROL ----------------
enum UserRole {
    ROLE_NONE,
    ROLE_PATIENT,
    ROLE_CARETAKER,
    ROLE_DOCTOR
};

// Demo credentials. CHANGE THESE before final demonstration.
const String PATIENT_PIN   = "1234";
const String CARETAKER_PIN = "2580";
const String DOCTOR_PIN    = "7319";

UserRole authenticatedUser = ROLE_NONE;
UserRole scheduledOwner = ROLE_NONE;

bool authenticated = false;
bool accessGranted = false;
bool alarmActive = false;
bool medicineTaken = false;
bool reminderExpired = false;

String enteredPIN = "";
int pinDigit = 0;
int selectedRole = 0; // 0 patient, 1 caretaker, 2 doctor

unsigned long accessGrantedAt = 0;
unsigned long authenticationAt = 0;
unsigned long reminderStartTime = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastButtonAction = 0;
unsigned long securityPromptStarted = 0;

const unsigned long ACCESS_WINDOW_MS = 20000;
const unsigned long AUTH_SESSION_MS = 60000;
const unsigned long ALARM_TIMEOUT_MS = 30000;
const unsigned long LONG_PRESS_MS = 1500;

// Security UI states
enum SecurityState {
    SECURITY_IDLE,
    SECURITY_ROLE,
    SECURITY_PIN
};

SecurityState securityState = SECURITY_IDLE;

// ---------------- REED ----------------
bool lastReedState = HIGH;

// ---------------- BUTTONS ----------------
bool lastSetState = HIGH;
bool lastNextState = HIGH;

// ---------------- FORENSIC LOG ----------------
struct LogEntry {
    String timeStamp;
    String event;
    String previousHash;
    String currentHash;
};

LogEntry logs[20];
int logIndex = 0;

String previousHash =
    "0000000000000000000000000000000000000000000000000000000000000000";

// =====================================================
// PROTOTYPES
// =====================================================

void bootScreen();
void hardwareTest();

void showHomeScreen();
void showReminderScreen();
void showMedicineTakenScreen();
void showMissedDoseScreen();
void showUnauthorizedScreen();

void showSecurityRoleScreen();
void showSecurityPinScreen();
void showAccessGrantedScreen();
void showAccessDeniedScreen();
void showAuthRequiredScreen();

void handleButtons();
void handleSecurityButtons();
void handleReminderButtons();

void enterSecurity();
void confirmRole();
void addPinDigit();
void confirmPin();
bool authenticateSelectedUser();
void clearAuthentication();

void enterReminderSetting();
void showReminderSetting();
void saveReminders();
void loadReminders();

void checkReminders();
void startAlarm(int slot);
void stopAlarm();
void checkReminderTimeout();

void checkBoxAccess();
void grantAuthorizedAccess();

void addLog(String event);
String generateHash(String input);
String roleName(UserRole role);

void connectWiFi();

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" SMART MEDICINE BOX");
    Serial.println(" FORENSIC ACCESS CONTROL MODE");
    Serial.println("========================================");

    // ---------------- I2C ----------------
    Wire.begin(SDA_PIN, SCL_PIN);
    delay(100);

    // ---------------- GPIO ----------------
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);

    pinMode(REED, INPUT_PULLUP);
    // Capture the real initial door state to avoid a false boot event.
    lastReedState = digitalRead(REED);

    pinMode(BTN_SET, INPUT_PULLUP);
    pinMode(BTN_NEXT, INPUT_PULLUP);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, LOW);

    // ---------------- OLED ----------------
    Serial.println("Initializing OLED...");

    if (!display.begin(
            SSD1306_SWITCHCAPVCC,
            SCREEN_ADDRESS))
    {
        Serial.println("OLED FAILED!");

        while (true) {
            digitalWrite(RED_LED, !digitalRead(RED_LED));
            delay(500);
        }
    }

    Serial.println("OLED OK");

    // ---------------- RTC ----------------
    Serial.println("Initializing RTC...");

    if (!rtc.begin())
    {
        rtcAvailable = false;

        Serial.println("RTC FAILED!");
        Serial.println("Check SDA=21, SCL=22, VCC=3.3V, GND=GND");

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("RTC NOT FOUND!");
        display.println();
        display.println("SDA -> GPIO 21");
        display.println("SCL -> GPIO 22");
        display.println("VCC -> 3.3V");
        display.println("GND -> GND");
        display.display();

        // Do not freeze the ESP32.
        delay(1200);
    }
    else
    {
        rtcAvailable = true;

        Serial.println("RTC OK");

        if (rtc.lostPower())
        {
            Serial.println("RTC lost power.");
            Serial.println("Setting RTC to compile time.");

            rtc.adjust(
                DateTime(F(__DATE__), F(__TIME__))
            );
        }
    }

    // ---------------- WiFi ----------------
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SMART MEDICINE BOX");
    display.println();
    display.println("Connecting WiFi...");
    display.display();

    connectWiFi();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("WiFi Connected");
    }
    else
    {
        Serial.println("WiFi Offline");
    }

    // ---------------- Firebase ----------------
    firebaseInit();

    // ---------------- Preferences ----------------
    loadReminders();

    // Restore schedule owner.
    preferences.begin("security", true);
    int storedOwner = preferences.getInt("owner", 0);
    preferences.end();

    if (storedOwner >= 0 && storedOwner <= 3)
        scheduledOwner = (UserRole)storedOwner;
    else
        scheduledOwner = ROLE_NONE;

    updateMedicineStatus("LOCKED");

    bootScreen();
    hardwareTest();

    addLog("SYSTEM_START");
    addLog("SYSTEM_LOCKED");

    Serial.println();
    Serial.println("========================================");
    Serial.println("SYSTEM READY - AUTHENTICATION REQUIRED");
    Serial.println("========================================");
}

// =====================================================
// ROLE NAME
// =====================================================

String roleName(UserRole role)
{
    switch (role)
    {
        case ROLE_PATIENT:
            return "PATIENT";

        case ROLE_CARETAKER:
            return "CARETAKER";

        case ROLE_DOCTOR:
            return "DOCTOR";

        default:
            return "NONE";
    }
}

// =====================================================
// SECURITY ENTRY
// =====================================================

void enterSecurity()
{
    if (securityState != SECURITY_IDLE)
        return;

    securityState = SECURITY_ROLE;
    selectedRole = 0;
    enteredPIN = "";
    pinDigit = 0;

    Serial.println();
    Serial.println("SECURITY AUTHENTICATION STARTED");

    addLog("AUTH_SCREEN_OPENED");

    showSecurityRoleScreen();
}

void showSecurityRoleScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SECURE ACCESS");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    const char* roles[] = {
        "PATIENT",
        "CARETAKER",
        "DOCTOR"
    };

    for (int i = 0; i < 3; i++)
    {
        display.setCursor(0, 20 + i * 11);

        if (selectedRole == i)
            display.print("> ");
        else
            display.print("  ");

        display.println(roles[i]);
    }

    display.setCursor(0, 56);
    display.println("NEXT=SELECT SET=OK");

    display.display();
}

void showSecurityPinScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("AUTH: ");

    if (selectedRole == 0)
        display.println("PATIENT");
    else if (selectedRole == 1)
        display.println("CARETAKER");
    else
        display.println("DOCTOR");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 20);
    display.println("ENTER 4-DIGIT PIN");

    display.setTextSize(2);
    display.setCursor(45, 35);

    for (int i = 0; i < 4; i++)
    {
        if (i < (int)enteredPIN.length())
            display.print("*");
        else
            display.print("_");
    }

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("DIGIT=");
    display.print(pinDigit);
    display.print(" NEXT  SET=OK");

    display.display();
}

void confirmRole()
{
    securityState = SECURITY_PIN;
    enteredPIN = "";
    pinDigit = 0;

    Serial.print("Selected role: ");

    if (selectedRole == 0)
        Serial.println("PATIENT");
    else if (selectedRole == 1)
        Serial.println("CARETAKER");
    else
        Serial.println("DOCTOR");

    showSecurityPinScreen();
}

void addPinDigit()
{
    if (enteredPIN.length() >= 4)
        return;

    enteredPIN += String(pinDigit);

    Serial.print("PIN digit entered: ");
    Serial.println("*");

    pinDigit = 0;

    showSecurityPinScreen();
}

bool authenticateSelectedUser()
{
    UserRole selected = ROLE_NONE;

    if (selectedRole == 0)
        selected = ROLE_PATIENT;
    else if (selectedRole == 1)
        selected = ROLE_CARETAKER;
    else
        selected = ROLE_DOCTOR;

    String expectedPIN;

    if (selected == ROLE_PATIENT)
        expectedPIN = PATIENT_PIN;
    else if (selected == ROLE_CARETAKER)
        expectedPIN = CARETAKER_PIN;
    else if (selected == ROLE_DOCTOR)
        expectedPIN = DOCTOR_PIN;

    if (enteredPIN == expectedPIN)
    {
        authenticatedUser = selected;
        authenticated = true;
        authenticationAt = millis();

        Serial.println("AUTHENTICATION SUCCESS");
        Serial.print("Authenticated user: ");
        Serial.println(roleName(authenticatedUser));

        addLog(
            "AUTH_SUCCESS_" + roleName(authenticatedUser)
        );

        return true;
    }

    Serial.println("AUTHENTICATION FAILED");

    addLog(
        "AUTH_FAILURE_" + roleName(selected)
    );

    return false;
}

void confirmPin()
{
    if (enteredPIN.length() != 4)
    {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 10);
        display.println("ENTER 4 DIGITS");
        display.display();
        delay(700);

        enteredPIN = "";
        pinDigit = 0;
        showSecurityPinScreen();
        return;
    }

    if (!authenticateSelectedUser())
    {
        digitalWrite(RED_LED, HIGH);
        digitalWrite(BUZZER_PIN, HIGH);

        showAccessDeniedScreen();

        delay(700);

        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(RED_LED, LOW);

        enteredPIN = "";
        pinDigit = 0;

        securityState = SECURITY_IDLE;
        return;
    }

    // If a dose is currently active, authentication is for dose access.
    if (alarmActive)
    {
        if (authenticatedUser == scheduledOwner)
        {
            grantAuthorizedAccess();
        }
        else
        {
            Serial.println("ACCESS DENIED - USER DOES NOT OWN SCHEDULE");

            addLog(
                "ACCESS_DENIED_ROLE_MISMATCH_" +
                roleName(authenticatedUser)
            );

            digitalWrite(RED_LED, HIGH);
            digitalWrite(BUZZER_PIN, HIGH);

            showAccessDeniedScreen();

            delay(1000);

            digitalWrite(BUZZER_PIN, LOW);
            digitalWrite(RED_LED, LOW);

            clearAuthentication();
        }

        securityState = SECURITY_IDLE;
        return;
    }

    // Otherwise this is a scheduler authentication.
    Serial.println("Scheduler authenticated.");
    Serial.print("Schedule owner will be: ");
    Serial.println(roleName(authenticatedUser));

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println("AUTHENTICATED");
    display.println();
    display.print("USER: ");
    display.println(roleName(authenticatedUser));
    display.println();
    display.println("SET = SCHEDULE");
    display.println("NEXT = CANCEL");
    display.display();

    securityState = SECURITY_IDLE;
}

// =====================================================
// CLEAR AUTH
// =====================================================

void clearAuthentication()
{
    authenticated = false;
    authenticatedUser = ROLE_NONE;
    enteredPIN = "";
    pinDigit = 0;
}

// =====================================================
// BUTTON HANDLER
// =====================================================

void handleSecurityButtons()
{
    bool setState = digitalRead(BTN_SET);
    bool nextState = digitalRead(BTN_NEXT);

    if (millis() - lastButtonAction < 180)
        return;

    if (securityState == SECURITY_ROLE)
    {
        if (lastNextState == HIGH && nextState == LOW)
        {
            selectedRole++;

            if (selectedRole > 2)
                selectedRole = 0;

            lastButtonAction = millis();
            showSecurityRoleScreen();
        }

        if (lastSetState == HIGH && setState == LOW)
        {
            lastButtonAction = millis();
            confirmRole();
        }
    }
    else if (securityState == SECURITY_PIN)
    {
        if (lastNextState == HIGH && nextState == LOW)
        {
            pinDigit++;

            if (pinDigit > 9)
                pinDigit = 0;

            lastButtonAction = millis();
            showSecurityPinScreen();
        }

        if (lastSetState == HIGH && setState == LOW)
        {
            lastButtonAction = millis();

            if (enteredPIN.length() < 4)
                addPinDigit();
            else
                confirmPin();
        }
    }

    lastSetState = setState;
    lastNextState = nextState;
}

// =====================================================
// REMINDER SETTING
// =====================================================

void enterReminderSetting()
{
    if (!authenticated)
    {
        showAuthRequiredScreen();
        addLog("SCHEDULE_DENIED_NO_AUTH");
        delay(700);
        return;
    }

    reminderSettingMode = true;
    selectedReminder = 0;
    reminderEditField = 0;

    Serial.println();
    Serial.println("REMINDER SETTING MODE");
    Serial.print("Scheduler: ");
    Serial.println(roleName(authenticatedUser));

    showReminderSetting();
}

void showReminderSetting()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("SET MEDICINE");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 16);
    display.print("OWNER: ");
    display.println(roleName(authenticatedUser));

    display.setCursor(0, 28);
    display.print(reminderNames[selectedReminder]);

    display.setCursor(0, 40);

    if (reminderEditField == 0)
        display.print("> ");
    else
        display.print("  ");

    display.print("HOUR: ");

    if (reminderHour[selectedReminder] < 10)
        display.print("0");

    display.println(reminderHour[selectedReminder]);

    display.setCursor(0, 52);

    if (reminderEditField == 1)
        display.print("> ");
    else
        display.print("  ");

    display.print("MIN: ");

    if (reminderMinute[selectedReminder] < 10)
        display.print("0");

    display.println(reminderMinute[selectedReminder]);

    display.display();
}

void handleReminderButtons()
{
    bool setState = digitalRead(BTN_SET);
    bool nextState = digitalRead(BTN_NEXT);

    if (millis() - lastButtonAction < 150)
        return;

    if (lastNextState == HIGH && nextState == LOW)
    {
        lastButtonAction = millis();

        if (reminderEditField == 0)
        {
            reminderHour[selectedReminder]++;

            if (reminderHour[selectedReminder] > 23)
                reminderHour[selectedReminder] = 0;
        }
        else
        {
            reminderMinute[selectedReminder]++;

            if (reminderMinute[selectedReminder] > 59)
                reminderMinute[selectedReminder] = 0;
        }

        showReminderSetting();
    }

    if (lastSetState == HIGH && setState == LOW)
    {
        lastButtonAction = millis();

        if (reminderEditField == 0)
        {
            reminderEditField = 1;
            showReminderSetting();
        }
        else
        {
            selectedReminder++;

            reminderEditField = 0;

            if (selectedReminder >= TOTAL_REMINDERS)
            {
                saveReminders();

                scheduledOwner = authenticatedUser;

                preferences.begin("security", false);
                preferences.putInt(
                    "owner",
                    (int)scheduledOwner
                );
                preferences.end();

                addLog(
                    "MEDICINE_SCHEDULED_OWNER_" +
                    roleName(scheduledOwner)
                );

                Serial.print("Medicine schedule owner: ");
                Serial.println(roleName(scheduledOwner));

                display.clearDisplay();
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                display.setCursor(0, 0);
                display.println("SCHEDULE SAVED");
                display.println();
                display.print("OWNER: ");
                display.println(roleName(scheduledOwner));
                display.println();
                display.println("ACCESS IS BOUND");
                display.println("TO THIS USER");

                display.display();

                delay(1500);

                reminderSettingMode = false;
                selectedReminder = 0;

                clearAuthentication();
            }
            else
            {
                showReminderSetting();
            }
        }
    }

    lastSetState = setState;
    lastNextState = nextState;
}

// =====================================================
// MAIN BUTTON HANDLER
// =====================================================

void handleButtons()
{
    bool setState = digitalRead(BTN_SET);
    bool nextState = digitalRead(BTN_NEXT);

    // Long NEXT from normal screen = security login.
    if (lastNextState == HIGH &&
        nextState == LOW)
    {
        securityPromptStarted = millis();

        while (digitalRead(BTN_NEXT) == LOW)
        {
            if (millis() - securityPromptStarted >= LONG_PRESS_MS)
            {
                enterSecurity();

                while (digitalRead(BTN_NEXT) == LOW)
                    delay(10);

                lastNextState = HIGH;
                lastSetState = digitalRead(BTN_SET);

                return;
            }

            delay(10);
        }
    }

    // Short SET after successful scheduler authentication = schedule.
    if (lastSetState == HIGH && setState == LOW)
    {
        if (authenticated &&
            !alarmActive &&
            !reminderSettingMode)
        {
            enterReminderSetting();

            while (digitalRead(BTN_SET) == LOW)
                delay(10);

            lastSetState = HIGH;
            lastNextState = digitalRead(BTN_NEXT);

            return;
        }
    }

    lastSetState = setState;
    lastNextState = nextState;
}

// =====================================================
// REMINDER CHECK
// =====================================================

void checkReminders()
{
    if (!rtcAvailable)
        return;

    DateTime now = rtc.now();

    for (int i = 0; i < TOTAL_REMINDERS; i++)
    {
        if (!reminderEnabled[i])
            continue;

        if (now.hour() == reminderHour[i] &&
            now.minute() == reminderMinute[i])
        {
            if (lastTriggeredDay != now.day() ||
                lastTriggeredSlot != i)
            {
                lastTriggeredDay = now.day();
                lastTriggeredSlot = i;

                startAlarm(i);
            }
        }
    }
}

// =====================================================
// START ALARM
// =====================================================

void startAlarm(int slot)
{
    if (scheduledOwner == ROLE_NONE)
    {
        addLog("REMINDER_DENIED_NO_SCHEDULE_OWNER");
        return;
    }

    alarmActive = true;
    medicineTaken = false;
    reminderExpired = false;
    accessGranted = false;

    reminderStartTime = millis();

    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);

    Serial.println();
    Serial.println("========================================");
    Serial.println("MEDICINE REMINDER ACTIVE");
    Serial.print("Reminder: ");
    Serial.println(reminderNames[slot]);
    Serial.print("Schedule Owner: ");
    Serial.println(roleName(scheduledOwner));
    Serial.println("AUTHENTICATION REQUIRED");
    Serial.println("========================================");

    addLog(
        "REMINDER_TRIGGERED_" +
        String(reminderNames[slot]) +
        "_OWNER_" +
        roleName(scheduledOwner)
    );

    updateMedicineStatus("REMINDER ACTIVE");
}

// =====================================================
// GRANT ACCESS
// =====================================================

void grantAuthorizedAccess()
{
    if (!alarmActive)
        return;

    if (!authenticated)
        return;

    if (authenticatedUser != scheduledOwner)
    {
        addLog(
            "ACCESS_DENIED_ROLE_MISMATCH_" +
            roleName(authenticatedUser)
        );

        showAccessDeniedScreen();
        return;
    }

    accessGranted = true;
    accessGrantedAt = millis();

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);

    Serial.println();
    Serial.println("========================================");
    Serial.println("ACCESS GRANTED");
    Serial.print("Authorized user: ");
    Serial.println(roleName(authenticatedUser));
    Serial.println("Door opening window: 20 seconds");
    Serial.println("========================================");

    addLog(
        "ACCESS_GRANTED_" +
        roleName(authenticatedUser)
    );

    updateMedicineStatus("ACCESS GRANTED");

    showAccessGrantedScreen();
}

// =====================================================
// REED SWITCH / FORENSIC ACCESS
// =====================================================

void checkBoxAccess()
{
    bool currentState = digitalRead(REED);

    // HIGH -> LOW = box opened
    if (lastReedState == HIGH &&
        currentState == LOW)
    {
        Serial.println();
        Serial.println("******** BOX OPENING DETECTED ********");

        if (accessGranted &&
            millis() - accessGrantedAt <= ACCESS_WINDOW_MS)
        {
            Serial.println("AUTHORIZED BOX OPEN");

            addLog(
                "AUTHORIZED_BOX_OPEN_" +
                roleName(authenticatedUser)
            );

            alarmActive = false;
            accessGranted = false;
            medicineTaken = true;
            reminderExpired = false;

            digitalWrite(BUZZER_PIN, LOW);
            digitalWrite(RED_LED, LOW);
            digitalWrite(GREEN_LED, HIGH);

            updateMedicineStatus("MEDICINE TAKEN");

            addLog(
                "MEDICINE_TAKEN_" +
                roleName(authenticatedUser)
            );

            showMedicineTakenScreen();

            clearAuthentication();
        }
        else
        {
            Serial.println("!!! UNAUTHORIZED BOX OPEN !!!");

            addLog("UNAUTHORIZED_BOX_OPEN");

            digitalWrite(RED_LED, HIGH);

            for (int i = 0; i < 3; i++)
            {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(150);

                digitalWrite(BUZZER_PIN, LOW);
                delay(150);
            }

            updateMedicineStatus("UNAUTHORIZED ACCESS");

            showUnauthorizedScreen();

            digitalWrite(RED_LED, LOW);
        }
    }

    lastReedState = currentState;
}

// =====================================================
// ALARM STOP
// =====================================================

void stopAlarm()
{
    alarmActive = false;

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);

    if (!medicineTaken)
        digitalWrite(GREEN_LED, LOW);

    Serial.println("Reminder stopped.");
}

// =====================================================
// TIMEOUT
// =====================================================

void checkReminderTimeout()
{
    if (!alarmActive)
        return;

    if (millis() - reminderStartTime < ALARM_TIMEOUT_MS)
        return;

    alarmActive = false;
    accessGranted = false;
    reminderExpired = true;

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);

    Serial.println("MISSED DOSE");

    addLog(
        "MISSED_DOSE_OWNER_" +
        roleName(scheduledOwner)
    );

    updateMedicineStatus("MISSED DOSE");

    showMissedDoseScreen();
}

// =====================================================
// AUTH / ACCESS SCREENS
// =====================================================

void showAuthRequiredScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("AUTHENTICATION");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 24);
    display.println("AUTHORIZED USER");

    display.setCursor(0, 39);
    display.println("REQUIRED");

    display.setCursor(0, 56);
    display.println("Hold NEXT = LOGIN");

    display.display();
}

void showAccessGrantedScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(12, 5);
    display.println("ACCESS");

    display.setCursor(20, 28);
    display.println("GRANTED");

    display.setTextSize(1);
    display.setCursor(10, 53);
    display.println("OPEN BOX NOW");

    display.display();
}

void showAccessDeniedScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 5);
    display.println("SECURITY ALERT");

    display.setTextSize(2);
    display.setCursor(5, 25);
    display.println("DENIED");

    display.display();
}

void showMedicineTakenScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(8, 10);
    display.println("MEDICINE");

    display.setCursor(20, 35);
    display.println("TAKEN");

    display.display();

    delay(1000);
}

void showMissedDoseScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(15, 10);
    display.println("MISSED");

    display.setCursor(27, 35);
    display.println("DOSE");

    display.display();
}

void showUnauthorizedScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(8, 8);
    display.println("FORENSIC ALERT");

    display.setTextSize(2);
    display.setCursor(5, 28);
    display.println("DENIED");

    display.display();

    delay(1200);
}

// =====================================================
// HOME SCREEN
// =====================================================

void showHomeScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("SMART MEDICINE BOX");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    if (rtcAvailable)
    {
        DateTime now = rtc.now();

        display.setCursor(0, 18);
        display.printf(
            "DATE : %02d/%02d/%04d",
            now.day(),
            now.month(),
            now.year()
        );

        display.setCursor(0, 32);
        display.printf(
            "TIME : %02d:%02d:%02d",
            now.hour(),
            now.minute(),
            now.second()
        );
    }
    else
    {
        display.setCursor(0, 22);
        display.println("RTC OFFLINE");
    }

    display.drawLine(0, 46, 127, 46, SSD1306_WHITE);

    display.setCursor(0, 53);

    if (scheduledOwner == ROLE_NONE)
        display.print("NO SCHEDULE");
    else
    {
        display.print("OWNER:");
        display.print(roleName(scheduledOwner));
    }

    display.display();
}

// =====================================================
// REMINDER SCREEN
// =====================================================

void showReminderScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(8, 5);
    display.println("MEDICINE");

    display.setCursor(28, 28);
    display.println("DUE");

    display.setTextSize(1);
    display.setCursor(4, 52);
    display.println("HOLD NEXT = LOGIN");

    display.display();
}

// =====================================================
// BOOT SCREEN
// =====================================================

void bootScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(12, 12);
    display.println("SMART");

    display.setCursor(28, 38);
    display.println("BOX");

    display.display();

    delay(1500);
}

// =====================================================
// HARDWARE TEST
// =====================================================

void hardwareTest()
{
    Serial.println("Hardware Test");

    digitalWrite(GREEN_LED, HIGH);
    delay(300);
    digitalWrite(GREEN_LED, LOW);

    digitalWrite(RED_LED, HIGH);
    delay(300);
    digitalWrite(RED_LED, LOW);

    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.print("Reed Switch: ");
    Serial.println(
        digitalRead(REED) == LOW ? "OPEN" : "CLOSED"
    );

    Serial.print("SET Button: ");
    Serial.println(
        digitalRead(BTN_SET) == LOW ? "Pressed" : "Released"
    );

    Serial.print("NEXT Button: ");
    Serial.println(
        digitalRead(BTN_NEXT) == LOW ? "Pressed" : "Released"
    );

    Serial.println("Hardware Test Complete");
}

// =====================================================
// REMINDER STORAGE
// =====================================================

void saveReminders()
{
    preferences.begin("medicine", false);

    for (int i = 0; i < TOTAL_REMINDERS; i++)
    {
        String h = "h" + String(i);
        String m = "m" + String(i);

        preferences.putInt(
            h.c_str(),
            reminderHour[i]
        );

        preferences.putInt(
            m.c_str(),
            reminderMinute[i]
        );
    }

    preferences.end();
}

void loadReminders()
{
    preferences.begin("medicine", true);

    for (int i = 0; i < TOTAL_REMINDERS; i++)
    {
        String h = "h" + String(i);
        String m = "m" + String(i);

        reminderHour[i] =
            preferences.getInt(
                h.c_str(),
                reminderHour[i]
            );

        reminderMinute[i] =
            preferences.getInt(
                m.c_str(),
                reminderMinute[i]
            );
    }

    preferences.end();
}

// =====================================================
// FORENSIC SHA-256 LOG
// =====================================================

String generateHash(String input)
{
    byte hash[32];

    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);

    mbedtls_sha256_starts(&ctx, 0);

    mbedtls_sha256_update(
        &ctx,
        (const unsigned char*)input.c_str(),
        input.length()
    );

    mbedtls_sha256_finish(&ctx, hash);

    mbedtls_sha256_free(&ctx);

    String output = "";

    char temp[3];

    for (int i = 0; i < 32; i++)
    {
        sprintf(temp, "%02x", hash[i]);
        output += temp;
    }

    return output;
}

void addLog(String event)
{
    String timestamp = "RTC_UNAVAILABLE";

    if (rtcAvailable)
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

        timestamp = String(buffer);
    }

    String record =
        timestamp +
        "|" +
        event +
        "|PREV=" +
        previousHash;

    String currentHash = generateHash(record);

    logs[logIndex].timeStamp = timestamp;
    logs[logIndex].event = event;
    logs[logIndex].previousHash = previousHash;
    logs[logIndex].currentHash = currentHash;

    previousHash = currentHash;

    Serial.println();
    Serial.println("----------------------------------------");
    Serial.println("FORENSIC EVENT");
    Serial.print("Time       : ");
    Serial.println(timestamp);
    Serial.print("Event      : ");
    Serial.println(event);
    Serial.print("Prev Hash  : ");
    Serial.println(logs[logIndex].previousHash);
    Serial.print("Curr Hash  : ");
    Serial.println(logs[logIndex].currentHash);
    Serial.println("----------------------------------------");

    uploadLog(event, currentHash, logs[logIndex].previousHash, roleName(authenticatedUser), roleName(scheduledOwner));

    logIndex++;

    if (logIndex >= 20)
        logIndex = 0;
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi()
{
    Serial.println("Connecting WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startTime = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startTime < 15000)
    {
        delay(250);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("WiFi Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("WiFi connection timeout.");
        Serial.println("Continuing Offline Mode.");
    }
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
    // Security authentication UI has priority.
    if (securityState != SECURITY_IDLE)
    {
        handleSecurityButtons();
        return;
    }

    // Reminder-setting UI has priority.
    if (reminderSettingMode)
    {
        handleReminderButtons();
        return;
    }

    firebaseLoop();

    // If an authenticated scheduler is waiting to press SET,
    // give them a chance to enter reminder setting.
    handleButtons();

    checkReminders();
    checkReminderTimeout();
    checkBoxAccess();

    // Authentication session timeout.
    if (authenticated &&
        millis() - authenticationAt > AUTH_SESSION_MS &&
        !alarmActive)
    {
        addLog(
            "AUTH_SESSION_EXPIRED_" +
            roleName(authenticatedUser)
        );

        clearAuthentication();
    }

    // Access window timeout.
    if (accessGranted &&
        millis() - accessGrantedAt > ACCESS_WINDOW_MS)
    {
        accessGranted = false;

        addLog(
            "ACCESS_WINDOW_EXPIRED_" +
            roleName(authenticatedUser)
        );

        clearAuthentication();
    }

    // OLED refresh.
    if (millis() - lastScreenUpdate >= 250)
    {
        lastScreenUpdate = millis();

        if (alarmActive)
        {
            if (accessGranted)
                showAccessGrantedScreen();
            else
                showReminderScreen();
        }
        else
        {
            showHomeScreen();
        }
    }

    delay(20);
}
