#pragma once
#include "Arduino.h"
/* Flash-backed EEPROM emulation; contents survive a fake "reboot" (setup() again). */
namespace fake { inline uint8_t eeprom[64]; inline int eeprom_commits = 0; }
struct FakeEEPROM {
    void begin(size_t) {}
    template <typename T> void get(int addr, T &v) { memcpy(&v, fake::eeprom + addr, sizeof(T)); }
    template <typename T> void put(int addr, const T &v) { memcpy(fake::eeprom + addr, &v, sizeof(T)); }
    bool commit() { ++fake::eeprom_commits; return true; }
};
inline FakeEEPROM EEPROM;
