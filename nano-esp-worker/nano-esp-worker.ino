#include <wchar.h>
#include <stdint.h>
#include <esp32-hal-psram.h>
#include <Wire.h>
#include <string.h>

#include "utils.h"
#include "model_psycho_230k_1214_q80.h"
#include "infer.h"
#include "platform.h"

#define SLAVE_ADDRESS 0x20
#define TOTAL_BYTES   150
#define CHUNK1_SIZE   128
#define CHUNK2_SIZE   (TOTAL_BYTES - CHUNK1_SIZE) // 22

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

}


void loop() {
    if (globalState == RECEIVED) {
        // 开始处理
        globalState = PROCESSING;
        Serial.println("Processing data...");


        LLM *llm = g_llm_ctx->llm;
        LoRA *lora = g_llm_ctx->lora;

        LLM_Config *cfg = &llm->config;
        FwdBuffer *s = &llm->state;
        uint32_t n_embd = cfg->n_embd;

        // Transformer层间传递的中间激活值
        float *x = s->x;


        // 解析数据帧
        uint8_t op = buffer[0]; // 命令，暂不用
        uint32_t layer;       memcpy(&layer,       &buffer[1], 4);  // 层序号（0开始）
        uint32_t pos;         memcpy(&pos,         &buffer[5], 4);  // 序列位置（0开始）
        uint32_t max_seq_len; memcpy(&max_seq_len, &buffer[9], 4);  // 上下文长度
        uint32_t n_embd_c;    memcpy(&n_embd_c,    &buffer[13], 4); // 嵌入向量维度（决定了后面数组多长），应等于n_embd

        
        Serial.print("layer: "); Serial.print(layer); Serial.print("\n");
        Serial.print("pos: "); Serial.print(pos); Serial.print("\n");
        Serial.print("max_seq_len: "); Serial.print(max_seq_len); Serial.print("\n");
        Serial.print("n_embd: "); Serial.print(n_embd); Serial.print("\n");
        
        deserialize_floats(&buffer[17], x, n_embd);

        Serial.print("收到的x：");
        for (uint32_t i = 0; i < n_embd; i++) {
            Serial.print(x[i]);
            Serial.print(" ");
        }
        Serial.print("\n");

        // 计算本层输出激活值
        transformer_block_forward(x, layer, pos, max_seq_len, 1, llm, lora);

        // 构建返回数据帧（只修改buffer的第一个字节和数组内容字节，帧头其他字段都不修改，原样返回）
        buffer[0] = 1; // 表示是从机发来的计算结果
        serialize_floats(x, &buffer[17], n_embd);

        Serial.print("返回的x：");
        for (uint32_t i = 0; i < n_embd; i++) {
            Serial.print(x[i]);
            Serial.print(" ");
        }
        Serial.print("\n");

        // delay(1000); // 注意：此处阻塞，但I2C onRequest 仍可响应



        globalState = READY;
        receiveComplete = false;
        computeComplete = true;
        chunkIndex = 0;
        Serial.println("Processing done.");
    }

    delay(10);
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
            // 接收第一包（128字节）
            if (numBytes != CHUNK1_SIZE) {
                Serial.println("Error: First chunk size mismatch!");
                return;
            }
            Wire.readBytes(buffer, CHUNK1_SIZE);
            chunkIndex = 1;
        }
        else if (chunkIndex == 1) {
            // 接收第二包（22字节）
            if (numBytes != CHUNK2_SIZE) {
                Serial.println("Error: Second chunk size mismatch!");
                return;
            }
            Wire.readBytes(buffer + CHUNK1_SIZE, CHUNK2_SIZE);
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
                Wire.write(buffer, CHUNK1_SIZE);
                chunkIndex = 1;
            }
            else if (chunkIndex == 1) {
                Wire.write(buffer + CHUNK1_SIZE, CHUNK2_SIZE);
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
