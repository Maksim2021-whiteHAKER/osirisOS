// x86_64/core/keyboard.c
#include "../lib/common.h"
#include "keyboard.h"
#include "console.h"
#include "shell.h"
#include "timer.h"

uint8_t kbd_modifiers = 0;
int ruskbd = 0;

// Раскладка ЙЦУКЕН, кодировка CP866 (совпадает со шрифтом, который
// stage2 заливает в знакогенератор VGA): А-Я = 0x80-0x9F, а-п = 0xA0-0xAF,
// р-я = 0xE0-0xEF, Ё/ё = 0xF0/0xF1, № = 0xFC.
// Индекс массива = set-1 сканкод клавиши.
static const unsigned char kbd_ru_normal[64] = {
    0x00, 0x00, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2D, 0x3D, 0x00, 0x00,   // 0x00-0x0F
    0xA9, 0xE6, 0xE3, 0xAA, 0xA5, 0xAD, 0xA3, 0xE8, 0xE9, 0xA7, 0xE5, 0xEA, 0x00, 0x00, 0xE4, 0xEB,   // 0x10-0x1F
    0xA2, 0xA0, 0xAF, 0xE0, 0xAE, 0xAB, 0xA4, 0xA6, 0xED, 0xF1, 0x00, 0x5C, 0xEF, 0xE7, 0xE1, 0xAC,   // 0x20-0x2F
    0xA8, 0xE2, 0xEC, 0xA1, 0xEE, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x30-0x3F
};
static const unsigned char kbd_ru_shift[64] = {
    0x00, 0x00, 0x21, 0x22, 0xFC, 0x3B, 0x25, 0x3A, 0x3F, 0x2A, 0x28, 0x29, 0x5F, 0x2B, 0x00, 0x00,   // 0x00-0x0F
    0x89, 0x96, 0x93, 0x8A, 0x85, 0x8D, 0x83, 0x98, 0x99, 0x87, 0x95, 0x9A, 0x00, 0x00, 0x94, 0x9B,   // 0x10-0x1F
    0x82, 0x80, 0x8F, 0x90, 0x8E, 0x8B, 0x84, 0x86, 0x9D, 0xF0, 0x00, 0x5C, 0x9F, 0x97, 0x91, 0x8C,   // 0x20-0x2F
    0x88, 0x92, 0x9C, 0x81, 0x9E, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 0x30-0x3F
};
static const unsigned char kbd_us_normal[64] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=',0,0,  // 0x00-0x0F
    'q','w','e','r','t','y','u','i','o','p','[',']',0,0,      // 0x10-0x1F
    'a','s','d','f','g','h','j','k','l',';',39,'`',0,'\\',    // 0x20-0x2F
    'z','x','c','v','b','n','m',',','.','/',0,' ',0,0,0,0,      // 0x30-0x3F
};

static const unsigned char kbd_us_shift[64] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+',0,0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,' ',0,0,0,0,
};

void irq_handler(int irq){
    if (irq == 0){
        timer_tick();
    }
    if (irq == 1){
        // клава
        uint8_t sc = inb(0x60);
        int c = scancode_to_char(sc);
        if (c == SPECIAL_ENTER){
            if (input_len > 0){
                string_copy(history[history_index], inputbuf, 256);
                history_index = (history_index + 1) % 10;
                if (history_count < 10) history_count++;
                history_pos = -1;
            }
            input_ready = 1;
        } else if (c == SPECIAL_ARROW_UP){
            if (history_count > 0){
                history_pos++;
                if (history_pos >= history_count) history_pos = history_count -1;
                int real_idx = (history_index - 1 - history_pos + 10) % 10;
                load_history_line(real_idx);
            }
        } else if (c == SPECIAL_ARROW_DOWN){
            if (history_pos > 0){
                history_pos--;
                int real_idx = (history_index - 1 - history_pos + 10) % 10;
                load_history_line(real_idx);
            } else if (history_pos == 0){
                history_pos--;
                clear_input_line();
            }
        } else if (c == SPECIAL_BACKSPACE){
            if (input_len > 0){
                input_len--;
                if (g_cursor_col == 0 && g_cursor_row > 0){
                    g_cursor_row--;
                    g_cursor_col = 79;
                } else if (g_cursor_col > 0){
                    g_cursor_col--;
                }
                VIDEO_MEM[(g_cursor_row *80) + g_cursor_col] = (crnt_attr << 8) | ' ';
                update_cursor();
            }
        } else if (c == SPECIAL_TAB){
            find_and_complete();
        } else if (c == SPECIAL_CAPS_LOCK){
            ruskbd = !ruskbd;
            if (ruskbd){
                rus_print("Рус: ВКЛ");   // "Рус: ВКЛ" (CP866)
                print_char('\n');
                redraw_input();
                clear_input_line();
            } else {
                rus_print("Рус: ВЫКЛ"); // "Рус: ВЫКЛ" (CP866)
                print_char('\n');
                redraw_input();
                clear_input_line();
            }
        } else if (c == ' '){
            if (input_len < 255) inputbuf[input_len++] = ' ';
            print_char(' ');
        } else if (c > 0){
            if (input_len < 255){
                inputbuf[input_len++] = c;
                print_char(c);
            }
        }
    }
    // потверждение прерываний для PIC
    // EOI
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

int scancode_to_char(uint8_t sc){
    //  фильтр опускания
    if (sc & 0x80){
        uint8_t make = sc & 0x7F;
        if (make == 0x2A || make == 0x36) kbd_modifiers &= ~MOD_SHIFT;
        if (make == 0x1D) kbd_modifiers &= ~MOD_CTRL;
        if (make == 0x38) kbd_modifiers &= ~MOD_ALT;
        return 0;
    }

    switch (sc){
        case 0x2A: case 0x36: kbd_modifiers |= MOD_SHIFT; return 0;
        case 0x1D: kbd_modifiers |= MOD_CTRL; return 0;
        case 0x38: kbd_modifiers |= MOD_ALT; return 0;

        case 0x0E: return SPECIAL_BACKSPACE;
        case 0x1C: return SPECIAL_ENTER;
        case 0x1:  return SPECIAL_ESC;
        case 0x0F: return SPECIAL_TAB;
        case 0x3A: return SPECIAL_CAPS_LOCK;
        case 0x45: return SPECIAL_NUM_LOCK;
        case 0x48: return SPECIAL_ARROW_UP;
        case 0x50: return SPECIAL_ARROW_DOWN;
        case 0x39: return ' ';
    }

    if (ruskbd){
        if (kbd_modifiers & MOD_SHIFT){
            if (sc < sizeof(kbd_ru_shift)) return kbd_ru_shift[sc];
        } else {
            if (sc < sizeof(kbd_ru_normal)) return kbd_ru_normal[sc];
        }
    } else {
        if (kbd_modifiers & MOD_SHIFT){
            if (sc < sizeof(kbd_us_shift)) return kbd_us_shift[sc];
        } else {
            if (sc < sizeof(kbd_us_normal)) return kbd_us_normal[sc];
        }
    }

    return 0;
}
