#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

void firebaseInit();
void firebaseLoop();
bool firebaseReady();

void updateStatus(bool online);
void uploadLog(
    String event,
    String hash,
    String previousHash,
    String authenticatedUser,
    String scheduleOwner
);

void updateMedicineStatus(String status);

#endif
