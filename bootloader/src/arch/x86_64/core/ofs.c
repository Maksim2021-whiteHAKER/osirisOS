// x86_64/core/ofs.c
// ОФС - Осирис Файловая Система. Этап 3: чтение, запись, удаление.
// Файлы лежат в области данных ПОДРЯД от своего стартового блока
// (без цепочек - как FAT, только проще). Имена хранятся на диске в UTF-8,
// на экране и во вводе - CP866, конвертация через console.c.
// Запись: свободное место ищем first-fit по битовой карте, потом пишем
// данные, обновляем битмап и одну запись таблицы прямо на диск.
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

// u32 в байты little-endian
static void wr32(uint8_t *p, uint32_t v){
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
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

/* ===== запись и удаление (этапы 2-3) ===== */

// аллокатор: найти подряд need свободных блоков (first-fit);
// возвращает номер первого блока или (uint32_t)-1, если места нет
static uint32_t ofs_alloc(uint32_t need){
    uint32_t run = 0;
    for (uint32_t b = 0; b < ofs_total_blocks; b++){
        if (ofs_bitmap[b >> 3] & (0x80 >> (b & 7))){ run = 0; continue; }
        run++;
        if (run == need) return b + 1 - need;
    }
    return (uint32_t)-1;
}

// пометить count блоков с start в ОЗУ: val=1 заняты, val=0 свободны
static void ofs_mark(uint32_t start, uint32_t count, int val){
    for (uint32_t b = start; b < start + count; b++){
        if (val) ofs_bitmap[b >> 3] |=  (0x80 >> (b & 7));
        else     ofs_bitmap[b >> 3] &= ~(0x80 >> (b & 7));
    }
}

// сбросить на диск сектора битовой карты, покрывающие [start, start+count)
// бит b лежит в байте b>>3, байт - в секторе с номером b>>12
static int ofs_bitmap_flush(uint32_t start, uint32_t count){
    uint32_t s0 = start >> 12;
    uint32_t s1 = (start + count - 1) >> 12;
    for (uint32_t s = s0; s <= s1; s++){
        if (ata_write_sector(OFS_BITMAP_LBA + s, ofs_bitmap + s * OFS_BLOCK_SIZE) != 0) return -1;
    }
    return 0;
}

// создать файл или перезаписать существующий.
// name - в CP866 (как с клавиатуры), text - содержимое байтами как есть
// (CP866 - так же его потом печатает читать/cat).
// возвращает размер или код ошибки (минус):
//   -1 ОФС не готова / таблица полна / нет места / ошибка диска
//   -2 имя не подходит (длиннее 47 байт UTF-8 или не сконвертировалось)
//   -3 пустое имя
int ofs_write(const char *name, const char *text){
    if (!ofs_ready) return -1;

    // имя: CP866 -> UTF-8. Буфер с запасом: 63 символа ввода * 3 байта
    char name_u8[224];
    int src_len = 0, chars;
    while (name[src_len] != '\0') src_len++;
    if (src_len == 0) return -3;
    chars = cp866_to_utf8_str(name, name_u8, (int)sizeof(name_u8));
    if (chars != src_len) return -2;

    int name_len = 0;
    while (name_u8[name_len] != '\0') name_len++;
    if (name_len > OFS_NAME_SIZE - 1) return -2;   // на диске 48 байт на имя

    uint32_t size = 0;
    while (text[size] != '\0') size++;
    uint32_t blocks = (size + OFS_BLOCK_SIZE - 1) / OFS_BLOCK_SIZE;

    // один проход по таблице: свободная запись + существующий файл с этим именем
    int free_idx = -1, same_idx = -1;
    for (int i = 0; i < OFS_TABLE_ENTRIES; i++){
        const uint8_t *e = ofs_table + i * OFS_ENTRY_SIZE;
        if (e[56] != OFS_FLAG_FILE){
            if (free_idx < 0) free_idx = i;
            continue;
        }
        char disk_cp[OFS_NAME_SIZE];
        utf8_to_cp866_str((const char*)e, disk_cp, OFS_NAME_SIZE);
        if (ofs_strcmp(disk_cp, name) == 0) same_idx = i;
    }
    if (free_idx < 0 && same_idx < 0) return -1;   // таблица полна

    // при перезаписи освобождаем старые блоки ДО поиска нового места
    // (в ОЗУ; если места потом не найдём - пометим обратно)
    uint32_t old_start = 0, old_blocks = 0;
    if (same_idx >= 0){
        const uint8_t *e = ofs_table + same_idx * OFS_ENTRY_SIZE;
        old_start  = rd32(e + 52);
        old_blocks = (rd32(e + 48) + OFS_BLOCK_SIZE - 1) / OFS_BLOCK_SIZE;
        ofs_mark(old_start, old_blocks, 0);
    }

    // место под новые данные
    uint32_t start = 0;
    if (blocks > 0){
        start = ofs_alloc(blocks);
        if (start == (uint32_t)-1){
            if (same_idx >= 0) ofs_mark(old_start, old_blocks, 1);
            return -1;
        }
        ofs_mark(start, blocks, 1);
    }

    // данные посекторно; последний неполный блок дополняем нулями
    static uint8_t sec[OFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < blocks; i++){
        uint32_t n = size - i * OFS_BLOCK_SIZE;
        if (n > OFS_BLOCK_SIZE) n = OFS_BLOCK_SIZE;
        for (uint32_t j = 0; j < OFS_BLOCK_SIZE; j++)
            sec[j] = (j < n) ? (uint8_t)text[i * OFS_BLOCK_SIZE + j] : 0;
        if (ata_write_sector(OFS_DATA_LBA + start + i, sec) != 0) return -1;
    }

    // запись таблицы: имя UTF-8 (нулями до 48), размер, старт, флаг
    int idx = (same_idx >= 0) ? same_idx : free_idx;
    uint8_t *e = ofs_table + idx * OFS_ENTRY_SIZE;
    for (int j = 0; j < OFS_NAME_SIZE; j++)
        e[j] = (j < name_len) ? (uint8_t)name_u8[j] : 0;
    wr32(e + 48, size);
    wr32(e + 52, start);
    e[56] = OFS_FLAG_FILE;

    // на диск: сектор таблицы с этой записью + тронутые сектора битмапа
    int tsec = (idx * OFS_ENTRY_SIZE) / OFS_BLOCK_SIZE;
    if (ata_write_sector(OFS_TABLE_LBA + tsec, ofs_table + tsec * OFS_BLOCK_SIZE) != 0) return -1;

    uint32_t bmin = start, bmax = start + blocks;
    if (old_blocks > 0){
        if (old_start < bmin) bmin = old_start;
        if (old_start + old_blocks > bmax) bmax = old_start + old_blocks;
    }
    if (bmax > bmin){
        if (ofs_bitmap_flush(bmin, bmax - bmin) != 0) return -1;
    }
    return (int)size;
}

// удалить файл по имени. name - в CP866 (как с клавиатуры).
// Порядок записи на диск: СНАЧАЛА таблица (файл исчезает), ПОТОМ битмап.
// Если питание пропадёт между ними - место останется помеченным занятым
// (утечка места), но чужие файлы не пострадают; обратный порядок дал бы
// запись, указывающую на уже свободные блоки - это хуже.
// возвращает размер удалённого файла или -1 (не готова / не найден)
int ofs_delete(const char *name){
    if (!ofs_ready) return -1;

    for (int i = 0; i < OFS_TABLE_ENTRIES; i++){
        uint8_t *e = ofs_table + i * OFS_ENTRY_SIZE;
        if (e[56] != OFS_FLAG_FILE) continue;

        char name_cp[OFS_NAME_SIZE];
        utf8_to_cp866_str((const char*)e, name_cp, OFS_NAME_SIZE);
        if (ofs_strcmp(name_cp, name) != 0) continue;

        uint32_t size    = rd32(e + 48);
        uint32_t start   = rd32(e + 52);
        uint32_t nblocks = (size + OFS_BLOCK_SIZE - 1) / OFS_BLOCK_SIZE;

        // 1) убрать запись из таблицы (ОЗУ + сектор на диске)
        for (int j = 0; j < OFS_ENTRY_SIZE; j++) e[j] = 0;
        int tsec = (i * OFS_ENTRY_SIZE) / OFS_BLOCK_SIZE;
        if (ata_write_sector(OFS_TABLE_LBA + tsec, ofs_table + tsec * OFS_BLOCK_SIZE) != 0) return -1;

        // 2) освободить блоки (ОЗУ + тронутые сектора битмапа на диск)
        if (nblocks > 0){
            ofs_mark(start, nblocks, 0);
            if (ofs_bitmap_flush(start, nblocks) != 0) return -1;
        }
        return (int)size;
    }
    return -1;
}
