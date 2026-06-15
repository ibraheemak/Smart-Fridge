// TFT_eSPI configuration for SmartFridge Display ESP32 devkit + ILI9488.
// Picked up automatically by TFT_eSPI instead of the library's User_Setup.h.

#define USER_SETUP_INFO "SmartFridge_Display"

#define ILI9488_DRIVER

// VSPI hardware pins (standard ESP32 devkit)
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_MISO  19   // connected to XPT2046 T_DO only — do NOT connect ILI9488 SDO
#define TFT_CS    26   // moved off GND to a free GPIO so TFT_eSPI can
                        // deselect the TFT while reading the XPT2046 touch chip
#define TFT_DC    27
#define TFT_RST    4   // needs a reset pulse on boot — do NOT tie to 3V3

// XPT2046 touch — shares VSPI bus
#define TOUCH_CS   5

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY        10000000
#define SPI_READ_FREQUENCY   10000000
#define SPI_TOUCH_FREQUENCY    500000
