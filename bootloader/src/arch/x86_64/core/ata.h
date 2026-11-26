// x86_64/core/ata.h
#ifndef ATA_H
#define ATA_H

#include "../lib/common.h"

// чтение одного сектора (512 байт) с жёсткого диска
// primary master, PIO через порты 0x1F0-0x1F7, режим LBA
// lba - номер сектора, buf - куда положить 512 байт
// возвращает 0 - успех, -1 - ошибка или таймаут
int ata_read_sector(uint32_t lba, uint8_t *buf);

#endif
