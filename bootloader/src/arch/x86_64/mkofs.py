#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mkofs.py - сборка ОФС (Осирис Файловая Система) внутри osiris.img.

Использование:
    python mkofs.py <образ.img> <папка с файлами> [--demo]
        --demo добавляет файл привет.txt с русским текстом (CP866)

    python mkofs.py selftest
        самопроверка: собирает тестовый образ в /tmp и проверяет чтение

Правила:
    - трогает ТОЛЬКО сектора 256..конец (зона ОФС);
    - сектора 0..255 (stage1, stage2, ядро) не изменяются - проверяется;
    - файлы кладутся в область данных ПОДРЯД, без цепочек;
    - имена файлов - UTF-8, до 47 байт (23 кириллические буквы).

Раскладка (посчитана ofs_math.py):
    суперблок:      LBA 256  (1 сектор)
    битовая карта:  LBA 257..264 (8 секторов)
    таблица файлов: LBA 265..280 (16 секторов, 128 записей по 64 байта)
    данные:         LBA 281..конец образа
"""
import os, struct, sys, tempfile

SEC = 512
OFS_START_LBA  = 256
SUPER_LBA      = 256
BITMAP_LBA     = 257
BITMAP_SECT    = 8
TABLE_LBA      = 265
TABLE_SECT     = 16
DATA_LBA       = 281
TABLE_ENTRIES  = 128
ENTRY_SIZE     = 64
NAME_SIZE      = 48
MAGIC   = b"OFS1"
VERSION = 1
LABEL   = b"OSIRIS-OFS"
FLAG_FILE = 1


def pack_super(total_blocks):
    # магия(4) версия(1) размер_блока(4) LBA_данных(4) всего_блоков(4)
    # записей_таблицы(4) метка(16), остальное - нули
    s = struct.pack("<4sBIIII16s", MAGIC, VERSION, SEC, DATA_LBA,
                    total_blocks, TABLE_ENTRIES, LABEL)
    return s + b"\x00" * (SEC - len(s))


def pack_entry(name_utf8, size, start, flags):
    nm = name_utf8.encode("utf-8")[:NAME_SIZE - 1]  # до 47 байт
    return struct.pack("<48sIIB7x", nm, size, start, flags)


def build(img_path, fsroot, demo=False):
    img = open(img_path, "r+b")
    img.seek(0, 2)
    img_bytes = img.tell()
    img_sectors = img_bytes // SEC
    data_blocks = img_sectors - DATA_LBA
    if data_blocks <= 0:
        sys.exit(f"[ОШИБКА] образ слишком мал: {img_sectors} секторов")
    print(f"образ: {img_path} = {img_sectors} секторов, данных: {data_blocks} блоков")

    # --- сохранить первые 256 секторов для проверки целостности ---
    img.seek(0)
    head_saved = img.read(OFS_START_LBA * SEC)

    # --- собрать список файлов ---
    files = []   # (имя, байты)
    if fsroot and os.path.isdir(fsroot):
        for fn in sorted(os.listdir(fsroot)):
            p = os.path.join(fsroot, fn)
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    files.append((fn, f.read()))
    elif fsroot:
        print(f"[ПРЕДУПРЕЖДЕНИЕ] папка {fsroot} не найдена - записываю только demo-файл")
    if demo and not any(fn == "привет.txt" for fn, _ in files):
        text = "Привет из ОФС!\r\nФайловая система ОСИРИС работает.\r\n"
        files.append(("привет.txt", text.encode("cp866")))
    if len(files) > TABLE_ENTRIES:
        sys.exit(f"[ОШИБКА] файлов {len(files)}, а таблица вмещает {TABLE_ENTRIES}")

    # --- распределить блоки ---
    entries = []   # готовые 64-байтные записи
    data = b""     # поток данных
    cursor = 0
    for fn, blob in files:
        blocks = (len(blob) + SEC - 1) // SEC
        entries.append(pack_entry(fn, len(blob), cursor, FLAG_FILE))
        data += blob + b"\x00" * (blocks * SEC - len(blob))
        print(f"  файл: {fn} -> {len(blob)} байт, блок {cursor}..{cursor + blocks - 1}")
        cursor += blocks
    if cursor > data_blocks:
        sys.exit(f"[ОШИБКА] нужно {cursor} блоков, а в области данных {data_blocks}")

    # --- битовая карта: 1 = блок занят ---
    bm = bytearray(BITMAP_SECT * SEC)
    for b in range(cursor):
        bm[b >> 3] |= 0x80 >> (b & 7)
    bitmap = bytes(bm)

    # --- таблица: записи + нули до 16 секторов ---
    table = b"".join(entries)
    table += b"\x00" * (TABLE_SECT * SEC - len(table))

    # --- записать зону ОФС: сначала обнулить её всю ---
    img.seek(OFS_START_LBA * SEC)
    img.write(b"\x00" * ((img_sectors - OFS_START_LBA) * SEC))
    img.seek(SUPER_LBA * SEC);   img.write(pack_super(data_blocks))
    img.seek(BITMAP_LBA * SEC);  img.write(bitmap)
    img.seek(TABLE_LBA * SEC);   img.write(table)
    img.seek(DATA_LBA * SEC);    img.write(data)
    img.close()

    # --- проверка целостности: сектора 0..255 не тронуты ---
    img = open(img_path, "rb")
    if img.read(OFS_START_LBA * SEC) == head_saved:
        print("сектора 0..255 (stage1+stage2+ядро): не тронуты OK")
    else:
        sys.exit("[ОШИБКА] зона ОС повреждена!")

    # --- контрольное чтение: таблица и данные совпадают с записанным ---
    img.seek(TABLE_LBA * SEC)
    if img.read(TABLE_SECT * SEC)[:len(b"".join(entries))] != b"".join(entries):
        sys.exit("[ОШИБКА] таблица читается неверно!")
    img.seek(DATA_LBA * SEC)
    if img.read(len(data)) != data:
        sys.exit("[ОШИБКА] данные читаются неверно!")
    img.close()
    print(f"ОФС записана: файлов {len(entries)}, блоков занято {cursor}, свободно {data_blocks - cursor}")


def selftest():
    tmp = tempfile.mkdtemp(prefix="ofs_test_")
    img_path = os.path.join(tmp, "test.img")
    fsroot = os.path.join(tmp, "fsroot")
    os.makedirs(fsroot)

    with open(img_path, "wb") as f:
        f.write(b"\xAB" * (10 * 1024 * 1024))   # маркер: весь образ 0xAB

    # тестовые файлы: русский CP866, многосекторный, пустой
    with open(os.path.join(fsroot, "привет.txt"), "wb") as f:
        f.write("Привет из ОФС!\r\nФайловая система ОСИРИС работает.\r\n".encode("cp866"))
    with open(os.path.join(fsroot, "readme.txt"), "wb") as f:
        f.write(bytes((i * 7) & 0xFF for i in range(2000)))          # 4 блока
    with open(os.path.join(fsroot, "пусто.txt"), "wb") as f:
        pass                                                          # 0 байт

    build(img_path, fsroot)

    # --- проверка секторов 0..255: маркер 0xAB остался ---
    with open(img_path, "rb") as f:
        head = f.read(OFS_START_LBA * SEC)
    assert head == b"\xAB" * (OFS_START_LBA * SEC), "зона ОС повреждена!"

    # --- проверка суперблока ---
    with open(img_path, "rb") as f:
        f.seek(SUPER_LBA * SEC); sup = f.read(SEC)
        f.seek(BITMAP_LBA * SEC); bm = f.read(BITMAP_SECT * SEC)
        f.seek(TABLE_LBA * SEC); tb = f.read(TABLE_SECT * SEC)
    magic, ver, bs, dlba, total, nent, label = struct.unpack("<4sBIIII16s", sup[:37])
    assert magic == MAGIC and ver == VERSION and bs == SEC
    assert dlba == DATA_LBA and total == 10 * 1024 * 1024 // SEC - DATA_LBA, (total,)
    assert nent == TABLE_ENTRIES and label.rstrip(b"\x00") == LABEL

    # --- проверка таблицы и данных тем же разбором, что в ядре ---
    expected = {"привет.txt": "Привет из ОФС!\r\nФайловая система ОСИРИС работает.\r\n".encode("cp866"),
                "readme.txt": bytes((i * 7) & 0xFF for i in range(2000)),
                "пусто.txt": b""}
    with open(img_path, "rb") as f:
        found = 0
        for i in range(TABLE_ENTRIES):
            e = tb[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE]
            name, size, start, flags = struct.unpack("<48sIIB", e[:57])
            if flags != FLAG_FILE:
                continue
            fn = name.rstrip(b"\x00").decode("utf-8")
            f.seek(DATA_LBA * SEC + start * SEC)
            data = f.read(size)
            assert data == expected[fn], f"данные {fn} не совпали!"
            print(f"  прочитан из образа: {fn} ({size} байт, блок {start}) OK")
            found += 1
    assert found == 3, f"найдено файлов {found}, ожидалось 3"

    # --- проверка битовой карты: ровно 5 блоков занято (4+1+0) ---
    used = sum(bin(b).count("1") for b in bm)
    assert used == 5, f"в битмапе {used} занятых битов, ожидалось 5"

    print("SELFTEST: OK - суперблок, битмап, таблица, данные и зона ОС проверены")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "selftest":
        selftest()
    elif len(sys.argv) >= 3:
        build(sys.argv[1], sys.argv[2], "--demo" in sys.argv[3:])
    else:
        print(__doc__)
        sys.exit(1)
