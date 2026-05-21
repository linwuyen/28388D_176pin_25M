//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Grid SOGI-PLL + PCMC with Slope Compensation - CPU1 Master
//
//#############################################################################
// $Copyright:
// Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"

// Define IPC Register Map for CPU1 View
#define Cpu1toCpu2IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU1VIEW *)0x0005CE00))
#define GpioDataRegs (*((volatile struct GPIO_DATA_REGS *)0x00007F00))

// Shared RAM Addresses for IPC data transfer
#define IPC_I_AMP_ADDR        0x03A000U // Float: Current Amplitude Reference (I_amp)
#define IPC_GRID_ENABLE_ADDR  0x03A004U // Uint32: Grid Connection Enable Switch (1 = Enable, 0 = Disable)

void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Boot CPU2 core
    //
    Device_bootCPU2(BOOT_MODE_CPU2);

    //
    // Transfer write access of ADCA, EPWM1, and CMPSS1 to CPU2
    //
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_ADCA, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM1, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_CMPSS1, SYSCTL_CPUSEL_CPU2);

    //
    // Initialize GPIO
    //
    Device_initGPIO();

    //
    // Initialize settings from SysConfig (Blinky default board setup)
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
    // Sync CPUs so that execution starts synchronously
    //
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // Set CPU1_LED initially ON (active-low, clear GPIO0)
    GpioDataRegs.GPACLEAR.bit.GPIO0 = 1;

    // Command parameters to be sent to CPU2
    float user_i_amp = 1.5f;       // Default grid-tied current amplitude (1.5 A)
    uint32_t user_grid_enable = 1; // Default enabled

    uint32_t ms_counter = 0;
    uint32_t toggle_timer = 0;

    for(;;)
    {
        // Non-blocking IPC: check if CPU2 has acknowledged/cleared previous IPC_FLAG10
        if(Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCFLG.bit.IPC10 == 0)
        {
            // Write commands to CPU1_TO_CPU2_MSG_RAM
            *((volatile float *)IPC_I_AMP_ADDR) = user_i_amp;
            *((volatile uint32_t *)IPC_GRID_ENABLE_ADDR) = user_grid_enable;

            // Trigger IPC_FLAG10 (command flag) and IPC_FLAG0 (interrupt trigger for CPU2)
            EALLOW;
            Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCSET.all = (1UL << 10) | (1UL << 0);
            EDIS;

            // Dynamic simulation: sweep I_amp slowly between 1.0A and 3.0A to show dynamic PLL + PCMC
            static float sweep_dir = 0.05f;
            user_i_amp += sweep_dir;
            if(user_i_amp > 3.0f)
            {
                user_i_amp = 3.0f;
                sweep_dir = -0.05f;
            }
            else if(user_i_amp < 1.0f)
            {
                user_i_amp = 1.0f;
                sweep_dir = 0.05f;
            }
        }

        // Toggle Heartbeat LED (GPIO0) every 500ms
        ms_counter++;
        if(ms_counter >= 500)
        {
            ms_counter = 0;
            GpioDataRegs.GPATOGGLE.bit.GPIO0 = 1;
        }

        // Delay 1ms
        DEVICE_DELAY_US(1000);
    }
}
