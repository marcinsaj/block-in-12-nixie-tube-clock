/* BLOCK: IN-12 Nixie Tube Clock – Cathode Poisoning Prevention

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

Cathode poisoning prevention - slot machine effect:
Every 10 seconds all tubes spin through digits 0-9 fast,
then decelerate and stop one by one from right to left.

Between slot machine cycles, tubes show 12:34 as a test pattern. */

#include <SPI.h>          // (included with Arduino IDE)

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
static const int ENA_PIN = PIN_PC3;  // High side power switch TPS22810

// SPI pins
static const int SS_PIN  = PIN_PB2;  // SPI - SS

// ============================================================================
// SLOT MACHINE SETTINGS
// ============================================================================

// How often to run the slot machine effect (milliseconds)
static const unsigned long REFRESH_INTERVAL = 10000;

// Speed at the start of the animation - fast spin (milliseconds per digit)
static const int SPEED_FAST = 25;

// Speed at the end of the animation - slow stop (milliseconds per digit)
static const int SPEED_SLOW = 180;

// How many fast full cycles (0-9) before deceleration starts
static const int FAST_CYCLES = 3;

// How many deceleration cycles per tube (more = longer, smoother stopping)
static const int DECEL_CYCLES = 2;

// ============================================================================
// VARIABLES
// ============================================================================

// Timer for triggering slot machine effect
unsigned long lastRefresh = 0;

// Test pattern digits shown between animations: "12:34"
static const uint8_t testDigits[4] = {1, 2, 3, 4};

void setup()
{
  // SPI for shift registers (nixie tubes)
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // Nixie HV power supply - turn ON
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, HIGH);

  // Show test pattern on startup
  NixieDisplay(testDigits[0], testDigits[1], testDigits[2], testDigits[3]);

  // Record start time
  lastRefresh = millis();
}

void loop()
{
  // Check if it's time to run the slot machine effect
  if (millis() - lastRefresh >= REFRESH_INTERVAL)
  {
    // Run the slot machine cathode refresh animation
    slotMachine();

    // Show test pattern again after animation
    NixieDisplay(testDigits[0], testDigits[1], testDigits[2], testDigits[3]);

    // Reset timer
    lastRefresh = millis();
  }
}

// ============================================================================
// SLOT MACHINE EFFECT
// ============================================================================
// Cathode poisoning prevention animation:
// 1. All 4 tubes spin through digits 0-9 together at fast speed (3 full cycles)
// 2. Each tube decelerates over 2 full 0-9 cycles and stops on its final digit
//    Tubes stop one by one from right to left
//
// The deceleration uses a quadratic curve (step^2) instead of linear.
// This makes the slowdown much more natural:
//   - First digits in the deceleration still feel fast
//   - Last few digits slow down very noticeably
//   - The "landing" on the final digit feels satisfying
//
// With 2 deceleration cycles (20 steps total), the quadratic formula is:
//   step_delay = SPEED_FAST + (SPEED_SLOW - SPEED_FAST) * (step * step) / (totalSteps * totalSteps)
//
// Example delays across 20 steps (25ms -> 180ms):
//   step  0: 25ms   (fast)
//   step  5: 35ms   (still fast)
//   step 10: 64ms   (medium)
//   step 15: 112ms  (getting slow)
//   step 19: 180ms  (very slow - landing)
void slotMachine()
{
  // Which tubes are still spinning (true = spinning, false = stopped)
  bool spinning[4] = {true, true, true, true};

  // === PHASE 1: All tubes spin together at fast speed ===
  // 3 complete cycles of digits 0-9 to build up momentum
  for (int cycle = 0; cycle < FAST_CYCLES; cycle++)
  {
    for (int digit = 0; digit < 10; digit++)
    {
      // All tubes show the same digit
      NixieDisplay(digit, digit, digit, digit);
      delay(SPEED_FAST);
    }
  }

  // === PHASE 2: Tubes decelerate and stop one by one, left to right ===
  // Total steps in deceleration = DECEL_CYCLES * 10 digits
  int totalSteps = DECEL_CYCLES * 10;

  for (int tube = 3; tube >= 0; tube--)
  {
    // This tube decelerates over 2 full 0-9 cycles (20 steps)
    for (int step = 0; step < totalSteps; step++)
    {
      // Current digit is step wrapped to 0-9
      uint8_t currentDigit = step % 10;

      // Calculate delay using quadratic curve for smooth deceleration
      // step^2 / totalSteps^2 gives a value from 0.0 to 1.0 (quadratic)
      // Multiply by speed range and add to base speed
      long stepDelay = SPEED_FAST + (long)(SPEED_SLOW - SPEED_FAST) * step * step / ((long)totalSteps * totalSteps);

      // Build display: stopped tubes show their final digit,
      // spinning tubes show the current spin digit
      uint8_t display[4];
      for (int t = 0; t < 4; t++)
      {
        if (spinning[t]) display[t] = currentDigit;
        else display[t] = testDigits[t];
      }

      NixieDisplay(display[0], display[1], display[2], display[3]);
      delay(stepDelay);
    }

    // This tube is now stopped, next tube continues decelerating
    spinning[tube] = false;
  }
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
