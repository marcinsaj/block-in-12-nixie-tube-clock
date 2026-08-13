/* BLOCK: IN-12 Nixie Tube Clock – LED Test

MiniCore Configuration
Before burning the bootloader, install MiniCore using the following Boards Manager URL:
https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json

In Arduino IDE -> Tools, configure the settings as follows:
Board: ATmega328P / ATmega328PA
Clock: External 12 MHz
Bootloader: Yes (UART0)
BOD: 2.7V
EEPROM: Retained
Compiler LTO: Enabled
Baud Rate: Default
Variant: 328P / 328PA

Make sure the clock is set to External 12 MHz.

Programming Procedure:
1. Select the correct Programmer: AVR ISP MKII (recommended).
2. Click Burn Bootloader to set the fuse bits and configure the microcontroller.
3. After completion, select Upload Using Programmer to flash the firmware.

Required library:
JLed - install via Arduino Library Manager (search "jled")

LED_SLP (PD5) - breathing effect using JLed (4 second cycle)
LED_STS (PD3) - blinks every 1 second using JLed */

#include <jled.h>         // https://github.com/jandelgado/jled

// ============================================================================
// PIN DECLARATIONS
// ============================================================================

// Nixie tube HV power supply on/off switch
static const int ENA_PIN = PIN_PC3;  // High side power switch TPS22810

// ============================================================================
// LED EFFECTS
// ============================================================================

// Sleep LED - breathing effect, 4000ms per cycle, repeats forever
auto ledSleep = JLed(PIN_PD5).Breathe(4000).Forever();

// Status LED - blinks every 1 second (500ms on, 500ms off), repeats forever
auto ledStatus = JLed(PIN_PD3).Blink(500, 500).Forever();

void setup()
{
  // Nixie HV power supply - turn OFF (not needed for LED test)
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, LOW);
}

void loop()
{
  // Update both LEDs every loop iteration (non-blocking)
  ledSleep.Update();
  ledStatus.Update();
}
