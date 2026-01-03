#include <Arduino.h>
#include <Wire.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "display_hal.h"

#define OLED_CMD 0  // 写命令
#define OLED_DATA 1 // 写数据

#define OLED_Command_Mode 0x00
#define OLED_Data_Mode    0x40

// 发送命令
void OLED_WriteData(uint8_t data) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(OLED_Data_Mode);
    Wire.write(data);
    Wire.endTransmission();
}

// 发送数据
void OLED_WriteCommand(uint8_t cmd) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(OLED_Command_Mode);
    Wire.write(cmd);
    Wire.endTransmission();
}

// 更新显存到OLED
void display_hal_refresh(uint8_t **FRAME_BUFFER) {
    for (uint8_t row = 0; row < FB_PAGES; row++) {
        uint8_t col = 0;
        OLED_WriteCommand(0xb0 + row); // 设置行起始地址
        OLED_WriteCommand(0x00 + (col & 0x0F));        // 设置低列起始地址
        OLED_WriteCommand(0x10 + (col & 0x0F)); // 设置高列起始地址

        Wire.beginTransmission(OLED_I2C_ADDR);
        Wire.write(OLED_Data_Mode);
        Wire.write(FRAME_BUFFER[row], 64);
        Wire.endTransmission();

        Wire.beginTransmission(OLED_I2C_ADDR);
        Wire.write(OLED_Data_Mode);
        Wire.write(FRAME_BUFFER[row] + 64, 64);
        Wire.endTransmission();
    }
}

// OLED的初始化
void display_hal_init(void) {

#ifdef SSD1309

    // SSD1309 ///////////////////////////////////////////////////////////////////////////////

    OLED_WriteCommand(0xFD);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0xAE); //--turn off oled panel
    OLED_WriteCommand(0xd5); //--set display clock divide ratio/oscillator frequency
    OLED_WriteCommand(0xf0);
    OLED_WriteCommand(0xA8); //--set multiplex ratio(1 to 64)
    OLED_WriteCommand(0x3f); //--1/64 duty
    OLED_WriteCommand(0xD3); //-set display offset  Shift Mapping RAM Counter (0x00~0x3F)
    OLED_WriteCommand(0x00); //-not offset
    OLED_WriteCommand(0x40); //--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
    OLED_WriteCommand(0xA1); //--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
    OLED_WriteCommand(0xC8); // Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
    OLED_WriteCommand(0xDA); //--set com pins hardware configuration
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); //--set contrast control register
    OLED_WriteCommand(0xFF); // Set SEG Output Current Brightness
    OLED_WriteCommand(0xD9); //--set pre-charge period
    OLED_WriteCommand(0x82); // Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
    OLED_WriteCommand(0xDB); //--set vcomh
    OLED_WriteCommand(0x34); // Set VCOM Deselect Level
    OLED_WriteCommand(0xA4); // Disable Entire Display On (0xa4/0xa5)
    OLED_WriteCommand(0xA6); // Disable Inverse Display On (0xa6/a7)
    OLED_WriteCommand(0x2E); // Stop scroll
    OLED_WriteCommand(0x20); // Set Memory Addressing Mode
    OLED_WriteCommand(0x00); // Set Memory Addressing Mode ab Horizontal addressing mode
    OLED_WriteCommand(0xAF);

#elif defined(SSD1306)

    // SSD1306 ///////////////////////////////////////////////////////////////////////////////

    OLED_WriteCommand(0xAE); // Display OFF
    OLED_WriteCommand(0xD5); // Set Display Clock Divide Ratio / Oscillator Frequency
    OLED_WriteCommand(0x80); // SSD1306 推荐值：0x80 (divide ratio = 1, freq = 8)
    OLED_WriteCommand(0xA8); // Set Multiplex Ratio
    OLED_WriteCommand(0x3F); // 1/64 duty (for 64 height)
    OLED_WriteCommand(0xD3); // Set Display Offset
    OLED_WriteCommand(0x00); // No offset
    OLED_WriteCommand(0x40); // Set Start Line (0x40 = 0)
    OLED_WriteCommand(0x8D); // Charge Pump Setting
    OLED_WriteCommand(0x14); // Enable charge pump (REQUIRED for SSD1306!)
    OLED_WriteCommand(0x20); // Memory Addressing Mode
    OLED_WriteCommand(0x00); // Horizontal Addressing Mode
    OLED_WriteCommand(0xA1); // Segment Re-map: column 127 mapped to SEG0 (正常左右方向)
    OLED_WriteCommand(0xC8); // COM Output Scan Direction: remapped (正常上下方向)
    OLED_WriteCommand(0xDA); // Set COM Pins Hardware Configuration
    OLED_WriteCommand(0x12); // Alternative COM pin configuration, disable COM left/right remap
    OLED_WriteCommand(0x81); // Set Contrast Control
    OLED_WriteCommand(0xCF); // SSD1306 typical value (0x7F~0xFF, 0xCF is bright)
    OLED_WriteCommand(0xD9); // Set Pre-Charge Period
    OLED_WriteCommand(0xF1); // SSD1306 typical: phase1 = 15, phase2 = 1 (0xF1)
    OLED_WriteCommand(0xDB); // Set VCOMH Deselect Level
    OLED_WriteCommand(0x40); // SSD1306 recommended (0x20, 0x30, 0x40 are common)
    OLED_WriteCommand(0xA4); // Entire Display ON (resume to RAM content)
    OLED_WriteCommand(0xA6); // Normal Display (not inverse)
    OLED_WriteCommand(0xAF); // Display ON
    // Page mode
    OLED_WriteCommand(0x20);
    OLED_WriteCommand(0x02);

#endif

}

void display_hal_close(void) {
    return;
}


#ifdef __cplusplus
}
#endif
