// wrapper.cpp
#include <Arduino.h>
#include <Wire.h>
#include <esp32-hal-psram.h>

extern "C" {

void print_str(char* msg) {
    Serial.print(msg);
}
void print_num(int i) {
    Serial.print(i);
}
void print_float(float i) {
    Serial.print(i);
}
void *psram_calloc(size_t n, size_t sizeoftype) {
    return ps_calloc(n, sizeoftype);
}



#define SLAVE_ADDRESS 0x20
#define TOTAL_BYTES   150
#define CHUNK1_SIZE   128
#define CHUNK2_SIZE   22

static uint8_t txBuffer[TOTAL_BYTES];
static uint8_t rxBuffer[TOTAL_BYTES];

// 序列化：float[n_float] -> uint8_t[n_float*4]
void serialize_floats(float* input, uint8_t* output, uint32_t n_float) {
    memcpy(output, input, n_float * sizeof(float));
}

// 反序列化：uint8_t[n_float*4] -> float[n_float]
void deserialize_floats(uint8_t* input, float* output, uint32_t n_float) {
    memcpy(output, input, n_float * sizeof(float));
}



void worker(float *x, uint32_t layer, uint32_t pos, uint32_t max_seq_len, uint32_t n_embd) {
    // 准备数据帧

    txBuffer[0] = 0;

    memcpy(&txBuffer[1], &layer, sizeof(uint32_t));
    memcpy(&txBuffer[5], &pos, sizeof(uint32_t));
    memcpy(&txBuffer[9], &max_seq_len, sizeof(uint32_t));
    memcpy(&txBuffer[13], &n_embd, sizeof(uint32_t));


    Serial.print("发送的x：");
    for (uint32_t i = 0; i < n_embd; i++) {
        // x[i] = 2.0 * (float)random(0, 32768) / 32768.0 - 1.0;
        Serial.print(x[i]);
        Serial.print(" ");
    }
    Serial.print("\n");

    memcpy(&txBuffer[17], x, n_embd * sizeof(float));

    // Step 1: 分两包发送 150 字节
    Serial.println("Sending chunk 1 (128 bytes)...");
    Wire.beginTransmission(SLAVE_ADDRESS);
    Wire.write(txBuffer, CHUNK1_SIZE);
    int err1 = Wire.endTransmission();
    if (err1 != 0) {
        Serial.println("Error sending chunk 1!");
        delay(1000);
        return;
    }

    delay(10); // 稍等

    Serial.println("Sending chunk 2 (22 bytes)...");
    Wire.beginTransmission(SLAVE_ADDRESS);
    Wire.write(txBuffer + CHUNK1_SIZE, CHUNK2_SIZE);
    int err2 = Wire.endTransmission();
    if (err2 != 0) {
        Serial.println("Error sending chunk 2!");
        delay(1000);
        return;
    }

    Serial.println("Data sent. Waiting before requesting response...");


    delay(100);






    while (1) {
        Wire.requestFrom(SLAVE_ADDRESS, CHUNK1_SIZE);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
                delay(100);
                continue;
            }
        }
        else if (Wire.available() == CHUNK1_SIZE) {
            Wire.readBytes(rxBuffer, CHUNK1_SIZE);
            Serial.println("Received Chunk 1.");
            break;
        }
        else {
            Serial.printf("Unexpected chunk 1 length: %d\n", Wire.available());
            break;
        }
    }
    while (1) {
        Wire.requestFrom(SLAVE_ADDRESS, CHUNK2_SIZE);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
                delay(100);
                continue;
            }
        }
        else if (Wire.available() == CHUNK2_SIZE) {
            Wire.readBytes(rxBuffer + CHUNK1_SIZE, CHUNK2_SIZE);
            Serial.println("Received Chunk 2.");
            break;
        }
        else {
            Serial.printf("Unexpected chunk 2 length: %d\n", Wire.available());
            break;
        }
    }


    // 解析数据帧
    // uint8_t op = rxBuffer[0]; // 命令，暂不用
    // uint32_t layer;       memcpy(&layer,       &rxBuffer[1], 4);  // 层序号（0开始）
    // uint32_t pos;         memcpy(&pos,         &rxBuffer[5], 4);  // 序列位置（0开始）
    // uint32_t max_seq_len; memcpy(&max_seq_len, &rxBuffer[9], 4);  // 上下文长度
    // uint32_t n_embd_c;    memcpy(&n_embd_c,    &rxBuffer[13], 4); // 嵌入向量维度（决定了后面数组多长），应等于n_embd

    deserialize_floats(&rxBuffer[17], x, n_embd);

    Serial.print("返回的x：");
    for (uint32_t i = 0; i < n_embd; i++) {
        Serial.print(x[i]);
        Serial.print(" ");
    }
    Serial.print("\n");

    Serial.print("返回的bytes：");
    for (uint32_t i = 0; i < TOTAL_BYTES; i++) {
        Serial.print(rxBuffer[i]);
        Serial.print(" ");
    }
    Serial.print("\n");
}

}
