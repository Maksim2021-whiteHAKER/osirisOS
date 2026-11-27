#!/usr/bin/env python3
# make_bad_ofs.py — испортить КОПИЮ образа ОФС для теста команды проверить (этап 7).
# Создаёт все четыре класса проблем, которые ищет ofs_check:
#   1) потерянные блоки: 500-503 заняты в битмапе, но ни одному файлу не принадлежат
#   2) битая запись:     «битый.txt» заявляет блоки 2000-2001, битмап про них не знает
#   3) двойной блок:     «двойник.txt» сидит на блоке 0 вместе с первым файлом образа
#   4) мусорная запись:  запись 3 с флагом «занято», но с пустым именем
# Запускать на СВЕЖЕСОБРАННОМ образе (mkofs.py), записи таблицы 1-3 должны быть свободны.
# Образ-оригинал не трогается: пишем в копию.
import shutil, struct, sys

SEC = 512
BITMAP = 257   # битмап, 8 секторов
TABLE  = 265   # таблица файлов, 16 секторов

src = sys.argv[1] if len(sys.argv) > 1 else 'disk.img'
dst = sys.argv[2] if len(sys.argv) > 2 else 'disk_bad.img'
shutil.copyfile(src, dst)
f = open(dst, 'r+b')

def bm_set(block, val):
    """поставить/снять бит блока block в битмапе на диске"""
    lba, off = BITMAP + (block >> 12), (block >> 3) & 511
    f.seek(lba * SEC + off)
    old = f.read(1)[0]
    bit = 0x80 >> (block & 7)
    f.seek(lba * SEC + off)
    f.write(bytes([(old | bit) if val else (old & ~bit)]))

def entry(i, name, size, start, flag):
    """перезаписать запись i таблицы целиком (48 имя + 4 размер + 4 старт + 1 флаг)"""
    lba, off = TABLE + (i * 64) // SEC, (i * 64) % SEC
    f.seek(lba * SEC + off)
    f.write(name.encode('utf-8').ljust(48, b'\x00')[:48] + struct.pack('<IIB', size, start, flag))

# 1) потерянные блоки
for b in range(500, 504):
    bm_set(b, 1)

# 2) битая запись: заявляет свободные блоки
entry(1, 'битый.txt', size=1000, start=2000, flag=1)

# 3) двойной блок: на блоке 0 уже сидит первый файл образа
entry(2, 'двойник.txt', size=51, start=0, flag=1)

# 4) мусорная запись: флаг занят, имени нет
entry(3, '', size=0, start=0, flag=1)

f.close()
print('готово:', dst)
print('ожидание при запуске проверить: файлов 4, потеряно 4 (500-503), битых 1, двойных 1, мусорных 1, итого 7')
