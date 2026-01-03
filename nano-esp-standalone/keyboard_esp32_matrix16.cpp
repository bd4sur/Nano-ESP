#include <Arduino.h>
#include <Wire.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "keyboard_hal.h"
#include "platform.h"


int32_t keyboard_hal_init() {
    return 0;
}

uint8_t keyboard_hal_read_key() {
    // 第一步：写入命令 0x03 到设备
    Wire.beginTransmission(KB_I2C_ADDR);
    Wire.write(0x03);
    uint8_t status = Wire.endTransmission();
    if (status != 0) {
        return 'X';  // 通信失败
    }

    delayMicroseconds(1000);  // 等待 1000 微秒（即 1 毫秒）

    // 第二步：从设备读取 1 个字节
    if (Wire.requestFrom(KB_I2C_ADDR, 1) != 1) {
        return 'X';  // 未收到数据
    }

    return Wire.read();  // 成功读取，返回字节值（0~255）
}


#ifdef __cplusplus
}
#endif
