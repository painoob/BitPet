// User_Setup.h para CYD (ESP32-2432S028) - tela ILI9341 2.8" 240x320 + toque XPT2046
// Copie este arquivo para: <Sketchbook>\libraries\TFT_eSPI\User_Setup.h (substituindo o original)
#define USER_SETUP_ID 28

#define ILI9341_2_DRIVER   // variante usada na CYD (se a tela ficar branca/sem imagem, tente ILI9341_DRIVER)

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON   // se as cores ficarem invertidas, o BitPet tambem tem um atalho: aperte BOOT no menu inicial

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define TOUCH_CS 33        // o toque e lido pela biblioteca XPT2046_Touchscreen (SPI proprio)

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
