/* BLOCK: IN-12 Nixie Tube Clock – RTC Test

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

Required libraries:
RTC_RX8025T - https://github.com/marcinsaj/RTC_RX8025T
TimeLib      - https://github.com/PaulStoffregen/Time

Sets the RTC to 12:34:00, configures 1-minute interrupt on PD2,
and displays current time on nixie tubes. */

#include <SPI.h>              // (included with Arduino IDE)
#include <Wire.h>             // (included with Arduino IDE)
#include <RTC_RX8025T.h>      // https://github.com/marcinsaj/RTC_RX8025T
#include <TimeLib.h>          // https://github.com/PaulStoffregen/Time

// Turn off tube - pass this value to NixieDisplay to keep a tube off
static const uint8_t CLR = 255;

// ============================================================================
// NIXIE TUBE DIGITS
// ============================================================================
const uint8_t tube_bit_map[4][10] = {
  // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9    // Digits
  {  8,  7,  6,  5,  4,  3,  2,  1,  0,  9 }, // Tube 1 Hours tens
  { 18, 17, 16, 15, 14, 13, 12, 11, 10, 19 }, // Tube 2 Hours ones
  { 28, 27, 26, 25, 24, 23, 22, 21, 20, 29 }, // Tube 3 Minutes tens
  { 38, 37, 36, 35, 34, 33, 32, 31, 30, 39 }  // Tube 4 Minutes ones
};

// ============================================================================
// PIN DECLARATIONS
// ============================================================================

// Nixie tube HV power supply on/off switch
static const int ENA_PIN     = PIN_PC3;  // High side power switch TPS22810

// SPI pins
static const int SS_PIN      = PIN_PB2;  // SPI - SS

// RTC interrupt
static const int INT_RTC_PIN = PIN_PD2;  // RTC interrupt input

// ============================================================================
// RTC VARIABLES
// ============================================================================

// Structure to store time read from RTC
tmElements_t tm;

// Flag set by interrupt when RTC signals a new minute
volatile bool rtcInterrupt = false;

void setup()
{
  // SPI for shift registers (nixie tubes)
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // Nixie HV power supply - turn ON
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);

  // RTC interrupt pin - falling edge triggers when RTC pulls INT low
  pinMode(INT_RTC_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_RTC_PIN), rtcInterruptHandler, FALLING);

  // Initialize RTC
  RTC_RX8025T.init();

  // Set the system time to 12:34:00 on 13 Aug 2026
  setTime(12, 34, 0, 13, 8, 26);

  // Write system time to the RTC chip
  RTC_RX8025T.set(now());

  // Configure RTC to generate interrupt every minute
  RTC_RX8025T.initTUI(INT_MINUTE);

  // Turn on the interrupt output
  RTC_RX8025T.statusTUI(INT_ON);

  // Read time from RTC and show it on tubes right away
  RTC_RX8025T.read(tm);
  showTime();
}

void loop()
{
  // When RTC signals a new minute, read time and update display
  if (rtcInterrupt)
  {
    // Read current time from RTC into tm structure
    RTC_RX8025T.read(tm);

    // Update nixie tubes with new time
    showTime();

    // Clear the interrupt flag
    rtcInterrupt = false;
  }
}

// ============================================================================
// RTC INTERRUPT HANDLER
// ============================================================================
// Called by hardware interrupt when RTC pulls INT pin low (every minute).
// Only sets a flag - actual work is done in loop().
void rtcInterruptHandler()
{
  rtcInterrupt = true;
}

// ============================================================================
// SHOW TIME ON NIXIE TUBES
// ============================================================================
// Splits hours and minutes into individual digits and sends them to tubes.
// Example: 12:34 -> tube1=1, tube2=2, tube3=3, tube4=4
void showTime()
{
  // Split hours into tens and ones digits
  uint8_t hourTens = tm.Hour / 10;
  uint8_t hourOnes = tm.Hour % 10;

  // Split minutes into tens and ones digits
  uint8_t minTens = tm.Minute / 10;
  uint8_t minOnes = tm.Minute % 10;

  // Send all 4 digits to the nixie tubes
  NixieDisplay(hourTens, hourOnes, minTens, minOnes);
}

// ============================================================================
// NIXIE DISPLAY FUNCTION
// ============================================================================
// Shows digits on 4 nixie tubes using 5 shift registers (5 bytes via SPI).
// Each tube has 10 cathodes (digits 0-9). To light a digit, we set one bit
// in the 5-byte data frame. Pass 0-9 to show a digit, or CLR to turn off.
void NixieDisplay(uint8_t tube1, uint8_t tube2, uint8_t tube3, uint8_t tube4)
{
  // 5 bytes = 40 bits, one bit for each cathode of all 4 tubes
  // All start as 0 - all cathodes off
  uint8_t data[5] = {0, 0, 0, 0, 0};

  // Store all 4 digits in an array so we can use a loop
  uint8_t digits[4] = {tube1, tube2, tube3, tube4};

  // For each tube, find which bit to set
  for (uint8_t tube = 0; tube < 4; tube++)
  {
    uint8_t digit = digits[tube];

    // Skip this tube if digit is not 0-9 (tube stays off)
    if (digit > 9) continue;

    // Look up which bit position controls this digit on this tube
    uint8_t bit_position = tube_bit_map[tube][digit];

    // Find which of the 5 bytes this bit belongs to
    uint8_t byte_number = bit_position / 8;

    // Find which bit inside that byte to set
    uint8_t bit_in_byte = bit_position % 8;

    // Set that one bit to 1, leave all other bits unchanged
    data[byte_number] = data[byte_number] | (1 << bit_in_byte);
  }

  // Send all 5 bytes to shift registers via SPI
  // We send from byte 4 to byte 0 because the first byte sent
  // gets pushed to the last register in the chain
  digitalWrite(SS_PIN, LOW);
  SPI.transfer(data[4]);
  SPI.transfer(data[3]);
  SPI.transfer(data[2]);
  SPI.transfer(data[1]);
  SPI.transfer(data[0]);
  digitalWrite(SS_PIN, HIGH);  // Latch - outputs update now
}
