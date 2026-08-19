/* BLOCK: IN-12 Nixie Tube Clock - Final Firmware

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
Make sure EEPROM is set to Retained, otherwise burning the bootloader
erases all saved clock settings.

Programming Procedure:
1. Select the correct Programmer: AVR ISP MKII (recommended).
2. Click Burn Bootloader to set the fuse bits and configure the microcontroller.
3. After completion, select Upload Using Programmer to flash the firmware.

Required libraries:
OneButton    - https://github.com/mathertel/OneButton
JLed         - https://github.com/jandelgado/jled
RTC_RX8025T  - https://github.com/marcinsaj/RTC_RX8025T
TimeLib      - https://github.com/PaulStoffregen/Time

WHAT THIS CLOCK DOES
Four IN-12 nixie tubes show hours and minutes. The time comes from an RX8025T
real time clock chip that sends an interrupt every full minute - the
microcontroller never counts time on its own.

An 8 position rotary switch selects the mode:
  1 IDL - idle, high voltage off, all tubes dark, status LED breathes
  2 CLK - normal clock, buttons do nothing
  3 SET - set the time, saved to the RTC chip
  4 SLP - set the hour when the tubes go dark, saved to EEPROM
  5 WUP - set the hour when the tubes light up again, saved to EEPROM
  6 LZO - leading zero in the hour on or off, saved to EEPROM
  7 REF - how often the cathode refresh animation runs, saved to EEPROM
  8 HRS - display format 12 or 24 hours, saved to EEPROM

All times are always entered in 24 hour format. The 12 hour setting only
changes how the time is shown in CLK mode. */

#include <SPI.h>              // (included with Arduino IDE)
#include <Wire.h>             // (included with Arduino IDE)
#include <EEPROM.h>           // (included with Arduino IDE)
#include <OneButton.h>        // https://github.com/mathertel/OneButton
#include <jled.h>             // https://github.com/jandelgado/jled
#include <RTC_RX8025T.h>      // https://github.com/marcinsaj/RTC_RX8025T
#include <TimeLib.h>          // https://github.com/PaulStoffregen/Time


// ============================================================================
// NIXIE TUBE DIGITS
// ============================================================================

// Turn off tube - pass this value to NixieDisplay to keep a tube off
static const uint8_t CLR = 255;

// Which bit of the 40 bit frame lights which digit on which tube
const uint8_t tube_bit_map[4][10] = {
  // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9    // Digits
  {  8,  7,  6,  5,  4,  3,  2,  1,  0,  9 }, // Tube 1 Hours tens
  { 18, 17, 16, 15, 14, 13, 12, 11, 10, 19 }, // Tube 2 Hours ones
  { 28, 27, 26, 25, 24, 23, 22, 21, 20, 29 }, // Tube 3 Minutes tens
  { 38, 37, 36, 35, 34, 33, 32, 31, 30, 39 }  // Tube 4 Minutes ones
};


// ============================================================================
// ROTARY SWITCH THRESHOLDS
// ============================================================================
// Each switch position feeds a different fraction of 5V to the ADC pin.
// Neighbour positions are about 114 ADC counts apart, so the boundaries
// below sit exactly halfway between them.
// Careful: the order is reversed - the highest voltage is position 1.
static const int ADC_THRESHOLD_1 = 57;   // below this = invalid (no position)
static const int ADC_THRESHOLD_2 = 171;  // boundary between position 1 and 2
static const int ADC_THRESHOLD_3 = 285;  // boundary between position 2 and 3
static const int ADC_THRESHOLD_4 = 398;  // boundary between position 3 and 4
static const int ADC_THRESHOLD_5 = 512;  // boundary between position 4 and 5
static const int ADC_THRESHOLD_6 = 626;  // boundary between position 5 and 6
static const int ADC_THRESHOLD_7 = 739;  // boundary between position 6 and 7
static const int ADC_THRESHOLD_8 = 853;  // boundary between position 7 and 8

// How often the rotary switch is read (milliseconds)
static const unsigned long ROTARY_READ_INTERVAL = 50;

// How many identical readings in a row are needed to accept a new mode.
// Three readings 50 ms apart means the switch must be steady for ~150 ms,
// so contact bounce cannot drag us through the modes in between.
static const uint8_t ROTARY_STABLE_READINGS = 3;


// ============================================================================
// MODE NUMBERS
// ============================================================================
// Rotary switch positions - each one is a different mode
static const uint8_t MODE_IDLE      = 1;  // IDL - everything off
static const uint8_t MODE_CLOCK     = 2;  // CLK - normal clock
static const uint8_t MODE_SET_TIME  = 3;  // SET - set the clock time
static const uint8_t MODE_SET_SLEEP = 4;  // SLP - set the sleep time
static const uint8_t MODE_SET_WAKE  = 5;  // WUP - set the wake up time
static const uint8_t MODE_SET_ZERO  = 6;  // LZO - leading zero on or off
static const uint8_t MODE_SET_REF   = 7;  // REF - cathode refresh interval
static const uint8_t MODE_SET_HOURS = 8;  // HRS - 12 or 24 hour format


// ============================================================================
// EEPROM MEMORY MAP
// ============================================================================
static const int EE_MAGIC         = 0;  // marks the memory as initialised
static const int EE_VERSION       = 1;  // layout version of the saved data
static const int EE_SLEEP_HOUR    = 2;  // 0-23
static const int EE_SLEEP_MINUTE  = 3;  // 0-59
static const int EE_WAKE_HOUR     = 4;  // 0-23
static const int EE_WAKE_MINUTE   = 5;  // 0-59
static const int EE_LEADING_ZERO  = 6;  // 0 = off, 1 = on
static const int EE_REFRESH_INDEX = 7;  // 0-3, index into refreshMinutes[]
static const int EE_HOUR_FORMAT   = 8;  // 12 or 24

// A brand new chip has 0xFF everywhere, so these two values tell us
// whether the settings were ever written by this firmware
static const uint8_t EE_MAGIC_VALUE   = 0xA5;
static const uint8_t EE_VERSION_VALUE = 1;

// Default settings, used on the first start and to replace any bad value
static const uint8_t DEFAULT_SLEEP_HOUR    = 0;
static const uint8_t DEFAULT_SLEEP_MINUTE  = 0;
static const uint8_t DEFAULT_WAKE_HOUR     = 0;
static const uint8_t DEFAULT_WAKE_MINUTE   = 0;
static const uint8_t DEFAULT_LEADING_ZERO  = 1;
static const uint8_t DEFAULT_REFRESH_INDEX = 1;
static const uint8_t DEFAULT_HOUR_FORMAT   = 24;

// The refresh setting is stored as an index 0-3 because 1440 does not fit
// into one byte. This table turns the index into a number of minutes.
static const uint16_t refreshMinutes[4] = { 0, 1, 60, 1440 };

// Minutes since midnight for 12:00. The daily refresh cycle runs at noon
// instead of midnight, because at night the tubes are most likely dark
// because of the sleep window and the cycle would be missed every day.
static const uint16_t NOON_MINUTES = 12 * 60;

// The whole day in minutes, used by the daily refresh cycle
static const uint16_t MINUTES_PER_DAY = 1440;


// ============================================================================
// SLOT MACHINE SETTINGS
// ============================================================================

// Speed at the start of the animation - fast spin (milliseconds per digit)
static const int SPEED_FAST = 25;

// Speed at the end of the animation - slow stop (milliseconds per digit)
static const int SPEED_SLOW = 180;

// How many fast full cycles (0-9) before deceleration starts
static const int FAST_CYCLES = 3;

// How many deceleration cycles per tube (more = longer, smoother stopping)
static const int DECEL_CYCLES = 2;


// ============================================================================
// BLINKING AND LED STATES
// ============================================================================

// A value being edited is on for 500 ms and off for 500 ms
static const unsigned long BLINK_INTERVAL = 500;

// The three effects a LED can have - used to avoid restarting an effect
// that is already running
static const uint8_t LED_OFF     = 0;
static const uint8_t LED_ON      = 1;
static const uint8_t LED_BREATHE = 2;

// One full breath of a LED takes 4 seconds
static const uint16_t LED_BREATHE_TIME = 4000;

// Time the high voltage needs to rise before the tubes can be written to
static const unsigned long HV_RISE_TIME = 100;


// ============================================================================
// PIN DECLARATIONS
// ============================================================================

// Settings - rotary switch
static const int ADC_SET_PIN  = PIN_PC2;  // Analog input for rotary switch

// Nixie tube HV power supply on/off switch
static const int ENA_PIN      = PIN_PC3;  // High side power switch TPS22810

// Buttons
static const int BT_ADJ_PIN   = PIN_PC0;  // ADJUST button
static const int BT_SAV_PIN   = PIN_PC1;  // SAVE button

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


// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

// Create button objects: pin, activeLow, pullupActive
// true, true = button connects to GND, internal pullup enabled
OneButton BT_ADJ(BT_ADJ_PIN, true, true);
OneButton BT_SAV(BT_SAV_PIN, true, true);

// Both LEDs start dark, updateLeds() gives them their real effect
auto ledStatus = JLed(LED_STS_PIN).Off();
auto ledSleep  = JLed(LED_SLP_PIN).Off();


// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Settings loaded from EEPROM at startup and kept in RAM
uint8_t sleepHour;
uint8_t sleepMinute;
uint8_t wakeHour;
uint8_t wakeMinute;
uint8_t leadingZero;
uint8_t refreshIndex;
uint8_t hourFormat;

// Time read from the RTC chip
tmElements_t tm;

// Set by the interrupt handler when the RTC signals a new minute
volatile bool rtcInterrupt = false;

// Which mode the rotary switch is in right now
uint8_t currentMode = MODE_IDLE;

// Rotary switch filtering
unsigned long lastRotaryRead = 0;  // when the switch was last read
uint8_t rotaryCandidate = 0;       // position seen in the last reading
uint8_t rotaryStableCount = 0;     // how many times in a row we saw it

// Button flags - the callbacks only set these, the mode handlers use them
bool adjustPressed = false;
bool savePressed = false;

// Editing state, shared by all settings modes
uint8_t editTube = 0;       // which tube is being edited now, 0 to 3
uint8_t editDigits[4];      // digits being edited, one per tube
uint8_t editOption = 0;     // single value being edited in LZO, REF and HRS
bool    editing = false;    // true = value is blinking and not saved yet

// True when the tubes are dark because the clock is inside the sleep window
bool sleeping = false;

// True when a refresh cycle is owed but could not be shown yet, because the
// tubes were dark or another mode was selected. The cycle then runs as soon
// as the clock is back to normal work, so a due cycle is never lost.
bool refreshPending = false;

// Current state of the high voltage supply and of both LEDs
bool    hvPowerOn = false;
uint8_t statusLedState = LED_OFF;
uint8_t sleepLedState = LED_OFF;


// ============================================================================
// setup() - runs once after power up
// ============================================================================
void setup()
{
  // SPI for the shift registers that drive the tubes
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // Nixie HV power supply - keep it off until we know the selected mode
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, LOW);
  hvPowerOn = false;

  // Make sure no cathode is lit while the high voltage comes up
  NixieDisplay(CLR, CLR, CLR, CLR);

  // Buttons - attach click functions and set debounce to 50ms
  BT_ADJ.attachClick(BT_ADJ_Click);
  BT_ADJ.setDebounceMs(50);

  BT_SAV.attachClick(BT_SAV_Click);
  BT_SAV.setDebounceMs(50);

  // LED pins
  pinMode(LED_STS_PIN, OUTPUT);
  pinMode(LED_SLP_PIN, OUTPUT);

  // RTC interrupt pin - falling edge triggers when the RTC pulls INT low
  pinMode(INT_RTC_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_RTC_PIN), rtcInterruptHandler, FALLING);

  // Start the RTC and ask it for one interrupt every full minute
  RTC_RX8025T.init();
  RTC_RX8025T.initTUI(INT_MINUTE);
  RTC_RX8025T.statusTUI(INT_ON);

  // Read the saved settings and repair anything that is out of range
  loadSettings();

  // Read the time and repair it too if the RTC returns nonsense
  RTC_RX8025T.read(tm);

  // Start from a known good time instead of random values
  if (tm.Hour > 23 || tm.Minute > 59) writeTimeToRtc(0, 0);

  // The tubes have just been powered up after a long time without voltage,
  // so they get one warm up cycle. The flag waits if the clock starts in
  // idle mode or inside the sleep window.
  refreshPending = true;

  // Look at the rotary switch and go straight into the selected mode
  int adcValue = analogRead(ADC_SET_PIN);
  uint8_t position = readRotaryPosition(adcValue);

  // An unreadable switch means we stay dark until it gives a real position
  if (position == 0) position = MODE_IDLE;

  // Start the filter from the position we are entering, so the switch has to
  // be turned and held before anything changes
  rotaryCandidate = position;
  rotaryStableCount = ROTARY_STABLE_READINGS;
  lastRotaryRead = millis();

  enterMode(position);
}


// ============================================================================
// loop() - runs over and over, everything here is non blocking
// ============================================================================
void loop()
{
  // Check buttons every loop - required by OneButton library
  BT_ADJ.tick();
  BT_SAV.tick();

  // Update both LEDs every loop iteration - required by JLed library
  ledStatus.Update();
  ledSleep.Update();

  // Look at the rotary switch and change mode when it was turned
  updateRotary();

  // The RTC pulled its interrupt pin low, so a new minute has started
  if (rtcInterrupt) handleRtcInterrupt();

  // Let the selected mode do its work
  switch (currentMode)
  {
    case MODE_IDLE:      runIdleMode();                   break;
    case MODE_CLOCK:     runClockMode();                  break;
    case MODE_SET_TIME:  runTimeEditMode(MODE_SET_TIME);  break;
    case MODE_SET_SLEEP: runTimeEditMode(MODE_SET_SLEEP); break;
    case MODE_SET_WAKE:  runTimeEditMode(MODE_SET_WAKE);  break;
    case MODE_SET_ZERO:  runOptionMode(MODE_SET_ZERO);    break;
    case MODE_SET_REF:   runOptionMode(MODE_SET_REF);     break;
    case MODE_SET_HOURS: runOptionMode(MODE_SET_HOURS);   break;
  }
}


// ============================================================================
// BT_ADJ_Click() - called by OneButton when the ADJUST button is clicked
// ============================================================================
// The callback only raises a flag, all the work happens in the mode handler
void BT_ADJ_Click()
{
  adjustPressed = true;
}


// ============================================================================
// BT_SAV_Click() - called by OneButton when the SAVE button is clicked
// ============================================================================
void BT_SAV_Click()
{
  savePressed = true;
}


// ============================================================================
// rtcInterruptHandler() - hardware interrupt, runs once every full minute
// ============================================================================
// Interrupt handlers must be as short as possible, so this one only sets
// a flag and lets loop() do the real work
void rtcInterruptHandler()
{
  rtcInterrupt = true;
}


// ============================================================================
// handleRtcInterrupt() - a new minute has started
// ============================================================================
void handleRtcInterrupt()
{
  // Read the fresh time from the RTC chip
  RTC_RX8025T.read(tm);

  // The flag is cleared here, before the possibly long refresh animation
  rtcInterrupt = false;

  // The sleep window may have just started or just ended, so the sleep LED
  // has to be checked every minute, in every mode
  updateLeds();

  // The refresh schedule ticks in every mode. When the animation cannot be
  // shown right now the request only waits in the flag, it is never lost.
  if (refreshDue()) refreshPending = true;

  // Only the clock mode shows the time, the settings modes keep their value
  if (currentMode == MODE_CLOCK) updateClockDisplay();
}


// ============================================================================
// updateRotary() - read the rotary switch and change mode when it was turned
// ============================================================================
void updateRotary()
{
  // The switch is read every 50 ms, the rest of the time we do nothing
  if (millis() - lastRotaryRead < ROTARY_READ_INTERVAL) return;

  lastRotaryRead = millis();

  // Read analog value from rotary switch (0-1023)
  int adcValue = analogRead(ADC_SET_PIN);

  // Convert ADC value to switch position (1-8)
  uint8_t position = readRotaryPosition(adcValue);

  // A zero means the contact is between two positions - ignore the reading
  // and stay in the current mode
  if (position == 0) return;

  // Count how many times in a row we saw the same position
  if (position == rotaryCandidate) rotaryStableCount = rotaryStableCount + 1;
  else
  {
    rotaryCandidate = position;
    rotaryStableCount = 1;
  }

  // Only a steady reading is allowed to change the mode
  if (rotaryStableCount < ROTARY_STABLE_READINGS) return;
  if (position == currentMode) return;

  enterMode(position);
}


// ============================================================================
// readRotaryPosition() - turn an ADC value into a switch position
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
// enterMode() - prepare everything for the newly selected mode
// ============================================================================
void enterMode(uint8_t newMode)
{
  currentMode = newMode;

  // Leaving a settings mode without pressing SAVE throws the edit away
  editing = false;
  editTube = 0;

  // A button pressed in the old mode must not act in the new one
  adjustPressed = false;
  savePressed = false;

  // Both LEDs depend on the mode, so they are set on every mode change
  updateLeds();

  // Idle mode is the only one with the tubes completely dark
  if (newMode == MODE_IDLE)
  {
    setHvPower(false);
    return;
  }

  // The clock mode decides about the high voltage on its own,
  // because it also has to handle the sleep window
  if (newMode == MODE_CLOCK)
  {
    updateClockDisplay();
    return;
  }

  // Every settings mode has all the tubes powered
  setHvPower(true);
  startEditing(newMode);
}


// ============================================================================
// runIdleMode() - IDL, the clock is resting
// ============================================================================
void runIdleMode()
{
  // The buttons do nothing here, but the flags still have to be cleared
  // so that an old press does not act after switching to another mode
  adjustPressed = false;
  savePressed = false;
}


// ============================================================================
// runClockMode() - CLK, normal clock work
// ============================================================================
void runClockMode()
{
  // The buttons do nothing in the clock mode either
  adjustPressed = false;
  savePressed = false;

  // Nothing else to do here - the display is written when the mode is
  // entered and then once every minute, when the RTC interrupt arrives
}


// ============================================================================
// updateClockDisplay() - show the time, fall asleep or wake up
// ============================================================================
void updateClockDisplay()
{
  // Ask the sleep rules what should happen right now
  bool sleepNow = isSleepTime();

  if (sleepNow)
  {
    // Inside the sleep window the tubes stay dark and the high voltage off.
    // A refresh cycle that falls due now simply waits in its flag.
    sleeping = true;
    setHvPower(false);
    return;
  }

  // Coming out of the sleep window - the tubes were dark for hours,
  // so they always get one refresh cycle, whatever the REF setting says
  if (sleeping)
  {
    sleeping = false;
    refreshPending = true;
  }

  // Normal work, so the tubes are lit
  setHvPower(true);

  // This is the only place where an owed refresh cycle is used up.
  // The animation ends on the digits of the current time, so it replaces
  // the plain display instead of coming on top of it.
  if (refreshPending)
  {
    runRefreshCycle();
    refreshPending = false;
  }
  else
  {
    showTime();
  }
}


// ============================================================================
// runTimeEditMode() - shared four digit editor for SET, SLP and WUP
// ============================================================================
void runTimeEditMode(uint8_t target)
{
  if (adjustPressed)
  {
    adjustPressed = false;

    // The first press after saving only starts the editing again,
    // it does not change the digit yet
    if (!editing)
    {
      editing = true;
      editTube = 0;
    }
    else
    {
      incrementDigit();
    }
  }

  if (savePressed)
  {
    savePressed = false;

    if (editing)
    {
      // Saving on the last tube stores the whole time and stops the blinking
      if (editTube == 3)
      {
        commitTime(target);
        editing = false;
        editTube = 0;
      }
      else
      {
        editTube = editTube + 1;
      }
    }
  }

  // Copy the digits into a display buffer so we can blank one of them
  uint8_t digits[4];

  for (uint8_t tube = 0; tube < 4; tube++)
  {
    digits[tube] = editDigits[tube];
  }

  // The tube being edited goes dark for half of every blink period
  if (editing && !blinkVisible()) digits[editTube] = CLR;

  NixieDisplay(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// runOptionMode() - shared single value editor for LZO, REF and HRS
// ============================================================================
void runOptionMode(uint8_t target)
{
  if (adjustPressed)
  {
    adjustPressed = false;

    // Just like in the time editor, the first press after saving
    // only starts the editing again
    if (!editing) editing = true;
    else nextOption(target);
  }

  if (savePressed)
  {
    savePressed = false;

    if (editing)
    {
      commitOption(target);
      editing = false;
    }
  }

  // The leading zero setting has its own way of showing the choice
  if (target == MODE_SET_ZERO)
  {
    showZeroOption();
    return;
  }

  // REF and HRS both show a number, and the whole number blinks
  if (editing && !blinkVisible())
  {
    NixieDisplay(CLR, CLR, CLR, CLR);
    return;
  }

  if (target == MODE_SET_REF) showValue(refreshMinutes[editOption]);
  else showValue(editOption);
}


// ============================================================================
// showZeroOption() - LZO, show the example time 01:23
// ============================================================================
// Leading zero on  -> 01:23 with all four tubes lit, only the zero blinks
// Leading zero off -> 1:23 with tube 1 dark, tubes 2, 3 and 4 blink
void showZeroOption()
{
  uint8_t digits[4] = { 0, 1, 1, 1 };

  // With the leading zero switched off the first tube is never lit
  if (editOption == 0) digits[0] = CLR;

  // During the dark half of the blink period hide the tubes that show
  // what this setting actually changes
  if (editing && !blinkVisible())
  {
    if (editOption == 1)
    {
      digits[0] = CLR;
    }
    else
    {
      digits[1] = CLR;
      digits[2] = CLR;
      digits[3] = CLR;
    }
  }

  NixieDisplay(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// startEditing() - load the value a settings mode starts from
// ============================================================================
void startEditing(uint8_t mode)
{
  if (mode == MODE_SET_TIME)
  {
    // The time editor starts from the time currently kept by the RTC
    RTC_RX8025T.read(tm);
    loadDigits(tm.Hour, tm.Minute);
  }
  else if (mode == MODE_SET_SLEEP)
  {
    loadDigits(sleepHour, sleepMinute);
  }
  else if (mode == MODE_SET_WAKE)
  {
    loadDigits(wakeHour, wakeMinute);
  }
  else if (mode == MODE_SET_ZERO)
  {
    editOption = leadingZero;
  }
  else if (mode == MODE_SET_REF)
  {
    editOption = refreshIndex;
  }
  else if (mode == MODE_SET_HOURS)
  {
    editOption = hourFormat;
  }

  // Editing starts right away, so the first tube blinks from the start
  editTube = 0;
  editing = true;
}


// ============================================================================
// loadDigits() - split an hour and a minute into the four editable digits
// ============================================================================
void loadDigits(uint8_t hour, uint8_t minute)
{
  editDigits[0] = hour / 10;
  editDigits[1] = hour % 10;
  editDigits[2] = minute / 10;
  editDigits[3] = minute % 10;
}


// ============================================================================
// incrementDigit() - ADJUST pressed, raise the digit on the edited tube
// ============================================================================
// Every tube has its own allowed range, so a wrong time can never be entered
void incrementDigit()
{
  editDigits[editTube] = editDigits[editTube] + 1;

  if (editTube == 0)
  {
    // Hours tens can only be 0, 1 or 2 - wrap back to 0 after 2
    if (editDigits[0] > 2) editDigits[0] = 0;

    // The new tens digit may make the ones digit illegal
    fixHourDigits();
  }
  else if (editTube == 1)
  {
    // Hours ones go up to 9, but only up to 3 when the tens digit is 2
    uint8_t maxOnes = 9;

    if (editDigits[0] == 2) maxOnes = 3;
    if (editDigits[1] > maxOnes) editDigits[1] = 0;
  }
  else if (editTube == 2)
  {
    // Minutes tens can only be 0 to 5
    if (editDigits[2] > 5) editDigits[2] = 0;
  }
  else
  {
    // Minutes ones go the full way from 0 to 9
    if (editDigits[3] > 9) editDigits[3] = 0;
  }
}


// ============================================================================
// fixHourDigits() - keep the hour inside 00-23 while the first tube is edited
// ============================================================================
// Example: the time is 19:00 and the user raises tube 1 from 1 to 2.
// Hour 29 does not exist, so the ones digit drops to 0 and we get 20:00.
// The correction happens straight away, the tubes never show 29.
void fixHourDigits()
{
  if (editDigits[0] == 2 && editDigits[1] > 3) editDigits[1] = 0;
}


// ============================================================================
// nextOption() - ADJUST pressed in LZO, REF or HRS
// ============================================================================
void nextOption(uint8_t target)
{
  if (target == MODE_SET_ZERO)
  {
    // Leading zero has only two states
    if (editOption == 0) editOption = 1;
    else editOption = 0;
  }
  else if (target == MODE_SET_REF)
  {
    // Step through the four refresh intervals: 0, 1, 60, 1440 minutes
    editOption = editOption + 1;

    if (editOption > 3) editOption = 0;
  }
  else
  {
    // Hour format switches between 12 and 24
    if (editOption == 24) editOption = 12;
    else editOption = 24;
  }
}


// ============================================================================
// commitTime() - SAVE pressed on the last tube in SET, SLP or WUP
// ============================================================================
void commitTime(uint8_t target)
{
  // Build the hour and the minute back out of the four digits
  uint8_t newHour = editDigits[0] * 10 + editDigits[1];
  uint8_t newMinute = editDigits[2] * 10 + editDigits[3];

  if (target == MODE_SET_TIME)
  {
    writeTimeToRtc(newHour, newMinute);
  }
  else if (target == MODE_SET_SLEEP)
  {
    // Keep the value in RAM and in EEPROM, so it works without a restart
    sleepHour = newHour;
    sleepMinute = newMinute;
    EEPROM.update(EE_SLEEP_HOUR, sleepHour);
    EEPROM.update(EE_SLEEP_MINUTE, sleepMinute);
  }
  else
  {
    wakeHour = newHour;
    wakeMinute = newMinute;
    EEPROM.update(EE_WAKE_HOUR, wakeHour);
    EEPROM.update(EE_WAKE_MINUTE, wakeMinute);
  }

  // The sleep window may have just changed, and setting both times to the
  // same hour is the only way to end a sleep that is already running
  updateLeds();
}


// ============================================================================
// writeTimeToRtc() - write an hour and a minute into the RTC chip
// ============================================================================
// This clock never reads the date, so the date registers are simply set to
// zero. Seconds always start at 0, which lets the clock be lined up with a
// time signal - it starts running exactly on a full minute.
//
// The time goes in through write(), not through set(). set() would push the
// value through makeTime() from TimeLib, and that function turns a day of 0
// into "one day earlier", which would give a completely wrong time.
// write() puts the values straight into the registers, without any maths.
//
// Two fields cannot be zero, because of how the library encodes them:
//   Wday - the library calculates 1 << (Wday - 1), so 0 would shift by -1
//   Year - the library writes Year - 30, so only 30 puts 00 into the chip
void writeTimeToRtc(uint8_t newHour, uint8_t newMinute)
{
  tmElements_t newTime;

  newTime.Second = 0;
  newTime.Minute = newMinute;
  newTime.Hour   = newHour;
  newTime.Wday   = 1;
  newTime.Day    = 0;
  newTime.Month  = 0;
  newTime.Year   = 30;

  RTC_RX8025T.write(newTime);

  // Read it back, so the rest of the firmware works with the new time
  RTC_RX8025T.read(tm);
}


// ============================================================================
// commitOption() - SAVE pressed in LZO, REF or HRS
// ============================================================================
void commitOption(uint8_t target)
{
  if (target == MODE_SET_ZERO)
  {
    leadingZero = editOption;
    EEPROM.update(EE_LEADING_ZERO, leadingZero);
  }
  else if (target == MODE_SET_REF)
  {
    refreshIndex = editOption;
    EEPROM.update(EE_REFRESH_INDEX, refreshIndex);
  }
  else
  {
    hourFormat = editOption;
    EEPROM.update(EE_HOUR_FORMAT, hourFormat);
  }
}


// ============================================================================
// isSleepTime() - is the current time inside the sleep window
// ============================================================================
bool isSleepTime()
{
  // Two identical times mean a window of zero length, so sleeping is off.
  // This also covers the 00:00 and 00:00 default.
  if (sleepHour == wakeHour && sleepMinute == wakeMinute) return false;

  // Convert hours and minutes into one number: minutes since midnight
  uint16_t sleepStart = sleepHour * 60 + sleepMinute;
  uint16_t wakeStart  = wakeHour  * 60 + wakeMinute;
  uint16_t nowMinutes = tm.Hour   * 60 + tm.Minute;

  // A window that stays inside one day, for example 09:00 to 17:00
  if (sleepStart < wakeStart) return (nowMinutes >= sleepStart && nowMinutes < wakeStart);

  // A window that crosses midnight, for example 22:00 to 07:00
  return (nowMinutes >= sleepStart || nowMinutes < wakeStart);
}


// ============================================================================
// refreshDue() - is a cathode refresh cycle due this minute
// ============================================================================
// This only answers whether the schedule says "now". Whether the animation
// can really be shown is decided later, in updateClockDisplay().
bool refreshDue()
{
  // Look up the interval in minutes for the saved setting
  uint16_t interval = refreshMinutes[refreshIndex];

  // Zero means the refresh cycle is switched off
  if (interval == 0) return false;

  // Minutes since midnight, used to decide when to run the refresh cycle
  uint16_t nowMinutes = tm.Hour * 60 + tm.Minute;

  // The daily cycle runs at noon and not at midnight. At night the tubes are
  // most likely dark because of the sleep window, so a midnight cycle would
  // be pushed to the morning almost every single day.
  if (interval == MINUTES_PER_DAY) return (nowMinutes == NOON_MINUTES);

  // Every other setting simply divides the day into equal steps
  return (nowMinutes % interval) == 0;
}


// ============================================================================
// setHvPower() - switch the high voltage supply for the tubes
// ============================================================================
void setHvPower(bool on)
{
  // Nothing to do when the supply is already in the wanted state
  if (hvPowerOn == on) return;

  hvPowerOn = on;

  if (on)
  {
    digitalWrite(ENA_PIN, HIGH);

    // Give the high voltage time to rise before writing to the tubes
    delay(HV_RISE_TIME);
  }
  else
  {
    // Clear the cathodes first, then cut the power
    NixieDisplay(CLR, CLR, CLR, CLR);
    digitalWrite(ENA_PIN, LOW);
  }
}


// ============================================================================
// updateLeds() - give both LEDs the effect that matches the current state
// ============================================================================
void updateLeds()
{
  // The status LED breathes in idle mode only, everywhere else it is lit
  uint8_t wantedStatus = LED_ON;

  if (currentMode == MODE_IDLE) wantedStatus = LED_BREATHE;

  // The sleep LED breathes whenever the clock is inside the sleep window,
  // no matter which mode the rotary switch is in. Turning the switch must
  // not hide the fact that the clock has already gone to sleep.
  // Idle mode is the only exception - there the sleep LED is dark.
  uint8_t wantedSleep = LED_OFF;

  if (currentMode != MODE_IDLE && isSleepTime()) wantedSleep = LED_BREATHE;

  // Only build a new effect when it really changed, otherwise the breathing
  // would restart from the beginning on every call
  if (wantedStatus != statusLedState)
  {
    statusLedState = wantedStatus;

    if (wantedStatus == LED_BREATHE) ledStatus = JLed(LED_STS_PIN).Breathe(LED_BREATHE_TIME).Forever();
    else ledStatus = JLed(LED_STS_PIN).On();
  }

  if (wantedSleep != sleepLedState)
  {
    sleepLedState = wantedSleep;

    if (wantedSleep == LED_BREATHE) ledSleep = JLed(LED_SLP_PIN).Breathe(LED_BREATHE_TIME).Forever();
    else ledSleep = JLed(LED_SLP_PIN).Off();
  }

  // Push the new effect out to the pins right away. Without this the LEDs
  // would only change on the next pass through loop(), which is too late
  // when a blocking refresh animation starts in the meantime.
  ledStatus.Update();
  ledSleep.Update();
}


// ============================================================================
// blinkVisible() - shared 500 ms on, 500 ms off rhythm for all edited values
// ============================================================================
bool blinkVisible()
{
  // Divide the running time into 500 ms slots and light the even ones
  return ((millis() / BLINK_INTERVAL) % 2) == 0;
}


// ============================================================================
// getDisplayDigits() - turn the RTC time into the four digits to show
// ============================================================================
// Handles the 12 or 24 hour format and the leading zero setting.
// A tube that has to stay dark gets CLR.
void getDisplayDigits(uint8_t digits[4])
{
  // Convert 24 hour time to 12 hour time for display only
  uint8_t displayHour = tm.Hour;

  if (hourFormat == 12)
  {
    if (displayHour == 0) displayHour = 12;
    else if (displayHour > 12) displayHour = displayHour - 12;
  }

  // Split the display hour into two digits
  digits[0] = displayHour / 10;
  digits[1] = displayHour % 10;

  // Minutes always keep both digits
  digits[2] = tm.Minute / 10;
  digits[3] = tm.Minute % 10;

  // When leading zero is off and the hour is below 10, turn tube 1 off
  if (leadingZero == 0 && digits[0] == 0) digits[0] = CLR;
}


// ============================================================================
// showTime() - write the current time to the tubes
// ============================================================================
void showTime()
{
  uint8_t digits[4];

  getDisplayDigits(digits);
  NixieDisplay(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// showValue() - write a number to the tubes, lined up to the right
// ============================================================================
// Unused tubes stay dark, for example 60 becomes __60 and 0 becomes ___0
void showValue(uint16_t value)
{
  uint8_t digits[4] = { CLR, CLR, CLR, CLR };

  // The last tube always shows something, even when the value is zero
  digits[3] = value % 10;

  // The other tubes only light up when the number is long enough
  if (value >= 10)   digits[2] = (value / 10) % 10;
  if (value >= 100)  digits[1] = (value / 100) % 10;
  if (value >= 1000) digits[0] = (value / 1000) % 10;

  NixieDisplay(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// runRefreshCycle() - run the animation and land on the current time
// ============================================================================
void runRefreshCycle()
{
  uint8_t digits[4];

  getDisplayDigits(digits);
  slotMachine(digits);
}


// ============================================================================
// slotMachine() - cathode poisoning prevention animation
// ============================================================================
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
// The whole animation blocks for about 4 seconds. Nothing is lost by that:
// the time is kept by the RTC chip, not counted here, and the buttons do
// nothing in clock mode anyway. Turning the rotary switch during the
// animation simply takes effect when it has finished.
//
// finalDigits holds the digits each tube lands on. A tube whose final digit
// is CLR goes dark as soon as it stops spinning.
void slotMachine(uint8_t finalDigits[4])
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

  // === PHASE 2: Tubes decelerate and stop one by one, right to left ===
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
        else display[t] = finalDigits[t];
      }

      NixieDisplay(display[0], display[1], display[2], display[3]);
      delay(stepDelay);
    }

    // This tube is now stopped, next tube continues decelerating
    spinning[tube] = false;
  }

  // Show the finished time once more, now with every tube stopped
  NixieDisplay(finalDigits[0], finalDigits[1], finalDigits[2], finalDigits[3]);
}


// ============================================================================
// loadSettings() - read the settings from EEPROM and repair bad values
// ============================================================================
// A brand new chip has 0xFF in every cell, so nothing here may be trusted
// before it has been checked against its allowed range.
void loadSettings()
{
  uint8_t magic = EEPROM.read(EE_MAGIC);
  uint8_t version = EEPROM.read(EE_VERSION);

  // Memory was never written by this firmware, or it holds an older layout
  if (magic != EE_MAGIC_VALUE || version != EE_VERSION_VALUE) saveDefaults();

  // Copy the saved settings into the working variables
  sleepHour    = EEPROM.read(EE_SLEEP_HOUR);
  sleepMinute  = EEPROM.read(EE_SLEEP_MINUTE);
  wakeHour     = EEPROM.read(EE_WAKE_HOUR);
  wakeMinute   = EEPROM.read(EE_WAKE_MINUTE);
  leadingZero  = EEPROM.read(EE_LEADING_ZERO);
  refreshIndex = EEPROM.read(EE_REFRESH_INDEX);
  hourFormat   = EEPROM.read(EE_HOUR_FORMAT);

  // Check every setting on its own and write the repair straight back,
  // so a bad value can never reach the running clock
  if (sleepHour > 23)
  {
    sleepHour = DEFAULT_SLEEP_HOUR;
    EEPROM.update(EE_SLEEP_HOUR, sleepHour);
  }

  if (sleepMinute > 59)
  {
    sleepMinute = DEFAULT_SLEEP_MINUTE;
    EEPROM.update(EE_SLEEP_MINUTE, sleepMinute);
  }

  if (wakeHour > 23)
  {
    wakeHour = DEFAULT_WAKE_HOUR;
    EEPROM.update(EE_WAKE_HOUR, wakeHour);
  }

  if (wakeMinute > 59)
  {
    wakeMinute = DEFAULT_WAKE_MINUTE;
    EEPROM.update(EE_WAKE_MINUTE, wakeMinute);
  }

  if (leadingZero > 1)
  {
    leadingZero = DEFAULT_LEADING_ZERO;
    EEPROM.update(EE_LEADING_ZERO, leadingZero);
  }

  if (refreshIndex > 3)
  {
    refreshIndex = DEFAULT_REFRESH_INDEX;
    EEPROM.update(EE_REFRESH_INDEX, refreshIndex);
  }

  if (hourFormat != 12 && hourFormat != 24)
  {
    hourFormat = DEFAULT_HOUR_FORMAT;
    EEPROM.update(EE_HOUR_FORMAT, hourFormat);
  }
}


// ============================================================================
// saveDefaults() - write the complete set of default settings to EEPROM
// ============================================================================
void saveDefaults()
{
  EEPROM.update(EE_SLEEP_HOUR, DEFAULT_SLEEP_HOUR);
  EEPROM.update(EE_SLEEP_MINUTE, DEFAULT_SLEEP_MINUTE);
  EEPROM.update(EE_WAKE_HOUR, DEFAULT_WAKE_HOUR);
  EEPROM.update(EE_WAKE_MINUTE, DEFAULT_WAKE_MINUTE);
  EEPROM.update(EE_LEADING_ZERO, DEFAULT_LEADING_ZERO);
  EEPROM.update(EE_REFRESH_INDEX, DEFAULT_REFRESH_INDEX);
  EEPROM.update(EE_HOUR_FORMAT, DEFAULT_HOUR_FORMAT);

  // The marker goes in last, so an interrupted write is noticed next time
  EEPROM.update(EE_MAGIC, EE_MAGIC_VALUE);
  EEPROM.update(EE_VERSION, EE_VERSION_VALUE);
}


// ============================================================================
// NixieDisplay() - send the four digits to the shift registers
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
