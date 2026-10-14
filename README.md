[![Build Arduino Sketch](https://github.com/ThePylaa/wiener_linien_CYD_monitor/actions/workflows/main.yml/badge.svg)](https://github.com/ThePylaa/wiener_linien_CYD_monitor/actions/workflows/main.yml)
# Wiener Linien ESP32 Public Transport Departure Monitor (Cheap Yellow Display / CYD)

> **Credit:** This project is based on the original **[Wiener Linien ESP32 Monitor](https://github.com/coppermilk/wiener_linien_esp32_monitor)** by **[coppermilk](https://github.com/coppermilk)**, built for the LILYGO T-Display-S3. Thank you for the idea and the groundwork! ❤️
>
> This fork ports it to the cheap, widely available ESP32-2432S028R board and adds multi-stop support, touch switching, battery saving and a web setup app with a stop and line browser.

**Brief description:** A real-time public transport departure monitor for the **ESP32-2432S028R "Cheap Yellow Display" (CYD)**. It shows a live countdown to the next Wiener Linien departures and lets you save **up to 20 stops** and tap the touchscreen to switch between them. Based on open data from the City of Vienna provided by Wiener Linien. (Data source: City of Vienna - https://data.wien.gv.at)

![Line notification](img/notification2.gif)

## Project components:
- 1x **ESP32-2432S028R "Cheap Yellow Display"** — a 2.8" 320×240 ILI9341 display with XPT2046 resistive touch on board. (Search "ESP32-2432S028R" or "CYD" on AliExpress.)
- 1x USB-C (or micro-USB, depending on board revision) cable.
- Optional: a 3D-printed stand / case.

## Key Features
- **Real-time Countdown**: Stay informed about the next public transport departure times.
- **Easy phone setup**: Scan the QR code on the display and **search stops by name** (e.g. "Stephansplatz") — no RBL numbers needed. Pick the directions you want from the live departures, and the display switches to the new stop right away.
- **Multiple saved stops**: Save up to **20 stops** and switch between them by tapping the screen. A stop can combine several platforms (e.g. both directions of a line).
- **Touch switching**: Tap the **right half** of the screen for the next stop, the **left half** for the previous one.
- **Up to 4 departure rows** on the larger CYD screen.
- **Battery friendly**: the backlight dims after a short idle time and the screen turns off after a minute (both adjustable). While it is off, Wi-Fi is powered down and the ESP32 goes into light sleep until you touch the screen.
- **Timely Updates**: Data is automatically refreshed (~every 20 seconds); switching stops refetches immediately.
- **Data Source**: Utilizes open data from the City of Vienna provided by Wiener Linien.

## Installation
To set up the monitor on an ESP32-2432S028R, follow these steps:

1. **Repository Download**:
   - Clone or download this repository to your local machine.

2. **Board configuration**:
   - In Arduino IDE, open *Tools → Board → Boards Manager*.
   - Install **esp32** by Espressif Systems, version **3.x**. (Note: this downloads a few 100 MB.)
   - Open *Tools → Board → ESP32 Arduino* and pick **ESP32 Dev Module** (the CYD is a plain ESP32-WROOM board).
   - **Important:** set *Tools → Partition Scheme* to **"Huge APP (3MB No OTA/1MB SPIFFS)"**. The sketch does not fit the default 1.2 MB app partition.

3. **Library Installation**:
   - In the Arduino IDE Library Manager, install: **TFT_eSPI**, **ArduinoJson** (version 7), **WiFiManager** (by tzapu), and **XPT2046_Touchscreen** (by Paul Stoffregen).

4. **Display configuration (important!)**:
   - TFT_eSPI is configured at compile time. Copy the [`User_Setup.h`](User_Setup.h) from this repository into your TFT_eSPI library folder, replacing the library's own file:
     - `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`
   - This file selects the ILI9341 driver, the CYD's display pins, and the backlight pin. If the colours look inverted on your board, swap `ILI9341_2_DRIVER` for `ILI9341_DRIVER` inside it.

5. **Device Connection & Flash**:
   - Connect the board over USB, select the correct COM port, open [`sketch/sketch.ino`](sketch/sketch.ino), and upload.

## Setup
Setup has two short steps and is done entirely from your phone.

**1. Wi-Fi** (first boot, or after a Wi-Fi reset)
- The display shows a QR code. Scan it (or join the Wi-Fi **"Wien Transport"** manually).
- Your phone opens the Wi-Fi page. Tap **Configure WiFi**, pick your home network from the list and enter the password. If the page doesn't open by itself, go to `http://192.168.4.1`.

**2. Stops**
- Once connected, the display shows a second QR code with its address on your home network. Scan it with a phone **on the same Wi-Fi**, or open `http://wien-monitor.local` (or the IP address shown).
- Find your stop one of three ways — no searching required:
  - **Search**: type a stop or line, e.g. `Stephansplatz` or `13A`. Results show every line serving each station.
  - **All lines**: every U-Bahn, tram, bus and night bus line. Tap one to see its full route as a line diagram (with transfers), switch direction, and tap a stop on it.
  - **A–Z**: every station in Vienna, alphabetically.
- You see every direction with its live departures and any service notices — including directions that have no departure right now (taken from the timetable). Tick the ones you want (e.g. *U1 → Leopoldau*), optionally rename the stop, choose how many rows to show, and tap **Add stop**. The display switches to it immediately.

The setup page also lets you rename, reorder, delete and edit stops, pick which stop is shown, adjust **brightness**, **dim** and **screen-off** timers, update the stop list and change the Wi-Fi network. If you already know a stop's RBL number, use **"Add it manually"** (RBL numbers can be found [here](https://till.mabe.at/rbl/)).

The stop search and line browser use the Wiener Linien open data (stop list and line routes). The display downloads it once (about 200 KB after trimming) and refreshes it monthly; your phone caches it, so searching and browsing work instantly. The *Your stops* list also shows the latest departures and disruptions for each stop.

## Using the display
- Tap the **right half** → next saved stop.
- Tap the **left half** → previous saved stop.
- **Hold a finger on the screen for about a second** → setup screen with the QR code for the setup page. Tap the screen to close it, or **Change Wi-Fi** to open the Wi-Fi hotspot again.

The header shows the stop name and the time. Switching is disabled if only one stop is configured.

## Battery saving
- The backlight dims after 20 seconds without touch and turns off after 60 seconds. You can change both on the setup page.
- While the screen is off, the display panel sleeps, Wi-Fi is switched off and the ESP32 goes into light sleep. Touching the screen wakes it within a moment. It shows the last known departures (adjusted for the time passed) while fresh data loads.
- The first touch only wakes or brightens the screen; it never switches stops.
- While the setup page is open on your phone, the display stays on.

## Serial diagnostics
Open the serial monitor at **115200 baud** to see timestamped status logs. The sketch now reports the main lifecycle events, selected profiles, API fetch status, parser results, filter misses, resets, and periodic heap/Wi-Fi status.

For extra verbose dumps, uncomment `DEBUG_SERIAL_WIEN_MONITOR` near the top of [`sketch/sketch.ino`](sketch/sketch.ino).

## Resetting
On the CYD the reset functions use the on-board **BOOT button (GPIO0)** — the small button next to the USB port. (Note: a quick press is *not* used for resetting, so it won't interfere; stop switching is done via the touchscreen.) In case you need to reset your device, we offer two options:

- **Factory Reset**:
  - Perform a factory reset by holding the BOOT button for more than 30 seconds. This will erase all data on your device, including Wi-Fi settings, all saved stops and display settings.

- **Soft Reset**:
  - A soft reset involves holding the BOOT button for 5 to 30 seconds and then releasing it. This will only erase your Wi-Fi settings.

### When to Use Each Reset?

**Use a Factory Reset if:**
- You plan to give away your device.
- Your device is malfunctioning, and other troubleshooting steps have failed.

**Use a Soft Reset if:**
- You encounter Wi-Fi connectivity issues.
- You want to restore your Wi-Fi settings to their default values.

With this Public Transport Departure Monitor, we aim to make your daily commute more predictable and efficient, reducing the time spent waiting for public transport. Stay up-to-date with real-time departure information and plan your journeys with confidence.

![Vienna Liner Monitor](img/monitor.jpeg)


