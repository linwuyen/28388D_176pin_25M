//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  Sensorless PMSM FOC with Luenberger Observer (CPU2)
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
#define EPwm1Regs         (*((volatile struct EPWM_REGS *)0x00004000))
#define EPwm2Regs         (*((volatile struct EPWM_REGS *)0x00004100))
#define EPwm3Regs         (*((volatile struct EPWM_REGS *)0x00004200))
#define AdcaRegs          (*((volatile struct ADC_REGS *)0x00007400))
#define AdcaResultRegs    (*((volatile struct ADC_RESULT_REGS *)0x00000B00))

// Interrupt prototypes
__interrupt void ipc0_isr(void);
__interrupt void adcA1_isr(void);

// Peripheral Initialization Prototypes
void initEPWMs(void);
void initCpuTimer0(void);
void initADCA(void);

// Motor Parameters (Typical small PMSM)
const float32_t PMSM_RS = 0.5f;          // Stator resistance (Ohm)
const float32_t PMSM_LS = 0.002f;         // Stator inductance (H)
const float32_t PMSM_PM = 0.05f;          // Permanent magnet flux linkage (Wb)
const float32_t PMSM_POLES = 4.0f;        // Number of pole pairs
const float32_t PMSM_J = 0.005f;          // Inertia (kg*m^2)

// Execution periods
const float32_t FOC_TS = 100.0e-6f;       // Control execution period (10kHz)

// FOC Commands and States
volatile float32_t foc_id_ref = 0.0f;     // d-axis current command (A)
volatile float32_t foc_iq_ref = 0.0f;     // q-axis current command (A)
volatile float32_t motor_ia = 0.0f;       // Motor phase A current (A)
volatile float32_t motor_ib = 0.0f;       // Motor phase B current (A)
volatile float32_t motor_ic = 0.0f;       // Motor phase C current (A)

// Clark and Park outputs
volatile float32_t foc_i_alpha = 0.0f;
volatile float32_t foc_i_beta = 0.0f;
volatile float32_t foc_id = 0.0f;
volatile float32_t foc_iq = 0.0f;
volatile float32_t foc_vd = 0.0f;
volatile float32_t foc_vq = 0.0f;
volatile float32_t foc_v_alpha = 0.0f;
volatile float32_t foc_v_beta = 0.0f;

// Luenberger Observer States and Gains
volatile float32_t obs_i_alpha = 0.0f;    // Estimated alpha current (A)
volatile float32_t obs_i_beta = 0.0f;     // Estimated beta current (A)
volatile float32_t obs_e_alpha = 0.0f;    // Estimated alpha Back-EMF (V)
volatile float32_t obs_e_beta = 0.0f;     // Estimated beta Back-EMF (V)

// Observer Feedback Gains g1 & g2 (tuned for 10kHz execution)
const float32_t OBS_G1 = 3500.0f;
const float32_t OBS_G2 = 800000.0f;

// PLL rotor position estimator states
volatile float32_t pll_theta_est = 0.0f;   // Estimated electrical angle (rad)
volatile float32_t pll_omega_est = 0.0f;   // Estimated electrical speed (rad/s)
static float32_t pll_pi_int = 0.0f;       // PLL Loop filter integrator
const float32_t PLL_KP = 12.0f;
const float32_t PLL_KI = 1000.0f;

// Current PI Controllers States & Gains
static float32_t pi_d_int = 0.0f;
static float32_t pi_q_int = 0.0f;
const float32_t FOC_C_KP = 2.5f;          // Current loop Kp
const float32_t FOC_C_KI = 300.0f;        // Current loop Ki

// Simulated PMSM motor model physical states (for closed loop simulation)
static float32_t sim_i_alpha = 0.0f;
static float32_t sim_i_beta = 0.0f;
static float32_t sim_theta_rotor = 0.0f;  // Actual electrical rotor angle
static float32_t sim_omega_rotor = 0.0f;  // Actual electrical rotor speed
static float32_t sim_load_torque = 0.2f;  // Constant load torque (N*m)

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

    // Register and enable ADCA1 Interrupt (Group 1, Channel 1)
    Interrupt_register(INT_ADCA1, adcA1_isr);
    Interrupt_enable(INT_ADCA1);

    // Initialize peripherals controlled by CPU2
    initEPWMs();
    initCpuTimer0();
    initADCA();

    // Sync CPUs so the control starts together
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Loop Forever
    for(;;)
    {
        // Background slow blink to show CPU2 is running
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
    // Verify CPU1 set IPC_FLAG10 (Iq_ref update flag)
    if(Cpu2toCpu1IpcRegs.CPU1TOCPU2IPCSTS.bit.IPC10 == 1)
    {
        // Read Iq_ref command from CPU1_TO_CPU2_MSG_RAM (0x03A000)
        foc_iq_ref = *((volatile float32_t *)0x03A000);

        // Clear/ACK both IPC_FLAG10 and IPC_FLAG0 flags
        EALLOW;
        Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 10) | (1UL << 0);
        EDIS;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// 10kHz ADCA1 Interrupt Service Routine (ISR)
// Triggers when ADCA SOC1 conversion is complete.
//
__interrupt void adcA1_isr(void)
{
    // 1. Clear ADCA Interrupt flag
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

    // 2. Simulated PMSM motor plant dynamics
    // Electrical state simulation: dI/dt = (V - R*I - E)/L
    float32_t sim_e_alpha = -PMSM_PM * sim_omega_rotor * sinf(sim_theta_rotor);
    float32_t sim_e_beta = PMSM_PM * sim_omega_rotor * cosf(sim_theta_rotor);
    
    sim_i_alpha += (foc_v_alpha - PMSM_RS * sim_i_alpha - sim_e_alpha) * FOC_TS / PMSM_LS;
    sim_i_beta += (foc_v_beta - PMSM_RS * sim_i_beta - sim_e_beta) * FOC_TS / PMSM_LS;
    
    // Torque simulation: Te = 1.5 * poles * Magnet_Flux * i_q
    // (We transform sim_i_alpha/beta to sim_i_q)
    float32_t sim_iq = -sim_i_alpha * sinf(sim_theta_rotor) + sim_i_beta * cosf(sim_theta_rotor);
    float32_t sim_Te = 1.5f * PMSM_POLES * PMSM_PM * sim_iq;
    
    // Mechanical state simulation: d_omega/dt = (Te - TL)/J
    // Mechanical rotor speed: omega_mech. Electrical speed: omega_elec = omega_mech * pole_pairs
    float32_t sim_omega_mech_dot = (sim_Te - sim_load_torque) / PMSM_J;
    float32_t sim_omega_mech = sim_omega_rotor / PMSM_POLES;
    sim_omega_mech += sim_omega_mech_dot * FOC_TS;
    sim_omega_rotor = sim_omega_mech * PMSM_POLES;
    
    // Rotor position integration
    sim_theta_rotor += sim_omega_rotor * FOC_TS;
    if(sim_theta_rotor >= 6.283185f)      sim_theta_rotor -= 6.283185f;
    else if(sim_theta_rotor < 0.0f)       sim_theta_rotor += 6.283185f;

    // Simulate three phase current readings (Clarke input)
    motor_ia = sim_i_alpha;
    motor_ib = -0.5f * sim_i_alpha + 0.8660254f * sim_i_beta;
    motor_ic = -motor_ia - motor_ib;

    // 3. FOC Front-End: Clarke Transformation
    foc_i_alpha = motor_ia;
    foc_i_beta = 0.5773502f * (motor_ia + 2.0f * motor_ib);

    // 4. Luenberger Observer State Equations
    float32_t i_err_alpha = foc_i_alpha - obs_i_alpha;
    float32_t i_err_beta = foc_i_beta - obs_i_beta;

    // Update observer estimated currents (Euler discretization)
    obs_i_alpha += FOC_TS * ((-PMSM_RS/PMSM_LS)*obs_i_alpha + (1.0f/PMSM_LS)*(foc_v_alpha - obs_e_alpha) + OBS_G1 * i_err_alpha);
    obs_i_beta  += FOC_TS * ((-PMSM_RS/PMSM_LS)*obs_i_beta  + (1.0f/PMSM_LS)*(foc_v_beta  - obs_e_beta)  + OBS_G1 * i_err_beta);

    // Update observer estimated Back-EMFs
    obs_e_alpha -= FOC_TS * OBS_G2 * i_err_alpha;
    obs_e_beta  -= FOC_TS * OBS_G2 * i_err_beta;

    // 5. PLL-based Rotor Angle and Speed Estimator
    // pll_err = -e_alpha * cos(theta_est) - e_beta * sin(theta_est) = Psi * omega * sin(theta_actual - theta_est)
    float32_t pll_err = -obs_e_alpha * cosf(pll_theta_est) - obs_e_beta * sinf(pll_theta_est);

    // PLL PI Loop filter
    pll_pi_int += PLL_KI * FOC_TS * pll_err;
    if(pll_pi_int > 500.0f)        pll_pi_int = 500.0f;
    else if(pll_pi_int < -500.0f)  pll_pi_int = -500.0f;

    pll_omega_est = PLL_KP * pll_err + pll_pi_int;
    
    pll_theta_est += pll_omega_est * FOC_TS;
    if(pll_theta_est >= 6.283185f)      pll_theta_est -= 6.283185f;
    else if(pll_theta_est < 0.0f)       pll_theta_est += 6.283185f;

    // 6. Park Transformation using Estimated Angle
    foc_id = foc_i_alpha * cosf(pll_theta_est) + foc_i_beta * sinf(pll_theta_est);
    foc_iq = -foc_i_alpha * sinf(pll_theta_est) + foc_i_beta * cosf(pll_theta_est);

    // 7. DQ Axis PI Current Loop (with Clamping Anti-Windup)
    float32_t err_id = foc_id_ref - foc_id;
    float32_t err_iq = foc_iq_ref - foc_iq;

    // Unsaturated control outputs
    float32_t vd_unsat = FOC_C_KP * err_id + pi_d_int;
    float32_t vq_unsat = FOC_C_KP * err_iq + pi_q_int;

    // Saturation limit: V_max = 120.0V (based on Bus voltage)
    float32_t v_limit = 120.0f;
    foc_vd = vd_unsat;
    if(foc_vd > v_limit)       foc_vd = v_limit;
    else if(foc_vd < -v_limit) foc_vd = -v_limit;

    foc_vq = vq_unsat;
    if(foc_vq > v_limit)       foc_vq = v_limit;
    else if(foc_vq < -v_limit) foc_vq = -v_limit;

    // Integrator updates with clamping check
    bool sat_d = (vd_unsat != foc_vd);
    bool same_sign_d = ((err_id > 0.0f && vd_unsat > v_limit) || (err_id < 0.0f && vd_unsat < -v_limit));
    if(!sat_d || !same_sign_d)
    {
        pi_d_int += FOC_C_KI * FOC_TS * err_id;
    }

    bool sat_q = (vq_unsat != foc_vq);
    bool same_sign_q = ((err_iq > 0.0f && vq_unsat > v_limit) || (err_iq < 0.0f && vq_unsat < -v_limit));
    if(!sat_q || !same_sign_q)
    {
        pi_q_int += FOC_C_KI * FOC_TS * err_iq;
    }

    // 8. Inverse Park Transformation
    foc_v_alpha = foc_vd * cosf(pll_theta_est) - foc_vq * sinf(pll_theta_est);
    foc_v_beta  = foc_vd * sinf(pll_theta_est) + foc_vq * cosf(pll_theta_est);

    // 9. Space Vector PWM Duty cycle computation (Min-Max injection simplification)
    float32_t va = foc_v_alpha;
    float32_t vb = -0.5f * foc_v_alpha + 0.8660254f * foc_v_beta;
    float32_t vc = -0.5f * foc_v_alpha - 0.8660254f * foc_v_beta;
    
    // Find min and max to extract zero-sequence voltage
    float32_t v_max = va;
    if(vb > v_max) v_max = vb;
    if(vc > v_max) v_max = vc;

    float32_t v_min = va;
    if(vb < v_min) v_min = vb;
    if(vc < v_min) v_min = vc;

    float32_t v_zero = 0.5f * (v_max + v_min);
    float32_t van = va - v_zero;
    float32_t vbn = vb - v_zero;
    float32_t vcn = vc - v_zero;

    // Normalize duties by Bus Voltage (300V)
    float32_t v_dc = 300.0f;
    float32_t da = 0.5f + (van / v_dc);
    float32_t db = 0.5f + (vbn / v_dc);
    float32_t dc = 0.5f + (vcn / v_dc);

    // Clamp duties to [0.02, 0.98] to prevent PWM pulse loss
    if(da > 0.98f) da = 0.98f; else if(da < 0.02f) da = 0.02f;
    if(db > 0.98f) db = 0.98f; else if(db < 0.02f) db = 0.02f;
    if(dc > 0.98f) dc = 0.98f; else if(dc < 0.02f) dc = 0.02f;

    // Write duties to ePWM1, ePWM2, ePWM3 (Up-Down mode, TBPRD = 2000)
    EPwm1Regs.CMPA.bit.CMPA = (uint16_t)(2000.0f * (1.0f - da));
    EPwm2Regs.CMPA.bit.CMPA = (uint16_t)(2000.0f * (1.0f - db));
    EPwm3Regs.CMPA.bit.CMPA = (uint16_t)(2000.0f * (1.0f - dc));

    // 10. Write Estimated Speed to CPU1 via Msg RAM every 10 cycles (1kHz)
    static uint16_t speed_ram_divider = 0;
    speed_ram_divider++;
    if(speed_ram_divider >= 10)
    {
        speed_ram_divider = 0;
        // CPU2 to CPU1 Msg RAM is located at 0x03B000
        *((volatile float32_t *)0x03B000) = pll_omega_est;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Initialize ePWM1, ePWM2, ePWM3 (Three Phase Bridge, Up-Down, 50kHz)
//
void initEPWMs(void)
{
    EALLOW;
    
    // Configure ePWM1
    EPwm1Regs.TBPRD = 2000;
    EPwm1Regs.TBCTR = 0;
    EPwm1Regs.TBCTL.bit.CTRMODE = 2;    // Up-down count
    EPwm1Regs.TBCTL.bit.PHSEN = 0;
    EPwm1Regs.TBCTL.bit.PRDLD = 0;
    EPwm1Regs.TBCTL.bit.HSPCLKDIV = 0;
    EPwm1Regs.TBCTL.bit.CLKDIV = 0;
    EPwm1Regs.AQCTLA.bit.CAU = 2;       // Action on CAU Up: Set High
    EPwm1Regs.AQCTLA.bit.CAD = 1;       // Action on CAD Down: Clear Low
    EPwm1Regs.CMPA.bit.CMPA = 1000;

    // Configure ePWM2
    EPwm2Regs.TBPRD = 2000;
    EPwm2Regs.TBCTR = 0;
    EPwm2Regs.TBCTL.bit.CTRMODE = 2;
    EPwm2Regs.TBCTL.bit.PHSEN = 0;
    EPwm2Regs.TBCTL.bit.PRDLD = 0;
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = 0;
    EPwm2Regs.TBCTL.bit.CLKDIV = 0;
    EPwm2Regs.AQCTLA.bit.CAU = 2;
    EPwm2Regs.AQCTLA.bit.CAD = 1;
    EPwm2Regs.CMPA.bit.CMPA = 1000;

    // Configure ePWM3
    EPwm3Regs.TBPRD = 2000;
    EPwm3Regs.TBCTR = 0;
    EPwm3Regs.TBCTL.bit.CTRMODE = 2;
    EPwm3Regs.TBCTL.bit.PHSEN = 0;
    EPwm3Regs.TBCTL.bit.PRDLD = 0;
    EPwm3Regs.TBCTL.bit.HSPCLKDIV = 0;
    EPwm3Regs.TBCTL.bit.CLKDIV = 0;
    EPwm3Regs.AQCTLA.bit.CAU = 2;
    EPwm3Regs.AQCTLA.bit.CAD = 1;
    EPwm3Regs.CMPA.bit.CMPA = 1000;

    EDIS;
}

//
// Initialize CPU Timer 0 (Triggers ADC SOC at 10kHz)
//
void initCpuTimer0(void)
{
    // Configure CPU Timer 0 to overflow every 100us (10kHz)
    // CPU2 frequency is 200MHz, so period is 100us * 200MHz = 20000 cycles.
    CPUTimer_setPeriod(CPUTIMER0_BASE, 20000 - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//
// Initialize ADCA (SOC0 & SOC1 triggered by CPU2 Timer 0)
//
void initADCA(void)
{
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCA_BASE);
    DEVICE_DELAY_US(1000);
    
    // Configure SOC0 (Ia) and SOC1 (Ib) triggering on CPU2 Timer 0
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_CPU2_TINT0, ADC_CH_ADCIN0, 15);
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER1, ADC_TRIGGER_CPU2_TINT0, ADC_CH_ADCIN1, 15);
    
    // Configure Interrupt 1: triggered by SOC1 (Ib) conversion completion
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER1);
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
}
