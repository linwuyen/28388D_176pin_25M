//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE: SysConfig LED Blinky Example
//
// <h1> LED Blinky Example (CPU2) </h1>
//
// This example demonstrates how to blink a LED using CPU2.
//
// \b External \b Connections \n
//  - None.
//
// \b Watch \b Variables \n
//  - None.
//
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

#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs (*((volatile struct GPIO_DATA_REGS *)0x00007F00))
#define EPwm1Regs (*((volatile struct EPWM_REGS *)0x00004000))

// Interrupt prototype
__interrupt void ipc0_isr(void);

#pragma DATA_SECTION(active_fault_code, "ramgs0")
volatile uint32_t active_fault_code;

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
    // Register and enable CPU2 IPC0 Interrupt
    //
    IPC_registerInterrupt(IPC_CPU2_L_CPU1_R, IPC_INT0, ipc0_isr);

    //
    // Sync CPUs so the blinking starts at the same time, though the LEDs toggle at different frequency
    //
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // Initialize active fault code
    active_fault_code = 0;
    uint32_t loop_count = 0;

    //
    // Loop Forever
    //
    for(;;)
    {
        // Background task: slow heartbeat blink to show CPU2 is running
        GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;
        
        loop_count++;
        // Simulate fault detection: set fault code to 0x02 (overtemp) after 5 seconds
        if(loop_count >= 5)
        {
            active_fault_code = 0x02;
        }

        DEVICE_DELAY_US(1000000); // 1 second interval
    }
}

//
// IPC0 Interrupt Service Routine (ISR)
// Triggers when CPU1 sets IPC_FLAG0.
// Must be ultra-fast (limit execution under 500ns).
//
__interrupt void ipc0_isr(void)
{
    // Verify CPU1 set IPC_FLAG10 (START_DDS command flag)
    if(Cpu2toCpu1IpcRegs.CPU1TOCPU2IPCSTS.bit.IPC10 == 1)
    {
        // Read new frequency from CPU1_TO_CPU2_MSG_RAM (0x03A000)
        uint32_t freq_val = *((volatile uint32_t *)0x03A000);

        // Clear/ACK both IPC_FLAG10 and IPC_FLAG0 flags
        EALLOW;
        Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 10) | (1UL << 0);
        EDIS;

        // Write directly to ePWM1 TBPRD register (simulating DDS frequency update)
        EPwm1Regs.TBPRD = (uint16_t)freq_val;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// End of File
//
