# BLOCK IN-12 Nixie Tube Clock

**[View the Kickstarter Campaign →](https://www.kickstarter.com/projects/marcinsaj/block-in-12-nixie-tube-clock)**

## Design Philosophy

This project was inspired by vintage laboratory and industrial control equipment. The enclosure is built from extruded aluminum profiles. The goal was to build a clock that does one thing well: display the time — and look distinctive doing it.

All clock settings are accessible directly from the front panel using a rotary switch and two push buttons. The clock operates entirely offline. There is no Wi-Fi, Bluetooth, or app.

## Key Features

- 4 × IN-12 Nixie tubes
- Heavy-duty enclosure built from extruded aluminum profiles
- 8-position rotary selector switch
- Two physical push buttons: **ADJUST** and **SAVE**
- Fully physical front-panel controls
- Programmable Sleep and Wake-Up times
- Automatic tube refresh routine
- Selectable 12-hour or 24-hour display
- No Wi-Fi, no Bluetooth, no app
- Rear-mounted main power switch
- 12 VDC / 1 A power input

## Datasheet

- Firmware - todo...
- Clock Enclosure Assembly Instructions - todo...
- [Schematic - BLOCK IN-12 Nixie Tube Clock](https://raw.githubusercontent.com/marcinsaj/block-in-12-nixie-tube-clock/main/datasheet/schematic-block-in-12-nixie-tube-clock.pdf)
- [Schematic - Rotary Selector - Settings Module](https://raw.githubusercontent.com/marcinsaj/block-in-12-nixie-tube-clock/main/datasheet/schematic-rotary-selector-block-in-12-nixie-tube-clock.pdf)

![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-01.webp)

## Front Panel Controls

The clock is operated using an **8-position rotary selector** and two push buttons.

| Control             | Function                                             |
| ------------------- | ---------------------------------------------------- |
| **ADJUST**          | Changes the value of the currently selected setting  |
| **SAVE**            | Confirms and saves the selected value                |
| **Rotary Selector** | Selects the operating mode or setting to be adjusted |

The front panel also includes a **Sleep indicator LED** located next to the **SLP** position.

This LED remains off during normal operation and slowly pulses only when the clock is actually in its scheduled Sleep period and the Nixie tubes are switched off.

## Rotary Selector

The rotary selector provides direct access to eight clock functions.

| Position | Function     | Description                                                                                         |
| -------- | ------------ | --------------------------------------------------------------------------------------------------- |
| **IDL**  | Idle         | Manual standby mode. The Nixie tubes are switched off while the clock continues running internally. |
| **CLK**  | Clock        | Normal clock operation. Displays the current time using the selected 12-hour or 24-hour format.     |
| **SET**  | Set Time     | Sets the current clock time.                                                                        |
| **SLP**  | Sleep Time   | Sets the time at which the Nixie tubes automatically switch off.                                    |
| **WUP**  | Wake-Up Time | Sets the time at which the Nixie tubes automatically switch back on.                                |
| **LZO**  | Leading Zero | Enables or disables the leading zero for single-digit hours.                                        |
| **REF**  | Tube Refresh | Sets the interval of the automatic tube refresh routine used to help reduce cathode poisoning.      |
| **HRS**  | Hour Format  | Selects the 12-hour or 24-hour time display format used during normal Clock operation.              |

![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-02.webp)

## Idle Mode

Selecting **IDL** manually places the clock in Idle mode.

The Nixie tubes are switched off, while the clock itself remains powered and continues keeping time.

An internal status LED, located at the **rear of the enclosure**, slowly pulses while the clock is in **IDL** mode. The LED is mounted inside the enclosure, and its light is visible through the rear ventilation openings.

This provides a subtle indication that the clock is still powered even though the Nixie tubes are off.

In all other operating modes, the internal status LED remains steadily illuminated.

## Sleep and Wake-Up

The **SLP** and **WUP** settings define an automatic daily Sleep period.

**SLP** specifies when the Nixie tubes switch off.

**WUP** specifies when the Nixie tubes switch back on.

For example:

```text
SLP  →  23:00
WUP  →  07:00
```

In this example, the Nixie tubes automatically switch off at 23:00 and switch back on at 07:00.

While the clock is within the scheduled Sleep period:

* the Nixie tubes are switched off
* the clock continues running internally
* the **Sleep indicator LED** next to **SLP** on the front panel slowly pulses

The pulsing front-panel LED indicates that the tubes are intentionally off because the clock is currently within its programmed Sleep period.

At the programmed **WUP** time, the Nixie tubes automatically switch back on and the Sleep indicator LED turns off.

## Leading Zero

The **LZO** setting controls whether a leading zero is displayed for single-digit hours.

```text
LZO ON   01:11
LZO OFF   1:11
```

## Tube Refresh

The **REF** setting controls how often the automatic Tube Refresh routine is performed.

During the refresh sequence, the clock cycles through the Nixie tube digits to help reduce cathode poisoning and promote more even cathode usage.

|  Setting | Refresh Interval |
| -------: | ---------------- |
|    **0** | Disabled         |
|    **1** | Every minute     |
|   **60** | Every hour       |
| **1440** | Every 24 hours   |

The values represent the refresh interval in **minutes**.

## Hour Format

The **HRS** setting determines how the time is displayed during normal **CLK** operation.

| Setting | Display Format |
| ------- | -------------- |
|  **12** | 12-hour format |
|  **24** | 24-hour format |

The HRS setting affects only the way the current time is displayed.

It does not affect the format used when setting the clock, Sleep time or Wake-Up time.

## 24-Hour Time Setup

All time-related settings are entered using the **24-hour format**, regardless of the selected **HRS** display mode.

This applies to:

* **SET** — current time
* **SLP** — Sleep time
* **WUP** — Wake-Up time

Using 24-hour time during setup eliminates the need for an additional AM/PM selection and keeps the adjustment process simple and unambiguous.

For example:

```text
6:30 AM  →  06:30
6:30 PM  →  18:30
```

Even when **HRS** is set to **12H**, SET, SLP and WUP are still adjusted using 24-hour time.

In the United States, the 24-hour format is also commonly referred to as military time.

## Status Indicators

The clock uses two separate status indicators.

| Indicator               | Location                                                                        | Behavior                                                                                    |
| ----------------------- | ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| **Internal Status LED** | Inside the enclosure at the rear, visible through the rear ventilation openings | Slowly pulses in **IDL** mode and remains steadily illuminated in all other operating modes |
| **Sleep Indicator LED** | Front panel, next to **SLP**                                                    | Slowly pulses only while the scheduled Sleep period is active                               |

The internal status LED provides a general indication that the clock is powered and operating.

The front-panel Sleep indicator has a separate purpose. It indicates specifically that the automatic Sleep schedule is currently active and that the Nixie tubes have been intentionally switched off by the clock.

## Rear Panel

The rear panel contains:

* **12 VDC / 1 A power input**
* **Main power switch**
* **Ventilation openings**

The main power switch is located directly next to the power input connector and completely switches the clock on or off.

The internal status LED is mounted inside the enclosure at the rear and is visible through the ventilation openings. It provides a discreet indication of the clock's operating state without requiring an additional externally mounted LED.

![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-03.webp)
