// x86_64/core/console.h helper
#ifndef CONSOLE_H
#define CONSOLE_H
#include "../lib/common.h"

extern uint8_t crnt_attr;
extern int g_cursor_row, g_cursor_col;
extern char inputbuf[256];
extern int input_len;
extern volatile int input_ready;
extern char history[10][256]; // 10 команд по 256 сим.
extern int history_index, /* куда писать новую */ history_count, /* сколько всего записано */ history_pos;

void set_color(uint8_t fg, uint8_t bg);
void reset_color(void);
void clear_screen(void);
void clear_row(int row);
void clear_input_line(void);
void load_history_line(int pos);
void scroll_screen(void);
void print_char(char c);
void print_string(const char* str);
void string_copy(char*,const char*, int);
void print_uint(unsigned int num);
void print_int(int num);
void print_int_negat(int num);
void print_hex(unsigned int num);
void printf(const char* fmt, ...);
void redraw_input(void);
void update_cursor(void);

#endif