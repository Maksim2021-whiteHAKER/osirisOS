// x86_64/core/ata.c
// Драйвер жёсткого диска: primary master, PIO, режим LBA.
// В protected mode BIOS недоступен, поэтому работаем через порты контроллера.
// Чтение и запись по одному сектору.
#include "../lib/common.h"
#include "ata.h"

// 16-битное чтение порта: в common.h есть только 8-битный inb,
// а регистр данных ATA (0x1F0) читается словами по 2 байта
static inline uint16_t ata_inw(uint16_t port){
    uint16_t ret;
    asm volatile ("inw %w1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

// 16-битная запись порта: в регистр данных ATA слово пишется так же
static inline void ata_outw(uint16_t port, uint16_t val){
    asm volatile ("outw %w0, %w1" :: "a"(val), "d"(port));
}

// порты контроллера ATA (primary master)
#define ATA_DATA       0x1F0  // данные: 256 слов = 512 байт
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2  // число секторов
#define ATA_LBA_LO     0x1F3  // LBA биты 0-7
#define ATA_LBA_MID    0x1F4  // LBA биты 8-15
#define ATA_LBA_HI     0x1F5  // LBA биты 16-23
#define ATA_DRIVE      0x1F6  // диск + биты 24-27 LBA + флаг LBA (0xE0)
#define ATA_STATUS     0x1F7  // чтение статуса
#define ATA_COMMAND    0x1F7  // запись команды
#define ATA_CONTROL    0x3F6  // альтернативный статус

#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_FLUSH     0xE7  // сброс кэша контроллера на диск
#define ATA_STAT_BSY      0x80  // контроллер занят
#define ATA_STAT_DRQ      0x08  // данные готовы к чтению/приёму
#define ATA_STAT_ERR      0x01  // ошибка команды

#define ATA_TIMEOUT 1000000      // предел ожидания (защита от зависания)

// пауза ~400 наносекунд: 4 чтения альтернативного статуса
static void ata_delay(void){
    for (int i = 0; i < 4; i++) inb(ATA_CONTROL);
}

int ata_read_sector(uint32_t lba, uint8_t *buf){
    uint16_t *out = (uint16_t*)buf;
    int timeout;

    // выбрать диск master в режиме LBA + старшие 4 бита LBA
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    // дождаться, пока контроллер освободится
    timeout = ATA_TIMEOUT;
    while (inb(ATA_STATUS) & ATA_STAT_BSY){
        if (--timeout == 0) return -1;
    }

    // передать параметры: 1 сектор, LBA 0-27
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    // ждать готовности данных: BSY снялся, DRQ встал
    timeout = ATA_TIMEOUT;
    for (;;){
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_STAT_ERR) return -1;
        if (!(st & ATA_STAT_BSY) && (st & ATA_STAT_DRQ)) break;
        if (--timeout == 0) return -1;
    }

    // прочитать 256 слов = 512 байт
    for (int i = 0; i < 256; i++){
        out[i] = ata_inw(ATA_DATA);
    }
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf){
    const uint16_t *in = (const uint16_t*)buf;
    int timeout;

    // выбрать диск master в режиме LBA + старшие 4 бита LBA
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    // дождаться, пока контроллер освободится
    timeout = ATA_TIMEOUT;
    while (inb(ATA_STATUS) & ATA_STAT_BSY){
        if (--timeout == 0) return -1;
    }

    // передать параметры: 1 сектор, LBA 0-27
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    // ждать готовности принять данные: BSY снялся, DRQ встал
    timeout = ATA_TIMEOUT;
    for (;;){
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_STAT_ERR) return -1;
        if (!(st & ATA_STAT_BSY) && (st & ATA_STAT_DRQ)) break;
        if (--timeout == 0) return -1;
    }

    // отдать 256 слов = 512 байт
    for (int i = 0; i < 256; i++){
        ata_outw(ATA_DATA, in[i]);
    }

    // FLUSH CACHE: приказать контроллеру сбросить буфер на диск и дождаться
    timeout = ATA_TIMEOUT;
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    while (inb(ATA_STATUS) & ATA_STAT_BSY){
        if (--timeout == 0) return -1;
    }
    if (inb(ATA_STATUS) & ATA_STAT_ERR) return -1;
    return 0;
}
