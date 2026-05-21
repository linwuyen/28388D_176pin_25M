//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Sensorless PMSM FOC - Speed Outer Loop (CPU1)
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"
#include <math.h>

// Direct register mapping to bypass missing global variable definitions
#define Cpu1toCpu2IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU1VIEW *)0x0005CE00))
#define GpioDataRegs      (*((volatile struct GPIO_DATA_REGS *)0x00007F00))

// FOC Global Variables for Monitoring
volatile float32_t foc_speed_ref = 120.0f;     // Target motor speed (electrical rad/s)
volatile float32_t foc_speed_feedback = 0.0f; // Speed feedback estimated by CPU2 Luenberger
volatile float32_t foc_iq_ref = 0.1f;          // Current loop Iq command (A)

// Speed Loop PI parameters (1kHz Execution)
const float32_t FOC_S_KP = 0.25f;              // Speed loop Kp
const float32_t FOC_S_KI = 8.0f;               // Speed loop Ki
const float32_t FOC_S_TS = 1.0e-3f;            // Speed loop period (1ms)
static float32_t foc_speed_pi_int = 0.0f;      // Integrator state

//
// Main
//
void main(void)
{
    // Initialize device clock and peripherals
    Device_init();

    // Boot CPU2 core
    Device_bootCPU2(BOOT_MODE_CPU2);

    // Transfer write access of ADCA and EPWM1, EPWM2, EPWM3 to CPU2 (FOC Core)
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_ADCA, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM1, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM2, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM3, SYSCTL_CPUSEL_CPU2);

    // Initialize GPIO and configure CPU1 LED (GPIO0)
    Device_initGPIO();

    // Initialize settings from SysConfig
    Board_init();

    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    Interrupt_initModule();

    // Initialize the PIE vector table with pointers to the shell Interrupt Service Routines (ISR).
    Interrupt_initVectorTable();

    // Sync CPUs so the control starts together
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Loop Forever
    uint32_t ms_counter = 0;
    for(;;)
    {
        // 1. Read Estimated Speed from CPU2 via Msg RAM (CPU2 to CPU1 Msg RAM at 0x03B000)
        foc_speed_feedback = *((volatile float32_t *)0x03B000);

        // 2. Execute Speed PI Loop (1kHz, background 1ms scheduler)
        float32_t speed_err = foc_speed_ref - foc_speed_feedback;
        
        // Anti-Windup Speed Integral
        foc_speed_pi_int += FOC_S_KI * FOC_S_TS * speed_err;
        if(foc_speed_pi_int > 5.0f)        foc_speed_pi_int = 5.0f;
        else if(foc_speed_pi_int < -5.0f)  foc_speed_pi_int = -5.0f;

        foc_iq_ref = FOC_S_KP * speed_err + foc_speed_pi_int;
        
        // Iq limit to safe current range [-6.0A, 6.0A]
        if(foc_iq_ref > 6.0f)        foc_iq_ref = 6.0f;
        else if(foc_iq_ref < -6.0f)  foc_iq_ref = -6.0f;

        // 3. Send Iq_ref to CPU2 via Msg RAM (CPU1 to CPU2 Msg RAM at 0x03A000)
        if(Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCFLG.bit.IPC10 == 0)
        {
            *((volatile float32_t *)0x03A000) = foc_iq_ref;

            // Trigger IPC_FLAG10 (command) and IPC_FLAG0 (interrupt trigger for CPU2)
            EALLOW;
            Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCSET.all = (1UL << 10) | (1UL << 0);
            EDIS;
        }

        // Background slow blink (500ms) to indicate CPU1 is healthy
        ms_counter++;
        if(ms_counter >= 500)
        {
            ms_counter = 0;
            GpioDataRegs.GPATOGGLE.bit.GPIO0 = 1;
        }

        DEVICE_DELAY_US(1000); // 1ms delay
    }
}
