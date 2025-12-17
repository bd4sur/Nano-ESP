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




#define TOTAL_BYTES   160
#define CHUNK1_SIZE   128
#define CHUNK2_SIZE   32

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



int32_t worker(uint8_t worker_i2c_addr, float *x, uint32_t layer_from, uint32_t layer_to, uint32_t pos, uint32_t max_seq_len, uint32_t n_embd) {
    // 准备数据帧

    txBuffer[0] = 0;

    memcpy(&txBuffer[1], &layer_from, sizeof(uint32_t));
    memcpy(&txBuffer[5], &layer_to, sizeof(uint32_t));
    memcpy(&txBuffer[9], &pos, sizeof(uint32_t));
    memcpy(&txBuffer[13], &max_seq_len, sizeof(uint32_t));
    memcpy(&txBuffer[17], &n_embd, sizeof(uint32_t));

/*
    Serial.print("发送的x：");
    for (uint32_t i = 0; i < n_embd; i++) {
        Serial.print(x[i]);
        Serial.print(" ");
    }
    Serial.print("\n");
*/

    memcpy(&txBuffer[21], x, n_embd * sizeof(float));

    // Step 1: 分两包发送 150 字节
    // Serial.println("Sending chunk 1...");
    Wire.beginTransmission(worker_i2c_addr);
    Wire.write(txBuffer, CHUNK1_SIZE);
    int err1 = Wire.endTransmission();
    if (err1 != 0) {
        Serial.println("Error sending chunk 1!");
        delay(10);
        return -1;
    }

    delay(10); // 稍等

    // Serial.println("Sending chunk 2...");
    Wire.beginTransmission(worker_i2c_addr);
    Wire.write(txBuffer + CHUNK1_SIZE, CHUNK2_SIZE);
    int err2 = Wire.endTransmission();
    if (err2 != 0) {
        Serial.println("Error sending chunk 2!");
        delay(10);
        return -1;
    }

    // Serial.println("Data sent. Waiting before requesting response...");


    delay(10);




    uint32_t is_req_ready = 0;

    // while (1) {
        Wire.requestFrom(worker_i2c_addr, CHUNK1_SIZE);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
                is_req_ready = 0;
            }
            return -1;
        }
        else if (Wire.available() == CHUNK1_SIZE) {
            Wire.readBytes(rxBuffer, CHUNK1_SIZE);
            is_req_ready = 1;
            // Serial.println("Received Chunk 1.");
        }
        else {
            Serial.printf("Unexpected chunk 1 length: %d\n", Wire.available());
            is_req_ready = 0;
            return -1;
        }
    // }

    delay(10);

    if (is_req_ready) {
        Wire.requestFrom(worker_i2c_addr, CHUNK2_SIZE);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
            }
            return -1;
        }
        else if (Wire.available() == CHUNK2_SIZE) {
            Wire.readBytes(rxBuffer + CHUNK1_SIZE, CHUNK2_SIZE);
            // Serial.println("Received Chunk 2.");

            // 解析数据帧
            // uint8_t op = rxBuffer[0]; // 命令，暂不用
            // uint32_t layer_from;  memcpy(&layer_from,  &rxBuffer[1], 4);  // 开始层序号（0开始）
            // uint32_t layer_to;    memcpy(&layer_to,    &rxBuffer[5], 4);  // 结束层序号（0开始）
            // uint32_t pos;         memcpy(&pos,         &rxBuffer[9], 4);  // 序列位置（0开始）
            // uint32_t max_seq_len; memcpy(&max_seq_len, &rxBuffer[13], 4); // 上下文长度
            // uint32_t n_embd_c;    memcpy(&n_embd_c,    &rxBuffer[17], 4); // 嵌入向量维度（决定了后面数组多长），应等于n_embd

            deserialize_floats(&rxBuffer[21], x, n_embd);

            return 0;
        }
        else {
            Serial.printf("Unexpected chunk 2 length: %d\n", Wire.available());
            return -1;
        }
    }

/*
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
*/
    return -1;

}

}
