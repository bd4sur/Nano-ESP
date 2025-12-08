#ifndef __NANO_UI_H__
#define __NANO_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "graphics.h"

#include "platform.h"

#define INPUT_BUFFER_LENGTH  (65536)
#define OUTPUT_BUFFER_LENGTH (65536)

#define IME_MODE_HANZI    (0)
#define IME_MODE_ALPHABET (1)
#define IME_MODE_NUMBER   (2)

#define ALPHABET_COUNTDOWN_MAX (30)
#define LONG_PRESS_THRESHOLD (360)

#define MAX_CANDIDATE_NUM (1024)     // 候选字最大数量
#define MAX_CANDIDATE_PAGE_NUM (108) // 候选字最大分页数
#define MAX_CANDIDATE_NUM_PER_PAGE (10) // 每页最多有几个候选字（每页10个字）

#define MAX_MENU_ITEMS (1024)
#define MAX_MENU_ITEM_LEN (24)

typedef struct {
    int32_t is_full_refresh; // 作为所有绘制函数的一个参数，用于控制是否整帧刷新。默认为1。0-禁用函数内的clear-refresh，1-启用函数内的clear-refresh
    int32_t refresh_ratio; // LLM推理过程中，屏幕刷新的分频系数，也就是每几次推理刷新一次屏幕
} Global_State;

typedef struct {
    uint8_t  prev_key;   // 上一次按键的键值
    uint8_t  key_code;   // 大于等于16为没有任何按键，0-15为按键
    int8_t   key_edge;   // 0：松开  1：上升沿  -1：下降沿(短按结束)  -2：下降沿(长按结束)
    uint32_t key_timer;  // 按下状态的计时器
    uint8_t  key_mask;   // 长按超时后，键盘软复位标记。此时虽然物理上依然按键，只要软复位标记为1，则认为是无按键，无论是边沿还是按住都不触发。直到物理按键松开后，软复位标记清0。
    uint8_t  key_repeat; // 触发一次长按后，只要不松手，该标记置1，直到物理按键松开后置0。若该标记为1，则在按住时触发连续重复动作。
} Key_Event;

typedef struct {
    int32_t state;
    int32_t x;
    int32_t y; // NOTE 设置文本框高度时，按照除末行外，每行margin-bottom:1px来计算。例如，如果希望恰好显示4行，则高度应为13*3+12=51px。
    int32_t width;
    int32_t height;
    wchar_t text[INPUT_BUFFER_LENGTH];
    int32_t length;
    int32_t break_pos[INPUT_BUFFER_LENGTH];
    int32_t line_num;
    int32_t view_lines;
    int32_t view_start_pos;
    int32_t view_end_pos;
    int32_t current_line;
    int32_t is_show_scroll_bar; // 是否显示滚动条：0不显示 1显示
} Widget_Textarea_State;

void render_line(wchar_t *line, uint32_t x, uint32_t y, uint8_t mode);

void render_text(
    wchar_t *text, int32_t start_line, int32_t length, int32_t *break_pos, int32_t line_num,
    int32_t x_offset, int32_t y_offset, int32_t width, int32_t height, int32_t do_typeset);

void draw_textarea(Key_Event *key_event, Global_State *global_state, Widget_Textarea_State *textarea_state);

void render_scroll_bar(int32_t line_num, int32_t current_line, int32_t view_lines, int32_t x, int32_t y, int32_t width, int32_t height);






void text_typeset(
    int32_t is_full,         // in  全量排版？0-仅计算翻页；1-先计算全部换行，再计算翻页
    wchar_t *text,          // in  待排版的文本
    int32_t view_width,      // in  视图宽度
    int32_t view_height,     // in  视图高度
    int32_t start_line,      // in  从哪行开始显示（用于滚动）
    int32_t *length,         // (in if is_full else out) 文本长度（字符数）
    int32_t *break_pos,      // (in if is_full else out) 折行位置（每行第一个字符的index）数组
    int32_t *line_num,       // (in if is_full else out) 可见行数
    int32_t *view_lines,     // out 可见行数
    int32_t *view_start_pos, // out 可见区域第一个字符的index
    int32_t *view_end_pos    // out 可见区域最后一个字符的index
) {
    int32_t char_count = 0;
    int32_t break_count = 0;
    int32_t line_x_pos = 0;

    // 全量排版：计算全部文本的length(char_count)、line_num(break_count)、break_pos
    if (is_full) {
        for (char_count = 0; char_count < wcslen(text); char_count++) {
            wchar_t ch = text[char_count];
            int32_t char_width = (ch < 127) ? ((ch == '\n') ? 0 : FONT_WIDTH_HALF) : FONT_WIDTH_FULL;
            if (char_count == 0 || line_x_pos + char_width >= view_width) {
                break_pos[break_count] = char_count;
                break_count++;
                line_x_pos = 0;
            }
            else if (ch == '\n') {
                break_pos[break_count] = char_count + 1;
                break_count++;
                line_x_pos = 0;
            }
            line_x_pos += char_width;
        }

        *line_num = break_count;
        *length = char_count;
    }

    // 计算当前视图最大能容纳的行数。
    //   NOTE 考虑到行间距为1，且末行以下无间距，因此分子加1以去除末行无间距的影响。
    //        例如，高度为64的屏幕，实际可容纳(64+1)/(12+1)=5行。
    int32_t max_view_lines = (view_height + 1) / (FONT_HEIGHT + 1);
    int32_t _line_num = *line_num;

    *view_lines = max_view_lines;

    // 对start_line的检查和标准化
    if (start_line < 0) {
        // start_line小于0，解释为将文字末行卷动到视图的某一行。例如：-1代表将文字末行卷动到视图的倒数1行、-max_view_lines代表将文字末行卷动到视图的第1行。
        //   若start_line小于-max_view_lines，则等效于-max_view_lines，保证文字内容不会卷到视图以外。
        if (-start_line <= max_view_lines) {
            if (_line_num >= max_view_lines) {
                start_line = _line_num - 1 - start_line - max_view_lines;
            }
            else {
                start_line = 0;
            }
        }
        else {
            start_line = _line_num - 1;
        }
    }
    else if (start_line >= _line_num) {
        // start_line超过了末行，则对文本行数取模后滚动
        start_line = start_line % _line_num;
    }

    // 情况1：start_line介于首行（0）和（使得末行进入可见区域以下1行的位置），即视图内不包含末行
    if (start_line < _line_num - max_view_lines) {
        *view_start_pos = break_pos[start_line];
        *view_end_pos = break_pos[start_line + max_view_lines] - 1;
    }
    // 情况2：start_line等于或超过了（使得末行恰好位于可见区域底行的位置），但尚未超出末行，也就是末行位于视图内
    //        若文本行数不大于视图行数，则一定满足此条件。
    else if (start_line >= _line_num - max_view_lines && start_line < _line_num) {
        *view_start_pos = break_pos[start_line];
        *view_end_pos = *length - 1;
    }
}


// 渲染一行文本，mode为1则为正显，为0则为反白
void render_line(wchar_t *line, uint32_t x, uint32_t y, uint8_t mode) {
    uint32_t x_pos = x;
    uint32_t y_pos = y;
    for (uint32_t i = 0; i < wcslen(line); i++) {
        uint32_t current_char = line[i];
        uint8_t font_width = 12;
        uint8_t font_height = 12;
        uint8_t *glyph = get_glyph(current_char, &font_width, &font_height);
        if (!glyph) {
            printf("出现了字库之外的字符！\n");
            break;
        }
        if (x_pos + font_width >= 128) {
            break;
        }
        // NOTE 反色显示时，在每个字符场面额外补充一条线，避免菜单中高亮区域看起来顶格
        fb_draw_line(x_pos, y_pos - 1, x_pos+font_width-1, y_pos - 1, 1 - (mode % 2));
        fb_draw_char(x_pos, y_pos, glyph, font_width, font_height, (mode % 2));
        x_pos += font_width;
    }
}

// 返回值：文本折行后的行数（含换行符）
void render_text(
    wchar_t *text, int32_t start_line, int32_t length, int32_t *break_pos, int32_t line_num,
    int32_t x_offset, int32_t y_offset, int32_t width, int32_t height,
    int32_t is_full_typeset)
{
    int32_t view_lines = 0;
    int32_t view_start_pos = 0;
    int32_t view_end_pos = 0;

    text_typeset(is_full_typeset, text, width, height, start_line, &length, break_pos, &line_num, &view_lines, &view_start_pos, &view_end_pos);

    int x_pos = x_offset;
    int y_pos = y_offset;

    for (int i = view_start_pos; i <= view_end_pos; i++) {
        uint32_t current_char = text[i];
        if (!current_char) break;
        uint8_t font_width = FONT_WIDTH_FULL;
        uint8_t font_height = FONT_HEIGHT;
        if (current_char == '\n') {
            x_pos = x_offset;
            if(i > 0) y_pos += (font_height + 1);
            continue;
        }
        uint8_t *glyph = get_glyph(current_char, &font_width, &font_height);
        if (!glyph) {
            printf("出现了字库之外的字符[%d]\n", current_char);
            glyph = get_glyph(12307, &font_width, &font_height); // 用字脚符号“〓”代替，参考https://ja.wikipedia.org/wiki/下駄記号
        }
        if (x_pos + font_width >= x_offset + width) {
            y_pos += (font_height + 1);
            x_pos = x_offset;
        }
        fb_draw_char(x_pos, y_pos, glyph, font_width, font_height, 1);
        x_pos += font_width;
    }

    // free(wrapped);
    // free(wrapped_clipped);
}

// 绘制滚动条
//   line_num - 文本总行数
//   current_line - 当前在屏幕顶端的是哪一行
//   view_lines - 屏幕最多容纳几行
void render_scroll_bar(int32_t current_line, int32_t line_num, int32_t view_lines, int32_t x, int32_t y, int32_t width, int32_t height) {

    // 对current_line的检查和标准化
    if (current_line < 0) {
        // current_line小于0，解释为将文字末行卷动到视图的某一行。例如：-1代表将文字末行卷动到视图的倒数1行、-max_view_lines代表将文字末行卷动到视图的第1行。
        //   若current_line小于-max_view_lines，则等效于-max_view_lines，保证文字内容不会卷到视图以外。
        if (-current_line <= view_lines) {
            if (line_num >= view_lines) {
                current_line = line_num - 1 - current_line - view_lines;
            }
            else {
                current_line = 0;
            }
        }
        else {
            current_line = line_num - 1;
        }
    }
    else if (current_line >= line_num) {
        // current_line超过了末行，则对文本行数取模后滚动
        current_line = current_line % line_num;
    }

    for (int n = y; n < y + height; n++) {
        fb_plot(x + width - 1, n, !(n % 3));
    }
    // 如果总行数装不满视图，则滚动条长度等于视图高度height
    uint8_t bar_height = (line_num < view_lines) ? (uint8_t)(height) : (uint8_t)((view_lines * height) / line_num);
    // 进度条高度不小于3px
    bar_height = (bar_height < 3) ? 3 : bar_height;
    uint8_t y_0 = (uint8_t)(y + (current_line * height) / line_num);
    fb_draw_line(x + width - 1, y_0, x + width - 1, y_0 + bar_height + 1, 1);
    fb_draw_line(x + width - 2, y_0, x + width - 2, y_0 + bar_height + 1, 1);
}


void draw_textarea(Key_Event *key_event, Global_State *global_state, Widget_Textarea_State *textarea_state) {
    text_typeset(
        1,
        textarea_state->text,
        textarea_state->width,
        textarea_state->height,
        textarea_state->current_line,
        &(textarea_state->length),
        textarea_state->break_pos,
        &(textarea_state->line_num),
        &(textarea_state->view_lines),
        &(textarea_state->view_start_pos),
        &(textarea_state->view_end_pos)
    );

    if (global_state->is_full_refresh) {
        fb_soft_clear();
    }

    render_text(
        textarea_state->text, textarea_state->current_line, textarea_state->length, textarea_state->break_pos, textarea_state->line_num,
        textarea_state->x, textarea_state->y, textarea_state->width, textarea_state->height, 0);

    if (textarea_state->is_show_scroll_bar) {
        render_scroll_bar(
            textarea_state->current_line, textarea_state->line_num, textarea_state->view_lines,
            textarea_state->x, textarea_state->y, textarea_state->width, textarea_state->height);
    }

    if (global_state->is_full_refresh) {
        gfx_refresh();
    }
}

#ifdef __cplusplus
}
#endif

#endif
