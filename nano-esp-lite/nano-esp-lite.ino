#include <wchar.h>
#include <stdint.h>
#include <esp32-hal-psram.h>
#include <Wire.h>

#include "model_psycho_230k_1214_q80.h"

#include "graphics.h"
#include "ui.h"
#include "infer.h"
#include "platform.h"

#define LLM_OUT_LENGTH (16)

wchar_t last_llm_out[LLM_OUT_LENGTH+1] = L"人类的本质是";

///////////////////////////////////////
// 全局GUI组件对象

Global_State           *global_state  = {0};
Key_Event              *key_event = {0};
Widget_Textarea_State  *w_textarea_main = {0};
Widget_Textarea_State  *w_textarea_prefill = {0};




int32_t on_llm_prefilling(Nano_Session *session) {

    if (session->t_0 == 0) {
        session->t_0 = global_state->timestamp;
    }
    else {
        session->tps = (session->pos - 1) / (float)(global_state->timestamp - session->t_0) * 1000;
    }

    // 屏幕刷新节流
    if (global_state->timestamp - global_state->llm_refresh_timestamp > (1000 / global_state->llm_refresh_max_fps)) {

        w_textarea_prefill->x = 0;
        w_textarea_prefill->y = 0;
        w_textarea_prefill->width = 128;
        w_textarea_prefill->height = 24;

        set_textarea(key_event, global_state, w_textarea_prefill, L"Pre-filling...", 0, 0);
    
        // 临时关闭draw_textarea的整帧绘制，以便在textarea上绘制进度条之后再统一写入屏幕，否则反复的clear会导致进度条闪烁。
        global_state->is_full_refresh = 0;

        fb_soft_clear();

        draw_textarea(key_event, global_state, w_textarea_prefill);

        fb_draw_line(0, 60, 128, 60, 1);
        fb_draw_line(0, 63, 128, 63, 1);
        fb_draw_line(127, 60, 127, 63, 1);
        fb_draw_line(0, 61, session->pos * 128 / (session->num_prompt_tokens - 2), 61, 1);
        fb_draw_line(0, 62, session->pos * 128 / (session->num_prompt_tokens - 2), 62, 1);

        gfx_refresh();

        // 重新开启整帧绘制，注意这个标记是所有函数共享的全局标记。
        global_state->is_full_refresh = 1;

        global_state->llm_refresh_timestamp = global_state->timestamp;
    }

    return LLM_RUNNING_IN_PREFILLING;
}

int32_t on_llm_decoding(Nano_Session *session) {

    if (session->t_0 == 0) {
        session->t_0 = global_state->timestamp;
    }
    else {
        session->tps = (session->pos - 1) / (float)(global_state->timestamp - session->t_0) * 1000;
    }

    // 屏幕刷新节流
    if (global_state->timestamp - global_state->llm_refresh_timestamp > (1000 / global_state->llm_refresh_max_fps)) {
        wchar_t tps_wcstr[50];
        swprintf(tps_wcstr, 50, L"\n[%d|%.1fTPS]", session->pos, session->tps);
        wcscat(session->output_text, tps_wcstr);

        set_textarea(key_event, global_state, w_textarea_main, session->output_text, -1, 1);
        draw_textarea(key_event, global_state, w_textarea_main);
        global_state->llm_refresh_timestamp = global_state->timestamp;
    }

    return LLM_RUNNING_IN_DECODING;
}

int32_t on_llm_finished(Nano_Session *session) {

    session->t_1 = global_state->timestamp;
    session->tps = (session->pos - 1) / (float)(session->t_1 - session->t_0) * 1000;

    int32_t output_len = wcslen(session->output_text);
    if (output_len < 50 + LLM_OUT_LENGTH) {
        wcscpy(last_llm_out, L"人类的本质是");
    }
    else {
        for (int32_t i = 0; i < LLM_OUT_LENGTH; i++) {
            last_llm_out[i] = session->output_text[output_len - 50 - LLM_OUT_LENGTH + i]; // 去掉后面附加的TPS信息
        }
        last_llm_out[LLM_OUT_LENGTH] = 0;
    }

    return LLM_STOPPED_NORMALLY;
}







void setup() {

    Serial.begin(115200);

    delay(100);

    Wire.begin();
    Wire.setClock(400000);

    ///////////////////////////////////////
    // 初始化GUI状态

    global_state = (Global_State*)calloc(1, sizeof(Global_State));
    key_event = (Key_Event*)calloc(1, sizeof(Key_Event));

    w_textarea_main = (Widget_Textarea_State*)calloc(1, sizeof(Widget_Textarea_State));
    w_textarea_prefill = (Widget_Textarea_State*)calloc(1, sizeof(Widget_Textarea_State));

    global_state->is_thinking_enabled = 1;
    global_state->is_full_refresh = 1;
    global_state->llm_refresh_max_fps = 10;
    global_state->llm_refresh_timestamp = 0;

    init_textarea(key_event, global_state, w_textarea_main, INPUT_BUFFER_LENGTH);
    init_textarea(key_event, global_state, w_textarea_prefill, INPUT_BUFFER_LENGTH);


    ///////////////////////////////////////
    // OLED 初始化

    gfx_init();

    show_splash_screen(key_event, global_state);

    delay(1000);

    ///////////////////////////////////////
    // LLM初始化

    if (global_state->llm_ctx) {
        llm_context_free(global_state->llm_ctx);
    }

    set_textarea(key_event, global_state, w_textarea_main, L" 正在加载语言模型\n Psycho-230k\n 请稍等...", 0, 0);
    draw_textarea(key_event, global_state, w_textarea_main);

    global_state->llm_lora_path = NULL;
    global_state->llm_repetition_penalty = 1.0f;
    global_state->llm_temperature = 1.0f;
    global_state->llm_top_p = 0.8f;
    global_state->llm_top_k = 20;
    global_state->llm_max_seq_len = 512;

    global_state->llm_ctx = llm_context_init_from_buffer(
        (uint8_t *)psycho_230k_1214_q80,
        global_state->llm_max_seq_len,
        global_state->llm_repetition_penalty,
        global_state->llm_temperature,
        global_state->llm_top_p,
        global_state->llm_top_k,
        global_state->timestamp);

    ///////////////////////////////////////
    // Session初始化

    wchar_t *prompt = (wchar_t*)calloc_dev(global_state->llm_max_seq_len + 1, sizeof(wchar_t));

    while (1) {
        wcscpy(prompt, last_llm_out);
        generate_sync(global_state->llm_ctx, prompt, global_state->llm_max_seq_len, on_llm_prefilling, on_llm_decoding, on_llm_finished);
    }

    llm_context_free(global_state->llm_ctx);

    free(global_state);
    free(key_event);

    free(w_textarea_main);
    free(w_textarea_prefill);

    gfx_close();

}


void loop() {

}
