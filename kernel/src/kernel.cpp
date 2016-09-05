//=======================================================================
// Copyright Baptiste Wicht 2013-2016.
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://www.opensource.org/licenses/MIT)
//=======================================================================

#include "kernel.hpp"
#include "physical_allocator.hpp"
#include "virtual_allocator.hpp"
#include "paging.hpp"
#include "kalloc.hpp"
#include "timer.hpp"
#include "drivers/keyboard.hpp"
#include "drivers/mouse.hpp"
#include "drivers/serial.hpp"
#include "disks.hpp"
#include "drivers/pci.hpp"
#include "acpi.hpp"
#include "interrupts.hpp"
#include "system_calls.hpp"
#include "arch.hpp"
#include "vesa.hpp"
#include "console.hpp"
#include "gdt.hpp"
#include "terminal.hpp"
#include "scheduler.hpp"
#include "logging.hpp"
#include "net/network.hpp"
#include "vfs/vfs.hpp"
#include "fs/sysfs.hpp"
#include "drivers/hpet.hpp"

extern "C" {

void _init();

void __cxa_pure_virtual(){
    k_print_line("A pure virtual function has been called");
    suspend_kernel();
}

} //end of extern "C"

void kernel_main() __attribute__((section(".start")));

void kernel_main(){
    //Make sure stack is aligned to 16 byte boundary
    asm volatile("and rsp, -16");

    arch::enable_sse();

    gdt::flush_tss();

    // Necessary for logging with Qemu
    serial::init();

    //Starting from here, the logging system can stop saving early logs
    logging::finalize();

    // Setup interrupts
    interrupt::setup_interrupts();

    //Compute virtual addresses for paging
    paging::early_init();

    //Init the virtual allocator
    virtual_allocator::init();

    //Prepare basic physical allocator for paging init
    physical_allocator::early_init();

    //Init all the physical
    paging::init();

    //Finalize physical allocator initialization for kalloc
    physical_allocator::init();

    //Init dynamic memory allocation
    kalloc::init();

    //Call global constructors
    _init();

    //Try to init VESA
    if(vesa::enabled() && !vesa::init()){
        vesa::disable();

        //Unfortunately, we are in long mode, we cannot go back
        //to text mode for now
        suspend_boot();
    }

    init_console();
    stdio::init_terminals();

    //Finalize memory operations (register sysfs values)
    paging::finalize();
    physical_allocator::finalize();
    virtual_allocator::finalize();
    kalloc::finalize();

    // Asynchronously initialized drivers
    acpi::init();
    hpet::init();

    //Install drivers
    timer::install();
    keyboard::install_driver();
    mouse::install();
    disks::detect_disks();
    pci::detect_devices();
    network::init();

    //Init the virtual file system
    vfs::init();

    //Starting from here, the logging system can output logs to file
    //TODO logging::to_file();

    //Only install system calls when everything else is ready
    install_system_calls();

    sysfs::set_constant_value(path("/sys"), path("/version"), "0.1");
    sysfs::set_constant_value(path("/sys"), path("/author"), "Baptiste Wicht");

    // Initialize the scheduler
    scheduler::init();

    // Start the secondary kernel processes
    network::finalize();
    stdio::finalize();

    // Start the scheduler
    scheduler::start();
}

void suspend_boot(){
    k_print_line("Impossible to continue boot...");
    suspend_kernel();
}

void suspend_kernel(){
    asm volatile("cli; hlt");
    __builtin_unreachable();
}
