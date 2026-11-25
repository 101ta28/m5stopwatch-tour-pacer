// Forward every include to the board package, including pins-only users.
#include_next <pins_arduino.h>

// Arduino.h in M5Stack 3.3.7 supplies these after including the variant.
// A source may include the variant before Arduino.h and again afterwards.
#if defined(ARDUINO_M5STACK_STOPWATCH) && defined(Arduino_h) && !defined(TOUR_CORE_PIN_MACROS_READY)
#if ESP_ARDUINO_VERSION == ESP_ARDUINO_VERSION_VAL(3, 3, 7)
#define TOUR_CORE_PIN_MACROS_READY
#undef NUM_DIGITAL_PINS
#undef NUM_ANALOG_INPUTS
#undef EXTERNAL_NUM_INTERRUPTS
#undef analogInputToDigitalPin
#undef digitalPinToInterrupt
#undef digitalPinHasPWM
#endif
#endif
