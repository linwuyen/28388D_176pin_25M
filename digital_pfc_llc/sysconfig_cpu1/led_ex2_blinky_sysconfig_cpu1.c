//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Digital PFC (Power Factor Correction) Control (CPU1)
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
#define EPwm2Regs         (*((volatile struct EPWM_REGS *)0x00004100))
#define AdcaRegs          (*((volatile struct ADC_REGS *)0x00007400))
#define AdcaResultRegs    (*((volatile struct ADC_RESULT_REGS *)0x00000B00))

// Interrupt prototypes
__interrupt void adcA1_isr(void);

// Peripheral Initialization Prototypes
void initEPWM2(void);
void initCpuTimer0(void);
void initADCA(void);

// PFC Global Variables for Watch/Monitoring
volatile float32_t pfc_v_ac = 0.0f;       // Sampled/Simulated AC line voltage (V)
volatile float32_t pfc_i_ac = 0.0f;       // Sampled/Simulated Boost inductor current (A)
volatile float32_t pfc_v_bus = 380.0f;    // Sampled/Simulated DC Bus voltage (V)
volatile float32_t pfc_v_bus_ref = 400.0f;// Target DC Bus voltage (V)
volatile float32_t pfc_i_ref = 0.0f;      // Current loop command reference (A)
volatile float32_t pfc_duty = 0.0f;       // Boost ePWM duty cycle command (0.0f to 1.0f)
volatile float32_t pfc_i_amp = 1.0f;      // Voltage loop PI output amplitude (A)

// Nonlinear Voltage Loop parameters
const float32_t PFC_V_KP0 = 0.15f;        // Base Kp for voltage loop
const float32_t PFC_V_KI0 = 2.5f;         // Base Ki for voltage loop
const float32_t PFC_V_KNl = 0.005f;       // Nonlinear error amplification factor
const float32_t PFC_V_TS = 200.0e-6f;     // Voltage loop execution period (5kHz = every 10 current cycles)
static float32_t pfc_v_pi_int = 0.5f;     // Integrator state

// Inductor Current Loop parameters (Anti-Windup PI)
const float32_t PFC_I_KP = 0.8f;          // Inductor current loop Kp
const float32_t PFC_I_KI = 150.0f;        // Inductor current loop Ki
const float32_t PFC_I_TS = 20.0e-6f;      // Inductor current loop execution period (50kHz)
static float32_t pfc_i_pi_int = 0.0f;     // Integrator state

// Inductor model internal states (for simulation in ADC ISR)
static float32_t sim_vac_phase = 0.0f;

//
// Main
//
void main(void)
{
    // Initialize device clock and peripherals
    Device_init();

    // Boot CPU2 core
    Device_bootCPU2(BOOT_MODE_CPU2);

    // Transfer write access of ADCB and EPWM3 to CPU2 (LLC controller)
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_ADCB, SYSCTL_CPUSEL_CPU2);
    SysCtl_selectCPUForPeripheralInstance(SYSCTL_CPUSEL_EPWM3, SYSCTL_CPUSEL_CPU2);

    // Initialize GPIO and configure CPU1 LED (GPIO0)
    Device_initGPIO();

    // Initialize settings from SysConfig
    Board_init();

    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    Interrupt_initModule();

    // Initialize the PIE vector table with pointers to the shell Interrupt Service Routines (ISR).
    Interrupt_initVectorTable();

    // Register and enable ADCA1 Interrupt (Group 1, Channel 1)
    Interrupt_register(INT_ADCA1, adcA1_isr);
    Interrupt_enable(INT_ADCA1);

    // Initialize peripherals controlled by CPU1
    initEPWM2();
    initCpuTimer0();
    initADCA();

    // Sync CPUs so the control starts together
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Loop Forever
    uint32_t ms_counter = 0;
    for(;;)
    {
        // Non-blocking IPC: Send Vbus to CPU2 to guide LLC load control
        if(Cpu1toCpu2IpcRegs.CPU1TOCPU2IPCFLG.bit.IPC10 == 0)
        {
            // Write pfc_v_bus to CPU1_TO_CPU2_MSG_RAM (0x03A000)
            // Send float32_t directly as bits
            *((volatile float32_t *)0x03A000) = pfc_v_bus;

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

//
// 50kHz ADCA1 Interrupt Service Routine (ISR)
// Triggers when SOC2 conversion completes
//
__interrupt void adcA1_isr(void)
{
    static uint16_t v_loop_divider = 0;

    // 1. Clear ADCA Interrupt status
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

    // 2. Simulated Grid and Boost Stage Model (since no real power board is attached)
    // Simulates 110Vrms, 60Hz input (amplitude = 155.5V)
    sim_vac_phase += 0.007539822f; // 2 * pi * 60Hz * 20us
    if(sim_vac_phase >= 6.283185f)
    {
        sim_vac_phase -= 6.283185f;
    }
    pfc_v_ac = 155.56f * sinf(sim_vac_phase);

    // Simulates dynamic load steps to test nonlinear loop
    // Every 4 seconds, load steps between 1.0A and 6.0A load current on the DC bus
    static uint32_t sim_cycle_cnt = 0;
    float32_t pfc_load_curr = 2.0f; // Amps on Vbus
    sim_cycle_cnt++;
    if(sim_cycle_cnt >= 200000) // 4 seconds at 50kHz
    {
        if(sim_cycle_cnt >= 400000)
        {
            sim_cycle_cnt = 0;
        }
        pfc_load_curr = 6.0f; // High load step
    }

    // PFC Output Bus Voltage simulation: C * dV/dt = I_pfc - I_load
    // I_pfc_power = 0.95 * V_ac * I_ac, I_pfc = I_pfc_power / V_bus
    float32_t pfc_p_in = fabsf(pfc_v_ac) * pfc_i_ac * 0.95f;
    float32_t pfc_i_bus = pfc_p_in / pfc_v_bus;
    // C_bus = 470uF. dV = (I_bus - I_load) * dt / C
    pfc_v_bus += (pfc_i_bus - pfc_load_curr) * 20e-6f / 470.0e-6f;
    // Add realistic noise to Vbus (e.g. 120Hz ripple)
    float32_t noise_vbus = 4.0f * sinf(2.0f * sim_vac_phase);
    float32_t feedback_v_bus = pfc_v_bus + noise_vbus;

    // 3. PFC Voltage Loop (Outer Loop: Executed at 5kHz)
    v_loop_divider++;
    if(v_loop_divider >= 10)
    {
        v_loop_divider = 0;

        // Nonlinear Error processing
        float32_t v_err = pfc_v_bus_ref - feedback_v_bus;
        
        // e_nl = e * (1 + Knl * e^2)
        float32_t v_err_nl = v_err * (1.0f + PFC_V_KNl * v_err * v_err);

        // Adjust PI gains dynamically
        float32_t kp_v = PFC_V_KP0 * (1.0f + 0.05f * v_err * v_err);
        float32_t ki_v = PFC_V_KI0 * (1.0f + 0.05f * v_err * v_err);

        // PI algorithm
        pfc_v_pi_int += ki_v * PFC_V_TS * v_err_nl;

        // Integrator Anti-windup
        if(pfc_v_pi_int > 12.0f)       pfc_v_pi_int = 12.0f;
        else if(pfc_v_pi_int < 0.0f)   pfc_v_pi_int = 0.0f;

        pfc_i_amp = kp_v * v_err_nl + pfc_v_pi_int;
        if(pfc_i_amp > 15.0f)          pfc_i_amp = 15.0f;
        else if(pfc_i_amp < 0.1f)      pfc_i_amp = 0.1f;
    }

    // 4. Current Loop Reference Generator
    // I_ref = I_amp * |sin(theta_ac)|
    float32_t vac_normalized = fabsf(pfc_v_ac) / 155.56f;
    pfc_i_ref = pfc_i_amp * vac_normalized;

    // Inductor Current feedback simulation: dI/dt = (V_ac - (1-d)*V_bus)/L
    // In real power electronics, this is sampled from ADC.
    float32_t sim_L = 1.0e-3f; // 1mH
    float32_t v_boost = pfc_v_ac;
    if(pfc_v_ac >= 0.0f)
    {
        v_boost = pfc_v_ac - (1.0f - pfc_duty) * pfc_v_bus;
    }
    else
    {
        v_boost = pfc_v_ac + (1.0f - pfc_duty) * pfc_v_bus;
    }
    pfc_i_ac += (v_boost * 20e-6f) / sim_L;
    // Inductor physical behavior clamp (reconstructed rectified current)
    if(pfc_i_ac < 0.0f) pfc_i_ac = 0.0f;

    // 5. Inductor Current Loop (Inner Loop: Executed at 50kHz)
    float32_t i_err = pfc_i_ref - pfc_i_ac;

    // Calculate duty cycle command
    float32_t pfc_duty_unsat = PFC_I_KP * i_err + pfc_i_pi_int;

    // Inductor current loop Anti-windup via Clamping
    pfc_duty = pfc_duty_unsat;
    if(pfc_duty > 0.95f)       pfc_duty = 0.95f;
    else if(pfc_duty < 0.05f)  pfc_duty = 0.05f;

    // If duty cycle is not saturated, or error has opposite sign of saturation, integrate
    bool is_saturated = (pfc_duty_unsat != pfc_duty);
    bool same_sign = ((i_err > 0.0f && pfc_duty_unsat > 0.95f) || (i_err < 0.0f && pfc_duty_unsat < 0.05f));
    
    if(!is_saturated || !same_sign)
    {
        pfc_i_pi_int += PFC_I_KI * PFC_I_TS * i_err;
        // Safety bound on integral term itself
        if(pfc_i_pi_int > 1.0f)       pfc_i_pi_int = 1.0f;
        else if(pfc_i_pi_int < -0.2f) pfc_i_pi_int = -0.2f;
    }

    // 6. Output to PWM Register
    // ePWM2 is in Up-Down mode, TBPRD = 2000.
    // Boost duty cycle represents switch turn-on time.
    uint16_t cmp_val = (uint16_t)(2000.0f * (1.0f - pfc_duty));
    EPwm2Regs.CMPA.bit.CMPA = cmp_val;

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Initialize ePWM2 (PFC Boost PWM, Up-down count, 50kHz)
//
void initEPWM2(void)
{
    EALLOW;
    EPwm2Regs.TBPRD = 2000;             // Period = 2000 (symmetric 50kHz)
    EPwm2Regs.TBCTR = 0;                // Clear counter
    EPwm2Regs.TBCTL.bit.CTRMODE = 2;    // Up-Down count mode
    EPwm2Regs.TBCTL.bit.PHSEN = 0;      // Phase loading disabled
    EPwm2Regs.TBCTL.bit.PRDLD = 0;      // Load from shadow
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = 0;  // HSPCLKDIV = /1
    EPwm2Regs.TBCTL.bit.CLKDIV = 0;     // CLKDIV = /1

    // Action Qualifiers for Boost Switch (ePWM2A active on high duty)
    EPwm2Regs.AQCTLA.bit.CAU = 2;       // Action on CMPA Up: Set High (Turn on)
    EPwm2Regs.AQCTLA.bit.CAD = 1;       // Action on CMPA Down: Clear Low (Turn off)

    EPwm2Regs.CMPA.bit.CMPA = 1000;     // Initial 50% duty
    EDIS;
}

//
// Initialize CpuTimer0 (Triggers ADC at 50kHz)
//
void initCpuTimer0(void)
{
    // 200MHz SYSCLK, 50kHz period = 4000 cycles
    CPUTimer_setPeriod(CPUTIMER0_BASE, 4000 - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//
// Initialize ADCA (Triggered by CPU1 Timer 0)
//
void initADCA(void)
{
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCA_BASE);
    DEVICE_DELAY_US(1000);

    // SOC0, SOC1, SOC2 setup
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_CPU1_TINT0, ADC_CH_ADCIN0, 15);
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER1, ADC_TRIGGER_CPU1_TINT0, ADC_CH_ADCIN1, 15);
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER2, ADC_TRIGGER_CPU1_TINT0, ADC_CH_ADCIN2, 15);

    // Interrupt 1 triggered by SOC2 completion
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER2);
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
}
