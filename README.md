# Nano-ESP

## 与主线的差异

- ui.h: INPUT_BUFFER_LENGTH OUTPUT_BUFFER_LENGTH
- ui.c: #include "arduino_wrapper.h"
- tokenizer.h: MAX_PROMPT_BUFFER_LENGTH
- platform.h: #define NANO_ESP32_S3
- infer.c: ps_calloc #include "arduino_wrapper.h"
- Nano-ESP.ino vs main.c

## ESP32-S3

注意：`ui.c`中的`time()`似乎不支持。

- 开发板：ESP32S3 Dev Module Octal (WROOM2)
- USB CDC On Boot: "Disabled"
- CPU Frequency: "240MHz (WiFi)"
- Core Debug Level: "None"
- USB DFU On Boot: "Disabled'
- Erase All Flash Before Sketch Upload: "Enabled"
- Events Run On: "Core 1"
- Flash Mode: "OPI 80MHz"
- Flash Size: "32MB (256Mb)"
- JTAG Adapter: "Disabled"
- Arduino Runs On: "Core 1"
- USB Firmware MSC On Boot: "Disabled"
- Partition Scheme: "32M Flash (13MB APP/6.75MB SPIFFS)
- PSRAM: "OPI PSRAM"
- Upload Mode: "USB-OTG CDC (TinyUSB)"
- Upload Speed: "921600"
- USB Mode: "Hardware CDC and JTAG"

## ESP32-P4

- 开发板：ESP32P4 Dev Module
- USB CDC On Boot: "Disabled'
- CPU Frequency: "360MHz"
- Core Debug Level: "None"
- USB DFU On Boot: "Disabled'
- Erase All Flash Before Sketch Upload: "Enabled'
- Flash Frequency: "80MHz'
- Flash Mode: "QIO"
- Flash Size: "32MB (256Mb)"
- JTAG Adapter: "Disabled'
- USB Firmware MSC On Boot: "Disabled'
- Partition Scheme: "32M Flash (13MB APP/6.75MB SPIFFS)"
- PSRAM:"Enabled"
- Upload Mode: "USB-OTG CDC (TinyUSB)"
- Upload Speed: "921600"
- USB Mode: "USB-OTG (TinyUSB)"
