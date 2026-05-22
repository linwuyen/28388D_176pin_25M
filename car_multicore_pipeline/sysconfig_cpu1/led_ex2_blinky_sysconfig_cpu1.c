//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Car Multicore Pipeline - CPU1 Main Code
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "../car_pipeline_shared.h"

// Simulated Hardware Semaphore register structure in GS0
typedef struct {
    volatile uint32_t SEM[32];
} HSEM_Regs;

// GS0 Memory Map: starting at 0x00D000
#pragma DATA_SECTION(hsem_regs_gs0, "ramgs0")
volatile HSEM_Regs hsem_regs_gs0;

#pragma DATA_SECTION(raw_sensor_data, "ramgs0")
volatile RawSensorData raw_sensor_data;

// Interrupt counter
volatile uint32_t timer_isr_count = 0;
volatile uint32_t lock_fail_count = 0;

//
// CpuTimer0 Interrupt Service Routine (100us interval)
//
__interrupt void cpuTimer0ISR(void)
{
    timer_isr_count++;

    // Try to acquire Sem 1 (we use SEM[1] as the lock flag)
    // CPU1 is the writer, so it locks SEM[1] to block CPU2 from reading half-written data
    if (hsem_regs_gs0.SEM[1] == 0)
    {
        hsem_regs_gs0.SEM[1] = 1; // Acquire lock

        // Simulate ADC total voltage (around 400V or 400000mV with slight noise)
        // Simulate ADC current (around 12A or 12000mA with noise)
        raw_sensor_data.total_voltage_mv = 400000 + (timer_isr_count % 100);
        raw_sensor_data.total_current_ma = 12000 + (timer_isr_count % 50);
        raw_sensor_data.timestamp_us = timer_isr_count * 100;

        hsem_regs_gs0.SEM[1] = 0; // Release lock
    }
    else
    {
        lock_fail_count++;
    }

    // Acknowledge Timer0 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Main
//
void main(void)
{
    // Initialize device clock and peripherals
    Device_init();

    // Configure RAMGS0 to be owned by CPU1 (default, but make sure)
    MemCfg_setGSRAMControllerSel(MEMCFG_SECT_GS0, MEMCFG_GSRAMCONTROLLER_CPU1);

    // Configure RAMGS1 to be owned by CPU2
    MemCfg_setGSRAMControllerSel(MEMCFG_SECT_GS1, MEMCFG_GSRAMCONTROLLER_CPU2);

    // Boot CPU2 core
    Device_bootCPU2(BOOT_MODE_CPU2);

    // Boot CM core
    Device_bootCM(BOOTMODE_BOOT_TO_S0RAM);

    // Initialize GPIO
    Device_initGPIO();

    // Initialize settings from SysConfig
    Board_init();

    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    Interrupt_initModule();

    // Initialize the PIE vector table
    Interrupt_initVectorTable();

    // Register CPU Timer 0 ISR
    Interrupt_register(INT_TIMER0, &cpuTimer0ISR);

    // Configure CPU Timer 0 to interrupt every 100 microseconds (at 200MHz CPU clock)
    // 200MHz clock: 100us = 20000 cycles
    CPUTimer_setPeriod(CPUTIMER0_BASE, 20000);
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);

    // Enable CPU Timer 0 Interrupt in PIE
    Interrupt_enable(INT_TIMER0);

    // Sync CPUs
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    // Start CPU Timer 0
    CPUTimer_startTimer(CPUTIMER0_BASE);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    uint32_t led_counter = 0;

    // Initialize SEM locks
    hsem_regs_gs0.SEM[1] = 0;

    for(;;)
    {
        // CPU1 Heartbeat LED toggling every 500ms
        led_counter++;
        if (led_counter >= 500)
        {
            led_counter = 0;
            GPIO_togglePin(CPU1_LED);
        }

        DEVICE_DELAY_US(1000); // 1ms delay loop
    }
}

//
// End of File
//
