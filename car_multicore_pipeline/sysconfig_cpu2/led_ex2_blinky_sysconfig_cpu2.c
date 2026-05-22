//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  Car Multicore Pipeline - CPU2 Processing Loop
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "../car_pipeline_shared.h"

// Simulated Hardware Semaphore structures
typedef struct {
    volatile uint32_t SEM[32];
} HSEM_Regs;

// GS0 Memory Map (CPU2 Read Only): starts at 0x00D000
#pragma DATA_SECTION(hsem_regs_gs0, "ramgs0")
volatile HSEM_Regs hsem_regs_gs0;

#pragma DATA_SECTION(raw_sensor_data, "ramgs0")
volatile RawSensorData raw_sensor_data;

// GS1 Memory Map (CPU2 Write/Read Owner): starts at 0x00E000
#pragma DATA_SECTION(hsem_regs_gs1, "ramgs1")
volatile HSEM_Regs hsem_regs_gs1;

#pragma DATA_SECTION(processed_data, "ramgs1")
volatile ProcessedData processed_data;

// Local processing variables
uint32_t local_voltage_sum = 0;
int32_t local_current_sum = 0;
uint32_t filter_samples = 0;
uint32_t process_loop_count = 0;

//
// Main
//
void main(void)
{
    // Initialize device clock and peripherals
    Device_init();

    // Initialize settings from SysConfig
    Board_init();

    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    Interrupt_initModule();

    // Initialize the PIE vector table
    Interrupt_initVectorTable();

    // Sync CPUs
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Initialize SEM locks in GS1
    hsem_regs_gs1.SEM[2] = 0;

    processed_data.avg_voltage_mv = 0;
    processed_data.avg_current_ma = 0;
    processed_data.state_flags = 0;
    processed_data.update_counter = 0;

    uint32_t led_counter = 0;

    for(;;)
    {
        process_loop_count++;

        // 1. Read Raw Sensor Data from GS0 using Lock 1
        // CPU2 is reader, it checks if CPU1 has locked GS0 (SEM[1] == 1)
        if (hsem_regs_gs0.SEM[1] == 0)
        {
            uint32_t current_volt = raw_sensor_data.total_voltage_mv;
            int32_t current_curr = raw_sensor_data.total_current_ma;

            // Simple cumulative moving average filter logic
            local_voltage_sum += current_volt;
            local_current_sum += current_curr;
            filter_samples++;
        }

        // 2. Perform 10-sample average processing and output to GS1
        if (filter_samples >= 10)
        {
            uint32_t avg_volt = local_voltage_sum / 10;
            int32_t avg_curr = local_current_sum / 10;

            // Check if Sem 2 is locked (CM is reading GS1)
            // CPU2 is writer, so it locks SEM[2] while writing to GS1
            if (hsem_regs_gs1.SEM[2] == 0)
            {
                hsem_regs_gs1.SEM[2] = 1; // Acquire lock

                processed_data.avg_voltage_mv = avg_volt;
                processed_data.avg_current_ma = avg_curr;
                processed_data.update_counter++;

                // Fault protection flags logic
                // Over-voltage threshold: 400.08 V (400080 mV)
                // Over-current threshold: 12.03 A (12030 mA)
                uint32_t flags = 0;
                if (avg_volt > 400080) {
                    flags |= 0x01; // Bit 0: Over-voltage
                }
                if (avg_curr > 12030) {
                    flags |= 0x02; // Bit 1: Over-current
                }
                processed_data.state_flags = flags;

                hsem_regs_gs1.SEM[2] = 0; // Release lock
            }

            // Reset accumulator
            local_voltage_sum = 0;
            local_current_sum = 0;
            filter_samples = 0;
        }

        // CPU2 Heartbeat LED toggling every 500ms
        led_counter++;
        if (led_counter >= 500)
        {
            led_counter = 0;
            GPIO_togglePin(CPU2_LED);
        }

        DEVICE_DELAY_US(1000); // 1ms execution cycle
    }
}

//
// End of File
//
