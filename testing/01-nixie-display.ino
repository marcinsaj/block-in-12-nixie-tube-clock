/* BLOCK: IN-12 Nixie Tube Clock – Nixie Display Testing

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

#include <SPI.h>          // (included with Arduino IDE)


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
const int ADC_SET_PIN  = PIN_PC2;  // Analog input

// Nixie tube HV power supply on/off switch
const int ENA_PIN      = PIN_PC3;  // High side power switch TPS22810

// SPI pins
const int MOSI_PIN     = PIN_PB3;  // SPI - MOSI
const int MISO_PIN     = PIN_PB4;  // SPI - MISO
const int SCK_PIN      = PIN_PB5;  // SPI - SCK
const int SS_PIN       = PIN_PB2;  // SPI - SS

// Buttons
const int BT_ADJ_PIN   = PIN_PC0;  // ADJUST button
const int BT_SAV_PIN   = PIN_PC1;  // SAVE button

// RTC
const int INT_RTC_PIN  = PIN_PD2;  // RTC interrupt input

// LEDs
const int LED_SLP_PIN  = PIN_PD5;  // Sleep indicator
const int LED_STS_PIN  = PIN_PD3;  // Status indicator


void setup()
{
  // SPI
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // Nixie HV power supply
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);
}

void loop()
{
  for(int i = 0; i <= 9; i++)
  {
    NixieDisplay(i,i,i,i);
    delay(1000);
  }
}

// ============================================================================
// NIXIE DISPLAY FUNCTION
// ============================================================================
// Shows digits on 4 nixie tubes using 5 shift registers (5 bytes via SPI).
// Each tube has 10 cathodes (digits 0-9). To light a digit, we set one bit
// in the 5-byte data frame. Pass 0-9 to show a digit, or 255 to turn off.
//
// EXAMPLE: NixieDisplay(1, 2, 3, 4) - show "12:34"
//
// All 5 bytes start as zeros:
//   data[0] = 0b00000000   (bits  0- 7)
//   data[1] = 0b00000000   (bits  8-15)
//   data[2] = 0b00000000   (bits 16-23)
//   data[3] = 0b00000000   (bits 24-31)
//   data[4] = 0b00000000   (bits 32-39)
//
// Tube 1, digit 1 -> bit_position = 7 -> byte 0, bit 7:
//   data[0] = 0b10000000
//                ^--- bit 7 set to 1
//
// Tube 2, digit 2 -> bit_position = 16 -> byte 2, bit 0:
//   data[2] = 0b00000001
//                     ^--- bit 0 set to 1
//
// Tube 3, digit 3 -> bit_position = 25 -> byte 3, bit 1:
//   data[3] = 0b00000010
//                    ^--- bit 1 set to 1
//
// Tube 4, digit 4 -> bit_position = 34 -> byte 4, bit 2:
//   data[4] = 0b00000100
//                   ^--- bit 2 set to 1
//
// Final 5-byte frame sent via SPI (only 4 ones in 40 bits):
//   data[4] = 0b00000100
//   data[3] = 0b00000010
//   data[2] = 0b00000001
//   data[1] = 0b00000000
//   data[0] = 0b10000000
//
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
    if (digit > 9)
    {
      continue;
    }

    // Look up which bit position controls this digit on this tube
    // Example: tube 1, digit 1 -> bit_position = 7
    uint8_t bit_position = tube_bit_map[tube][digit];

    // Find which of the 5 bytes this bit belongs to
    // Example: bit 7 / 8 = 0 -> it's in byte 0 (data[0])
    uint8_t byte_number = bit_position / 8;

    // Find which bit inside that byte to set
    // Example: bit 7 % 8 = 7 -> it's bit 7 inside data[0]
    uint8_t bit_in_byte = bit_position % 8;

    // Set that one bit to 1, leave all other bits unchanged
    // Example: 1 << 7 = 0b10000000
    //   data[0] = 0b00000000 | 0b10000000 = 0b10000000
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
