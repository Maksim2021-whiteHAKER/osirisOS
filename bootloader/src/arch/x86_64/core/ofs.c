// x86_64/core/ofs.c
// ОФС - Осирис Файловая Система. Первый этап: только чтение.
// Файлы лежат в области данных ПОДРЯД от своего стартового блока
// (без цепочек - как FAT, только проще). Имена хранятся на диске в UTF-8,
// на экране и во вводе - CP866, конвертация через console.c.
#include "../lib/common.h"
#include "console.h"
#include "ata.h"
#include "ofs.h"

static uint8_t ofs_super[512];       // суперблок
static uint8_t ofs_bitmap[8 * 512];  // битовая карта (8 секторов)
static uint8_t ofs_table[16 * 512];  // таблица файлов целиком (8 КиБ)
static int ofs_ready = 0;
static uint32_t ofs_total_blocks = 0; // размер области данных из суперблока

// u32 из байтов little-endian (без выравнивания и packed struct)
static uint32_t rd32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// сравнение строк как в shell: до нуля, посимвольно
static int ofs_strcmp(const char *a, const char *b){
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

int ofs_mount(void){
    if (ata_read_sector(OFS_SUPER_LBA, ofs_super) != 0) return -1;

    // магия "OFS1" и версия 1
    if (ofs_super[0] != 'O' || ofs_super[1] != 'F' ||
        ofs_super[2] != 'S' || ofs_super[3] != '1') return -1;
    if (ofs_super[4] != 1) return -1;

    ofs_total_blocks = rd32(ofs_super + 13);

    // битовая карта: 8 секторов
    for (int s = 0; s < 8; s++){
        if (ata_read_sector(OFS_BITMAP_LBA + s, ofs_bitmap + s * 512) != 0) return -1;
    }
    // таблица файлов: 16 секторов
    for (int s = 0; s < 16; s++){
        if (ata_read_sector(OFS_TABLE_LBA + s, ofs_table + s * 512) != 0) return -1;
    }
    ofs_ready = 1;
    return 0;
}

void ofs_list(void){
    if (!ofs_ready){ rus_print("ОФС не подключена\n"); return; }

    int count = 0;
    for (int i = 0; i < OFS_TABLE_ENTRIES; i++){
        const uint8_t *e = ofs_table + i * OFS_ENTRY_SIZE;
        if (e[56] != OFS_FLAG_FILE) continue;

        // имя на диске UTF-8 -> на экран CP866
        char name_cp[OFS_NAME_SIZE];
        utf8_to_cp866_str((const char*)e, name_cp, OFS_NAME_SIZE);

        int len = 0;
        while (name_cp[len] != '\0') len++;
        print_string(name_cp);
        for (int pad = len; pad < 26; pad++) print_char(' ');  // ровная колонка

        print_uint(rd32(e + 48));
        rus_print(" байт\n");
        count++;
    }
    if (count == 0){
        rus_print("(пусто - файлов нет)\n");
    } else {
        print_uint(count);
        rus_print(" файл(ов), свободно: ");
        print_uint(ofs_free_blocks());
        rus_print(" бл.\n");
    }
}

int ofs_read(const char *name){
    if (!ofs_ready) return -1;

    static uint8_t buf[512];   // сектор данных

    for (int i = 0; i < OFS_TABLE_ENTRIES; i++){
        const uint8_t *e = ofs_table + i * OFS_ENTRY_SIZE;
        if (e[56] != OFS_FLAG_FILE) continue;

        char name_cp[OFS_NAME_SIZE];
        utf8_to_cp866_str((const char*)e, name_cp, OFS_NAME_SIZE);
        if (ofs_strcmp(name_cp, name) != 0) continue;

        // файл найден: читаем блоками, печатаем посимвольно
        uint32_t size  = rd32(e + 48);
        uint32_t start = rd32(e + 52);
        uint32_t done  = 0;

        while (done < size){
            if (ata_read_sector(OFS_DATA_LBA + start + done / OFS_BLOCK_SIZE, buf) != 0) return -1;
            uint32_t n = size - done;
            if (n > OFS_BLOCK_SIZE) n = OFS_BLOCK_SIZE;
            for (uint32_t j = 0; j < n; j++) print_char((char)buf[j]);
            done += n;
        }
        if (size == 0) rus_print("(файл пустой)\n");
        return (int)size;
    }
    return -1;   // файла с таким именем нет
}

uint32_t ofs_free_blocks(void){
    if (!ofs_ready) return 0;
    uint32_t free_n = 0;
    for (uint32_t b = 0; b < ofs_total_blocks; b++){
        if (!(ofs_bitmap[b >> 3] & (0x80 >> (b & 7)))) free_n++;
    }
    return free_n;
}
