//                            USER DEFINED SETTINGS
//   CYD (ESP32-2432S028) Specific Setup for TFT_eSPI

#define USER_SETUP_INFO "CYD_ESP32_2432S028"

// Driver correto para a CYD (ILI9341)
#define ILI9341_DRIVER

// Correção da inversão de cores (caso o verde/vermelho fiquem trocados)
#define TFT_RGB_ORDER TFT_RGB  // ou TFT_RGB se necessário

// ##################################################################################
// Section 2. Define the pins for ESP32 on CYD
// ##################################################################################

#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18

#define TFT_CS   15  // Chip select control pin
#define TFT_DC    2  // Data Command control pin
#define TFT_RST   4  // Reset pin (could connect to RST pin)

#define TFT_BL   21  // LED back-light control pin for CYD
#define TFT_BACKLIGHT_ON HIGH  

#define TOUCH_CS 33     // Chip select pin (T_CS) of touch screen XPT2046

// ##################################################################################
// Section 3. Define the fonts that are to be used here
// ##################################################################################

#define LOAD_GLCD   
#define LOAD_FONT2  
#define LOAD_FONT4  
#define LOAD_FONT6  
#define LOAD_FONT7  
#define LOAD_FONT8  
#define LOAD_GFXFF  

#define SMOOTH_FONT

// ##################################################################################
// Section 4. Other options
// ##################################################################################

#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000

// Frequência correta para o XPT2046 da CYD
#define SPI_TOUCH_FREQUENCY  2500000