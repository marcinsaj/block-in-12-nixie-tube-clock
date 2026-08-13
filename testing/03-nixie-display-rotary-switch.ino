/* BLOCK: IN-12 Nixie Tube Clock – Rotary Switch Position Display

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

Rotary switch wiring:
8-position rotary switch connected to PC2 (analog input).
Each position outputs a fraction of 5V through a resistor divider:
  Position 1 = 1/9 of 5V = ~0.556V = ~114 ADC
  Position 2 = 2/9 of 5V = ~1.111V = ~228 ADC
  Position 3 = 3/9 of 5V = ~1.667V = ~341 ADC
  Position 4 = 4/9 of 5V = ~2.222V = ~455 ADC
  Position 5 = 5/9 of 5V = ~2.778V = ~569 ADC
  Position 6 = 6/9 of 5V = ~3.333V = ~682 ADC
  Position 7 = 7/9 of 5V = ~3.889V = ~796 ADC
  Position 8 = 8/9 of 5V = ~4.444V = ~910 ADC */

#include <SPI.h>          // (included with Arduino IDE)

// Turn off tube - pass this value to NixieDisplay to keep a tube off
static const uint8_t CLR = 255;

// ADC thresholds for rotary switch positions
// Each position is 1/9 of 5V apart = ~114 ADC counts
// We use midpoints between positions as boundaries
// Position 1: ADC ~114, lower bound 57
// Position 2: ADC ~228, lower bound 171
// ...and so on
static const int ADC_THRESHOLD_1 = 57;   // below this = invalid (no position)
static const int ADC_THRESHOLD_2 = 171;  // boundary between position 1 and 2
static const int ADC_THRESHOLD_3 = 285;  // boundary between position 2 and 3
static const int ADC_THRESHOLD_4 = 398;  // boundary between position 3 and 4
static const int ADC_THRESHOLD_5 = 512;  // boundary between position 4 and 5
static const int ADC_THRESHOLD_6 = 626;  // boundary between position 5 and 6
static const int ADC_THRESHOLD_7 = 739;  // boundary between position 6 and 7
static const int ADC_THRESHOLD_8 = 853;  // boundary between position 7 and 8

// ============================================================================
// NIXIE TUBE DIGITS
// ============================================================================
const uint8_t tube_bit_map[4][10] = {
  // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9    // Digits
  {  8,  7,  6,  5,  4,  3,  2,  1,  0,  9 }, // Tube 1 Hours
  { 18, 17, 16, 15, 14, 13, 12, 11, 10, 19 }, // Tube 2 Hours
  { 28, 27, 26, 25, 24, 23, 22, 21, 20, 29 }, // Tube 3 Minutes
  { 38, 37, 36, 35, 34, 33, 32, 31, 30, 39 }  // Tube 4 Minutes
};

// ============================================================================
// PIN DECLARATIONS
// ============================================================================

// Settings - rotary switch
static const int ADC_SET_PIN  = PIN_PC2;  // Analog input for rotary switch

// Nixie tube HV power supply on/off switch
static const int ENA_PIN      = PIN_PC3;  // High side power switch TPS22810

// SPI pins
static const int MOSI_PIN     = PIN_PB3;  // SPI - MOSI
static const int MISO_PIN     = PIN_PB4;  // SPI - MISO
static const int SCK_PIN      = PIN_PB5;  // SPI - SCK
static const int SS_PIN       = PIN_PB2;  // SPI - SS

// RTC
static const int INT_RTC_PIN  = PIN_PD2;  // RTC interrupt input

// LEDs
static const int LED_SLP_PIN  = PIN_PD5;  // Sleep indicator
static const int LED_STS_PIN  = PIN_PD3;  // Status indicator

void setup()
{
  // SPI
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // LEDs
  pinMode(LED_SLP_PIN, OUTPUT);
  pinMode(LED_STS_PIN, OUTPUT);

  // RTC interrupt
  pinMode(INT_RTC_PIN, INPUT);

  // Nixie HV power supply - turn on
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);
}

void loop()
{
  // Read analog value from rotary switch (0-1023)
  int adcValue = analogRead(ADC_SET_PIN);

  // Convert ADC value to switch position (1-8)
  uint8_t position = readRotaryPosition(adcValue);

  // Show the position on all 4 tubes at once
  // If position is 0 (invalid reading), turn all tubes off
  if (position == 0) NixieDisplay(CLR, CLR, CLR, CLR);
  else NixieDisplay(position, position, position, position);

  // Small delay to avoid flickering from noisy readings
  delay(50);
}

// ============================================================================
// ROTARY SWITCH READING
// ============================================================================
// Converts an ADC value (0-1023) to a rotary switch position (1-8).
// Returns 0 if the reading is below the lowest threshold (invalid).
// Uses midpoint boundaries between expected ADC values for each position.
uint8_t readRotaryPosition(int adcValue)
{
  // Below minimum - no valid position detected
  if (adcValue < ADC_THRESHOLD_1) return 0;

  // Check from highest ADC down - highest voltage = position 1 (reversed)
  if (adcValue >= ADC_THRESHOLD_8) return 1;
  if (adcValue >= ADC_THRESHOLD_7) return 2;
  if (adcValue >= ADC_THRESHOLD_6) return 3;
  if (adcValue >= ADC_THRESHOLD_5) return 4;
  if (adcValue >= ADC_THRESHOLD_4) return 5;
  if (adcValue >= ADC_THRESHOLD_3) return 6;
  if (adcValue >= ADC_THRESHOLD_2) return 7;

  // Between THRESHOLD_1 and THRESHOLD_2 = position 8
  return 8;
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
