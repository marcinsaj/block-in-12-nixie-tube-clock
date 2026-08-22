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
changes how the time is shown in CLK mode.

The firmware runs under a hardware watchdog. If the program ever hangs, the
microcontroller resets itself and the clock comes straight back, because the
time lives in the RTC chip and the settings live in EEPROM. */

#include <SPI.h>              // (included with Arduino IDE)
#include <Wire.h>             // (included with Arduino IDE)
#include <EEPROM.h>           // (included with Arduino IDE)
#include <avr/wdt.h>          // (part of the AVR compiler, nothing to install)
#include <OneButton.h>        // https://github.com/mathertel/OneButton
#include <jled.h>             // https://github.com/jandelgado/jled
#include <RTC_RX8025T.h>      // https://github.com/marcinsaj/RTC_RX8025T
#include <TimeLib.h>          // https://github.com/PaulStoffregen/Time

// A note on TimeLib, because its name suggests more than it does here.
// It is included for exactly one thing: the tmElements_t type that the RTC
// library fills in. Not a single TimeLib function is called anywhere in this
// firmware, so the linker drops the whole library - checked in the symbol
// table of the compiled binary, where none of its functions appear.
//
// This matters because TimeLib can keep a software clock of its own. That
// clock only ever moves forward inside now(), which nothing here calls, and
// the library installs no interrupt and no timer to move it on its own.
//
// Worth knowing as well: Time.cpp holds its own cache in a variable that is
// also called tm. It is static, so it exists only inside that file and has
// nothing to do with the global tm declared further down. The compiled
// binary contains exactly one symbol named tm, and it is ours.
//
// In short: the time this clock works with comes from the RX8025T chip and
// from nowhere else.


// ============================================================================
// NIXIE TUBE DIGITS
// ============================================================================

// Turn off tube - pass this value to NixieDisplay to keep a tube off
static const uint8_t CLR = 255;

// Not a digit and not CLR. It is used to say "we do not know what the tubes
// are showing right now", so the next picture is always really sent out.
static const uint8_t UNKNOWN_DIGIT = 254;

// Which bit of the 40 bit frame lights which digit on which tube
static const uint8_t tube_bit_map[4][10] = {
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

// The refresh setting that runs the cycle every single minute. This one gets
// the short animation, because a long one would never leave the display alone.
static const uint16_t EVERY_MINUTE = 1;


// ============================================================================
// FIXED DATE WRITTEN INTO THE RTC
// ============================================================================
// This clock never reads or shows a date, but the RX8025T keeps counting one
// anyway, and its datasheet is blunt about what may be put in there:
//
//   "Note with caution that writing non-existent date data may interfere
//    with normal operation of the calendar counter."
//
// The same datasheet gives the ranges: the date counter runs 01 to 31, the
// month counter runs 01 to 12. A zero in either of them is not a real date,
// so the date registers must not simply be filled with zeros. The calendar
// is documented as counting from January 1 2001, so that is the date used.
//
// The week register is a one-hot value and the datasheet warns that anything
// other than its seven listed values may interfere with normal operation.
// 1 January 2001 was a Monday.
static const uint8_t RTC_FIXED_DAY   = 1;   // date counter, allowed range 1-31
static const uint8_t RTC_FIXED_MONTH = 1;   // month counter, allowed range 1-12
static const uint8_t RTC_FIXED_WDAY  = 2;   // TimeLib counts 1 = Sunday, so 2 = Monday
static const uint8_t RTC_FIXED_YEAR  = 31;  // TimeLib counts from 1970, 31 writes 01 = 2001


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

// The same two numbers for the short version of the animation.
// With the refresh setting on "every minute" the full animation would take
// over the display - about 6.5 seconds out of every 60. One cycle each still
// spins and still lands on the time, but is over in about 3 seconds.
// Every other refresh setting keeps the full animation.
static const int SHORT_FAST_CYCLES = 1;
static const int SHORT_DECEL_CYCLES = 1;


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

// How brightly the LEDs are driven, 0 is dark and 255 is full power.
// They are only small indicators sitting next to four glowing tubes, so they
// are deliberately turned down - at full power they draw the eye away from
// the tubes and are unpleasant in a dark room.
//
// The value sets the steady brightness and, for the breathing effect, the
// brightness at the top of each breath. Two separate values because two
// different LEDs hardly ever look equally bright at the same setting.
// These two numbers are the only place to adjust it.
static const uint8_t LED_STS_BRIGHTNESS = 64;
static const uint8_t LED_SLP_BRIGHTNESS = 64;

// Time the high voltage needs to rise before the tubes can be written to
static const unsigned long HV_RISE_TIME = 100;

// How long the clock waits after power up before the high voltage converter
// is switched on for the very first time.
//
// Straight after power up the 5 V rail is still settling and the RTC is only
// just starting to answer, while the converter is by far the biggest load on
// the board. Switching it on into a supply that has not finished coming up is
// the worst possible moment for it: the dip it causes could pull the rail
// below the 2.7 V brown out level and reset the microcontroller, which would
// simply start the whole thing over again.
//
// Half a second of quiet solves that and costs nothing - a clock that comes
// to life half a second after being plugged in looks completely normal.
//
// This wait happens only once, at startup. Later mode changes switch the
// converter on immediately.
static const unsigned long HV_STARTUP_DELAY = 500;


// ============================================================================
// WATCHDOG
// ============================================================================
// The watchdog is a small timer inside the microcontroller that keeps running
// on its own and cannot be stopped by a hung program. Every pass through
// loop() the firmware calls wdt_reset(), which means "I am still alive".
// If that ever stops happening - for example because the I2C bus to the RTC
// locks up and a read never comes back - the watchdog runs out and resets the
// microcontroller. The clock then starts again from setup() instead of
// standing frozen with the tubes stuck on one old time.
//
// The clock has no display for error messages and normally sits on a shelf
// for months, so restarting itself is the only sensible way out of a hang.
// A restart costs nothing here: the time lives in the RTC chip and the
// settings live in EEPROM, so nothing is lost.
//
// 4 seconds is far longer than any normal step of the program needs. The only
// long job is the refresh animation, and that one feeds the watchdog on every
// single step of the way.
static const uint8_t WDT_TIMEOUT = WDTO_4S;


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
// MOSI, MISO and SCK are driven by the SPI library itself. They are written
// down here only so the whole pin map of the board is in one place.
static const int MOSI_PIN     = PIN_PB3;  // SPI - MOSI (handled by SPI library)
static const int MISO_PIN     = PIN_PB4;  // SPI - MISO (not used, no data comes back)
static const int SCK_PIN      = PIN_PB5;  // SPI - SCK  (handled by SPI library)
static const int SS_PIN       = PIN_PB2;  // SPI - SS, used as the latch pin

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

// Both LEDs start dark, updateLeds() gives them their real effect.
// Their pins need no pinMode() anywhere in setup() - JLed sets the direction
// itself before its first write to the pin.
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

// Time read from the RTC chip. Only three places ever write to it:
// readRtcTime() once the values have passed validation, advanceOneMinute()
// while repairing a broken RTC, and writeTimeToRtc() when the read back
// failed. The interrupt handler never touches it, so it needs no volatile.
tmElements_t tm;

// Set by the interrupt handler when the RTC signals a new minute
volatile bool rtcInterrupt = false;

// Set by readRtcTime() when the RTC answered normally but what it sent back
// is not a real time or a real date - something like 33:69, or a month of 00.
// The chip is clearly there and talking, its registers simply hold rubbish,
// so they have to be written over. This happens on a brand new chip, after
// the backup battery has been flat, and after a disturbed write.
bool rtcContentBad = false;

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

// True when the owed cycle has to be the full length animation, whatever the
// REF setting says. It is set in the two places where the tubes have been cold
// for a long time and need the long animation the most: after the clock wakes
// up from the sleep window, and once after power up. Both happen rarely, so
// there is no reason to shorten them. Cleared together with refreshPending.
bool refreshFull = false;

// Current state of the high voltage supply and of both LEDs
bool    hvPowerOn = false;
uint8_t statusLedState = LED_OFF;
uint8_t sleepLedState = LED_OFF;

// What was last sent to the tubes. showDigits() compares against this and
// skips the SPI transfer when the picture did not change. At power up we do
// not know yet what the shift registers hold, so it starts as unknown.
uint8_t lastShown[4] = { UNKNOWN_DIGIT, UNKNOWN_DIGIT, UNKNOWN_DIGIT, UNKNOWN_DIGIT };

// When the current blinking started. Editing always begins with the value
// lit, instead of catching the dark half of a free running rhythm.
unsigned long blinkStart = 0;


// ============================================================================
// setup() - runs once after power up
// ============================================================================
void setup()
{
  // High voltage supply off, first thing in setup(). The board holds the
  // enable pin of the TPS22810 low with a pull down resistor, so the supply
  // is already off before any code runs - this line simply takes that over
  // in software and keeps the tubes dark until a mode has been chosen.
  pinMode(ENA_PIN, OUTPUT);
  digitalWrite(ENA_PIN, LOW);
  hvPowerOn = false;

  // Switch the watchdog off, and do it early.
  // After a watchdog reset the watchdog is still running, and it comes back
  // with the shortest timeout it has - about 16 ms. Left alone it would reset
  // the microcontroller again and again and the clock would never start.
  // MCUSR holds the flags saying why the last reset happened, and one of them
  // blocks switching the watchdog off, so it is cleared first.
  MCUSR = 0;
  wdt_disable();

  // Now start the watchdog again with the real timeout. Everything from here
  // on, setup() included, is watched. Nothing in setup() takes anywhere near
  // 4 seconds, and the refresh animation feeds the watchdog while it runs.
  wdt_enable(WDT_TIMEOUT);

  // SPI for the shift registers that drive the tubes
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  SPI.begin();

  // The shift registers hold random bits after power up, so the cathodes are
  // cleared before the high voltage is ever switched on
  showDigits(CLR, CLR, CLR, CLR);

  // Buttons - attach click functions and set debounce to 50ms
  BT_ADJ.attachClick(BT_ADJ_Click);
  BT_ADJ.setDebounceMs(50);

  BT_SAV.attachClick(BT_SAV_Click);
  BT_SAV.setDebounceMs(50);

  // RTC interrupt pin - falling edge triggers when the RTC pulls INT low
  pinMode(INT_RTC_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_RTC_PIN), rtcInterruptHandler, FALLING);

  // Start the RTC and ask it for one interrupt every full minute
  RTC_RX8025T.init();
  RTC_RX8025T.initTUI(INT_MINUTE);
  RTC_RX8025T.statusTUI(INT_ON);

  // Read the saved settings and repair anything that is out of range
  loadSettings();

  // Read the time and make sure the chip is left holding something sensible.
  // A brand new RTC, or one whose backup battery went flat, comes up with
  // random register contents - that is exactly the "first power up gives
  // strange values" case, and it is dealt with here once and for all.
  //
  // No usable time at all - start the clock from 00:00
  if (!readRtcTime()) writeTimeToRtc(0, 0);

  // Time was fine but the date registers were not - keep the time and write
  // the whole lot back, which puts a real date into the chip
  else if (rtcContentBad) writeTimeToRtc(tm.Hour, tm.Minute);

  // The tubes have just been powered up after a long time without voltage,
  // so they get one warm up cycle. The flag waits if the clock starts in
  // idle mode or inside the sleep window.
  //
  // This one is always the full length animation, for the same reason as the
  // cycle after waking up: cold tubes need the long one, and it only happens
  // once per power up anyway.
  refreshPending = true;
  refreshFull = true;

  // Look at the rotary switch to find out which mode the clock starts in
  int adcValue = analogRead(ADC_SET_PIN);
  uint8_t position = readRotaryPosition(adcValue);

  // An unreadable switch means we stay dark until it gives a real position
  if (position == 0) position = MODE_IDLE;

  // Hand both LEDs over to JLed now, before the wait below, so the status LED
  // already carries the effect its mode calls for - lit, or breathing in idle
  currentMode = position;
  updateLeds();

  // Everything above this line is small signal work - I2C, EEPROM, pins.
  // The high voltage converter is switched on for the first time a few lines
  // below, inside enterMode(), so this is the moment to let the power supply
  // finish settling before that load arrives.
  //
  // The wait is not a plain delay(). JLed needs regular Update() calls to
  // animate, so the time is spent driving the LEDs instead of standing still.
  // A breathing status LED therefore breathes from the very first second.
  unsigned long waitStart = millis();

  while (millis() - waitStart < HV_STARTUP_DELAY)
  {
    ledStatus.Update();
    ledSleep.Update();
    wdt_reset();
  }

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
  // Tell the watchdog the firmware is alive and still doing its rounds.
  // If the program ever hangs, this line stops being reached, the watchdog
  // runs out and the microcontroller restarts itself.
  wdt_reset();

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
  // The flag is cleared first, before anything slow happens. If the RTC sends
  // the next pulse while we are still working, the flag is set again and the
  // new minute is handled on the next pass instead of being thrown away.
  rtcInterrupt = false;

  // Read the fresh time from the RTC chip. A failed read changes nothing -
  // the clock simply keeps showing the last time it knows.
  bool timeIsFresh = readRtcTime();

  // The chip answered with something impossible, so its registers are written
  // over with a sane time. Without this repair the display would sit frozen
  // for ever, because every following read would be rejected just the same.
  if (rtcContentBad)
  {
    // Two different cases end up here, and only one of them needs correcting.
    //
    // The date was wrong but the time was fine - tm already holds this very
    // minute, so it is written back as it is.
    //
    // The time itself was rubbish - then tm still holds the time from the
    // previous minute, because this read was rejected. Writing that back
    // would leave the clock running a minute slow for good, so it is stepped
    // on by one first. This interrupt is exactly what says a minute passed.
    if (!timeIsFresh) advanceOneMinute();

    writeTimeToRtc(tm.Hour, tm.Minute);
  }

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
// readRtcTime() - read the time from the RTC, but only keep a sane answer
// ============================================================================
// Two completely different things can go wrong, and they need different
// answers, so they are told apart here:
//
// 1. The I2C transfer fails. That says nothing about what is in the chip,
//    it only means this one conversation did not happen. The old time is
//    kept and nothing is written anywhere.
//
// 2. The transfer works but the numbers are impossible - 33:69, or a month
//    of 00. Then the registers really do hold rubbish. rtcContentBad is
//    raised so the caller can write a sane time over them. A brand new chip
//    or one whose backup battery went flat comes up exactly like this.
//
// Returns true when tm now holds a usable time, false when the old one stayed.
bool readRtcTime()
{
  tmElements_t fresh;

  rtcContentBad = false;

  // Case 1 - the chip did not answer properly, so judge nothing
  if (RTC_RX8025T.read(fresh) != 0) return false;

  // Case 2, the time itself. No working counter can produce these values,
  // and they must never reach the tubes or the sleep window.
  if (fresh.Hour > 23 || fresh.Minute > 59 || fresh.Second > 59)
  {
    rtcContentBad = true;
    return false;
  }

  // The time is good, so it is kept whatever the date registers say
  tm = fresh;

  // Case 2, the date. This clock never shows a date, but the chip keeps
  // counting one and its datasheet warns that impossible dates can upset the
  // calendar counter. So a bad date is repaired too - and because the time
  // above was already accepted, repairing it costs nothing but the seconds.
  if (fresh.Day < 1 || fresh.Day > 31) rtcContentBad = true;
  if (fresh.Month < 1 || fresh.Month > 12) rtcContentBad = true;
  if (fresh.Wday < 1 || fresh.Wday > 7) rtcContentBad = true;

  return true;
}


// ============================================================================
// advanceOneMinute() - move the kept time on by one minute
// ============================================================================
// Used when the RTC handed back rubbish and the clock has to fall back on the
// last time it knows. That time is one minute old: the minute interrupt that
// brought us there is exactly what says the minute has passed. Stepping it on
// keeps the repaired clock on time instead of a minute behind.
//
// Only the hour and the minute are touched. The date is never displayed and
// writeTimeToRtc() puts its own fixed date into the chip anyway.
void advanceOneMinute()
{
  tm.Minute = tm.Minute + 1;

  if (tm.Minute > 59)
  {
    tm.Minute = 0;
    tm.Hour = tm.Hour + 1;

    if (tm.Hour > 23) tm.Hour = 0;
  }
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

  // Count how many times in a row we saw the same position. Counting stops at
  // the limit, because a counter that keeps growing would roll over to zero
  // every few seconds and start the waiting time again for no reason.
  if (position == rotaryCandidate)
  {
    if (rotaryStableCount < ROTARY_STABLE_READINGS) rotaryStableCount = rotaryStableCount + 1;
  }
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

  // Coming out of the sleep window - the tubes were dark for hours, so they
  // always get one refresh cycle, whatever the REF setting says, and that one
  // is always the full length animation
  if (sleeping)
  {
    sleeping = false;
    refreshPending = true;
    refreshFull = true;
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
    refreshFull = false;
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

    // Every press restarts the blink rhythm, so the value lights up at once
    // and the user immediately sees what changed
    blinkStart = millis();

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

    // The tube we move on to should be lit the moment it is selected
    blinkStart = millis();

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

  showDigits(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// runOptionMode() - shared single value editor for LZO, REF and HRS
// ============================================================================
void runOptionMode(uint8_t target)
{
  if (adjustPressed)
  {
    adjustPressed = false;

    // Every press restarts the blink rhythm, so the new value lights up at once
    blinkStart = millis();

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
    showDigits(CLR, CLR, CLR, CLR);
    return;
  }

  if (target == MODE_SET_REF) showValue(refreshIntervalFor(editOption));
  else showValue(editOption);
}


// ============================================================================
// showZeroOption() - LZO, show the example time 01:11
// ============================================================================
// Leading zero on  -> 01:11 with all four tubes lit, only the zero blinks
// Leading zero off -> 1:11 with tube 1 dark, tubes 2, 3 and 4 blink
//
// The example uses the same digit on the last three tubes on purpose. The
// only thing that changes between the two settings is the first tube, and
// with 1:11 nothing else on the display draws the eye away from it.
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

  showDigits(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// startEditing() - load the value a settings mode starts from
// ============================================================================
void startEditing(uint8_t mode)
{
  if (mode == MODE_SET_TIME)
  {
    // The time editor starts from the time currently kept by the RTC.
    // If that read fails we simply edit the last known time.
    readRtcTime();
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

  // Editing starts right away, so the first tube blinks from the start.
  // The blink rhythm is restarted here, otherwise entering a settings mode
  // could land in the dark half and the value would stay off for a moment.
  editTube = 0;
  editing = true;
  blinkStart = millis();
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
// Seconds always start at 0, which lets the clock be lined up with a time
// signal - it starts running exactly on a full minute.
//
// The date registers get a real, existing date (1 January 2001) even though
// this clock never reads a date. The chip counts the calendar on its own and
// its datasheet warns that impossible dates can upset the calendar counter,
// so it is given something it can actually count from. See the constants
// near the top of the file for the details.
//
// The time goes in through write(), not through set(). write() puts the
// values straight into the registers, without running them through the date
// arithmetic in TimeLib first.
void writeTimeToRtc(uint8_t newHour, uint8_t newMinute)
{
  tmElements_t newTime;

  newTime.Second = 0;
  newTime.Minute = newMinute;
  newTime.Hour   = newHour;
  newTime.Wday   = RTC_FIXED_WDAY;
  newTime.Day    = RTC_FIXED_DAY;
  newTime.Month  = RTC_FIXED_MONTH;
  newTime.Year   = RTC_FIXED_YEAR;

  RTC_RX8025T.write(newTime);

  // Read it back, so the rest of the firmware works with the new time
  if (!readRtcTime())
  {
    // The chip did not answer, so at least keep working with what was asked
    // for. Without this the clock would silently show the old time.
    tm.Hour = newHour;
    tm.Minute = newMinute;
    tm.Second = 0;
  }
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
// refreshIntervalFor() - turn a saved refresh index into a number of minutes
// ============================================================================
// The index comes from EEPROM or from the editor, so it is checked here in
// one single place. Anything outside 0-3 would read past the end of the table,
// so a bad index simply means "refresh switched off".
uint16_t refreshIntervalFor(uint8_t index)
{
  if (index > 3) return 0;

  return refreshMinutes[index];
}


// ============================================================================
// refreshDue() - is a cathode refresh cycle due this minute
// ============================================================================
// This only answers whether the schedule says "now". Whether the animation
// can really be shown is decided later, in updateClockDisplay().
bool refreshDue()
{
  // Look up the interval in minutes for the saved setting
  uint16_t interval = refreshIntervalFor(refreshIndex);

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

    // After a power change we do not trust our own memory of the tubes any
    // more, so the next picture is sent out even if it looks unchanged
    forgetTubes();
  }
  else
  {
    // Clear the cathodes first, then cut the power
    showDigits(CLR, CLR, CLR, CLR);
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

    // MaxBrightness caps the top of the breath, Set holds a steady level.
    // Both end up at the same brightness, so the LED never jumps in level
    // when the effect changes.
    if (wantedStatus == LED_BREATHE) ledStatus = JLed(LED_STS_PIN).Breathe(LED_BREATHE_TIME).MaxBrightness(LED_STS_BRIGHTNESS).Forever();
    else ledStatus = JLed(LED_STS_PIN).Set(LED_STS_BRIGHTNESS);
  }

  if (wantedSleep != sleepLedState)
  {
    sleepLedState = wantedSleep;

    if (wantedSleep == LED_BREATHE) ledSleep = JLed(LED_SLP_PIN).Breathe(LED_BREATHE_TIME).MaxBrightness(LED_SLP_BRIGHTNESS).Forever();
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
  // Time counted from the moment the blinking started, so a fresh value is
  // always lit first and only then goes dark
  unsigned long elapsed = millis() - blinkStart;

  // Divide that time into 500 ms slots and light the even ones
  return ((elapsed / BLINK_INTERVAL) % 2) == 0;
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
  showDigits(digits[0], digits[1], digits[2], digits[3]);
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

  showDigits(digits[0], digits[1], digits[2], digits[3]);
}


// ============================================================================
// runRefreshCycle() - run the animation and land on the current time
// ============================================================================
// Two lengths of the same animation. The short one is used when the refresh
// setting is "every minute", because there the full animation would be running
// almost as often as the clock shows the time.
//
// A cycle owed after waking up, and the one owed after power up, are never
// shortened. The tubes stood cold for a long time and that is the moment they
// need the long animation the most, so refreshFull overrules the setting.
void runRefreshCycle()
{
  uint8_t digits[4];

  getDisplayDigits(digits);

  // Full animation unless both things are true: the setting says every
  // minute, and this is an ordinary scheduled cycle rather than one owed
  // after waking up or after power up
  bool useShort = false;

  if (refreshIntervalFor(refreshIndex) == EVERY_MINUTE && !refreshFull) useShort = true;

  if (useShort) slotMachine(digits, SHORT_FAST_CYCLES, SHORT_DECEL_CYCLES);
  else slotMachine(digits, FAST_CYCLES, DECEL_CYCLES);
}


// ============================================================================
// slotMachine() - cathode poisoning prevention animation
// ============================================================================
// 1. All 4 tubes spin through digits 0-9 together at fast speed
// 2. Each tube decelerates over a few full 0-9 cycles and stops on its final
//    digit. Tubes stop one by one from right to left
//
// fastCycles and decelCycles say how long the animation is. The full version
// uses 3 and 2 and takes about 6.5 seconds, the short one uses 1 and 1 and
// takes about 3 seconds. See runRefreshCycle() for which is used when.
//
// The deceleration uses a quadratic curve (step^2) instead of linear.
// This makes the slowdown much more natural:
//   - First digits in the deceleration still feel fast
//   - Last few digits slow down very noticeably
//   - The "landing" on the final digit feels satisfying
//
// The animation blocks for its whole length. Nothing is lost by that: the
// time is kept by the RTC chip, not counted here, and the buttons do nothing
// in clock mode anyway, which is the only mode the animation ever runs in.
// Turning the rotary switch during the animation simply takes effect when it
// has finished. The watchdog is fed on every step, so it never fires here.
//
// finalDigits holds the digits each tube lands on. A tube whose final digit
// is CLR goes dark as soon as it stops spinning.
void slotMachine(uint8_t finalDigits[4], int fastCycles, int decelCycles)
{
  // Which tubes are still spinning (true = spinning, false = stopped)
  bool spinning[4] = {true, true, true, true};

  // === PHASE 1: All tubes spin together at fast speed ===
  // Complete cycles of digits 0-9 to build up momentum
  for (int cycle = 0; cycle < fastCycles; cycle++)
  {
    for (int digit = 0; digit < 10; digit++)
    {
      // This animation blocks for seconds and never goes back to loop(),
      // so it has to feed the watchdog itself
      wdt_reset();

      // All tubes show the same digit
      showDigits(digit, digit, digit, digit);
      delay(SPEED_FAST);
    }
  }

  // === PHASE 2: Tubes decelerate and stop one by one, right to left ===
  // Total steps in deceleration = decelCycles * 10 digits
  int totalSteps = decelCycles * 10;

  for (int tube = 3; tube >= 0; tube--)
  {
    // This tube decelerates over its full 0-9 cycles
    for (int step = 0; step < totalSteps; step++)
    {
      // Still inside the long animation, so keep feeding the watchdog
      wdt_reset();

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

      showDigits(display[0], display[1], display[2], display[3]);
      delay(stepDelay);
    }

    // This tube is now stopped, next tube continues decelerating
    spinning[tube] = false;
  }

  // Show the finished time once more, now with every tube stopped
  showDigits(finalDigits[0], finalDigits[1], finalDigits[2], finalDigits[3]);
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
// showDigits() - show four digits, but only send them when they changed
// ============================================================================
// Every mode writes to the tubes on every pass through loop(), which would be
// thousands of SPI transfers per second showing the very same picture. The
// last picture is remembered here, so the shift registers and the latch pin
// are only used when something really changed. This is quieter for the
// electronics sitting next to the high voltage supply.
void showDigits(uint8_t tube1, uint8_t tube2, uint8_t tube3, uint8_t tube4)
{
  // Same picture as last time, so there is nothing to send
  if (tube1 == lastShown[0] && tube2 == lastShown[1] && tube3 == lastShown[2] && tube4 == lastShown[3]) return;

  // Remember the new picture and then send it out
  lastShown[0] = tube1;
  lastShown[1] = tube2;
  lastShown[2] = tube3;
  lastShown[3] = tube4;

  NixieDisplay(tube1, tube2, tube3, tube4);
}


// ============================================================================
// forgetTubes() - forget what the tubes are showing
// ============================================================================
// Called after the high voltage was switched, when we can no longer be sure
// that the tubes still show what we last sent. UNKNOWN_DIGIT can never match
// a real digit, so the next call to showDigits() always writes for real.
void forgetTubes()
{
  lastShown[0] = UNKNOWN_DIGIT;
  lastShown[1] = UNKNOWN_DIGIT;
  lastShown[2] = UNKNOWN_DIGIT;
  lastShown[3] = UNKNOWN_DIGIT;
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
