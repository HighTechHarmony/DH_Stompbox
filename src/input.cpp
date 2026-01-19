#include "input.h"

// Pin Assignments
const int ENC_A = 2;
const int ENC_B = 3;
const int ENC_BTN = 4;
const int FOOT1 = 12;
const int FOOT2 = 9;
const int POT_PIN = A1; // Audio shield VOL pad

// Encoder State (interrupt-driven)
volatile int encoderRaw = 0;      // high-resolution transitions (4 per detent)
volatile int encoderPosition = 0; // user-facing detent count
volatile uint8_t encoderState = 0;

// state transition table: index = (prev<<2)|curr
const int8_t encoder_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0};

// Encoder Reading ISR
void encoderISR()
{
    uint8_t A = digitalRead(ENC_A);
    uint8_t B = digitalRead(ENC_B);
    uint8_t curr = (A << 1) | B;
    uint8_t idx = (encoderState << 2) | curr;
    encoderRaw += encoder_table[idx];
    encoderState = curr;

    // Only update user-facing position on full detent (typically 4 transitions)
    while (encoderRaw >= 4)
    {
        encoderPosition++;
        encoderRaw -= 4;
    }
    while (encoderRaw <= -4)
    {
        encoderPosition--;
        encoderRaw += 4;
    }
}

void setupInput()
{
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_BTN, INPUT_PULLUP);
    pinMode(FOOT1, INPUT_PULLUP);
    pinMode(FOOT2, INPUT_PULLUP);
    pinMode(POT_PIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // Initial encoder state and attach interrupts
    encoderState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);
}
