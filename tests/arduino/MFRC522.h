#pragma once
#include "Arduino.h"
/* A reader "sees" a card only while its antenna is on and fake card_present is set. */
struct MFRC522 {
    struct Uid { byte size = 0; byte uidByte[10] = {0}; } uid;
    enum { RxGain_max = 0x70 };
    bool antenna = false;
    bool card_present = false;
    byte card_uid[10] = {0};
    byte card_uid_size = 0;
    void PCD_Init(byte, byte) {}
    void PCD_SetAntennaGain(int) {}
    void PCD_AntennaOn() { antenna = true; }
    void PCD_AntennaOff() { antenna = false; }
    void PCD_DumpVersionToSerial() {}
    bool PICC_IsNewCardPresent() { return antenna && card_present; }
    bool PICC_ReadCardSerial() {
        uid.size = card_uid_size;
        memcpy(uid.uidByte, card_uid, card_uid_size);
        return true;
    }
    void PICC_HaltA() {}
    void PCD_StopCrypto1() {}
};
