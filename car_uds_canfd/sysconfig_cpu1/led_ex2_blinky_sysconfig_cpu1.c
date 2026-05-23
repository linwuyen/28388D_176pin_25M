//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Car UDS CAN FD - CPU1 Main Code
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"

//
// Main
//
void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Configure GS0 RAM to be owned by CPU2
    //
    MemCfg_setGSRAMControllerSel(MEMCFG_SECT_GS0, MEMCFG_GSRAMCONTROLLER_CPU2);

    //
    // Allocate CAN_B peripheral to CM core (1U = CM, 0U = CPU1/CPU2)
    //
    SysCtl_allocateSharedPeripheral(SYSCTL_PALLOCATE_CAN_B, 1U);

    //
    // Configure GPIOs for CAN B
    //
    GPIO_setPinConfig(GPIO_12_CANB_TX);
    GPIO_setPinConfig(GPIO_13_CANB_RX);

    //
    // Boot CPU2 core
    //
    Device_bootCPU2(BOOT_MODE_CPU2);

    //
    // Boot CM core
    //
    Device_bootCM(BOOTMODE_BOOT_TO_S0RAM);

    //
    // Initialize GPIO and configure the GPIO pin as a push-pull output
    //
    Device_initGPIO();

    //
    // Initialize settings from SysConfig
    //
    Board_init();

    //
    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    //
    Interrupt_initModule();

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    //
    Interrupt_initVectorTable();

    //
    // Sync CPUs
    //
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    uint32_t ms_counter = 0;

    //
    // Loop Forever
    //
    for(;;)
    {
        // Toggle LED every 500ms
        ms_counter++;
        if(ms_counter >= 500)
        {
            ms_counter = 0;
            GPIO_togglePin(CPU1_LED);
        }

        // Delay 1ms
        DEVICE_DELAY_US(1000);
    }
}

//
// End of File
//
