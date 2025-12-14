#include <wchar.h>
#include <stdint.h>
#include <esp32-hal-psram.h>

#include <Wire.h>

// #include "model_nano_2m7.h"
#include "model_psycho_230k.h"
#include "model_psycho_150k_q80_gs32.h"
#include "graphics.h"
#include "ui.h"
#include "infer.h"

static Nano_Context *g_llm_ctx;

Global_State           *global_state;
Key_Event              *key_event;
Widget_Textarea_State  *widget_textarea_state;

int32_t on_prefilling(Nano_Session *session) {
    if (session->t_0 == 0) {
        session->t_0 = millis();
    }
    else {
        session->tps = (session->pos - 1) / (double)(millis() - session->t_0) * 1000;
    }

    if (g_llm_ctx->llm->arch == LLM_ARCH_NANO) {
        if (session->pos <= 1) {
            uint32_t new_token[1];
            new_token[0] = session->output_ids[0];
            wchar_t *next_token_wcs = decode_nano(g_llm_ctx->tokenizer, new_token, 1);
            char output[10];
            _wcstombs(output, next_token_wcs, 1);
            Serial.print(output);
        }
        uint32_t new_token[1];
        new_token[0] = session->next_token;
        wchar_t *next_token_wcs = decode_nano(g_llm_ctx->tokenizer, new_token, 1);
        char output[10];
        _wcstombs(output, next_token_wcs, 1);
        Serial.print(output);

        wchar_t line[200];
        char input0[200] = "Pre-filling...";
        _mbstowcs(line, input0, strlen(input0));
        wcscpy(widget_textarea_state->text, line);
        widget_textarea_state->current_line = -1;
        widget_textarea_state->is_show_scroll_bar = 1;
        draw_textarea(key_event, global_state, widget_textarea_state);
    }

    // Serial.print("Pre-filling: ");
    // Serial.println(((float)(session->pos + 1) / (float)session->num_prompt_tokens * 100.0f));
    // Serial.print("P TPS: ");
    // Serial.println(session->tps);

    return LLM_RUNNING_IN_PREFILLING;
}

int32_t on_decoding(Nano_Session *session) {
    if (session->t_0 == 0) {
        session->t_0 = millis();
    }
    else {
        session->tps = (session->pos - 1) / (double)(millis() - session->t_0) * 1000;
    }

    // NOTE Qwen模型有时会输出奇怪的token，也就是把unicode字符从中间切开的不完整token。因此Qwen模型仍然需要直接从vocab中解码出这样的裸字符串并输出。
    if (g_llm_ctx->llm->arch == LLM_ARCH_NANO) {
        uint32_t new_token[1];
        new_token[0] = session->next_token;
        wchar_t *next_token_wcs = decode_nano(g_llm_ctx->tokenizer, new_token, 1);
        // char output[10];
        // _wcstombs(output, next_token_wcs, 1);
        // Serial.print(output);

        // char output[1030];
        // _wcstombs(output, session->output_text, session->pos+1);
        // Serial.print(output);

        wcscpy(widget_textarea_state->text, session->output_text);
        widget_textarea_state->current_line = -1;
        widget_textarea_state->is_show_scroll_bar = 1;
        draw_textarea(key_event, global_state, widget_textarea_state);
    }

    return LLM_RUNNING_IN_DECODING;
}

int32_t on_finished(Nano_Session *session) {
    // printf("[%ls]\n", drop_thinking(session->output_text));
    session->t_1 = millis();
    session->tps = (session->pos - 1) / (double)(session->t_1 - session->t_0) * 1000;

    Serial.print("\nTPS = ");
    Serial.println(session->tps);
    Serial.println("");

    char tps_mbstr[100];
    wchar_t tps_wcstr[50];
    sprintf(tps_mbstr, "\n[平均速度%.1f词元/秒]", session->tps);
    _mbstowcs(tps_wcstr, tps_mbstr, strlen(tps_mbstr));
    wcscat(session->output_text, tps_wcstr);
    wcscpy(widget_textarea_state->text, session->output_text);
    widget_textarea_state->current_line = -1;
    widget_textarea_state->is_show_scroll_bar = 1;
    draw_textarea(key_event, global_state, widget_textarea_state);

    delay(1000);

    return LLM_STOPPED_NORMALLY;
}

void setup() {

    global_state = (Global_State*)calloc(1, sizeof(Global_State));
    key_event = (Key_Event*)calloc(1, sizeof(Key_Event));
    widget_textarea_state = (Widget_Textarea_State*)calloc(1, sizeof(Widget_Textarea_State));

    global_state->is_full_refresh = 1;
    global_state->refresh_ratio = 2; // 默认每2个token刷新一次屏幕

    widget_textarea_state->x = 0;
    widget_textarea_state->y = 0;
    widget_textarea_state->width = 128;
    widget_textarea_state->height = 64;
    widget_textarea_state->line_num = 0;
    widget_textarea_state->current_line = 0;



    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);

    gfx_init();


    // uint8_t *model = (uint8_t*)ps_calloc(20000000, sizeof(uint8_t));

    // for (int i = 10000000; i < 10986052; i++) {
    //     model[i] = psycho_230k[i-10000000];
    // }

    // Serial.println(psycho_230k[0]);
    // Serial.println(model[10000000]);

    // Serial.println(psycho_230k[100000]);
    // Serial.println(model[10100000]);

    // g_llm_ctx = llm_context_init_from_buffer((uint8_t *)psycho_230k, 512, 1.0, 1.0, 0.8, 20, micros());
    g_llm_ctx = llm_context_init_from_buffer((uint8_t *)psycho_150k_q80_gs32, 512, 1.0, 1.0, 0.8, 20, micros());
    // g_llm_ctx = llm_context_init_from_buffer((uint8_t *)nano_2m7, 512, 1.0, 1.0, 0.8, 20, micros());

    Serial.print("NanoLM @ ESP32\nBD4SUR 2025-12\n");

    Serial.println("Loading Psycho-230k LLM ...");

    Serial.print("  g_llm_ctx->max_seq_len = ");
    Serial.println(g_llm_ctx->max_seq_len);

    Serial.print("  block_size = ");
    Serial.println(g_llm_ctx->llm->config.block_size);
    Serial.print("  vocab_size = ");
    Serial.println(g_llm_ctx->llm->config.vocab_size);
    Serial.print("  n_layer = ");
    Serial.println(g_llm_ctx->llm->config.n_layer);
    Serial.print("  n_embd = ");
    Serial.println(g_llm_ctx->llm->config.n_embd);
    Serial.print("  n_head = ");
    Serial.println(g_llm_ctx->llm->config.n_head);
    Serial.print("  n_kv_head = ");
    Serial.println(g_llm_ctx->llm->config.n_kv_head);
    Serial.print("  n_hidden = ");
    Serial.println(g_llm_ctx->llm->config.n_hidden);
    Serial.print("  is_shared_classifier = ");
    Serial.println(g_llm_ctx->llm->config.is_shared_classifier);
    Serial.print("  head_dim = ");
    Serial.println(g_llm_ctx->llm->config.head_dim);
    Serial.print("  llm->arch = ");
    Serial.println(g_llm_ctx->llm->arch);
    Serial.print("  llm->quant_type = ");
    Serial.println(g_llm_ctx->llm->quant_type);

    Serial.println("Psycho-230k LLM loaded.");

    char input[512] = "人类的本质是";
    wchar_t prompt[512];

    uint32_t len = _mbstowcs(prompt, input, strlen(input));

    Serial.print("Prompt = ");
    Serial.println(input);

    while (1) {
        int32_t status = generate_sync(g_llm_ctx, prompt, 510, on_prefilling, on_decoding, on_finished);
    }

    // llm_context_free(g_llm_ctx);
}

void loop() {

}
