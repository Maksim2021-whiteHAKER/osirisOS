@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo   OSIRIS Bootloader Builder (Hybrid C+asm)
echo ========================================
echo.

set BUILD_DIR=..\..\..\build\x86_64
set SRC_DIR=.
set IMG_SIZE_MB=10
set IMG_SECTORS=%IMG_SIZE_MB% * 2048
set CFLAGS=-ffreestanding -nostdlib -O1 -fno-builtin -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-3dnow -mgeneral-regs-only -I%SRC_DIR%

set STAGE1_SECTOR=0
set STAGE2_SECTOR=1
set KERNEL_SECTOR=5

:: Проверка наличия инструментов
where nasm.exe >nul 2>&1 || (echo [ERROR] NASM not found! & goto error)
where python.exe >nul 2>&1 || (echo [ERROR] Python not found! & goto error)
where i686-elf-gcc >nul 2>&1 || (echo [ERROR] i686-elf-gcc not found! & goto error)
where i686-elf-ld >nul 2>&1 || (echo [ERROR] i686-elf-ld not found! & goto error)
where i686-elf-objcopy >nul 2>&1 || (echo [ERROR] i686-elf-objcopy not found! & goto error)

:: Создание директории сборки
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%" 2>nul
if errorlevel 1 (echo [ERROR] Cannot create build directory! & goto error)

echo [1/5] Compiling stage1...
nasm.exe -f bin "%SRC_DIR%\boot\stage1.asm" -o "%BUILD_DIR%\stage1.bin"
if errorlevel 1 (echo [ERROR] Stage1 compilation failed! & goto error)
dir "%BUILD_DIR%\stage1.bin" | find "stage1.bin"

echo [2/5] Compiling stage2...
nasm.exe -f bin "%SRC_DIR%\boot\stage2.asm" -o "%BUILD_DIR%\stage2.bin"
if errorlevel 1 (echo [ERROR] Stage2 compilation failed! & goto error)
dir "%BUILD_DIR%\stage2.bin" | find "stage2.bin"

echo [3/5] Compiling hybrid kernel (C+asm)...
i686-elf-gcc -c "%SRC_DIR%\core\main.c" -o "%BUILD_DIR%\main.o" %CFLAGS%
i686-elf-gcc -c "%SRC_DIR%\core\shell.c" -o "%BUILD_DIR%\shell.o" %CFLAGS%
i686-elf-gcc -c "%SRC_DIR%\core\console.c" -o "%BUILD_DIR%\console.o" %CFLAGS%
i686-elf-gcc -c "%SRC_DIR%\core\keyboard.c" -o "%BUILD_DIR%\keyboard.o" %CFLAGS%
i686-elf-gcc -c "%SRC_DIR%\core\idt.c" -o "%BUILD_DIR%\idt.o" %CFLAGS%
i686-elf-gcc -c "%SRC_DIR%\core\timer.c" -o "%BUILD_DIR%\timer.o" %CFLAGS%
if errorlevel 1 (echo [ERROR] C compilation failed! & goto error)

nasm -f elf32 "%SRC_DIR%\boot\core_start.asm" -o "%BUILD_DIR%\start.o"
if errorlevel 1 (echo [ERROR] Assembly of core_start.asm failed! & goto error)

nasm -f elf32 "%SRC_DIR%\core\interrupts.asm" -o "%BUILD_DIR%\interrupts.o"
if errorlevel 1 (echo [ERROR] Assembly of core_start.asm failed! & goto error)

i686-elf-ld -T "%SRC_DIR%\linker.ld" -o "%BUILD_DIR%\kernel.elf" "%BUILD_DIR%\start.o" "%BUILD_DIR%\main.o" "%BUILD_DIR%\shell.o" "%BUILD_DIR%\console.o" "%BUILD_DIR%\keyboard.o" "%BUILD_DIR%\idt.o" "%BUILD_DIR%\timer.o" "%BUILD_DIR%\interrupts.o"
if errorlevel 1 (echo [ERROR] Linking failed! & goto error)

i686-elf-objcopy -O binary "%BUILD_DIR%\kernel.elf" "%BUILD_DIR%\kernel.bin"
if errorlevel 1 (echo [ERROR] objcopy failed! & goto error)
dir "%BUILD_DIR%\kernel.bin" | find "kernel.bin"

echo [4/5] Creating HDD image (%IMG_SIZE_MB% MB)...
python -c "with open(r'%BUILD_DIR%\osiris.img', 'wb') as f: f.write(b'\x00' * (%IMG_SECTORS% * 512))"
if errorlevel 1 (echo [ERROR] Failed to create image! & goto error)

echo Writing stage1 to sector %STAGE1_SECTOR%...
python -c "img=open(r'%BUILD_DIR%\osiris.img', 'r+b'); img.write(open(r'%BUILD_DIR%\stage1.bin', 'rb').read()); img.close()"
if errorlevel 1 (echo [ERROR] Failed to write stage1! & goto error)

echo Writing stage2 to sector %STAGE2_SECTOR%...
python -c "img=open(r'%BUILD_DIR%\osiris.img', 'r+b'); img.seek(%STAGE2_SECTOR%*512); img.write(open(r'%BUILD_DIR%\stage2.bin', 'rb').read()); img.close()"
if errorlevel 1 (echo [ERROR] Failed to write stage2! & goto error)

echo Writing kernel to sector %KERNEL_SECTOR%...
python -c "img=open(r'%BUILD_DIR%\osiris.img', 'r+b'); img.seek(%KERNEL_SECTOR%*512); img.write(open(r'%BUILD_DIR%\kernel.bin', 'rb').read()); img.close()"
if errorlevel 1 (echo [ERROR] Failed to write kernel! & goto error)

echo [5/5] Verifying boot signature...
python -c "import struct; img=open(r'%BUILD_DIR%\osiris.img', 'rb'); img.seek(510); sig=struct.unpack('<H', img.read(2))[0]; exit(0 if sig==0xAA55 else 1)" >nul 2>&1
if errorlevel 1 (echo [WARNING] Boot signature not found!) else echo Boot signature OK.

echo.
echo ========================================
echo   BUILD SUCCESSFUL!
echo ========================================
echo.
echo Output image: %BUILD_DIR%\osiris.img
dir "%BUILD_DIR%\osiris.img" | find "osiris.img"
echo.
echo To run: qemu-system-x86_64 -drive file=%BUILD_DIR%\osiris.img,format=raw
echo.
goto end

:error
echo.
echo ========================================
echo   BUILD FAILED!
echo ========================================
:end
pause