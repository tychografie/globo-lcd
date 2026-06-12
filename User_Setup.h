// User_Setup.h for LilyGo T-Display-S3 (ST7789 170x320, 8-bit parallel)
#define USER_SETUP_INFO "T-Display-S3"

// Driver
#define ST7789_DRIVER

// Resolution
#define TFT_WIDTH  170
#define TFT_HEIGHT 320

// 8-bit parallel interface
#define TFT_PARALLEL_8_BIT

// Pins for T-Display-S3
#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  5
#define TFT_WR   8
#define TFT_RD   9

#define TFT_D0   39
#define TFT_D1   40
#define TFT_D2   41
#define TFT_D3   42
#define TFT_D4   45
#define TFT_D5   46
#define TFT_D6   47
#define TFT_D7   48

#define TFT_BL   38
#define TFT_BACKLIGHT_ON HIGH

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// SPI (not used but required by lib)
#define SPI_FREQUENCY  40000000

// Use INIT_SEQUENCE_3 for T-Display-S3
#define INIT_SEQUENCE_3
