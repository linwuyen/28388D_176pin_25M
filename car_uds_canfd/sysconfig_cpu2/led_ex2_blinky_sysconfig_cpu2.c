//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  Car UDS CAN FD - CPU2 Main Code
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "../car_uds_shared.h"

// Define CPU2 GS0 RAM section for the shared structure
#pragma DATA_SECTION(uds_shared_data, "ramgs0")
volatile UDS_SharedData uds_shared_data;

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
    // Sync CPUs
    //
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // Initialize shared variables
    uds_shared_data.active_fault_code = 0;
    uds_shared_data.cell_voltage_mv = 3700; // 3.7V normal
    uds_shared_data.cell_temp_c = 25;       // 25 C normal

    uint32_t ms_counter = 0;
    uint32_t elapsed_ms = 0;

    //
    // Loop Forever
    //
    for(;;)
    {
        // Simulate real-time voltage fluctuation slightly
        uds_shared_data.cell_voltage_mv = 3700 + (elapsed_ms % 50);

        // Simulate temperature rise: after 5 seconds, trip overtemperature
        if (elapsed_ms < 5000)
        {
            uds_shared_data.cell_temp_c = 25 + (elapsed_ms / 200); // 25 -> 49 C
            uds_shared_data.active_fault_code = 0;
        }
        else
        {
            uds_shared_data.cell_temp_c = 65; // Over-temperature
            uds_shared_data.active_fault_code = 0x02; // Over-temp DTC trigger
        }

        // Toggle LED every 500ms
        ms_counter++;
        if(ms_counter >= 500)
        {
            ms_counter = 0;
            GPIO_togglePin(CPU2_LED);
        }

        // Delay 1ms
        DEVICE_DELAY_US(1000);
        elapsed_ms++;
    }
}

//
// End of File
//
