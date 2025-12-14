#include <wchar.h>
#include <stdint.h>
#include <esp32-hal-psram.h>
#include <Wire.h>

// #include "model_psycho_230k_1214_q80.h"
#include "model_psycho_230k_1214_q80.h"

#include "graphics.h"
#include "ui.h"
// #include "ups.h"
#include "keyboard_hal.h"
#include "infer.h"
#include "prompt.h"

#include "platform.h"

#define PREFILL_LED_ON  system("echo \"1\" > /sys/devices/platform/leds/leds/green:status/brightness");
#define PREFILL_LED_OFF system("echo \"0\" > /sys/devices/platform/leds/leds/green:status/brightness");
#define DECODE_LED_ON   system("echo \"1\" > /sys/devices/platform/leds/leds/blue:status/brightness");
#define DECODE_LED_OFF  system("echo \"0\" > /sys/devices/platform/leds/leds/blue:status/brightness");

// 推理引擎实例（单例模式）
static Nano_Context *g_llm_ctx = NULL;

static char *MODEL_PATH_1 = MODEL_ROOT_DIR "/nano_168m_625000_sft_947000_q80.bin";
static char *MODEL_PATH_2 = MODEL_ROOT_DIR "/nano_56m_99000_sft_v2_200000_q80.bin";
static char *MODEL_PATH_3 = MODEL_ROOT_DIR "/1-基础模型-99000_q80.bin";
static char *LORA_PATH_3  = MODEL_ROOT_DIR "/2-插件-猫娘.bin";
static char *MODEL_PATH_4 = MODEL_ROOT_DIR "/qwen3-0b6-q80.bin";
static char *MODEL_PATH_5 = MODEL_ROOT_DIR "/qwen3-1b7-q80.bin";
static char *MODEL_PATH_6 = MODEL_ROOT_DIR "/qwen3-4b-instruct-2507-q80.bin";

static float g_tps_of_last_session = 0.0f;
static wchar_t g_llm_output_of_last_session[OUTPUT_BUFFER_LENGTH] = L"";
static wchar_t g_asr_output[10] = L"请说话...";

static wchar_t g_anniversory[OUTPUT_BUFFER_LENGTH] = L"我在博客中";

// 全局设置
int32_t g_config_auto_submit_after_asr = 1; // ASR结束后立刻提交识别内容到LLM
int32_t g_config_tts_mode = 0; // TTS工作模式：0-关闭   1-实时   2-全部生成后统一TTS

char *g_model_path = NULL;
char *g_lora_path = NULL;
float g_repetition_penalty = 1.05f;
float g_temperature = 1.0f;
float g_top_p = 0.5f;
unsigned int g_top_k = 0;
unsigned long long g_random_seed = 0;
uint32_t g_max_seq_len = 512;


// 传递PTT状态的命名管道
int g_ptt_fifo_fd = 0;
// 传递ASR识别结果的命名管道
int g_asr_fifo_fd = 0;
// 传递给TTS的文字内容的命名管道
int g_tts_fifo_fd = 0;

#define PTT_FIFO_PATH "/tmp/ptt_fifo"

#define ASR_FIFO_PATH "/tmp/asr_fifo"
#define ASR_BUFFER_SIZE (65536)

#define TTS_FIFO_PATH "/tmp/tts_fifo"
#define TTS_BUFFER_SIZE (4096)


// TTS分句用全局变量
int32_t g_tts_split_from = 0; // 切句子的起始位置


///////////////////////////////////////
// 全局GUI组件对象

Global_State           *global_state  = {0};
Key_Event              *key_event = {0};
Widget_Textarea_State  *widget_textarea_state = {0};
Widget_Textarea_State  *asr_textarea_state = {0};
Widget_Textarea_State  *prefilling_textarea_state = {0};
Widget_Input_State     *widget_input_state = {0};
Widget_Menu_State      *main_menu_state = {0};
Widget_Menu_State      *model_menu_state = {0};
Widget_Menu_State      *setting_menu_state = {0};

// 全局状态标志
int32_t STATE = -1;
int32_t PREV_STATE = -1;





// return time in milliseconds, for benchmarking the model speed
long time_in_ms() {
    return (long)millis();
}




// 优雅关机
int32_t graceful_shutdown() {
    return 0;
}




// 穷人的ASR服务状态检测：通过读取ASR服务的日志前64kB中是否出现“init finished”来判断
int32_t check_asr_server_status() {
    return 0;
}





// 以只读方式打开ASR命名管道（非阻塞）
int32_t open_asr_fifo() {
    return 0;
}

// 读取ASR管道内容
int32_t read_asr_fifo(wchar_t *asr_text) {
    return 0;
}

// 向TTS输入FIFO中写文本内容
int32_t write_tts_fifo(char *text_bytes, int32_t len) {
    return 0;
}


int32_t send_tts_request(wchar_t *text, int32_t is_finished) {
    return 0;
}


int32_t stop_tts() {
    return 0;
}


// 向PTT状态FIFO中写PTT状态
int32_t set_ptt_status(uint8_t status) {
    return 0;
}




int32_t on_llm_prefilling(Key_Event *key_event, Global_State *global_state, Nano_Session *session) {
    if (session->t_0 == 0) {
        session->t_0 = time_in_ms();
    }
    else {
        session->tps = (session->pos - 1) / (double)(time_in_ms() - session->t_0) * 1000;
    }

    // 长/短按A键中止推理
    if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
        wcscpy(g_llm_output_of_last_session, L"");
        g_tps_of_last_session = session->tps;
        return LLM_STOPPED_IN_PREFILLING;
    }

    // PREFILL_LED_ON

    prefilling_textarea_state->x = 0;
    prefilling_textarea_state->y = 0;
    prefilling_textarea_state->width = 128;
    prefilling_textarea_state->height = 24;

    wcscpy(prefilling_textarea_state->text, L"Pre-filling...");
    prefilling_textarea_state->current_line = 0;
    prefilling_textarea_state->is_show_scroll_bar = 0;

    // 每隔refresh_ratio个token刷新一次屏幕
    if (global_state->timer % global_state->refresh_ratio == 0) {
    
        // 临时关闭draw_textarea的整帧绘制，以便在textarea上绘制进度条之后再统一写入屏幕，否则反复的clear会导致进度条闪烁。
        global_state->is_full_refresh = 0;

        fb_soft_clear();

        draw_textarea(key_event, global_state, prefilling_textarea_state);

        fb_draw_line(0, 60, 128, 60, 1);
        fb_draw_line(0, 63, 128, 63, 1);
        fb_draw_line(127, 60, 127, 63, 1);
        fb_draw_line(0, 61, session->pos * 128 / (session->num_prompt_tokens - 2), 61, 1);
        fb_draw_line(0, 62, session->pos * 128 / (session->num_prompt_tokens - 2), 62, 1);

        gfx_refresh();

        // 重新开启整帧绘制，注意这个标记是所有函数共享的全局标记。
        global_state->is_full_refresh = 1;

    }

    g_tts_split_from = 0;

    // PREFILL_LED_OFF
    return LLM_RUNNING_IN_PREFILLING;
}

int32_t on_llm_decoding(Key_Event *key_event, Global_State *global_state, Nano_Session *session) {
    if (session->t_0 == 0) {
        session->t_0 = time_in_ms();
    }
    else {
        session->tps = (session->pos - 1) / (double)(time_in_ms() - session->t_0) * 1000;
    }

    // 长/短按A键中止推理
    if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
        wcscpy(g_llm_output_of_last_session, session->output_text);
        g_tps_of_last_session = session->tps;
        return LLM_STOPPED_IN_DECODING;
    }

    // DECODE_LED_ON

    wcscpy(widget_textarea_state->text, session->output_text);
    widget_textarea_state->current_line = -1;
    widget_textarea_state->is_show_scroll_bar = 1;

    // 每隔refresh_ratio个token刷新一次屏幕
    if (global_state->timer % global_state->refresh_ratio == 0) {
        draw_textarea(key_event, global_state, widget_textarea_state);
    }

    // DECODE_LED_OFF

#ifdef TTS_ENABLED
    if (g_config_tts_mode > 0) {
        send_tts_request(session->output_text, 0);
    }
#endif

    return LLM_RUNNING_IN_DECODING;
}

int32_t on_llm_finished(Nano_Session *session) {
    session->t_1 = time_in_ms();
    session->tps = (session->pos - 1) / (double)(session->t_1 - session->t_0) * 1000;
    wcscpy(g_llm_output_of_last_session, session->output_text);

    g_tps_of_last_session = session->tps;

    return LLM_STOPPED_NORMALLY;
}


///////////////////////////////////////
// 全局组件操作过程

void init_main_menu() {
    wcscpy(main_menu_state->title, L"Nano-Pod V2512");
    wcscpy(main_menu_state->items[0], L"电子鹦鹉");
    wcscpy(main_menu_state->items[1], L"电子书");
    wcscpy(main_menu_state->items[2], L"设置");
    wcscpy(main_menu_state->items[3], L"安全关机");
    wcscpy(main_menu_state->items[4], L"本机自述");
    main_menu_state->item_num = 5;
    init_menu(key_event, global_state, main_menu_state);
}

void init_model_menu() {
    wcscpy(model_menu_state->title, L"Select LLM");
    wcscpy(model_menu_state->items[0], L"Nano-168M-QA");
    wcscpy(model_menu_state->items[1], L"Nano-56M-QA");
    wcscpy(model_menu_state->items[2], L"Nano-56M-Neko");
    wcscpy(model_menu_state->items[3], L"Qwen3-0.6B");
    wcscpy(model_menu_state->items[4], L"Qwen3-1.7B");
    wcscpy(model_menu_state->items[5], L"Qwen3-4B-Inst-2507");
    model_menu_state->item_num = 6;
    init_menu(key_event, global_state, model_menu_state);
}

void refresh_model_menu() {
    draw_menu(key_event, global_state, model_menu_state);
}

void init_setting_menu() {
    wcscpy(setting_menu_state->title, L"设置");
    wcscpy(setting_menu_state->items[0], L"语言模型生成参数");
    wcscpy(setting_menu_state->items[1], L"语音合成(TTS)设置");
    wcscpy(setting_menu_state->items[2], L"语音识别(ASR)设置");
    setting_menu_state->item_num = 3;
    init_menu(key_event, global_state, setting_menu_state);
}


///////////////////////////////////////
// 事件处理回调
//   NOTE 现阶段，回调函数里面引用的都是全局变量。后续可以container或者ctx之类的参数形式传进去。

// 通用的菜单事件处理
int32_t menu_event_handler(
    Key_Event *ke, Global_State *gs, Widget_Menu_State *ms,
    int32_t (*menu_item_action)(int32_t), int32_t prev_focus, int32_t current_focus
) {
    // 短按0-9数字键：直接选中屏幕上显示的那页的相对第几项
    if (ke->key_edge == -1 && (ke->key_code >= KEYCODE_NUM_0 && ke->key_code <= KEYCODE_NUM_9)) {
        if (ke->key_code < ms->item_num) {
            ms->current_item_intex = ms->first_item_intex + (uint32_t)(ke->key_code) - 1;
        }
        return menu_item_action(ms->current_item_intex);
    }
    // 短按A键：返回上一个焦点状态
    else if (ke->key_edge == -1 && ke->key_code == KEYCODE_NUM_A) {
        return prev_focus;
    }
    // 短按D键：执行菜单项对应的功能
    else if (ke->key_edge == -1 && ke->key_code == KEYCODE_NUM_D) {
        return menu_item_action(ms->current_item_intex);
    }
    // 长+短按*键：光标向上移动
    else if ((ke->key_edge == -1 || ke->key_edge == -2) && ke->key_code == KEYCODE_NUM_STAR) {
        if (ms->first_item_intex == 0 && ms->current_item_intex == 0) {
            ms->first_item_intex = ms->item_num - ms->items_per_page;
            ms->current_item_intex = ms->item_num - 1;
        }
        else if (ms->current_item_intex == ms->first_item_intex) {
            ms->first_item_intex--;
            ms->current_item_intex--;
        }
        else {
            ms->current_item_intex--;
        }

        draw_menu(ke, gs, ms);

        return current_focus;
    }
    // 长+短按#键：光标向下移动
    else if ((ke->key_edge == -1 || ke->key_edge == -2) && ke->key_code == KEYCODE_NUM_HASH) {
        if (ms->first_item_intex == ms->item_num - ms->items_per_page && ms->current_item_intex == ms->item_num - 1) {
            ms->first_item_intex = 0;
            ms->current_item_intex = 0;
        }
        else if (ms->current_item_intex == ms->first_item_intex + ms->items_per_page - 1) {
            ms->first_item_intex++;
            ms->current_item_intex++;
        }
        else {
            ms->current_item_intex++;
        }

        draw_menu(ke, gs, ms);

        return current_focus;
    }

    return current_focus;
}


// 通用的文本框卷行事件处理
int32_t textarea_event_handler(
    Key_Event *ke, Global_State *gs, Widget_Textarea_State *ts,
    int32_t prev_focus, int32_t current_focus
) {
    // 短按A键：回到上一个焦点
    if (ke->key_edge == -1 && ke->key_code == KEYCODE_NUM_A) {
        return prev_focus;
    }

    // 长+短按*键：推理结果向上翻一行。如果翻到顶，则回到最后一行。
    else if ((ke->key_edge == -1 || ke->key_edge == -2) && ke->key_code == KEYCODE_NUM_STAR) {
        if (ts->current_line <= 0) { // 卷到顶
            ts->current_line = ts->line_num - 5;
        }
        else {
            ts->current_line--;
        }

        draw_textarea(ke, gs, ts);

        return current_focus;
    }

    // 长+短按#键：推理结果向下翻一行。如果翻到底，则回到第一行。
    else if ((ke->key_edge == -1 || ke->key_edge == -2) && ke->key_code == KEYCODE_NUM_HASH) {
        if (ts->current_line >= (ts->line_num - 5)) { // 卷到底
            ts->current_line = 0;
        }
        else {
            ts->current_line++;
        }

        draw_textarea(ke, gs, ts);

        return current_focus;
    }

    return current_focus;
}





///////////////////////////////////////
// 菜单条目动作回调

// 主菜单各条目的动作
int32_t main_menu_item_action(int32_t item_index) {
    // 0.电子鹦鹉
    if (item_index == 0) {
        init_model_menu();
        return 4;
    }

    // 1.电子书
    else if (item_index == 1) {
        return -3;
    }

    // 2.设置
    else if (item_index == 2) {
        init_setting_menu();
        return 5;
    }

    // 3.安全关机
    else if (item_index == 3) {
        return 31;
    }

    // 4.本机自述
    else if (item_index == 4) {
        return 26;
    }
    return -2;
}

int32_t model_menu_item_action(int32_t item_index) {
    if (g_llm_ctx) {
        llm_context_free(g_llm_ctx);
    }

    if (item_index == 0) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Nano-168M-QA\n 请稍等...");
        g_model_path = MODEL_PATH_1;
        g_lora_path = NULL;
        g_repetition_penalty = 1.05f;
        g_temperature = 1.0f;
        g_top_p = 0.5f;
        g_top_k = 0;
        g_max_seq_len = 512;
        global_state->refresh_ratio = 1;
    }
    else if (item_index == 1) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Nano-56M-QA\n 请稍等...");
        g_model_path = MODEL_PATH_2;
        g_lora_path = NULL;
        g_repetition_penalty = 1.05f;
        g_temperature = 1.0f;
        g_top_p = 0.5f;
        g_top_k = 0;
        g_max_seq_len = 512;
        global_state->refresh_ratio = 1;
    }
    else if (item_index == 2) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Nano-56M-Neko\n 请稍等...");
        g_model_path = MODEL_PATH_3;
        g_lora_path = LORA_PATH_3;
        g_repetition_penalty = 1.05f;
        g_temperature = 1.0f;
        g_top_p = 0.5f;
        g_top_k = 0;
        g_max_seq_len = 512;
        global_state->refresh_ratio = 1;
    }
    else if (item_index == 3) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Qwen3-0.6B\n 请稍等...");
        g_model_path = MODEL_PATH_4;
        g_lora_path = NULL;
        g_repetition_penalty = 1.0f;
        g_temperature = 0.6f;
        g_top_p = 0.95f;
        g_top_k = 20;
        g_max_seq_len = 32768;
        global_state->refresh_ratio = 4;
    }
    else if (item_index == 4) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Qwen3-1.7B\n 请稍等...");
        g_model_path = MODEL_PATH_5;
        g_lora_path = NULL;
        g_repetition_penalty = 1.0f;
        g_temperature = 0.6f;
        g_top_p = 0.95f;
        g_top_k = 20;
        g_max_seq_len = 32768;
        global_state->refresh_ratio = 1;
    }
    else if (item_index == 5) {
        wcscpy(widget_textarea_state->text, L" 正在加载语言模型\n Qwen3-4B-Inst-2507\n 请稍等...");
        g_model_path = MODEL_PATH_6;
        g_lora_path = NULL;
        g_repetition_penalty = 1.0f;
        g_temperature = 0.7f;
        g_top_p = 0.8f;
        g_top_k = 20;
        g_max_seq_len = 32768;
        global_state->refresh_ratio = 1;
    }

    widget_textarea_state->current_line = 0;
    widget_textarea_state->is_show_scroll_bar = 0;
    draw_textarea(key_event, global_state, widget_textarea_state);

    g_random_seed = (unsigned int)micros();
    // g_llm_ctx = llm_context_init(g_model_path, g_lora_path, g_max_seq_len, g_repetition_penalty, g_temperature, g_top_p, g_top_k, g_random_seed);
    g_llm_ctx = llm_context_init_from_buffer((uint8_t *)psycho_230k_1214_q80, 512, 1.0, 1.0, 0.8, 20, g_random_seed);

    wcscpy(widget_textarea_state->text, L"加载完成~");
    widget_textarea_state->current_line = 0;
    widget_textarea_state->is_show_scroll_bar = 0;
    draw_textarea(key_event, global_state, widget_textarea_state);

    delayMicroseconds(500*1000);

    // 以下两条路选一个：

    // 1、直接进入电子鹦鹉
    init_input(key_event, global_state, widget_input_state);
    return 0;

    // 2、或者回到主菜单
    // refresh_menu(key_event, global_state, main_menu_state);
    // return -2;
}

int32_t setting_menu_item_action(int32_t item_index) {
    // 语言模型生成参数设置
    if (item_index == 0) {
        wcscpy(widget_textarea_state->text, L"暂未实现");
        widget_textarea_state->current_line = 0;
        widget_textarea_state->is_show_scroll_bar = 0;
        draw_textarea(key_event, global_state, widget_textarea_state);

        delayMicroseconds(500*1000);

        refresh_menu(key_event, global_state, setting_menu_state);
        return 5;
    }
    // TTS设置
    else if (item_index == 1) {
        return 32;
    }
    // ASR设置
    else if (item_index == 2) {
        return 33;
    }
    else {
        return 5;
    }
}



void setup() {

    Serial.begin(115200);

    delay(100);

    Wire.begin();
    Wire.setClock(400000);

    // if(!setlocale(LC_CTYPE, "")) return -1;

    ///////////////////////////////////////
    // 初始化GUI状态

    global_state = (Global_State*)ps_calloc(1, sizeof(Global_State));
    key_event = (Key_Event*)ps_calloc(1, sizeof(Key_Event));
    widget_textarea_state = (Widget_Textarea_State*)ps_calloc(1, sizeof(Widget_Textarea_State));
    asr_textarea_state = (Widget_Textarea_State*)ps_calloc(1, sizeof(Widget_Textarea_State));
    prefilling_textarea_state = (Widget_Textarea_State*)ps_calloc(1, sizeof(Widget_Textarea_State));
    widget_input_state = (Widget_Input_State*)ps_calloc(1, sizeof(Widget_Input_State));
    main_menu_state = (Widget_Menu_State*)ps_calloc(1, sizeof(Widget_Menu_State));
    model_menu_state = (Widget_Menu_State*)ps_calloc(1, sizeof(Widget_Menu_State));
    setting_menu_state = (Widget_Menu_State*)ps_calloc(1, sizeof(Widget_Menu_State));

    global_state->llm_status = LLM_STOPPED_NORMALLY;
    global_state->is_recording = 0;
    global_state->asr_start_timestamp = 0;
    global_state->is_full_refresh = 1;
    global_state->refresh_ratio = 2; // 默认每2个token刷新一次屏幕

    widget_textarea_state->x = 0;
    widget_textarea_state->y = 0;
    widget_textarea_state->width = 128;
    widget_textarea_state->height = 64;
    widget_textarea_state->line_num = 0;
    widget_textarea_state->current_line = 0;

    key_event->key_code = KEYCODE_NUM_IDLE; // 大于等于16为没有任何按键，0-15为按键
    key_event->key_edge = 0;   // 0：松开  1：上升沿  -1：下降沿(短按结束)  -2：下降沿(长按结束)
    key_event->key_timer = 0;  // 按下计时器
    key_event->key_mask = 0;   // 长按超时后，键盘软复位标记。此时虽然物理上依然按键，只要软复位标记为1，则认为是无按键，无论是边沿还是按住都不触发。直到物理按键松开后，软复位标记清0。
    key_event->key_repeat = 0; // 触发一次长按后，只要不松手，该标记置1，直到物理按键松开后置0。若该标记为1，则在按住时触发连续重复动作。

    // 空按键状态：用于定时器事件
    Key_Event *void_key_event = (Key_Event*)ps_calloc(1, sizeof(Key_Event));
    void_key_event->key_code = KEYCODE_NUM_IDLE;
    void_key_event->key_edge = 0;
    void_key_event->key_timer = 0;
    void_key_event->key_mask = 0;
    void_key_event->key_repeat = 0;

    ///////////////////////////////////////
    // UPS传感器初始化
#ifdef UPS_ENABLED
    ups_init();
#endif

    ///////////////////////////////////////
    // OLED 初始化

    gfx_init();

    show_splash_screen(key_event, global_state);

    ///////////////////////////////////////
    // 矩阵按键初始化与读取

    keyboard_hal_init();
    key_event->prev_key = KEYCODE_NUM_IDLE;


    while (1) {
        uint8_t key = keyboard_hal_read_key();
        // 边沿
        if (key_event->key_mask != 1 && (key != key_event->prev_key)) {
            // 按下瞬间（上升沿）
            if (key != KEYCODE_NUM_IDLE) {
                key_event->key_code = key;
                key_event->key_edge = 1;
                key_event->key_timer = 0;
            }
            // 松开瞬间（下降沿）
            else {
                key_event->key_code = key_event->prev_key;
                // 短按（或者通过长按触发重复动作状态后反复触发）
                if (key_event->key_repeat == 1 || (key_event->key_timer >= 0 && key_event->key_timer < LONG_PRESS_THRESHOLD)) {
                    key_event->key_edge = -1;
                    key_event->key_timer = 0;
                }
                // 长按
                else if (key_event->key_timer >= LONG_PRESS_THRESHOLD) {
                    key_event->key_edge = -2;
                    key_event->key_timer = 0;
                    key_event->key_repeat = 1;
                }
            }
        }
        // 按住或松开
        else {
            // 按住
            if (key != KEYCODE_NUM_IDLE) {
                key_event->key_code = key;
                key_event->key_edge = 0;
                key_event->key_timer++;
                // 若重复动作标记key_repeat在一次长按后点亮，则继续按住可以反复触发短按
                if (key_event->key_repeat == 1) {
                    key_event->key_edge = -2;
                    key_event->key_mask = 1; // 软复位置1，即强制恢复为无按键状态，以便下一次轮询检测到下降沿（尽管物理上有键按下），触发长按事件
                    key = KEYCODE_NUM_IDLE; // 便于后面设置prev_key为KEYCODE_NUM_IDLE（无键按下）
                    key_event->key_repeat = 1;
                }
                // 如果没有点亮动作标记key_repeat，则达到长按阈值后触发长按事件
                else if (key_event->key_timer >= LONG_PRESS_THRESHOLD) {
                    // printf("按住超时触发长按：%d，计时=%d，key_mask=%d\n", (int)key, key_event->key_timer, (int)key_event->key_mask);
                    key_event->key_edge = -2;
                    key_event->key_mask = 1; // 软复位置1，即强制恢复为无按键状态，以便下一次轮询检测到下降沿（尽管物理上有键按下），触发长按事件
                    key = KEYCODE_NUM_IDLE; // 便于后面设置prev_key为KEYCODE_NUM_IDLE（无键按下）
                }
            }
            // 松开
            else {
                key_event->key_code = KEYCODE_NUM_IDLE;
                key_event->key_edge = 0;
                key_event->key_timer = 0;
                key_event->key_mask = 0;
                key_event->key_repeat = 0;
            }
        }
        key_event->prev_key = key;

        switch(STATE) {

        /////////////////////////////////////////////
        // 初始状态：欢迎屏幕。按任意键进入主菜单
        /////////////////////////////////////////////

        case -1:

            // 节流
            if (global_state->timer % 10 == 0) {
                show_splash_screen(key_event, global_state);
            }

            // 按下任何键，不论长短按，进入主菜单
            if (key_event->key_edge < 0 && key_event->key_code != KEYCODE_NUM_IDLE) {
                init_main_menu();
                STATE = -2;
            }

            break;

        /////////////////////////////////////////////
        // 主菜单。
        /////////////////////////////////////////////

        case -2:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                refresh_menu(key_event, global_state, main_menu_state);
            }
            PREV_STATE = STATE;

            STATE = menu_event_handler(key_event, global_state, main_menu_state, main_menu_item_action, -1, -2);

            break;

        /////////////////////////////////////////////
        // 文本显示状态
        /////////////////////////////////////////////

        case -3:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                wcscpy(widget_textarea_state->text, g_anniversory);
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 1;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }
            PREV_STATE = STATE;

            STATE = textarea_event_handler(key_event, global_state, widget_textarea_state, -2, -3);

            break;

        /////////////////////////////////////////////
        // 文字编辑器状态
        /////////////////////////////////////////////

        case 0:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                refresh_input(key_event, global_state, widget_input_state);
            }
            PREV_STATE = STATE;

            // 长+短按A键：删除一个字符；如果输入缓冲区为空，则回到主菜单
            if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
                if (widget_input_state->state == 0 && widget_input_state->length <= 0) {
                    init_input(key_event, global_state, widget_input_state);
                    STATE = -2;
                }
            }
#ifdef ASR_ENABLED
            // 按下C键：开始PTT
            else if (key_event->key_edge > 0 && key_event->key_code == KEYCODE_NUM_C) {
                STATE = 21;
            }
#endif
            // 短按D键：提交
            else if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_D) {
                if (widget_input_state->state == 0) {
                    STATE = 8;
                }
            }

            draw_input(key_event, global_state, widget_input_state);

            break;

        /////////////////////////////////////////////
        // 选择语言模型状态
        /////////////////////////////////////////////

        case 4:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                refresh_menu(key_event, global_state, model_menu_state);
            }
            PREV_STATE = STATE;

            STATE = menu_event_handler(key_event, global_state, model_menu_state, model_menu_item_action, -2, 4);

            break;


        /////////////////////////////////////////////
        // 设置菜单
        /////////////////////////////////////////////

        case 5:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                refresh_menu(key_event, global_state, setting_menu_state);
            }
            PREV_STATE = STATE;

            STATE = menu_event_handler(key_event, global_state, setting_menu_state, setting_menu_item_action, -2, 5);

            break;


        /////////////////////////////////////////////
        // 语言推理进行中（异步，每个iter结束后会将控制权交还事件循环，而非自行阻塞到最后一个token）
        //   实际上就是将generate_sync的while循环打开，将其置于大的事件循环。
        /////////////////////////////////////////////

        case 8:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                wchar_t prompt[MAX_PROMPT_BUFFER_LENGTH] = L"";

                // 如果输入为空，则随机选用一个预置prompt
                if (wcslen(widget_input_state->text) == 0) {
                    set_random_prompt(widget_input_state->text, micros());
                    widget_input_state->length = wcslen(widget_input_state->text);
                }

                // 根据模型类型应用prompt模板
                if (g_llm_ctx->llm->arch == LLM_ARCH_NANO) {
                    wcscat(prompt, L"<|instruct_mark|>");
                    wcscat(prompt, widget_input_state->text);
                    wcscat(prompt, L"<|response_mark|>");
                }
                else if (g_llm_ctx->llm->arch == LLM_ARCH_QWEN2 || g_llm_ctx->llm->arch == LLM_ARCH_QWEN3) {
                    wcscpy(prompt, widget_input_state->text);
                    // wcscat(prompt, L" /no_think");
                }
                else {
                    printf("Error: unknown model arch.\n");
                    // exit(EXIT_FAILURE);
                }

                // 初始化对话session
                global_state->llm_session = llm_session_init(g_llm_ctx, prompt, g_max_seq_len);
            }
            PREV_STATE = STATE;

            // 事件循环主体：即同步版本的while(1)的循环体

            global_state->llm_status = llm_session_step(g_llm_ctx, global_state->llm_session);

            if (global_state->llm_status == LLM_RUNNING_IN_PREFILLING) {
                global_state->llm_status = on_llm_prefilling(key_event, global_state, global_state->llm_session);
                // 外部被动中止
                if (global_state->llm_status == LLM_STOPPED_IN_PREFILLING) {
                    llm_session_free(global_state->llm_session);
                    STATE = 10;
                }
                else {
                    STATE = 8;
                }
            }
            else if (global_state->llm_status == LLM_RUNNING_IN_DECODING) {
                global_state->llm_status = on_llm_decoding(key_event, global_state, global_state->llm_session);
                // 外部被动中止
                if (global_state->llm_status == LLM_STOPPED_IN_DECODING) {
#ifdef TTS_ENABLED
                    if (g_config_tts_mode > 0) {
                        stop_tts();
                    }
#endif
                    llm_session_free(global_state->llm_session);
                    STATE = 10;
                }
                else {
                    STATE = 8;
                }
            }
            else if (global_state->llm_status == LLM_STOPPED_NORMALLY) {
                global_state->llm_status = on_llm_finished(global_state->llm_session);
                llm_session_free(global_state->llm_session);
                STATE = 10;
            }
            else {
                global_state->llm_status = on_llm_finished(global_state->llm_session);
                llm_session_free(global_state->llm_session);
                STATE = 10;
            }

            break;


        /////////////////////////////////////////////
        // 推理结束（自然结束或中断），显示推理结果
        /////////////////////////////////////////////

        case 10:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                // 计算提示语+生成内容的行数
                wchar_t *prompt_and_output = (wchar_t *)ps_calloc(OUTPUT_BUFFER_LENGTH * 2, sizeof(wchar_t));
                wcscat(prompt_and_output, L"Homo:\n");
                wcscat(prompt_and_output, widget_input_state->text);
                wcscat(prompt_and_output, L"\n--------------------\nNano:\n");
                wcscat(prompt_and_output, g_llm_output_of_last_session);
                if (global_state->llm_status == LLM_STOPPED_IN_PREFILLING || global_state->llm_status == LLM_STOPPED_IN_DECODING) {
                    Serial.print("推理中止。\n");
                    wcscat(prompt_and_output, L"\n\n[Nano:推理中止]");
                }
                else if (global_state->llm_status == LLM_STOPPED_NORMALLY) {
                    Serial.print("推理自然结束。\n");
                }
                else {
                    Serial.print("推理异常结束。\n");
                    wcscat(prompt_and_output, L"\n\n[Nano:推理异常结束]");
                }
                wchar_t tps_wcstr[50];
                swprintf(tps_wcstr, 50, L"\n\n[平均速度%.1f词元/秒]", g_tps_of_last_session);
                wcscat(prompt_and_output, tps_wcstr);

                wcscpy(g_llm_output_of_last_session, prompt_and_output);

                free(prompt_and_output);

                wcscpy(widget_textarea_state->text, g_llm_output_of_last_session);
                widget_textarea_state->current_line = -1;
                widget_textarea_state->is_show_scroll_bar = 1;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }
            PREV_STATE = STATE;

            // 短按D键：重新推理。推理完成后，并不清除输入缓冲区，因此再次按D键会重新推理。
            if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_D) {
                STATE = 8;
            }
            else {
                // 短按A键：停止TTS
                if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_A) {
#ifdef TTS_ENABLED
                    if (g_config_tts_mode > 0) {
                        stop_tts();
                    }
#endif
                }
                STATE = textarea_event_handler(key_event, global_state, widget_textarea_state, 0, 10);
            }

            break;

        /////////////////////////////////////////////
        // ASR实时识别进行中（响应ASR客户端回报的ASR文本内容）
        /////////////////////////////////////////////

        case 21:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                // 设置PTT状态为按下（>0）
                if (set_ptt_status(66) < 0) break;

                // 打开ASR管道
                if (open_asr_fifo() < 0) break;


                asr_textarea_state->x = 0;
                asr_textarea_state->y = 0;
                asr_textarea_state->width = 128;
                asr_textarea_state->height = 51; // NOTE 详见结构体定义处的说明

                wcscpy(asr_textarea_state->text, L"请说话...");

                global_state->is_recording = 1;
                global_state->asr_start_timestamp = 0;
            }
            PREV_STATE = STATE;

            // 实时显示ASR结果
            if (global_state->is_recording == 1) {
                int32_t len = read_asr_fifo(g_asr_output);

                // 临时关闭draw_textarea的整帧绘制，以便在textarea上绘制进度条之后再统一写入屏幕，否则反复的clear会导致进度条闪烁。
                global_state->is_full_refresh = 0;
                fb_soft_clear();

                // 显示ASR结果
                // if (len > 0) {
                    wcscpy(asr_textarea_state->text, g_asr_output);
                    asr_textarea_state->current_line = -1;
                    asr_textarea_state->is_show_scroll_bar = 1;
                    draw_textarea(key_event, global_state, asr_textarea_state);
                // }

                // 绘制录音持续时间
                wchar_t rec_duration[50];
                swprintf(rec_duration, 50, L" %ds ", (int32_t)(millis() - global_state->asr_start_timestamp));
                render_line(rec_duration, 0, 52, 0);

                gfx_refresh();

                // 重新开启整帧绘制，注意这个标记是所有函数共享的全局标记。
                global_state->is_full_refresh = 1;

            }

            // 松开按钮，停止PTT
            if (global_state->is_recording > 0 && key_event->key_edge == 0 && key_event->key_code == KEYCODE_NUM_IDLE) {
                Serial.print("松开PTT\n");
                global_state->is_recording = 0;
                global_state->asr_start_timestamp = 0;

                close(g_asr_fifo_fd);

                // // 设置PTT状态为松开（==0）
                if (set_ptt_status(0) < 0) break;
                close(g_ptt_fifo_fd);

                wcscpy(asr_textarea_state->text, L" \n \n      识别完成");
                asr_textarea_state->current_line = 0;
                asr_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, asr_textarea_state);

                delayMicroseconds(500*1000);

                wcscpy(widget_input_state->text, g_asr_output);
                widget_input_state->length = wcslen(g_asr_output);

                wcscpy(g_asr_output, L"请说话...");

                // ASR后立刻提交到LLM？
                if (g_config_auto_submit_after_asr) {
                    printf("立刻提交LLM：%ls\n", widget_input_state->text);
                    STATE = 8;
                }
                else {
                    widget_input_state->current_page = 0;
                    STATE = 0;
                }

            }

            // 短按A键：清屏，清除输入缓冲区，回到初始状态
            else if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_A) {
                // 刷新文本输入框
                init_input(key_event, global_state, widget_input_state);
                STATE = 0;
            }

            break;

        /////////////////////////////////////////////
        // 本机自述
        /////////////////////////////////////////////

        case 26:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {

            }
            PREV_STATE = STATE;

            wchar_t readme_buf[128];
            // 节流
            if (global_state->timer % 200 == 0) {
                swprintf(readme_buf, 128, L"Nano-Pod v2512\n电子鹦鹉·端上大模型\n(c) 2025 BD4SUR\n\nUPS:%04dmV/%d%% ", global_state->ups_voltage, global_state->ups_soc);
                wcscpy(widget_textarea_state->text, readme_buf);
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }

            // 按A键返回主菜单
            if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
                STATE = -2;
            }

            break;


        /////////////////////////////////////////////
        // 关机确认
        /////////////////////////////////////////////

        case 31:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                wcscpy(widget_textarea_state->text, L"确定关机？\n\n·长按D键: 关机\n·短按A键: 返回");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }
            PREV_STATE = STATE;

            // 长按D键确认关机
            if (key_event->key_edge == -2 && key_event->key_code == KEYCODE_NUM_D) {
                wcscpy(widget_textarea_state->text, L" \n \n    正在安全关机...");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                if (graceful_shutdown() >= 0) {
                    // exit(0);
                }
                // 关机失败，返回主菜单
                else {
                    wcscpy(widget_textarea_state->text, L"安全关机失败");
                    widget_textarea_state->current_line = 0;
                    widget_textarea_state->is_show_scroll_bar = 0;
                    draw_textarea(key_event, global_state, widget_textarea_state);

                    delayMicroseconds(1000*1000);

                    STATE = -2;
                }
            }

            // 长短按A键取消关机，返回主菜单
            else if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
                STATE = -2;
            }

            break;


        /////////////////////////////////////////////
        // TTS设置
        /////////////////////////////////////////////

        case 32:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                wcscpy(widget_textarea_state->text, L"语音合成(TTS)设置\n\n·0:关闭\n·1:实时请求合成\n·2:生成结束后合成");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }
            PREV_STATE = STATE;

            // 选项0
            if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_0) {
                g_config_tts_mode = 0;

                wcscpy(widget_textarea_state->text, L"TTS已关闭。");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                delayMicroseconds(500*1000);

                STATE = 5;
            }

            // 选项1
            else if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_1) {
                g_config_tts_mode = 1;

                wcscpy(widget_textarea_state->text, L"TTS设置为实时请求。");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                delayMicroseconds(500*1000);

                STATE = 5;
            }

            // 选项2
            else if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_2) {
                g_config_tts_mode = 2;

                wcscpy(widget_textarea_state->text, L"TTS设置为生成结束后统一请求合成。");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                delayMicroseconds(500*1000);

                STATE = 5;
            }

            // 长短按A键，返回设置菜单
            else if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
                STATE = 5;
            }

            break;


        /////////////////////////////////////////////
        // ASR设置
        /////////////////////////////////////////////

        case 33:

            // 首次获得焦点：初始化
            if (PREV_STATE != STATE) {
                wcscpy(widget_textarea_state->text, L"语音识别(TTS)设置\n识别结果立刻提交？\n·0:先编辑再提交\n·1:立刻提交");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);
            }
            PREV_STATE = STATE;

            // 选项0
            if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_0) {
                g_config_auto_submit_after_asr = 0;

                wcscpy(widget_textarea_state->text, L"ASR自动提交已关闭");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                delayMicroseconds(500*1000);

                STATE = 5;
            }

            // 选项1
            else if (key_event->key_edge == -1 && key_event->key_code == KEYCODE_NUM_1) {
                g_config_auto_submit_after_asr = 1;

                wcscpy(widget_textarea_state->text, L"ASR自动提交已开启");
                widget_textarea_state->current_line = 0;
                widget_textarea_state->is_show_scroll_bar = 0;
                draw_textarea(key_event, global_state, widget_textarea_state);

                delayMicroseconds(500*1000);

                STATE = 5;
            }

            // 长短按A键，返回设置菜单
            else if ((key_event->key_edge == -1 || key_event->key_edge == -2) && key_event->key_code == KEYCODE_NUM_A) {
                STATE = 5;
            }

            break;















        default:
            break;
        }



        // 定期检查系统状态
        if (global_state->timer % 600 == 0) {
#ifdef ASR_ENABLED
            // ASR服务状态
            global_state->is_asr_server_up = check_asr_server_status();
#endif
#ifdef UPS_ENABLED
            // UPS电压和电量
            global_state->ups_voltage = read_ups_voltage();
            global_state->ups_soc = read_ups_soc();
            // printf("%d mV | %d%%\n", global_state->ups_voltage, global_state->ups_soc);
#endif
        }

        global_state->timer = (global_state->timer == 2147483647) ? 0 : (global_state->timer + 1);
    }

    llm_context_free(g_llm_ctx);

    free(global_state);
    free(key_event);
    free(widget_textarea_state);
    free(widget_input_state);
    free(main_menu_state);
    free(model_menu_state);
    free(prefilling_textarea_state);

    free(void_key_event);

    gfx_close();

#ifdef MATMUL_PTHREAD
    matmul_pthread_cleanup();
#endif

}


void loop() {

}
