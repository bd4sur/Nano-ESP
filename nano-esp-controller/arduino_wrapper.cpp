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



#define CHUNK_BYTES   (128)
#define TOTAL_BYTES   (CHUNK_BYTES * 2)


static uint8_t txBuffer[TOTAL_BYTES];
static uint8_t rxBuffer[TOTAL_BYTES];

static uint32_t compute_time_ms = 20;

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

    uint32_t op = 0;
    uint32_t res_1 = 0;
    uint32_t delay_ms = 0;

    memcpy(&txBuffer[0], &op, sizeof(uint32_t));
    memcpy(&txBuffer[4], &layer_from, sizeof(uint32_t));
    memcpy(&txBuffer[8], &layer_to, sizeof(uint32_t));
    memcpy(&txBuffer[12], &pos, sizeof(uint32_t));
    memcpy(&txBuffer[16], &max_seq_len, sizeof(uint32_t));
    memcpy(&txBuffer[20], &res_1, sizeof(uint32_t));
    memcpy(&txBuffer[24], &delay_ms, sizeof(uint32_t));
    memcpy(&txBuffer[28], &n_embd, sizeof(uint32_t));

    memcpy(&txBuffer[32], x, n_embd * sizeof(float));

    // Step 1: 发送两帧
    // Serial.println("Sending chunk 1...");
    Wire.beginTransmission(worker_i2c_addr);
    Wire.write(txBuffer, CHUNK_BYTES);
    int err1 = Wire.endTransmission();
    if (err1 != 0) {
        Serial.println("Error sending chunk 1!");
        delay(10);
        return -1;
    }

    delay(10); // 稍等

    // Serial.println("Sending chunk 2...");
    Wire.beginTransmission(worker_i2c_addr);
    Wire.write(txBuffer + CHUNK_BYTES, CHUNK_BYTES);
    int err2 = Wire.endTransmission();
    if (err2 != 0) {
        Serial.println("Error sending chunk 2!");
        delay(10);
        return -1;
    }

    // Serial.println("Data sent. Waiting before requesting response...");

    delay(compute_time_ms);




    uint32_t is_req_ready = 0;

    // while (1) {
        Wire.requestFrom(worker_i2c_addr, CHUNK_BYTES);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
                is_req_ready = 0;
            }
            return -1;
        }
        else if (Wire.available() == CHUNK_BYTES) {
            Wire.readBytes(rxBuffer, CHUNK_BYTES);
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
        Wire.requestFrom(worker_i2c_addr, CHUNK_BYTES);
        if (Wire.available() == 1) {
            uint8_t status = Wire.read();
            if (status == 0xFF) {
                Serial.println("Slave reported BUSY (0xFF).");
            }
            return -1;
        }
        else if (Wire.available() == CHUNK_BYTES) {
            Wire.readBytes(rxBuffer + CHUNK_BYTES, CHUNK_BYTES);
            // Serial.println("Received Chunk 2.");

            // 解析数据帧
            memcpy(&compute_time_ms, &rxBuffer[24], 4);
            deserialize_floats(&rxBuffer[32], x, n_embd);

            return 0;
        }
        else {
            Serial.printf("Unexpected chunk 2 length: %d\n", Wire.available());
            return -1;
        }
    }

    return -1;

}

}
