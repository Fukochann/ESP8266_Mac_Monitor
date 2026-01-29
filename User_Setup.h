//  User defined information reported by "Read_User_Setup" test & diagnostics example
#define USER_SETUP_INFO "User_Setup_GMTI130_Relay_Fixed"

// ##################################################################################
//
// Section 1. Call up the right driver file and any options for it
//
// ##################################################################################

// Define ST7789 driver
#define ST7789_DRIVER      // Full configuration option
#define ST7789_240x240     // 关键：指定ST7789为240*240分辨率

// Define color order
#define TFT_RGB_ORDER TFT_BGR  // 关键：IPS屏默认BGR顺序

// Define width and height
#define TFT_WIDTH  240 
#define TFT_HEIGHT 240 

// Color inversion
#define TFT_INVERSION_OFF  // 关键：IPS屏默认关闭颜色反转

// ##################################################################################
//
// Section 2. Define the pins that are used to interface with the display here
//
// ##################################################################################

// ------------------- 背光设置 (移至 D6) -------------------
// // 原 D1 被 RST 占用，原 D6 空闲 (且支持PWM)，所以背光移到 D6
// #define TFT_BL   PIN_D6            // 关键：背光引脚接 D6 (GPIO12)
// #define TFT_BACKLIGHT_ON HIGH      // 关键：高电平打开背光

// // ------------------- SPI 引脚设置 -------------------
// 这里的设置是为了避开继电器占用的 D3 和 D4

#define TFT_MISO  -1      // 关键：设为 -1。因为 D6 现在用于背光，且屏幕不需要读取数据
#define TFT_MOSI  PIN_D7  // 关键：MOSI 固定接 D7 (GPIO13)
#define TFT_SCLK  PIN_D5  // 关键：SCLK 固定接 D5 (GPIO14)

#define TFT_CS    -1      // 关键：CS 接地，设为 -1 禁用片选

// ------------------- 控制引脚 (避让继电器) -------------------
// 继电器1 占用了 D3
// 继电器2 占用了 D4
// 所以我们将 DC 和 RST 移走：

#define TFT_DC    PIN_D2  // 关键：DC 接 D2 (GPIO4) [原D3被继电器占用]
#define TFT_RST   PIN_D1  // 关键：RST 接 D1 (GPIO5) [原D4被继电器占用]

// ##################################################################################
//
// Section 3. Define the fonts that are to be used here
//
// ##################################################################################

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts
#define SMOOTH_FONT

// ##################################################################################
//
// Section 4. Other options
//
// ##################################################################################

#define SPI_FREQUENCY  27000000   // 关键：ST7789稳定频率
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000