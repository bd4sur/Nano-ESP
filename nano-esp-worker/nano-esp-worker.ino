#include <wchar.h>
#include <stdint.h>
#include <esp32-hal-psram.h>
#include <Wire.h>
#include <string.h>

#include <Adafruit_NeoPixel.h>

#include "utils.h"
#include "model_psycho_230k_1214_q80.h"
#include "infer.h"
#include "platform.h"

#define LED_PIN    48
#define NUMPIXELS  1

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define SLAVE_ADDRESS 0x14

#define CHUNK_BYTES   (128)
#define TOTAL_BYTES   (CHUNK_BYTES * 2)


// 全局状态枚举
enum State {
    READY,
    RECEIVING,
    RECEIVED,
    PROCESSING
};

volatile State globalState = READY;
uint8_t buffer[TOTAL_BYTES];
volatile uint8_t chunkIndex = 0; // 0: first chunk, 1: second chunk
volatile bool receiveComplete = false;
volatile bool computeComplete = false;

// 推理引擎实例（单例模式）
static Nano_Context *g_llm_ctx = NULL;

// 序列化：float[n_float] -> uint8_t[n_float*4]
void serialize_floats(float* input, uint8_t* output, uint32_t n_float) {
    memcpy(output, input, n_float * sizeof(float));
}

// 反序列化：uint8_t[n_float*4] -> float[n_float]
void deserialize_floats(uint8_t* input, float* output, uint32_t n_float) {
    memcpy(output, input, n_float * sizeof(float));
}


void setup() {

    Serial.begin(115200);

    delay(100);

    Wire.begin(SLAVE_ADDRESS);
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    Wire.setClock(400000);

    Serial.println("I2C Slave ready.");

    globalState == PROCESSING;

    g_llm_ctx = llm_context_init_from_buffer((uint8_t *)psycho_230k_1214_q80, 512, 1.0, 1.0, 0.8, 20, micros());

    globalState == READY;
    computeComplete = false;

    pixels.begin();
    pixels.setBrightness(255);
    pixels.clear();
    pixels.show();
}


void loop() {
    if (globalState == RECEIVED) {
        // 开始处理
        globalState = PROCESSING;
        Serial.println("Processing data...");


        uint64_t t0 = micros();


        LLM *llm = g_llm_ctx->llm;
        LoRA *lora = g_llm_ctx->lora;

        LLM_Config *cfg = &llm->config;
        FwdBuffer *s = &llm->state;
        uint32_t n_embd = cfg->n_embd;

        // Transformer层间传递的中间激活值
        float *x = s->x;


        // 解析数据帧
        uint32_t op;          memcpy(&op,          &buffer[0], 4);  // 命令，暂不用
        uint32_t layer_from;  memcpy(&layer_from,  &buffer[4], 4);  // 开始层序号（0开始）
        uint32_t layer_to;    memcpy(&layer_to,    &buffer[8], 4);  // 结束层序号（0开始）
        uint32_t pos;         memcpy(&pos,         &buffer[12], 4);  // 序列位置（0开始）
        uint32_t max_seq_len; memcpy(&max_seq_len, &buffer[16], 4); // 上下文长度
        uint32_t res_1;       memcpy(&res_1,       &buffer[20], 4); // 保留字段
        uint32_t delay_ms;    memcpy(&delay_ms,    &buffer[24], 4); // 计算耗时ms
        uint32_t n_embd_c;    memcpy(&n_embd_c,    &buffer[28], 4); // 嵌入向量维度（决定了后面数组多长），应等于n_embd

        deserialize_floats(&buffer[32], x, n_embd);

        pixels.fill(pixels.Color(0, 255, 255));
        pixels.show();

        // 计算各层输出激活值
        for (uint32_t l = layer_from; l <= layer_to; l++) {
            transformer_block_forward(x, l, pos, max_seq_len, 1, llm, lora);
        }

        pixels.clear();
        pixels.show();

        uint64_t t1 = micros();
        delay_ms = (uint32_t)((t1 - t0) / 1000) + 3;
        Serial.println(delay_ms);

        // 构建返回数据帧
        op = 1;
        memcpy(&buffer[0], &op, sizeof(uint32_t));
        memcpy(&buffer[24], &delay_ms, sizeof(uint32_t));
        serialize_floats(x, &buffer[32], n_embd);


        globalState = READY;
        receiveComplete = false;
        computeComplete = true;
        chunkIndex = 0;
        Serial.println("Processing done.");
    }

}

void onReceive(int numBytes) {
    if (globalState == PROCESSING) {
        // 忽略新数据（主机可能还在发，但此处无法阻止）
        // 实际上主机应先轮询状态，但本设计靠 onRequest 返回 0xFF 告知忙
        return;
    }

    if (globalState == READY || globalState == RECEIVING) {
        globalState = RECEIVING;

        if (chunkIndex == 0) {
            // 接收第一包
            if (numBytes != CHUNK_BYTES) {
                Serial.println("Error: First chunk size mismatch!");
                return;
            }
            Wire.readBytes(buffer, CHUNK_BYTES);
            chunkIndex = 1;
        }
        else if (chunkIndex == 1) {
            // 接收第二包
            if (numBytes != CHUNK_BYTES) {
                Serial.println("Error: Second chunk size mismatch!");
                return;
            }
            Wire.readBytes(buffer + CHUNK_BYTES, CHUNK_BYTES);
            receiveComplete = true;
            globalState = RECEIVED;
            Serial.println("Full message received.");
        }
    }
}

void onRequest() {
    if (globalState == PROCESSING) {
        // 返回 0xFF 表示忙
        Wire.write((uint8_t)0xFF);
    }
    else if (globalState == READY) {
        if (computeComplete) {
            if (chunkIndex == 0) {
                Wire.write(buffer, CHUNK_BYTES);
                chunkIndex = 1;
            }
            else if (chunkIndex == 1) {
                Wire.write(buffer + CHUNK_BYTES, CHUNK_BYTES);
                chunkIndex = 0;
                computeComplete = false;
            }
        }
    }
    else {
        // 其他状态（如 RECEIVING/RECEIVED）不应被请求，但安全起见返回忙
        Wire.write((uint8_t)0xFF);
    }
}
