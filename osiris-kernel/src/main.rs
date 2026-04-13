// Ядро
#![no_std]
#![no_main]

use core::panic::PanicInfo;

// /// Инициализация .bss-сегмента
// #[inline(never)]
// unsafe fn clear_bss() {
//     extern "C" {
//         static mut __bss_start: u8;
//         static mut __bss_end: u8;
//     }

//     let start = &raw mut __bss_start;
//     let end = &raw mut __bss_end;
//     let mut ptr = start;
//     while ptr < end {
//         *ptr = 0;
//         ptr = ptr.offset(1);
//     }
// }

// в osiris-kernel/src/main.rs
#[repr(C)]
pub struct KernelHeader {
    pub magic: u32,
    pub size: u32,
    pub entry_point: u64,
}

#[no_mangle]
#[link_section = ".header"]  // в начало бинарника
static HEADER: KernelHeader = KernelHeader {
    magic: 0x5349524F,  // "OSIR"
    size: 0,  // будет заполнено линкером
    entry_point: _start as u64,
};

#![no_std]
#![no_main]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { halt() };
}

/// Простая функция остановки процессора
unsafe fn halt() -> ! {
    asm!("hlt", options(noreturn));
}

/// Запись символа в VGA‑память
fn write_char(x: usize, y: usize, c: u8, color: u8) {
    let vga = 0xb8000 as *mut u8;
    let offset = (y * 80 + x) * 2;
    unsafe {
        *vga.offset(offset as isize) = c;
        *vga.offset(offset as isize + 1) = color;
    }
}

/// Вывод строки в VGA
fn print_string(x: usize, y: usize, s: &[u8], color: u8) {
    for (i, &byte) in s.iter().enumerate() {
        write_char(x + i, y, byte, color);
    }
}

/// Структура заголовка ядра
#[repr(C)]
struct KernelHeader {
    magic: u32,
    size: u32,
    entry_point: u64,
}

/// Функция чтения сектора с диска (BIOS INT 13h)
fn read_sector(sector: u64, buffer: *mut u8) -> bool {
    unsafe {
        // Пример вызова BIOS для чтения сектора
        // В реальности нужно использовать inline assembly
        asm!(
            "int 0x13",
            inout("ax") 0x0201,  // AH=02h (read), AL=1 (sectors)
            inout("cx") ((sector >> 8) as u16 & 0xFF00) | ((sector & 0x3F) + 1),
            inout("dx") 0x0000,  // ES:BX = 0:0
            in("bx") buffer as u16,
            out("flags") _,
        );
        // Проверка флага ошибки
        // Если CF=1, операция не удалась
        true // Упрощённо: всегда возвращаем успех
    }
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    // Очистка экрана
    for i in 0..(80 * 25) {
        write_char(i % 80, i / 80, b' ', 0x07);
    }

    // Вывод приветствия
    print_string(0, 0, b"OSIRIS BOOT", 0x0F);

    // Адрес загрузки ядра
    const KERNEL_LOAD_ADDR: usize = 0x200000;
    let kernel_addr = KERNEL_LOAD_ADDR as *mut u8;

    // Читаем заголовок ядра (первый сектор)
    if !read_sector(1, kernel_addr) {
        print_string(0, 1, b"Failed to read kernel header!", 0x0C);
        unsafe { halt() };
    }

    // Проверяем магическое число
    let header = kernel_addr as *const KernelHeader;
    unsafe {
        if (*header).magic != 0x5349524F { // "OSIR" в little‑endian
            print_string(0, 1, b"Invalid kernel magic!", 0x0C);
            unsafe { halt() };
        }
    }

    // Загружаем всё ядро
    let kernel_size = unsafe { (*header).size as usize };
    let sectors_to_read = (kernel_size + 511) / 512;

    for sector in 0..sectors_to_read {
        let dest = (KERNEL_LOAD_ADDR + sector * 512) as *mut u8;
        if !read_sector((sector + 2) as u64, dest) {
            print_string(0, 2, b"Failed to load kernel!", 0x0C);
            unsafe { halt() };
        }
    }

    // Получаем точку входа
    let entry_point = unsafe { (*header).entry_point };

    // Переходим на ядро
    let kernel_entry: fn() -> ! = unsafe { core::mem::transmute(entry_point) };
    kernel_entry();
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    // unsafe { clear_bss(); }

    let vga_buffer = 0xb8000 as *mut u8;
    let text = b"OSIRIS BOOTING...";

    for (i, &byte) in text.iter().enumerate() {
        unsafe {
            *vga_buffer.offset(i as isize * 2) = byte;
            *vga_buffer.offset(i as isize * 2 + 1) = 0x0F;
        }
    }

    loop { unsafe { core::arch::asm!("hlt"); } }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    let vga_buffer = 0xb8000 as *mut u8;
    let error_msg = b"KERNEL PANIC!";

    for (i, &byte) in error_msg.iter().enumerate() {
        unsafe {
            *vga_buffer.offset(i as isize * 2) = byte;
            *vga_buffer.offset(i as isize * 2 + 1) = 0x4F;
        }
    }
    loop { unsafe { core::arch::asm!("hlt"); } }
}