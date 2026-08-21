# BLOCK IN-12 Nixie Tube Clock

## [Kickstarter Campaign](https://www.kickstarter.com/projects/marcinsaj/block-in-12-nixie-tube-clock)

## Design Philosophy

This project was inspired by vintage laboratory and industrial control equipment. The enclosure is built from extruded aluminum profiles. The goal was to build a clock that does one thing well: display the time. All clock settings are accessible directly from the front panel using a rotary switch and two push buttons. The clock operates entirely offline. There is no Wi-Fi, Bluetooth, or app.

# BLOCK IN-12 — Features and Controls

## Key Features

* 4 × IN-12 Nixie tubes
* Heavy-duty enclosure built from extruded aluminum profiles
* 8-position rotary selector switch
* Two physical push buttons: **ADJUST** and **SAVE**
* Fully physical front-panel controls
* No Wi-Fi, no Bluetooth, no app
* Rear-mounted main power switch
* 12 VDC / 1 A power input

## Front Panel Controls

The clock is operated using an **8-position rotary selector** and two push buttons.

| Control             | Function                                             |
| ------------------- | ---------------------------------------------------- |
| **ADJUST**          | Changes the value of the currently selected setting  |
| **SAVE**            | Confirms and saves the selected value                |
| **Rotary Selector** | Selects the operating mode or setting to be adjusted |

## Rotary Selector

The rotary selector provides direct access to eight clock functions.

| Position | Function     | Description                                                                                                                                                                            |
| -------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **IDL**  | Idle         | Standby mode. The Nixie tubes are switched off while the clock continues running internally. The rear status LED slowly pulses to indicate that the clock is powered and in Idle mode. |
| **CLK**  | Clock        | Normal clock operation. Displays the current time using the selected 12-hour or 24-hour format.                                                                                        |
| **SET**  | Set Time     | Sets the current clock time.                                                                                                                                                           |
| **SLP**  | Sleep Time   | Sets the time at which the Nixie tubes automatically switch off.                                                                                                                       |
| **WUP**  | Wake-Up Time | Sets the time at which the Nixie tubes automatically switch back on.                                                                                                                   |
| **LZO**  | Leading Zero | Enables or disables the leading zero for single-digit hours.                                                                                                                           |
| **REF**  | Tube Refresh | Sets the interval of the automatic tube refresh routine used to help reduce cathode poisoning.                                                                                         |
| **HRS**  | Hour Format  | Selects the 12-hour or 24-hour time display format used in normal Clock mode.                                                                                                          |

## Leading Zero

The **LZO** setting controls whether a leading zero is displayed for single-digit hours.

```text
LZO ON   08:35
LZO OFF   8:35
```

## Tube Refresh

The **REF** setting controls how often the automatic Tube Refresh routine is performed.

During the refresh sequence, the clock cycles through the Nixie tube digits to help reduce cathode poisoning.

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
| **12**  | 12-hour format |
| **24**  | 24-hour format |

This setting affects only the way the current time is displayed in **CLK** mode.

## 24-Hour Time Setup

All time-related settings are entered using the **24-hour format**, regardless of the selected **HRS** display mode.

This applies to:

* **SET** — current time
* **SLP** — sleep time
* **WUP** — wake-up time

Using 24-hour time during setup eliminates the need for an additional AM/PM setting and makes the adjustment process faster.

For example:

```text
6:30 AM  →  06:30
6:30 PM  →  18:30
```

Even when **HRS** is set to **12H**, SET, SLP and WUP are still adjusted using 24-hour time.

In the United States, this format is also commonly referred to as **military time**.

## Rear Panel

The rear panel contains:

* **12 VDC / 1 A power input**
* **Main power switch**
* **Status LED**

The main power switch is located directly next to the power input connector and completely switches the clock on or off.

The rear status LED indicates that the clock is powered. In **IDL** mode, it slowly pulses while the Nixie tubes remain switched off.


![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-01.webp)
![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-02.webp)
![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-03.webp)
![BLOCK - IN-12 Nixie Tube Clock](https://github.com/marcinsaj/block-in-12-nixie-tube-clock/blob/main/extras/block-in-12-nixie-tube-clock-04.webp)

