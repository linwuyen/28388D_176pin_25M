//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  Digital LLC Resonant Converter Control (CPU2)
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"
#include <math.h>

// Direct register mapping to bypass missing global variable definitions
#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs      (*((volatile struct GPIO_DATA_REGS *)0x00007F00))
#define EPwm3Regs         (*((volatile struct EPWM_REGS *)0x00004200))
#define AdcbRegs          (*((volatile struct ADC_REGS *)0x00007480))
#define AdcbResultRegs    (*((volatile struct ADC_RESULT_REGS *)0x00000B20))

// Interrupt prototypes
__interrupt void ipc0_isr(void);
__interrupt void adcB1_isr(void);

// Peripheral Initialization Prototypes
void initEPWM3(void);
void initCpuTimer0(void);
void initADCB(void);

// LLC Global Variables for Watch/Monitoring
volatile float32_t llc_v_out = 0.0f;       // Sampled/Simulated LLC output voltage (V)
volatile float32_t llc_v_out_ref = 48.0f;  // Target LLC output voltage (V)
volatile float32_t llc_fs = 100.0e3f;     // Switching frequency command (Hz)
volatile float32_t llc_v_bus = 0.0f;       // Received PFC Bus voltage from CPU1 (V)
volatile float32_t llc_error = 0.0f;      // Voltage error (V)

// LLC Non-linear / Gain Scheduling PI parameters
const float32_t LLC_KP0 = 300.0f;          // Base Kp
const float32_t LLC_KI0 = 50000.0f;        // Base Ki
const float32_t LLC_FR = 100.0e3f;         // LLC resonant frequency (100kHz)
const float32_t LLC_TS = 10.0e-6f;         // LLC loop execution period (100kHz)
static float32_t llc_pi_int = 0.0f;        // Integrator state

// LLC plant simulator internal states
static float32_t sim_llc_i_out = 2.0f;     // LLC simulated output load current

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

    // Register and enable ADCB1 Interrupt (Group 1, Channel 2)
    Interrupt_register(INT_ADCB1, adcB1_isr);
    Interrupt_enable(INT_ADCB1);

    // Initialize peripherals controlled by CPU2
    initEPWM3();
    initCpuTimer0();
    initADCB();

    // Sync CPUs so the control starts together
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Loop Forever
    uint32_t ms_counter = 0;
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
    // Verify CPU1 set IPC_FLAG10 (Vbus broadcast flag)
    if(Cpu2toCpu1IpcRegs.CPU1TOCPU2IPCSTS.bit.IPC10 == 1)
    {
        // Read PFC bus voltage from CPU1_TO_CPU2_MSG_RAM (0x03A000)
        llc_v_bus = *((volatile float32_t *)0x03A000);

        // Clear/ACK both IPC_FLAG10 and IPC_FLAG0 flags
        EALLOW;
        Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 10) | (1UL << 0);
        EDIS;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// 100kHz ADCB1 Interrupt Service Routine (ISR)
// Triggers when ADCB SOC0 conversion is complete.
//
__interrupt void adcB1_isr(void)
{
    // 1. Clear ADCB Interrupt flag
    ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);

    // 2. LLC plant simulation (simulating secondary output voltage)
    // LLC Gain Curve approximation: Vout = Vbus * n * Gain(fs, Load)
    // where n = 1/8. Gain = 1 / (1 + Q * (fs/fr - fr/fs)^2)
    // Q depends on output load.
    static uint32_t sim_cycle_cnt = 0;
    sim_cycle_cnt++;
    if(sim_cycle_cnt >= 400000) // Steps load every 4 seconds at 100kHz
    {
        if(sim_cycle_cnt >= 800000)
        {
            sim_cycle_cnt = 0;
        }
        sim_llc_i_out = 10.0f; // High load current
    }
    else
    {
        sim_llc_i_out = 2.0f;  // Light load current
    }

    // Approximate LLC gain formula
    float32_t fn = llc_fs / LLC_FR;
    float32_t Q = 0.1f * sim_llc_i_out; // Q factor increases with load
    float32_t gain = 1.0f / sqrtf(1.0f + Q * Q * (fn - 1.0f/fn) * (fn - 1.0f/fn));
    
    // Target output voltage simulated. If Vbus is low, LLC output drops.
    float32_t llc_v_out_ideal = llc_v_bus * (1.0f / 8.0f) * gain;
    
    // Simulate output capacitor filter dynamics: C_out = 1000uF
    // dV = (I_llc - I_load) * dt / C
    float32_t llc_i_rect = (llc_v_out_ideal / (llc_v_out + 0.1f)) * sim_llc_i_out;
    llc_v_out += (llc_i_rect - sim_llc_i_out) * 10e-6f / 1000e-6f;
    if(llc_v_out < 0.0f) llc_v_out = 0.0f;

    // 3. LLC Voltage PI Controller with Gain Scheduling
    llc_error = llc_v_out_ref - llc_v_out;

    // If Vbus is below 350V, PFC is not ready. Keep LLC frequency high (min gain) to protect it.
    if(llc_v_bus < 350.0f)
    {
        llc_fs = 150.0e3f; // Max frequency = minimum output voltage
        llc_pi_int = 0.0f;
    }
    else
    {
        // Gain Scheduling: Compensate for the LLC non-linear gain slope (sensitivity)
        // Since gain drop per Hz is higher at low frequency (near fr), we use a smaller gain there.
        // At high frequency, the gain slope is flat, so we need higher PI gains.
        float32_t freq_ratio = llc_fs / LLC_FR;
        float32_t gain_scheduler = freq_ratio * freq_ratio; // (fs/fr)^2
        
        float32_t kp_llc = LLC_KP0 * gain_scheduler;
        float32_t ki_llc = LLC_KI0 * gain_scheduler;

        // Anti-Windup Clamping
        float32_t u_ctrl_unsat = kp_llc * llc_error + llc_pi_int;
        
        // LLC switching frequency (negative relationship: higher frequency -> lower gain)
        // fs = f_nominal - u_ctrl
        float32_t fs_unsat = 100.0e3f - u_ctrl_unsat;

        // Clamp frequency to [70kHz, 150kHz] to prevent ZCS region entry and high frequency limit
        llc_fs = fs_unsat;
        if(llc_fs > 150.0e3f)      llc_fs = 150.0e3f;
        else if(llc_fs < 70.0e3f)  llc_fs = 70.0e3f;

        // Integrator update with anti-windup clamping
        bool is_saturated = (fs_unsat != llc_fs);
        // Same sign saturation check (For negative control action: positive error wants to increase Vout,
        // which means decreasing fs. So if error > 0 and fs hits minimum, it is saturated.)
        bool same_sign = ((llc_error > 0.0f && fs_unsat < 70.0e3f) || (llc_error < 0.0f && fs_unsat > 150.0e3f));

        if(!is_saturated || !same_sign)
        {
            llc_pi_int += ki_llc * LLC_TS * llc_error;
            // Bound on internal integrator state itself
            if(llc_pi_int > 50000.0f)       llc_pi_int = 50000.0f;
            else if(llc_pi_int < -50000.0f) llc_pi_int = -50000.0f;
        }
    }

    // 4. Update ePWM3 Period (TBPRD) and Compare registers for PFM
    // EPWM CLK is 100MHz. ePWM3 is configured for Up-down count mode.
    // fs = 100MHz / (2 * TBPRD) -> TBPRD = 100MHz / (2 * fs)
    uint16_t tbprd_val = (uint16_t)(100.0e6f / (2.0f * llc_fs));
    
    EALLOW;
    EPwm3Regs.TBPRD = tbprd_val;
    // Maintain 50% duty cycle on A channel
    EPwm3Regs.CMPA.bit.CMPA = tbprd_val >> 1;
    EDIS;

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Initialize ePWM3 (LLC PFM, Up-down count with Deadband)
//
void initEPWM3(void)
{
    EALLOW;
    EPwm3Regs.TBPRD = 500;              // Initial 100kHz (100MHz / (2 * 500))
    EPwm3Regs.TBCTR = 0;                // Clear timer counter
    EPwm3Regs.TBCTL.bit.CTRMODE = 2;    // Up-down count mode (symmetric)
    EPwm3Regs.TBCTL.bit.PHSEN = 0;      // Phase loading disabled
    EPwm3Regs.TBCTL.bit.PRDLD = 0;      // Active period register load from shadow
    EPwm3Regs.TBCTL.bit.HSPCLKDIV = 0;  // High speed clock divider = /1
    EPwm3Regs.TBCTL.bit.CLKDIV = 0;     // Clock divider = /1

    // CMPA 50% duty cycle initially
    EPwm3Regs.CMPA.bit.CMPA = 250;

    // Set Action Qualifiers: Channel A active high
    EPwm3Regs.AQCTLA.bit.CAU = 2;       // Set high on CMPA Up
    EPwm3Regs.AQCTLA.bit.CAD = 1;       // Clear low on CMPA Down

    // Active Deadband Configuration (Complementary Output with 200ns Deadband)
    // 200ns deadband on 200MHz SYSCLK (EPWMCLK = 100MHz) -> 20 cycles
    EPwm3Regs.DBCTL.bit.OUT_MODE = 3;   // DB_FULL_ENABLE: dead-band is fully enabled
    EPwm3Regs.DBCTL.bit.POLSEL = 2;     // DB_ACTV_LOC: Active Low Complementary (A and B互補)
    EPwm3Regs.DBCTL.bit.IN_MODE = 0;    // EPWM1A is source for both falling and rising edge
    
    EPwm3Regs.DBRED.bit.DBRED = 20;     // Rising Edge Delay (RED) = 20 cycles
    EPwm3Regs.DBFED.bit.DBFED = 20;     // Falling Edge Delay (FED) = 20 cycles
    EDIS;
}

//
// Initialize CPU Timer 0 (Triggers ADC SOC at 100kHz)
//
void initCpuTimer0(void)
{
    // Configure CPU Timer 0 to overflow every 10us (100kHz)
    // CPU2 frequency is 200MHz, so period is 10us * 200MHz = 2000 cycles.
    CPUTimer_setPeriod(CPUTIMER0_BASE, 2000 - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//
// Initialize ADCB (SOC0 triggered by CPU2 Timer 0)
//
void initADCB(void)
{
    ADC_setPrescaler(ADCB_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCB_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCB_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCB_BASE);
    DEVICE_DELAY_US(1000);
    
    // Configure SOC0: trigger on CPU2 Timer 0, channel ADCIN0, sample window of 15 SYSCLK cycles
    ADC_setupSOC(ADCB_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_CPU2_TINT0, ADC_CH_ADCIN0, 15);
    
    // Configure Interrupt 1: triggered by SOC0 conversion completion
    ADC_setInterruptSource(ADCB_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_enableInterrupt(ADCB_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCB_BASE, ADC_INT_NUMBER1);
}
