//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE: SysConfig LED Blinky Example
//
//! \addtogroup driver_dual_example_list
//! <h1> LED Blinky Example</h1>
//!
//! This example demonstrates how to blink a LED using CPU1 and blink another
//! LED using CPU2 (led_ex1_blinky_cpu2.c).
//!
//! \b External \b Connections \n
//!  - None.
//!
//! \b Watch \b Variables \n
//!  - None.
//!
//
//#############################################################################
//
//
// $Copyright:
// Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com
//
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions 
// are met:
// 
//   Redistributions of source code must retain the above copyright 
//   notice, this list of conditions and the following disclaimer.
// 
//   Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the 
//   documentation and/or other materials provided with the   
//   distribution.
// 
//   Neither the name of Texas Instruments Incorporated nor the names of
//   its contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// $
//#############################################################################

//
// Included Files
//
// Make sure to include "board.h" to use SysConfig
//
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"

#define Cpu1toCpu2IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU1VIEW *)0x0005CE00))
#define GpioDataRegs (*((volatile struct GPIO_DATA_REGS *)0x00007F00))

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
    // Boot CPU2 core
    //
    Device_bootCPU2(BOOT_MODE_CPU2);

    //
    // Transfer write access of ADCA and EPWM1 to CPU2
    //
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_ADCA, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM1, SYSCTL_CPUSEL_CPU2);

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
    // Sync CPUs so the blinking starts at the same time, though the LEDs toggle at different frequency
    //
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // Set CPU1_LED initially ON (active-low, clear GPIO0)
    GpioDataRegs.GPACLEAR.bit.GPIO0 = 1;

    //
    // Loop Forever
    //
    uint32_t ms_counter = 0;
    uint32_t s_curve_freq = 1000;

    for(;;)
    {
        // Non-blocking IPC: check if CPU2 has cleared previous IPC_FLAG10
        if(Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCFLG.bit.IPC10 == 0)
        {
            // Write new S-Curve frequency parameter to CPU1_TO_CPU2_MSG_RAM (0x03A000)
            *((volatile uint32_t *)0x03A000) = s_curve_freq;

            // Simple S-curve frequency parameter increment logic for simulation
            s_curve_freq += 10;
            if(s_curve_freq > 5000)
            {
                s_curve_freq = 1000;
            }

            // Trigger IPC_FLAG10 (command) and IPC_FLAG0 (interrupt trigger for CPU2)
            EALLOW;
            Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCSET.all = (1UL << 10) | (1UL << 0);
            EDIS;
        }

        // Toggle LED every 500ms
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

//
// End of File
//
