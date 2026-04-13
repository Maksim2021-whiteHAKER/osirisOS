// загрузчик
#![no_std]
#![no_main]

use::core::PanicInfo

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { halt() }
}

unsafe fn halt() -> {
    loop {
        #[cfg(target_arch="x86_64")]
        asm("hlt");

        #[cfg(target_arch="e2k")]
        asm("idle");
    }
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    #[cfg(target_arch="x86_64")]
        x86_64::boot();

        #[cfg(target_arch="e2k")]
        e2k::boot();
}