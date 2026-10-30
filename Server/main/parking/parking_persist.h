#ifndef PARKING_PERSIST_H
#define PARKING_PERSIST_H

/* Keeps RFID sessions, the log, the replay window and the parking clock in NVS. */
void parking_persist_load(void);
void parking_persist_save(void);

#endif
