#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// Pin Assignments
extern const int ENC_A;
extern const int ENC_B;
extern const int ENC_BTN;
extern const int FOOT1;
extern const int FOOT2;
extern const int POT_PIN;

// Encoder State
extern volatile int encoderRaw;
extern volatile int encoderPosition;
extern volatile uint8_t encoderState;

void setupInput();
void encoderISR();

#endif // INPUT_H
