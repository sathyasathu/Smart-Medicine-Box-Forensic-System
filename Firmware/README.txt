SMART MEDICINE BOX - INTEGRATED FORENSIC ACCESS CONTROL
===========================================================

This version is based on the uploaded SmartMedicineBox_final project and
keeps the existing hardware and Firebase architecture.

FINAL PINS
----------
SDA         GPIO 21
SCL         GPIO 22
Green LED   GPIO 19
Red LED     GPIO 23
Buzzer      GPIO 18
Reed        GPIO 4
SET         GPIO 25
NEXT        GPIO 26

SECURITY MODEL
--------------
The medicine schedule is bound to the authenticated scheduler:
PATIENT, CARETAKER, or DOCTOR.

Only the same role/PIN that owns the current schedule can authorize the
dose when the reminder is active.

The reed switch is the physical event sensor:
- Authorized user authenticates -> access window opens for 20 seconds.
- Reed opens inside that window -> AUTHORIZED_BOX_OPEN + MEDICINE_TAKEN.
- Reed opens without a valid access window -> UNAUTHORIZED_BOX_OPEN,
  red LED + buzzer, and forensic log.

Forensic records include:
- RTC timestamp
- event
- authenticated user
- schedule owner
- previous SHA-256 hash
- current SHA-256 hash
- Firebase path /MedicineBox/Logs

PIN DEMO VALUES
---------------
PATIENT   1234
CARETAKER 2580
DOCTOR    7319

CHANGE THESE BEFORE YOUR FINAL DEMO:
PATIENT_PIN
CARETAKER_PIN
DOCTOR_PIN

HOW TO USE THE BUTTONS
----------------------
1. From the normal home screen, HOLD NEXT for about 1.5 seconds.
2. Select PATIENT / CARETAKER / DOCTOR with NEXT.
3. Press SET to select the role.
4. Enter the four PIN digits:
   - Press NEXT repeatedly to choose a digit 0-9.
   - Press SET to confirm that digit.
5. After successful authentication, press SET to enter reminder setup.
6. Configure MORNING, AFTERNOON, and NIGHT:
   - NEXT increments the selected hour/minute.
   - SET switches hour -> minute -> next reminder.
7. After the third reminder is saved, that authenticated user becomes
   the schedule owner and the authentication session is cleared.

WHEN THE MEDICINE IS DUE
------------------------
1. Alarm + red LED activate.
2. OLED says authentication is required.
3. HOLD NEXT to log in.
4. The user must select the same role and enter the same PIN that
   originally created the schedule.
5. Matching credentials -> ACCESS GRANTED for 20 seconds.
6. Open the box using the reed switch.
7. The system records MEDICINE_TAKEN and clears the alarm.

UNAUTHORIZED ACCESS
-------------------
If the reed switch opens without an active authorized access window:
- UNAUTHORIZED_BOX_OPEN is recorded.
- Red LED turns on.
- Buzzer sounds.
- Firebase receives the forensic event when online.
- The SHA-256 chain continues from the previous event hash.

IMPORTANT LIMITATION
--------------------
Your existing hardware has no electronic lock. Therefore the system
cannot physically stop a person from opening the cardboard box. It can
authenticate the intended user, detect the physical opening, alarm on
unauthorized access, and preserve a tamper-evident forensic audit trail.

RTC
---
The code uses DS3231 on I2C:
SDA -> GPIO 21
SCL -> GPIO 22
If the RTC loses power, it is initialized to compile time only.

FILES
-----
SmartMedicineBox_final_20260807061720.ino
firebase.cpp
firebase.h
config.h

Before uploading, keep your existing config.h credentials. Do not share
your Firebase password or private credentials publicly.
