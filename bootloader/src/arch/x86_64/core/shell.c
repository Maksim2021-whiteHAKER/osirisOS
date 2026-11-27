// x86_64/core/shell.c
#include "../lib/common.h"
#include "console.h"
#include "idt.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "ofs.h"

/* ===== КОМАНДЫ =====
   Верхний ряд — латиница, нижний — русские синонимы (в исходнике UTF-8).
   Русский синоним = та же команда со сдвигом +CMD_COUNT_EN. */
static const char* commands[] = {
    "ver", "clr", "help", "echo", "uptime", "ls", "cat", "write", "delete", "append", "info", "copy", "rename",
    "версия", "очистить", "помощь", "эхо", "время_работы", "дир", "читать", "записать", "удалить", "дополнить", "инфо",
    "копировать", "переименовать",
    0
};

#define CMD_VER    0
#define CMD_CLR    1
#define CMD_HELP   2
#define CMD_ECHO   3
#define CMD_UPTIME 4
#define CMD_LS     5
#define CMD_CAT    6
#define CMD_WRITE  7
#define CMD_DELETE 8
#define CMD_APPEND 9
#define CMD_INFO   10
#define CMD_COPY   11
#define CMD_RENAME 12
#define CMD_COUNT_EN 13     /* сколько команд в верхнем ряду */
#define CMD_MAX      26     /* всего команд (лат + рус) */
#define CMD_BUF      16     /* размер CP866-копии (до 15 байт) */

/* CP866-копии команд: ввод с клавиатуры приходит в CP866,
   сравнивать удобнее там же. Заполняются один раз при старте. */
static char cmd_cp866[CMD_MAX][CMD_BUF];
static int  cmd_total = 0;

static void init_commands_cp866(void){
    cmd_total = 0;
    while (commands[cmd_total] != 0 && cmd_total < CMD_MAX){
        utf8_to_cp866_str(commands[cmd_total], cmd_cp866[cmd_total], CMD_BUF);
        cmd_total++;
    }
}

static int int_stringcmp(const char* a, const char* b){
    while (*a && *b && *a == *b) {a++; b++;}
    return *a - *b;
}

int start_with(const char* str, const char* prefix){
    while (*prefix){
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

/* точное совпадение ввода с командой; возвращает канонический номер 0..10 или -1 */
static int find_command(const char* input){
    for (int i = 0; i < cmd_total; i++){
        if (int_stringcmp(input, cmd_cp866[i]) == 0){
            return (i < CMD_COUNT_EN) ? i : i - CMD_COUNT_EN;
        }
    }
    return -1;
}

/* длина префикса echo/эхо во вводе (0 = это не echo) */
static int echo_prefix_len(const char* input){
    if (start_with(input, "echo")) return 4;
    if (start_with(input, cmd_cp866[CMD_ECHO + CMD_COUNT_EN])) return 3;
    return 0;
}

/* длина префикса cat/читать во вводе (0 = это не cat) */
static int cat_prefix_len(const char* input){
    if (start_with(input, "cat")) return 3;
    if (start_with(input, cmd_cp866[CMD_CAT + CMD_COUNT_EN])) return 6;
    return 0;
}

/* длина префикса write/записать во вводе (0 = это не write) */
static int write_prefix_len(const char* input){
    if (start_with(input, "write")) return 5;
    if (start_with(input, cmd_cp866[CMD_WRITE + CMD_COUNT_EN])) return 8;
    return 0;
}

/* длина префикса delete/удалить во вводе (0 = это не delete) */
static int delete_prefix_len(const char* input){
    if (start_with(input, "delete")) return 6;
    if (start_with(input, cmd_cp866[CMD_DELETE + CMD_COUNT_EN])) return 7;
    return 0;
}

/* длина префикса copy/копировать во вводе (0 = это не copy) */
static int copy_prefix_len(const char* input){
    if (start_with(input, "copy")) return 4;
    if (start_with(input, cmd_cp866[CMD_COPY + CMD_COUNT_EN])) return 10;
    return 0;
}
/* длина префикса rename/переименовать во вводе (0 = это не rename) */
static int rename_prefix_len(const char* input){
    if (start_with(input, "rename")) return 6;
    if (start_with(input, cmd_cp866[CMD_RENAME + CMD_COUNT_EN])) return 13;
    return 0;
}

/* длина префикса append/дополнить во вводе (0 = это не append) */
static int append_prefix_len(const char* input){
    if (start_with(input, "append")) return 6;
    if (start_with(input, cmd_cp866[CMD_APPEND + CMD_COUNT_EN])) return 9;
    return 0;
}

/* длина префикса info/инфо во вводе (0 = это не info) */
static int info_prefix_len(const char* input){
    if (start_with(input, "info")) return 4;
    if (start_with(input, cmd_cp866[CMD_INFO + CMD_COUNT_EN])) return 4;
    return 0;
}

int find_and_complete(){
    inputbuf[input_len] = '\0';
    int match_count = 0;
    int match_last_index = -1;
    for (int i = 0; i < cmd_total; i++){
        if (start_with(cmd_cp866[i], inputbuf)){
            match_count++;
            match_last_index = i;
        }
    }
    if (match_count == 0) return 0;

    if (match_count == 1){
        string_copy(inputbuf, cmd_cp866[match_last_index], 256);
        input_len = 0;
        while (inputbuf[input_len] != '\0') input_len++;
        redraw_input();
    } else if (match_count > 1){
        print_string("\n");
        for (int i = 0; i < cmd_total; i++){
            if (start_with(cmd_cp866[i], inputbuf)){
                rus_print(commands[i]);       /* UTF-8 оригинал — через конвертер */
                print_string("  ");
            }
        }
        print_string("\n");
        redraw_input();
    }
    return 0;
}

/* разобрать "имя1 имя2" после префикса; 1 = оба имени прочитаны */
static int parse_two_names(const char* in, int len, int offset, char* n1, char* n2){
    int a = 0, b = 0;
    while (offset < len && in[offset] == ' ') offset++;
    while (offset < len && in[offset] != ' ' && a < 63) n1[a++] = in[offset++];
    n1[a] = '\0';
    while (offset < len && in[offset] == ' ') offset++;
    while (offset < len && in[offset] != ' ' && b < 63) n2[b++] = in[offset++];
    n2[b] = '\0';
    return (a > 0 && b > 0) ? 1 : 0;
}

// точка входа shell_main
void shell_main() {
    init_commands_cp866();        /* русские команды -> CP866-копии, до sti! */

    clear_screen();
    set_color(FG_WHITE, BLACK_ATTR);
    rus_print("Ядро Осирис v1.0.9 (C+asm)\n");
    printf("Setting up IDT...\n");
    init_idt();
    set_idt_gate(0x20, (uint32_t)irq0, 0x08, 0x8E);
    set_idt_gate(0x21, (uint32_t)irq1, 0x08, 0x8E);
    pic_remap();
    printf("IDT and PIC configured Enabling interrupts...\n");

    /* подключаем ОФС (чтение суперблока и таблицы с диска через ATA) */
    if (ofs_mount() == 0){
        set_color(FG_CYAN, BLACK_ATTR);
        rus_print("ОФС: подключена\n");
        reset_color();
    } else {
        set_color(FG_RED, BLACK_ATTR);
        rus_print("ОФС: не найдена (mkofs.py собирал образ?)\n");
        reset_color();
    }

    set_color(FG_CYAN, BLACK_ATTR);
    print_char('>');
    reset_color();
    init_timer(100);
    asm volatile("sti");

    for (;;) {
        if (input_ready){
            input_ready = 0;
            inputbuf[input_len] = '\0';
            print_string("\n");
            if (input_len > 0){
                int cmd = find_command(inputbuf);
                int ep = echo_prefix_len(inputbuf);
                int cp = cat_prefix_len(inputbuf);
                int wp = write_prefix_len(inputbuf);
                int dp = delete_prefix_len(inputbuf);
                int ap = append_prefix_len(inputbuf);
                int ip = info_prefix_len(inputbuf);
                int kyp = copy_prefix_len(inputbuf);
                int rp = rename_prefix_len(inputbuf);
                uint32_t isize = 0, istart = 0, iblocks = 0;

                if (ep > 0){                        /* echo / эхо */
                    int offset = ep;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    print_string(inputbuf + offset);
                    print_string("\n");
                } else if (cmd == CMD_LS){          /* ls / дир */
                    ofs_list();
                } else if (cmd == CMD_CAT && cp == 0){   /* cat / читать без имени */
                    rus_print("Использование: читать имя_файла\n");
                } else if (cp > 0){                 /* cat / читать + имя файла */
                    int offset = cp;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    if (offset >= input_len){
                        rus_print("Использование: читать имя_файла\n");
                    } else if (ofs_read(inputbuf + offset) < 0){
                        set_color(FG_RED, BLACK_ATTR);
                        rus_print("файл не найден: ");
                        reset_color();
                        print_string(inputbuf + offset);
                        print_string("\n");
                    } else {
                        print_string("\n");
                    }
                } else if (cmd == CMD_WRITE && wp == 0){  /* write / записать без аргументов */
                    rus_print("Использование: записать имя_файла текст\n");
                } else if (wp > 0){                  /* write / записать + имя + текст */
                    int offset = wp;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    char wname[64];
                    int nl = 0;
                    while (offset < input_len && inputbuf[offset] != ' ' && nl < 63)
                        wname[nl++] = inputbuf[offset++];
                    wname[nl] = '\0';
                    while (offset < input_len && inputbuf[offset] == ' ') offset++;
                    if (nl == 0 || offset >= input_len){
                        rus_print("Использование: записать имя_файла текст\n");
                    } else {
                        int wres = ofs_write(wname, inputbuf + offset);
                        if (wres >= 0){
                            set_color(FG_CYAN, BLACK_ATTR);
                            rus_print("записано ");
                            reset_color();
                            print_uint((unsigned int)wres);
                            rus_print(" байт в ");
                            print_string(wname);
                            print_string("\n");
                        } else if (wres == -2){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("имя не подходит (до 23 кириллических букв)\n");
                            reset_color();
                        } else if (wres == -3){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("пустое имя файла\n");
                            reset_color();
                        } else {
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("не удалось записать: нет места или ОФС не готова\n");
                            reset_color();
                        }
                    }
                } else if (cmd == CMD_DELETE && dp == 0){  /* delete / удалить без имени */
                    rus_print("Использование: удалить имя_файла\n");
                } else if (dp > 0){                  /* delete / удалить + имя */
                    int offset = dp;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    if (offset >= input_len){
                        rus_print("Использование: удалить имя_файла\n");
                    } else {
                        int dres = ofs_delete(inputbuf + offset);
                        if (dres >= 0){
                            set_color(FG_CYAN, BLACK_ATTR);
                            rus_print("удалено: ");
                            reset_color();
                            print_string(inputbuf + offset);
                            rus_print(" (");
                            print_uint((unsigned int)dres);
                            rus_print(" байт)\n");
                        } else {
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("файл не найден: ");
                            reset_color();
                            print_string(inputbuf + offset);
                            print_string("\n");
                        }
                    }
                } else if (cmd == CMD_APPEND && ap == 0){  /* append / дополнить без имени */
                    rus_print("Использование: дополнить имя_файла текст\n");
                } else if (ap > 0){                  /* append / дополнить + имя + текст */
                    int offset = ap;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    char aname[64];
                    int nl = 0;
                    while (offset < input_len && inputbuf[offset] != ' ' && nl < 63)
                        aname[nl++] = inputbuf[offset++];
                    aname[nl] = '\0';
                    while (offset < input_len && inputbuf[offset] == ' ') offset++;
                    if (nl == 0 || offset >= input_len){
                        rus_print("Использование: дополнить имя_файла текст\n");
                    } else {
                        int added = 0;
                        int ares = ofs_append(aname, inputbuf + offset, &added);
                        if (ares >= 0){
                            set_color(FG_CYAN, BLACK_ATTR);
                            rus_print("дописано ");
                            reset_color();
                            print_uint((unsigned int)added);
                            rus_print(" байт, размер файла ");
                            print_uint((unsigned int)ares);
                            rus_print(" байт\n");
                        } else if (ares == -2){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("имя не подходит (до 23 кириллических букв)\n");
                            reset_color();
                        } else if (ares == -3){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("пустое имя файла\n");
                            reset_color();
                        } else {
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("не удалось дописать: нет места или ОФС не готова\n");
                            reset_color();
                        }
                    }
                } else if (ip > 0){                  /* info / инфо: без имени - сводка по ОФС */
                    int offset = ip;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    if (offset >= input_len){
                        uint32_t total, used, files;
                        ofs_stats(&total, &used, &files);
                        rus_print("ОФС: блоков данных ");
                        print_uint(total);
                        rus_print(", занято ");
                        print_uint(used);
                        rus_print(", свободно ");
                        print_uint(total - used);
                        rus_print(", файлов ");
                        print_uint(files);
                        print_string("\n");
                        rus_print("(суперблок LBA 256, битмап 257-264, таблица 265-280, данные 281+)\n");
                    } else if (ofs_info(inputbuf + offset, &isize, &istart, &iblocks) == 0){
                        print_string(inputbuf + offset);
                        rus_print(": ");
                        print_uint(isize);
                        rus_print(" байт, старт блок ");
                        print_uint(istart);
                        rus_print(", блоков ");
                        print_uint(iblocks);
                        rus_print(" (сектора ");
                        print_uint(OFS_DATA_LBA + istart);
                        print_string("-");
                        print_uint(OFS_DATA_LBA + istart + (iblocks ? iblocks - 1 : 0));
                        rus_print(")\n");
                    } else {
                        set_color(FG_RED, BLACK_ATTR);
                        rus_print("файл не найден: ");
                        reset_color();
                        print_string(inputbuf + offset);
                        print_string("\n");
                    }
                } else if (cmd == CMD_COPY && kyp == 0){   /* copy / копировать без аргументов */
                    rus_print("Использование: копировать старое_имя новое_имя\n");
                } else if (kyp > 0){                        /* copy / копировать старое новое */
                    int offset = kyp;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    char name1[64], name2[64];
                    if (!parse_two_names(inputbuf, input_len, offset, name1, name2)){
                        rus_print("Использование: копировать старое_имя новое_имя\n");
                    } else {
                        int cres = ofs_copy(name1, name2);
                        if (cres >= 0){
                            set_color(FG_CYAN, BLACK_ATTR);
                            rus_print("скопировано ");
                            reset_color();
                            print_uint((unsigned int)cres);
                            rus_print(" байт: ");
                            print_string(name1);
                            print_string(" -> ");
                            print_string(name2);
                            print_string("\n");
                        } else if (cres == -2){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("новое имя не подходит (до 23 кириллических букв)\n");
                            reset_color();
                        } else if (cres == -3){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("пустое имя файла\n");
                            reset_color();
                        } else if (cres == -5){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("имя занято: ");
                            reset_color();
                            print_string(name2);
                            print_string("\n");
                        } else if (cres == -4){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("файл не найден: ");
                            reset_color();
                            print_string(name1);
                            print_string("\n");
                        } else {
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("не удалось скопировать: нет места или ОФС не готова\n");
                            reset_color();
                        }
                    }
                } else if (cmd == CMD_RENAME && rp == 0){  /* rename / переименовать без аргументов */
                    rus_print("Использование: переименовать старое_имя новое_имя\n");
                } else if (rp > 0){                        /* rename / переименовать старое новое */
                    int offset = rp;
                    if (input_len > offset && inputbuf[offset] == ' ') offset++;
                    char name1[64], name2[64];
                    if (!parse_two_names(inputbuf, input_len, offset, name1, name2)){
                        rus_print("Использование: переименовать старое_имя новое_имя\n");
                    } else {
                        int rres = ofs_rename(name1, name2);
                        if (rres == 0){
                            set_color(FG_CYAN, BLACK_ATTR);
                            rus_print("переименовано: ");
                            reset_color();
                            print_string(name1);
                            print_string(" -> ");
                            print_string(name2);
                            print_string("\n");
                        } else if (rres == -2){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("новое имя не подходит (до 23 кириллических букв)\n");
                            reset_color();
                        } else if (rres == -3){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("пустое имя файла\n");
                            reset_color();
                        } else if (rres == -5){
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("имя занято: ");
                            reset_color();
                            print_string(name2);
                            print_string("\n");
                        } else {
                            set_color(FG_RED, BLACK_ATTR);
                            rus_print("файл не найден: ");
                            reset_color();
                            print_string(name1);
                            print_string("\n");
                        }
                    }
                } else if (cmd == CMD_VER){         /* ver / версия */
                    rus_print("Ядро Осирис v1.0.9 (C+asm)\n");
                } else if (cmd == CMD_CLR){         /* clr / очистить */
                    clear_screen();
                } else if (cmd == CMD_HELP){        /* help / помощь */
                    print_string("Commands: clr, help, ver, echo, uptime, ls, cat, write, delete, append, info, copy, rename\n");
                    rus_print("Команды: версия, очистить, помощь, эхо, время_работы, дир, читать, записать, удалить, дополнить, инфо, копировать, переименовать\n");
                    rus_print("(Рус/Англ: Caps Lock)\n");
                } else if (cmd == CMD_UPTIME){      /* uptime / время_работы */
                    unsigned long seconds = get_ticks() / 100;
                    print_string("Uptime: ");
                    print_uint(seconds);
                    print_string(" sec.\n");
                } else {
                    set_color(FG_RED, BLACK_ATTR);
                    rus_print("Неизвестная команда: ");
                    reset_color();
                    print_string(inputbuf);
                    print_string("\n");
                }
            }
            input_len = 0;
            set_color(FG_CYAN, BLACK_ATTR);
            print_string(">");
            reset_color();
        }
        asm volatile("hlt");
    }
}
