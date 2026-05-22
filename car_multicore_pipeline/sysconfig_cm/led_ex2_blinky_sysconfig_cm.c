//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  Car Multicore Pipeline - CM Telemetry Processing
//
//#############################################################################

#include "cm.h"
#include "../car_pipeline_shared.h"

// Simulated Hardware Semaphore register structure in CM
typedef struct {
    volatile uint32_t SEM[32];
} HSEM_Regs;

// GS1 maps to CM address 0x20016000
#define hsem_regs_gs1 (*((volatile HSEM_Regs *)0x20016000U))
#define processed_data (*((volatile ProcessedData *)0x20016040U))

// Local CM telemetry buffer
volatile ProcessedData local_telemetry;
volatile uint32_t telemetry_read_count = 0;
volatile uint32_t telemetry_lock_fail = 0;
volatile uint32_t fault_state_counter = 0;

//
// Main
//
void main(void)
{
    // Initialize CM Core Clock
    CM_init();

    // Reset local data
    local_telemetry.avg_voltage_mv = 0;
    local_telemetry.avg_current_ma = 0;
    local_telemetry.state_flags = 0;
    local_telemetry.update_counter = 0;

    uint32_t loop_counter = 0;

    while (1)
    {
        loop_counter++;

        // Telemetry task runs every 10ms (10000us)
        // CM is the reader, it checks if CPU2 has locked GS1 (SEM[2] == 1)
        if (hsem_regs_gs1.SEM[2] == 0)
        {
            local_telemetry.avg_voltage_mv = processed_data.avg_voltage_mv;
            local_telemetry.avg_current_ma = processed_data.avg_current_ma;
            local_telemetry.state_flags = processed_data.state_flags;
            local_telemetry.update_counter = processed_data.update_counter;

            telemetry_read_count++;

            // If a fault flag is active, track it
            if (local_telemetry.state_flags != 0)
            {
                fault_state_counter++;
            }
        }
        else
        {
            telemetry_lock_fail++;
        }

        // Delay 10ms
        // At 120MHz, 10ms is approximately 1.2M cycles
        // We can use a simple delay loop
        uint32_t delay = 100000;
        while(delay--)
        {
            __asm(" nop");
        }
    }
}

//
// End of File
//
