TFT_eSPI Library Configuration for YamuraPyrometer
====================================================

These files are copies of the modified TFT_eSPI library setup files required
to run the YamuraPyrometer sketch with the Hosyond 4.0" 480x320 ST7796S display.

Installed library location:  E:\Arduino\libraries\TFT_eSPI\
Files modified:              User_Setup.h  (copy User_Setup_Select.h included for reference, unmodified)

To apply these settings to a new machine:
  Copy User_Setup.h to <Arduino sketchbook>\libraries\TFT_eSPI\User_Setup.h
  User_Setup_Select.h should already default to #include <User_Setup.h> (no change needed)


Changes made to User_Setup.h
-----------------------------

1. DRIVER - changed from ILI9486_DRIVER (Teensy 4.1 config) to ST7796_DRIVER
   Was:  #define ILI9486_DRIVER
   Now:  #define ST7796_DRIVER

2. SPI PINS - changed from Teensy 4.1 pins to ESP32 VSPI pins
   Was:  MISO=12, MOSI=11, SCLK=13  (Teensy 4.1)
   Now:  MISO=19, MOSI=23, SCLK=18  (ESP32 hardware SPI / VSPI)
   CS, DC, RST were coincidentally correct (15, 2, 4) and unchanged.

3. TFT_WIDTH / TFT_HEIGHT - commented out
   Was:  #define TFT_WIDTH  480
         #define TFT_HEIGHT 320
   Now:  both commented out
   Reason: these defines are for ST7789/ST7735 only. Leaving them active for
   ST7796 overrides the driver's native portrait dimensions (320x480) and
   causes only ~70% of the display width to be addressable after rotation.
   The ST7796 driver sets its own correct dimensions internally.

4. TFT_BL (backlight pin) - was already commented out, left unchanged.
   Note: GPIO21 is used for I2C SDA in this project - do not assign TFT_BL to 21.

5. SPI_FREQUENCY - already set to 20000000 (20MHz), left unchanged.
   Note: The display works at 20MHz. 40MHz and above causes init failures
   with this display module.


Hardware pin mapping (ESP32 WROOM-32D)
---------------------------------------
Signal    GPIO
------    ----
MISO       19   (shared with SD card)
MOSI       23   (shared with SD card)
SCLK       18   (shared with SD card)
TFT CS     15
TFT DC      2
TFT RST     4
SD  CS      5
I2C SDA    21
I2C SCL    22
