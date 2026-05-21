//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE: SysConfig LED Blinky Example with SOGI-PLL (CPU2)
//
//#############################################################################

// Included Files
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"
#include <math.h>

// Direct peripheral register pointer mappings to bypass missing global variable linking
#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs      (*((volatile struct GPIO_DATA_REGS *)0x00007F00))
#define EPwm1Regs         (*((volatile struct EPWM_REGS *)0x00004000))
#define AdcaRegs          (*((volatile struct ADC_REGS *)0x00007400))
#define AdcaResultRegs    (*((volatile struct ADC_RESULT_REGS *)0x00000B00))

// Interrupt prototypes
__interrupt void ipc0_isr(void);
__interrupt void adcA1_isr(void);

// Peripheral Initialization Prototypes
void initEPWM1(void);
void initCpuTimer0(void);
void initADCA(void);

// SOGI-PLL Global Variables for Watch / Monitoring
volatile float32_t pll_v_grid = 0.0f;     // Input grid voltage (simulated or sampled)
volatile float32_t pll_v_prime = 0.0f;    // SOGI in-phase output (v')
volatile float32_t pll_qv_prime = 0.0f;   // SOGI quadrature output (qv')
volatile float32_t pll_theta = 0.0f;      // Estimated phase angle (theta)
volatile float32_t pll_freq = 60.0f;      // Estimated frequency in Hz
volatile float32_t pll_error = 0.0f;     // Phase error

// SOGI filter state variables
static float32_t sogi_v_g_z1 = 0.0f;
static float32_t sogi_v_g_z2 = 0.0f;
static float32_t sogi_vp_z1 = 0.0f;
static float32_t sogi_vp_z2 = 0.0f;
static float32_t sogi_qvp_z1 = 0.0f;
static float32_t sogi_qvp_z2 = 0.0f;

// SOGI Coefficients (50kHz sampling, 60Hz grid, damping = sqrt(2))
const float32_t SOGI_B0 = 0.00530311f;
const float32_t SOGI_B1 = 0.00001999f;
const float32_t SOGI_A1 = -1.98933709f;
const float32_t SOGI_A2 = 0.98939379f;

// PLL Loop Filter states and coefficients
static float32_t pll_pi_int = 0.0f;
const float32_t PLL_KP = 160.0f;
const float32_t PLL_KI = 20000.0f;
const float32_t PLL_TS = 20.0e-6f;          // 50kHz sampling period
const float32_t PLL_OMEGA_NOM = 376.991118f; // 2 * pi * 60

// Simulated grid phase variable
static float32_t sim_theta = 0.0f;

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

    // Initialize the PIE vector table with pointers to the shell Interrupt Service Routines (ISR).
    Interrupt_initVectorTable();

    // Register and enable CPU2 IPC0 Interrupt (Group 1, Channel 13)
    IPC_registerInterrupt(IPC_CPU2_L_CPU1_R, IPC_INT0, ipc0_isr);

    // Register and enable 50kHz ADCA1 Interrupt (Group 1, Channel 1)
    Interrupt_register(INT_ADCA1, adcA1_isr);
    Interrupt_enable(INT_ADCA1);

    // Initialize peripherals controlled by CPU2
    initEPWM1();
    initCpuTimer0();
    initADCA();

    // Sync CPUs so the blinking and control starts together
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Loop Forever
    for(;;)
    {
        // Background task: slow heartbeat blink to show CPU2 is running
        GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;
        DEVICE_DELAY_US(1000000); // 1 second interval
    }
}

//
// IPC0 Interrupt Service Routine (ISR)
// Triggers when CPU1 sets IPC_FLAG0.
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
// 50kHz ADCA1 Interrupt Service Routine (ISR)
// Triggers when ADCA SOC0 conversion is complete.
//
__interrupt void adcA1_isr(void)
{
    // 1. Clear ADCA Interrupt flag
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

    // 2. Generate simulated grid voltage for verification and test
    // phase step = 2 * pi * 60Hz * 20us = 0.007539822368 rad
    sim_theta += 0.007539822368f;
    if(sim_theta >= 6.283185307f)
    {
        sim_theta -= 6.283185307f;
    }
    
    // Simulate grid voltage of 1.0f amplitude at 60Hz
    pll_v_grid = sinf(sim_theta);

    // 3. SOGI QSG (Quadrature Signal Generator) Difference Equations (Tustin discretization)
    pll_v_prime = SOGI_B0 * (pll_v_grid - sogi_v_g_z2) - SOGI_A1 * sogi_vp_z1 - SOGI_A2 * sogi_vp_z2;
    pll_qv_prime = SOGI_B1 * (pll_v_grid + 2.0f * sogi_v_g_z1 + sogi_v_g_z2) - SOGI_A1 * sogi_qvp_z1 - SOGI_A2 * sogi_qvp_z2;

    // Update SOGI state history
    sogi_v_g_z2 = sogi_v_g_z1;
    sogi_v_g_z1 = pll_v_grid;

    sogi_vp_z2 = sogi_vp_z1;
    sogi_vp_z1 = pll_v_prime;

    sogi_qvp_z2 = sogi_qvp_z1;
    sogi_qvp_z1 = pll_qv_prime;

    // 4. PLL Phase Detector (PD)
    // error = v' * cos(theta) + qv' * sin(theta) = sin(theta_grid - theta_pll)
    pll_error = pll_v_prime * cosf(pll_theta) + pll_qv_prime * sinf(pll_theta);

    // 5. PLL Loop Filter (PI Controller)
    pll_pi_int += PLL_KI * PLL_TS * pll_error;

    // Anti-windup limit for integrator: limit to +/- 2 * pi * 10 Hz (+/- 62.83 rad/s)
    float32_t int_limit = 62.831853f;
    if(pll_pi_int > int_limit)       pll_pi_int = int_limit;
    else if(pll_pi_int < -int_limit) pll_pi_int = -int_limit;

    // Estimated grid angular frequency
    float32_t omega_est = PLL_OMEGA_NOM + PLL_KP * pll_error + pll_pi_int;

    // Clamp frequency to [45Hz, 65Hz] -> omega in [282.74, 408.41] rad/s
    float32_t omega_max = 408.407045f;
    float32_t omega_min = 282.743339f;
    if(omega_est > omega_max)      omega_est = omega_max;
    else if(omega_est < omega_min) omega_est = omega_min;

    pll_freq = omega_est / 6.283185307f;

    // 6. PLL Phase Integration
    pll_theta += omega_est * PLL_TS;
    if(pll_theta >= 6.283185307f)
    {
        pll_theta -= 6.283185307f;
    }
    else if(pll_theta < 0.0f)
    {
        pll_theta += 6.283185307f;
    }

    // 7. Modulate ePWM1 CMPA to synchronize output with the locked grid phase
    // Output duty cycle: centered at 50% (CMPA = 1000) with modulation depth of 0.8 (amplitude +/- 800)
    float32_t duty_cycle = 0.5f + 0.4f * sinf(pll_theta);
    uint16_t cmpa_val = (uint16_t)(2000.0f * duty_cycle);
    EPwm1Regs.CMPA.bit.CMPA = cmpa_val;

    // 8. Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Initialize ePWM1 (Symmetric PWM, 50kHz)
//
void initEPWM1(void)
{
    EALLOW;
    EPwm1Regs.TBPRD = 2000;             // Set period count (gives 50kHz at 100MHz EPWM clock)
    EPwm1Regs.TBCTR = 0;                // Clear timer counter
    EPwm1Regs.TBCTL.bit.CTRMODE = 2;    // Up-down count mode (symmetric)
    EPwm1Regs.TBCTL.bit.PHSEN = 0;      // Phase loading disabled
    EPwm1Regs.TBCTL.bit.PRDLD = 0;      // Active period register load from shadow
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0;  // High speed clock divider = /1
    EPwm1Regs.TBCTL.bit.CLKDIV = 0;     // Clock divider = /1

    // Action Qualifier configuration
    EPwm1Regs.AQCTLA.bit.CAU = 2;       // Force EPWM1A High on CAU event (count up match)
    EPwm1Regs.AQCTLA.bit.CAD = 1;       // Force EPWM1A Low on CAD event (count down match)
    
    // Initial compare match value (50% duty cycle)
    EPwm1Regs.CMPA.bit.CMPA = 1000;
    EDIS;
}

//
// Initialize CPU Timer 0 (Triggers ADC SOC at 50kHz)
//
void initCpuTimer0(void)
{
    // Configure CPU Timer 0 to overflow every 20us (50kHz)
    // CPU2 frequency is 200MHz, so period is 20us * 200MHz = 4000 cycles.
    CPUTimer_setPeriod(CPUTIMER0_BASE, 4000 - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    
    // Start CPU Timer 0
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//
// Initialize ADCA (SOC0 triggered by CPU2 Timer 0)
//
void initADCA(void)
{
    // ADC clock prescaler (divide SYSCLK by 4 for 50MHz ADC clock, as SYSCLK is 200MHz)
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    
    // Set 12-bit resolution and single-ended signal mode
    ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    
    // Set interrupt pulse mode (late interrupt, when conversion completes)
    ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
    
    // Enable the ADC converter
    ADC_enableConverter(ADCA_BASE);
    
    // Allow at least 1ms delay after power-up
    DEVICE_DELAY_US(1000);
    
    // Configure SOC0: trigger on CPU2 Timer 0, channel ADCIN0, sample window of 15 SYSCLK cycles
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_CPU2_TINT0, ADC_CH_ADCIN0, 15);
    
    // Configure Interrupt 1: triggered by SOC0 conversion completion
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
}

// End of File
