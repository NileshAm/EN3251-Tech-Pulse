
#include <Arduino.h>

#define ONBOARD_LED 8
#define INTERRUPT_PIN 21
#define EXTERNAL_LED 0
#define RELAY_PIN 7

#define LED_ON  LOW
#define LED_OFF HIGH

volatile bool interruptDetected = false;

// Internal LED
unsigned long previousMillis = 0;
const unsigned long blinkInterval = 500;
bool onboardState = false;

// External LED interrupt indicator
unsigned long ledStartMillis = 0;
const unsigned long ledOnDuration = 100;
bool ledActive = false;

// Relay
unsigned long relayPreviousMillis = 0;
const unsigned long relayInterval = 5000;
bool relayState = false;

// Button debounce
const unsigned long debounceInterval = 100;
bool lastButtonReading = LOW;
bool stableButtonState = LOW;
unsigned long lastDebounceMillis = 0;

// Interrupt Service Routine
void IRAM_ATTR handleInterrupt() {
    interruptDetected = true;
}

void setup() {

    pinMode(ONBOARD_LED, OUTPUT);
    pinMode(EXTERNAL_LED, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);

    // Button connected between GPIO 21 and 3.3V
    // Requires a pull-down resistor
    pinMode(INTERRUPT_PIN, INPUT_PULLDOWN);

    digitalWrite(ONBOARD_LED, LED_OFF);
    digitalWrite(EXTERNAL_LED, LOW);
    digitalWrite(RELAY_PIN, LOW);

    attachInterrupt(
        digitalPinToInterrupt(INTERRUPT_PIN),
        handleInterrupt,
        RISING
    );
}

void loop() {

    unsigned long currentMillis = millis();

    // 1. Blink internal LED every 500 ms
    if (currentMillis - previousMillis >= blinkInterval) {
        previousMillis = currentMillis;
        onboardState = !onboardState;

        digitalWrite(
            ONBOARD_LED,
            onboardState ? LED_ON : LED_OFF
        );
    }

    // 2. Debounce button and detect one valid press
    bool buttonReading = digitalRead(INTERRUPT_PIN);

    if (buttonReading != lastButtonReading) {
        lastDebounceMillis = currentMillis;
    }

    if (currentMillis - lastDebounceMillis >= debounceInterval) {

        if (buttonReading != stableButtonState) {
            stableButtonState = buttonReading;

            if (stableButtonState == HIGH) {
                // Valid button press: flash external LED
                digitalWrite(EXTERNAL_LED, HIGH);
                ledStartMillis = currentMillis;
                ledActive = true;
            }
        }
    }

    lastButtonReading = buttonReading;

    // Turn external LED OFF after 100 ms
    if (ledActive &&
        currentMillis - ledStartMillis >= ledOnDuration) {

        digitalWrite(EXTERNAL_LED, LOW);
        ledActive = false;
    }

    // 3. Toggle relay every 1 second
    if (currentMillis - relayPreviousMillis >= relayInterval) {
        relayPreviousMillis = currentMillis;
        relayState = !relayState;

        digitalWrite(
            RELAY_PIN,
            relayState ? HIGH : LOW
        );
    }

    // Clear interrupt notification flag
    if (interruptDetected) {
        noInterrupts();
        interruptDetected = false;
        interrupts();
    }
}
