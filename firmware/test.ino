/* BLOCK IN-12 Nixie Tube Clock – Programming and Firmware

MiniCore Configuration
Before burning the bootloader, install MiniCore using the following Boards Manager URL:
https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json

In Arduino IDE → Tools, configure the settings as follows:
Board: ATmega328P / ATmega328PA
Clock: External 12 MHz
Bootloader: Yes (UART0)
BOD: 2.7V
EEPROM: Retained
Compiler LTO: Enabled
Baud Rate: Default
Variant: 328P / 328PA

⚠ Make sure the clock is set to External 12 MHz. ⚠

Programming Procedure:
1. Select the correct Programmer: AVR ISP MKII (recommended).
2. Click Burn Bootloader to set the fuse bits and configure the microcontroller.
3. After completion, select Upload Using Programmer to flash the firmware. */

#include <RTC_RX8025T.h>  // https://github.com/marcinsaj/RTC_RX8025T
#include <TimeLib.h>      // https://github.com/PaulStoffregen/Time
#include <Wire.h>         // https://arduino.cc/en/Reference/Wire (included with Arduino IDE)
#include <OneButton.h>    // https://github.com/mathertel/OneButton
#include <EEPROM.h>       // (included with Arduino IDE)
#include <SPI.h>          // (included with Arduino IDE)
#include <avr/pgmspace.h> // (included with Arduino IDE)
#include <avr/wdt.h>      // (included with Arduino IDE)

// Serial debugging 1-ON, 0-OFF
#define DEBUG 0

#if DEBUG
#include <SoftwareSerial.h>

const int RX_PIN = PIN_PD0;
const int TX_PIN = PIN_PD1;

SoftwareSerial mySerial(RX_PIN, TX_PIN);
#endif

// ============================================================================
// NIXIE TUBE DIGITS
// ============================================================================
const uint8_t tube_bit_map[4][10] = {
  // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9    // Digits
  {  8,  7,  6,  5,  4,  3,  2,  1,  0,  9 }, // Tube 1 Hours
  { 18, 17, 16, 15, 14, 13, 12, 11, 10, 19 }, // Tube 2 Hours
  { 28, 27, 26, 25, 24, 23, 22, 21, 20, 29 }, // Tube 3 Minutes
  { 48, 47, 46, 45, 44, 43, 42, 41, 40, 49 }  // Tube 4 Minutes
};

// ============================================================================
// PIN DECLARATIONS
// ============================================================================

// Settings - rotary switch
const int ADC_SET_PIN  = PIN_PC2;  // Analog input

// Nixie tube dc-dc converter on/off switch
const int ENA_PIN      = PIN_PC3;  // High side power switch TPS22810

// SPI pins
const int MOSI_PIN     = PIN_PB3;  // SPI - MOSI
const int MISO_PIN     = PIN_PB4;  // SPI - MISO
const int SCK_PIN      = PIN_PB5;  // SPI - SCK
const int SS_PIN       = PIN_PB2;  // SPI - MISO

// Buttons
const int ADJ_BT_PIN   = PIN_PD1;  // ADJUST button
const int SAV_BT_PIN   = PIN_PC0;  // SAVE button

// RTC
const int INT_RTC_PIN  = PIN_PD2;  // RTC interrupt input

// LED - Used to indicate potential hardware issues during the boot process
const int LED_SLP_PIN  = PIN_PD5;  // Sleep indicator
const int LED_STS_PIN  = PIN_PD3;  // Status indicator
