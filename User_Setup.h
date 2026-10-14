// =============================================================================
// TFT_eSPI configuration for the ESP32-2432S028R "Cheap Yellow Display" (CYD)
// -----------------------------------------------------------------------------
// TFT_eSPI is configured at COMPILE TIME, not from the sketch. Copy this file
// into your TFT_eSPI library folder, replacing the library's own User_Setup.h:
//
//   Arduino IDE: Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//
// (Make sure User_Setup_Select.h still has `#include <User_Setup.h>` enabled,
//  which is the library default.)
//
// The 2.8" panel on the CYD is an ILI9341. The XPT2046 touch controller is
// driven separately by the sketch on its own SPI bus, so it is intentionally
// NOT configured here.
// =============================================================================

#define USER_SETUP_INFO "ESP32-2432S028R CYD"

// Display driver. If colours look inverted/wrong on your particular CYD batch,
// swap this for `#define ILI9341_DRIVER` instead.
#define ILI9341_2_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Backlight control.
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// Display SPI pins (CYD wiring).
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1  // tied to the ESP32 reset line on this board

// Drive the display on the HSPI port so the VSPI port stays free for the
// XPT2046 touch controller (which the sketch drives on its own SPIClass(VSPI)).
// Without this both peripherals would fight over the same SPI bus.
#define USE_HSPI_PORT

// Fonts used by the sketch (GFXFF = the FreeSansBold GFX fonts).
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// SPI clocks.
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
