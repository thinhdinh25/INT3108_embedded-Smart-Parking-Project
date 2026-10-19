#include <ESP32Servo.h>

// Standalone SG90 test for the ESP32-C3 board used by the display controller.
// Servo signal wires: entry barrier -> GPIO0, exit barrier -> GPIO1.
constexpr int ENTRY_SERVO_PIN = 0;
constexpr int EXIT_SERVO_PIN = 1;

// SG90 units vary. These are common starting values; remove the barrier arms
// for the first run and adjust if either servo strains against a stop.
constexpr int SERVO_MIN_US = 500;
constexpr int SERVO_MAX_US = 2500;
constexpr int CLOSED_ANGLE = 0;
constexpr int OPEN_ANGLE = 90;
constexpr unsigned long HOLD_TIME_MS = 3000;

Servo entryServo;
Servo exitServo;

void setBothServos(int angle) {
  entryServo.write(angle);
  exitServo.write(angle);
  Serial.printf("Both servos: %s (%d degrees)\n",
                angle == OPEN_ANGLE ? "OPEN" : "CLOSED", angle);
}

void setup() {
  Serial.begin(115200);

  entryServo.setPeriodHertz(50);
  exitServo.setPeriodHertz(50);
  entryServo.attach(ENTRY_SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  exitServo.attach(EXIT_SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);

  setBothServos(CLOSED_ANGLE);
  delay(HOLD_TIME_MS);
}

void loop() {
  setBothServos(OPEN_ANGLE);
  delay(HOLD_TIME_MS);

  setBothServos(CLOSED_ANGLE);
  delay(HOLD_TIME_MS);
}
