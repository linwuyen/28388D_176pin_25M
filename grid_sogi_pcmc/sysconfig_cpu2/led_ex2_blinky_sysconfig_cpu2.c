//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  Grid SOGI-PLL + PCMC with Slope Compensation - CPU2 Control Core
//
//#############################################################################
// $Copyright:
// Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"
#include <math.h>

// Define Registers for CPU2 local view
#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs (*((volatile struct GPIO_DATA_REGS *)0x00007F00))
#define EPwm1Regs (*((volatile struct EPWM_REGS *)0x00004000))

// Shared RAM Addresses for IPC data transfer
#define IPC_I_AMP_ADDR        0x03A000U // Float: Current Amplitude Reference (I_amp)
#define IPC_GRID_ENABLE_ADDR  0x03A004U // Uint32: Grid Connection Enable Switch (1 = Enable, 0 = Disable)

// Function Prototypes
__interrupt void cpuTimer0_isr(void);
__interrupt void ipc0_isr(void);
void initEPWM1_PCMC(void);
void initCMPSS1_PCMC(void);
void initEPWM_XBAR_PCMC(void);

// Control Parameters & Global Variables (for observation in Expressions)
volatile float grid_v_sim = 0.0f;    // Simulated Grid Voltage (Vg)
volatile float ind_current_sim = 0.0f; // Simulated Inductor Current (iL)
volatile float pll_theta = 0.0f;     // PLL Phase Angle
volatile float pll_freq = 60.0f;     // PLL Estimated Frequency (Hz)
volatile float i_ref = 0.0f;         // AC Current Reference (i_ref = I_amp * sin(theta))
volatile float dac_ramp_start = 0.0f; // Current cycle ramp start voltage limit
volatile float duty_cycle = 0.0f;    // Calculated duty cycle from PCMC simulation
volatile uint32_t cbc_trip_count = 0; // Number of peak current trips in current session

// IPC Command Cache (read from Shared RAM)
volatile float cmd_i_amp = 0.0f;       // Read from CPU1
volatile uint32_t cmd_grid_enable = 0; // Read from CPU1

// SOGI-PLL State Variables & Coefficients (50kHz sample rate, Ts = 20us)
#define TS_SEC 0.000020f // 20 microseconds
const float SOGI_b0 = 0.00530311f;
const float SOGI_b1 = 0.00001999f;
const float SOGI_a1 = -1.98933709f;
const float SOGI_a2 = 0.98939379f;

float sogi_vg_z1 = 0.0f, sogi_vg_z2 = 0.0f;
float sogi_vd_z1 = 0.0f, sogi_vd_z2 = 0.0f;
float sogi_vq_z1 = 0.0f, sogi_vq_z2 = 0.0f;

// PLL Loop Filter (PI) Variables
float pll_int_err = 0.0f;
const float pll_kp = 160.0f;
const float pll_ki = 20000.0f;
const float omega_nom = 376.991118f; // 2 * pi * 60

void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Initialize settings from SysConfig (GPIO Heartbeat, etc.)
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
    // Configure hardware PCMC modules (ePWM1, CMPSS1, EPWM X-BAR)
    //
    initEPWM1_PCMC();
    initCMPSS1_PCMC();
    initEPWM_XBAR_PCMC();

    //
    // Register ISRs
    //
    EALLOW;
    Interrupt_register(INT_TIMER0, &cpuTimer0_isr);
    EDIS;
    IPC_registerInterrupt(IPC_CPU2_L_CPU1_R, IPC_INT0, ipc0_isr);

    //
    // Configure CpuTimer0 to trigger at 50kHz (20us period)
    //
    CPUTimer_setPeriod(CPUTIMER0_BASE, 200000000ULL / 50000ULL); // 200MHz SYSCLK, 50kHz frequency
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);

    //
    // Sync CPUs so that execution starts synchronously
    //
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EALLOW;
    Interrupt_enable(INT_TIMER0);
    EDIS;
    EINT;
    ERTM;

    //
    // Loop Forever
    //
    for(;;)
    {
        // Background task: Toggle Heartbeat LED (GPIO1) every 500ms
        GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;
        DEVICE_DELAY_US(500000);
    }
}

//
// Initialize ePWM1 for PCMC with SYNCO generation
//
void initEPWM1_PCMC(void)
{
    EALLOW;
    // Disable TBCLK sync during setup
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // Period = 1000 (100kHz switching frequency on 100MHz EPWMCLK)
    EPwm1Regs.TBPRD = 1000;
    EPwm1Regs.TBCTR = 0;
    EPwm1Regs.TBCTL.bit.CTRMODE = 0;      // Up-count mode
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0;    // Pre-scale /1
    EPwm1Regs.TBCTL.bit.CLKDIV = 0;       // Pre-scale /1

    // Setup SYNCO output on CTR=0 for CMPSS Ramp Sync
    EPwm1Regs.EPWMSYNCOUTEN.bit.ZEROEN = 1;     // CTR = 0 generates EPWMxSYNCO

    // Action Qualifier: CTR=0 sets EPWM1A High, CMPA sets EPWM1A Low (Duty cycle safety limit)
    EPwm1Regs.AQCTLA.bit.ZRO = 2;         // CTR = 0: Set High
    EPwm1Regs.AQCTLA.bit.CAU = 1;         // CTR = CMPA: Set Low
    EPwm1Regs.CMPA.bit.CMPA = 900;        // Max duty cycle limit at 90%

    // Configure ePWM Digital Compare Subsystem (DCAH triggered by TRIPIN4 from EPWM X-BAR)
    EPwm1Regs.DCTRIPSEL.bit.DCAHCOMPSEL = 3; // 3 = TRIPIN4 selected for DCAH
    EPwm1Regs.TZDCSEL.bit.DCAEVT2 = 2;       // DCAH = High triggers DCAEVT2

    // Configure ePWM Trip Zone (TZ)
    EPwm1Regs.TZSEL.bit.DCAEVT2 = 1;         // Enable DCAEVT2 as Cycle-by-Cycle (CBC) trip source
    EPwm1Regs.TZCTL.bit.DCAEVT2 = 2;         // Force EPWM1A Low when DCAEVT2 is active
    EPwm1Regs.TZCTL.bit.TZA = 2;             // Force EPWM1A Low when regular TZ is active

    // Enable TBCLK sync
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    EDIS;
}

//
// Initialize CMPSS1 with Ramp Generator for Slope Compensation
//
void initCMPSS1_PCMC(void)
{
    EALLOW;
    // Enable CMPSS1 module
    CMPSS_enableModule(CMPSS1_BASE);

    // High comparator source = negative input connected to internal DAC
    CMPSS_configHighComparator(CMPSS1_BASE, CMPSS_INSRC_DAC);

    // Config DAC: Load on PWMSYNC, VDDA as ref, RAMP generator as DAC value source
    CMPSS_configDAC(CMPSS1_BASE, CMPSS_DACVAL_PWMSYNC | CMPSS_DACREF_VDDA | CMPSS_DACSRC_RAMP);

    // Sync Ramp Generator with PWMSYNC1 (ePWM1 SYNCO)
    CMPSS_configureSyncSource(CMPSS1_BASE, 1); // 1 = PWMSYNC1

    // Configure Ramp Generator:
    // Decrement = 9 LSB/SYSCLK cycle (corresponds to slope compensation)
    // Delay = 10 SYSCLK cycles before starting ramp decrement
    // Use shadow register for RAMPMAXREFS updates
    CMPSS_configRamp(CMPSS1_BASE, 3000, 9, 10, 1, true);

    // Config output: Async comparator output drives CTRIPH & CTRIPOUTH directly for speed
    CMPSS_configOutputsHigh(CMPSS1_BASE, CMPSS_TRIP_ASYNC_COMP | CMPSS_TRIPOUT_ASYNC_COMP);
    EDIS;
}

//
// Route CMPSS1 CTRIPH to ePWM X-BAR TRIP4
//
void initEPWM_XBAR_PCMC(void)
{
    EALLOW;
    // Mux 0 select CMPSS1.CTRIPH (value = 0x0000 for XBAR_EPWM_MUX00_CMPSS1_CTRIPH)
    XBAR_setEPWMMuxConfig(XBAR_TRIP4, XBAR_EPWM_MUX00_CMPSS1_CTRIPH);
    // Enable Mux 0 on TRIP4
    XBAR_enableEPWMMux(XBAR_TRIP4, XBAR_MUX00);
    EDIS;
}

//
// IPC0 Interrupt Service Routine (ISR) - Reads references from CPU1
//
__interrupt void ipc0_isr(void)
{
    // Verify CPU1 set IPC_FLAG10
    if(Cpu2toCpu1IpcRegs.CPU1TOCPU2IPCSTS.bit.IPC10 == 1)
    {
        // Cache commands from Shared RAM
        cmd_i_amp = *((volatile float *)IPC_I_AMP_ADDR);
        cmd_grid_enable = *((volatile uint32_t *)IPC_GRID_ENABLE_ADDR);

        // Acknowledge and clear IPC flags
        EALLOW;
        Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 10) | (1UL << 0);
        EDIS;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// 50kHz Control Interrupt (CpuTimer0) - Executes SOGI-PLL & PCMC Hardware Updates
//
__interrupt void cpuTimer0_isr(void)
{
    static float time_sec = 0.0f;

    // 1. Simulate Grid Voltage (110V RMS, 60Hz nominal, unit normalized to 1.0f base for PLL)
    time_sec += TS_SEC;
    if(time_sec > 1.0f)
    {
        time_sec -= 1.0f;
    }
    grid_v_sim = sinf(2.0f * 3.14159265f * 60.0f * time_sec);

    // 2. SOGI QSG (Second-Order Generalized Integrator Quadrature Signal Generator)
    // Discrete difference equations (Tustin discretized at 50kHz)
    float vd = SOGI_b0 * (grid_v_sim - sogi_vg_z2) - SOGI_a1 * sogi_vd_z1 - SOGI_a2 * sogi_vd_z2;
    float vq = SOGI_b1 * (grid_v_sim + 2.0f * sogi_vg_z1 + grid_v_sim) - SOGI_a1 * sogi_vq_z1 - SOGI_a2 * sogi_vq_z2;

    // Shift SOGI delay states
    sogi_vg_z2 = sogi_vg_z1;
    sogi_vg_z1 = grid_v_sim;
    sogi_vd_z2 = sogi_vd_z1;
    sogi_vd_z1 = vd;
    sogi_vq_z2 = sogi_vq_z1;
    sogi_vq_z1 = vq;

    // 3. Phase Detector & PI Loop Filter (PLL)
    float sin_theta = sinf(pll_theta);
    float cos_theta = cosf(pll_theta);
    float pll_err = vd * cos_theta + vq * sin_theta;

    // PI Integrator
    pll_int_err += pll_ki * TS_SEC * pll_err;
    // Frequency anti-windup (limit output frequency range: 45Hz to 65Hz)
    float freq_int_limit_max = 2.0f * 3.14159265f * (65.0f - 60.0f);
    float freq_int_limit_min = 2.0f * 3.14159265f * (45.0f - 60.0f);
    if(pll_int_err > freq_int_limit_max) pll_int_err = freq_int_limit_max;
    if(pll_int_err < freq_int_limit_min) pll_int_err = freq_int_limit_min;

    float omega_est = omega_nom + pll_kp * pll_err + pll_int_err;
    pll_freq = omega_est / (2.0f * 3.14159265f);

    // Phase integration
    pll_theta += omega_est * TS_SEC;
    if(pll_theta > (2.0f * 3.14159265f))
    {
        pll_theta -= (2.0f * 3.14159265f);
    }

    // 4. PCMC Current Reference & Hardware CMPSS Ramp Updates
    if(cmd_grid_enable == 1)
    {
        // Positive-only AC current reference with offset to fit single-ended analog range
        // I_ref is bounded to ensure physical current bounds
        i_ref = cmd_i_amp * sin_theta;
        
        // Add DC offset bias (2.5A) to simulate grid current sensor voltage shift
        float i_peak_ref_limit = i_ref + 2.5f; 
        
        // Assume current sensor conversion: 0.4 V/A offset at 1.65V (offset 0A = 1.65V)
        // V_sensor = i_peak * 0.4 + 1.65V
        float v_sensor_peak = i_peak_ref_limit * 0.4f + 1.65f;
        
        // Map voltage (0V to 3.3V) to 12-bit DAC value (0 to 4095)
        float dac_val_f = (v_sensor_peak / 3.3f) * 4095.0f;
        uint16_t dac_val = (uint16_t)dac_val_f;
        if(dac_val > 4000) dac_val = 4000;
        if(dac_val < 100) dac_val = 100;

        dac_ramp_start = v_sensor_peak; // Store for monitoring

        // Write the calculated ramp maximum peak reference to CMPSS Shadow Register
        CMPSS_setMaxRampValue(CMPSS1_BASE, dac_val);
    }
    else
    {
        i_ref = 0.0f;
        CMPSS_setMaxRampValue(CMPSS1_BASE, 0);
        dac_ramp_start = 0.0f;
    }

    // 5. High-Fidelity Sub-microsecond PCMC Hardware-in-Loop Emulation
    // This section simulates the physical inductor current rising/falling and 
    // interacting with CMPSS & ePWM Trip Zone within this 20us interrupt step.
    {
        float S_n = 2.0f; // Inductor current rising slope (A/us) when PWM = 1
        float S_f = 1.5f; // Inductor current falling slope (A/us) when PWM = 0
        float R_sense = 0.4f; // 0.4 V/A sensor gain
        float S_e = 9.0f * (3.3f / 4095.0f) * 200.0f; // Slope Compensation: 9 LSB/CLK = 1.45 V/us

        float i_ind = ind_current_sim;
        uint16_t pwm_out = 1; // Start of period: EPWM1A forced High
        float trip_time_us = 20.0f; // Default trip at end of cycle (no trip)

        // Run sub-microsecond integration steps (20 steps of 1us each)
        uint16_t step;
        for(step = 0; step < 20; step++)
        {
            float t_us = (float)step;
            
            // Calculate active CMPSS DAC voltage (decrements by S_e over time)
            float cmpss_dac_v = dac_ramp_start - S_e * t_us;
            if(cmpss_dac_v < 0.0f) cmpss_dac_v = 0.0f;

            // Convert simulated current to sensor voltage
            float v_ind_sense = i_ind * R_sense + 1.65f;

            if(pwm_out == 1)
            {
                // Current rises
                i_ind += S_n * 1.0f; // 1us step

                // Trip condition: Check if inductor current voltage crosses DAC ramp limit,
                // or if it exceeds max duty cycle CMPA limit (18us)
                if((v_ind_sense >= cmpss_dac_v) || (t_us >= 18.0f))
                {
                    pwm_out = 0; // Hardware Trip Zone (CBC) triggers, PWM turned off
                    trip_time_us = t_us;
                    cbc_trip_count++;
                }
            }
            else
            {
                // Current falls
                i_ind -= S_f * 1.0f;
                if(i_ind < 0.0f) i_ind = 0.0f;
            }
        }

        // Save simulated variables back for expressions display
        ind_current_sim = i_ind;
        duty_cycle = trip_time_us / 20.0f;
    }

    // Acknowledge Timer Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
