//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE:  BMS AFE SPI-DMA & CRC-8 Verification with Retry FSM (CPU2)
//
// DESCRIPTION:
//   This project implements a simulated AFE (Analog Front-End) chip interface on CPU2.
//   1. SPI-A configuration (LSPCLK = 50MHz, 10MHz baud, 8-bit character mode, loopback enabled).
//   2. DMA Channel 1 (RX) and Channel 2 (TX) configured for 192-byte zero-copy transfer.
//   3. 256-byte pre-computed lookup table for fast CRC-8 (polynomial 0x07) validation.
//   4. A robust 3-retry FSM with a 100us CPU Timer 1 delay for transient fault recovery.
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"

// Peripheral register mapping bypasses
#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs      (*((volatile struct GPIO_DATA_REGS *)0x00007F00))

// 256-byte pre-computed CRC-8 Lookup Table (Poly = 0x07)
const uint16_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

// AFE SPI communication buffers (192 words each)
uint16_t afe_tx_buf[192];
uint16_t afe_rx_buf[192];

// FSM State definition
typedef enum {
    AFE_STATE_IDLE = 0,
    AFE_STATE_TX_RX_START = 1,
    AFE_STATE_WAIT_DMA = 2,
    AFE_STATE_CHECK_CRC = 3,
    AFE_STATE_RETRY_DELAY = 4,
    AFE_STATE_FAULT = 5,
    AFE_STATE_SUCCESS = 6
} AFE_State;

volatile AFE_State afe_fsm_state = AFE_STATE_IDLE;

// FSM Synchronization flags and counts
volatile bool timer0_flag = false;
volatile bool dma_done = false;
volatile uint16_t retry_count = 0;
volatile uint16_t led_counter = 0;

// Test error injection variables
volatile bool inject_crc_error = false;
volatile bool inject_transient_error = false;

// Active fault code (0 = OK, 3 = FAULT_AFE_COMM_FAILED)
volatile uint32_t active_fault_code = 0;

// Interrupt prototypes
__interrupt void ipc0_isr(void);
__interrupt void timer0_isr(void);
__interrupt void dma_ch1_isr(void);

// Peripheral configurations
void initSPIA(void);
void initDMA(void);
void initTimers(void);

// Fast CRC-8 computation function
uint16_t Calculate_CRC8(const uint16_t *data, uint16_t len)
{
    uint16_t crc = 0x00;
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        uint16_t index = (crc ^ (data[i] & 0xFF)) & 0xFF;
        crc = crc8_table[index];
    }
    return crc;
}

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

    // Register and enable interrupts
    IPC_registerInterrupt(IPC_CPU2_L_CPU1_R, IPC_INT0, ipc0_isr);

    Interrupt_register(INT_TIMER0, timer0_isr);
    Interrupt_enable(INT_TIMER0);

    Interrupt_register(INT_DMA_CH1, dma_ch1_isr);
    Interrupt_enable(INT_DMA_CH1);

    // Initialize simulated AFE interface peripherals
    initSPIA();
    initDMA();
    initTimers();

    // Sync CPUs
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    // Start Timer 0 (10ms background ticker)
    CPUTimer_startTimer(CPUTIMER0_BASE);

    bool timer1_running = false;
    uint16_t i;

    // Loop Forever (State Machine Loop)
    for(;;)
    {
        switch(afe_fsm_state)
        {
            case AFE_STATE_IDLE:
                if(timer0_flag)
                {
                    timer0_flag = false;
                    afe_fsm_state = AFE_STATE_TX_RX_START;
                }
                break;

            case AFE_STATE_TX_RX_START:
                // Clear RX Buffer
                for(i = 0; i < 192; i++)
                {
                    afe_rx_buf[i] = 0;
                }

                // Populate TX Buffer with mock incrementing test patterns
                for(i = 0; i < 191; i++)
                {
                    afe_tx_buf[i] = i & 0xFF;
                }
                // Calculate CRC-8 over elements 0..190 and write to the 192nd byte
                afe_tx_buf[191] = Calculate_CRC8((const uint16_t *)afe_tx_buf, 191);

                // Reset SPI FIFO to clear any residue before DMA transfer
                SPI_disableFIFO(SPIA_BASE);
                SPI_enableFIFO(SPIA_BASE);
                SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX8, SPI_FIFO_RX8);

                dma_done = false;

                // Reconfigure DMA Addresses to their base pointers
                DMA_configAddresses(DMA_CH1_BASE, (const void *)afe_rx_buf, (const void *)(SPIA_BASE + SPI_O_RXBUF));
                DMA_configAddresses(DMA_CH2_BASE, (const void *)(SPIA_BASE + SPI_O_TXBUF), (const void *)afe_tx_buf);

                // Start DMA Channels
                DMA_startChannel(DMA_CH1_BASE);
                DMA_startChannel(DMA_CH2_BASE);

                afe_fsm_state = AFE_STATE_WAIT_DMA;
                break;

            case AFE_STATE_WAIT_DMA:
                // Awaiting completion interrupt (dma_done is set in dma_ch1_isr)
                if(dma_done)
                {
                    afe_fsm_state = AFE_STATE_CHECK_CRC;
                }
                break;

            case AFE_STATE_CHECK_CRC:
            {
                // Inject fault simulation if test flags are set
                if(inject_crc_error)
                {
                    afe_rx_buf[0] ^= 0xFF; // Corrupt first byte permanently
                }
                else if(inject_transient_error && (retry_count == 0))
                {
                    afe_rx_buf[0] ^= 0xFF; // Corrupt only on first attempt
                }

                // Verify packet CRC integrity
                uint16_t calc_crc = Calculate_CRC8((const uint16_t *)afe_rx_buf, 191);
                uint16_t rx_crc = afe_rx_buf[191];

                if(calc_crc != rx_crc)
                {
                    if(retry_count < 3)
                    {
                        retry_count++;
                        afe_fsm_state = AFE_STATE_RETRY_DELAY;
                    }
                    else
                    {
                        // Hard failure reported
                        active_fault_code = 0x03; // FAULT_AFE_COMM_FAILED
                        afe_fsm_state = AFE_STATE_FAULT;
                    }
                }
                else
                {
                    // Success path
                    active_fault_code = 0x00;
                    retry_count = 0;
                    afe_fsm_state = AFE_STATE_SUCCESS;
                }
                break;
            }

            case AFE_STATE_RETRY_DELAY:
                // Non-blocking 100us timer wait
                if(!timer1_running)
                {
                    CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
                    CPUTimer_clearOverflowFlag(CPUTIMER1_BASE);
                    CPUTimer_startTimer(CPUTIMER1_BASE);
                    timer1_running = true;
                }
                else
                {
                    if(CPUTimer_getTimerOverflowStatus(CPUTIMER1_BASE))
                    {
                        CPUTimer_stopTimer(CPUTIMER1_BASE);
                        timer1_running = false;
                        afe_fsm_state = AFE_STATE_TX_RX_START; // Retry
                    }
                }
                break;

            case AFE_STATE_FAULT:
            case AFE_STATE_SUCCESS:
                afe_fsm_state = AFE_STATE_IDLE;
                break;

            default:
                afe_fsm_state = AFE_STATE_IDLE;
                break;
        }

        // Extremely short delay to prevent thrashing compiler optimization
        DEVICE_DELAY_US(1);
    }
}

//
// IPC0 Interrupt Service Routine (ISR)
// Triggers when CPU1 sets IPC_FLAG0.
//
__interrupt void ipc0_isr(void)
{
    // Clear/ACK both IPC_FLAG0 flags
    EALLOW;
    Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 0);
    EDIS;

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// CPU Timer 0 Interrupt Service Routine (ISR)
// Triggers every 10ms for BMS control loop tasks.
//
__interrupt void timer0_isr(void)
{
    timer0_flag = true;

    // Heartbeat LED blink (GPIO1 toggle) every 500ms (50 counts of 10ms)
    led_counter++;
    if(led_counter >= 50)
    {
        led_counter = 0;
        GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// DMA Channel 1 ISR (RX Completed)
//
__interrupt void dma_ch1_isr(void)
{
    DMA_stopChannel(DMA_CH1_BASE);
    DMA_stopChannel(DMA_CH2_BASE);
    dma_done = true;

    // Acknowledge PIE Group 7 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP7);
}

//
// Initialize SPI-A (Master Mode, 10MHz Baud)
//
void initSPIA(void)
{
    // Enable peripheral clock
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SPIA);

    SPI_disableModule(SPIA_BASE);

    // Protocol: Clock Polarity 0, Phase 0 (SPI_PROT_POL0PHA0), 10MHz Baud, 8-bit word length
    SPI_setConfig(SPIA_BASE, 50000000, SPI_PROT_POL0PHA0, SPI_MODE_MASTER, 10000000, 8);

    // Digital loopback enabled internally for validation
    SPI_enableLoopback(SPIA_BASE);

    // FIFO Enabled
    SPI_enableFIFO(SPIA_BASE);
    SPI_setFIFOInterruptLevel(SPIA_BASE, SPI_FIFO_TX8, SPI_FIFO_RX8);

    SPI_enableModule(SPIA_BASE);
}

//
// Initialize DMA Controller and configure Channel 1 (RX) & Channel 2 (TX)
//
void initDMA(void)
{
    // Enable peripheral clock
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_DMA);

    // Reset DMA controller
    DMA_initController();

    // DMA Channel 1 configuration (RX)
    DMA_configAddresses(DMA_CH1_BASE, (const void *)afe_rx_buf, (const void *)(SPIA_BASE + SPI_O_RXBUF));
    DMA_configBurst(DMA_CH1_BASE, 8, 0, 1);
    DMA_configTransfer(DMA_CH1_BASE, 24, 0, 1);
    DMA_configMode(DMA_CH1_BASE, DMA_TRIGGER_SPIARX, DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE | DMA_CFG_SIZE_16BIT);

    // Configure DMA RX interrupt at transfer completion
    DMA_setInterruptMode(DMA_CH1_BASE, DMA_INT_AT_END);
    DMA_enableInterrupt(DMA_CH1_BASE);
    DMA_enableTrigger(DMA_CH1_BASE);

    // DMA Channel 2 configuration (TX)
    DMA_configAddresses(DMA_CH2_BASE, (const void *)(SPIA_BASE + SPI_O_TXBUF), (const void *)afe_tx_buf);
    DMA_configBurst(DMA_CH2_BASE, 8, 1, 0);
    DMA_configTransfer(DMA_CH2_BASE, 24, 1, 0);
    DMA_configMode(DMA_CH2_BASE, DMA_TRIGGER_SPIATX, DMA_CFG_ONESHOT_DISABLE | DMA_CFG_CONTINUOUS_DISABLE | DMA_CFG_SIZE_16BIT);
    DMA_enableTrigger(DMA_CH2_BASE);
}

//
// Initialize CPU Timer 0 (10ms) and Timer 1 (100us retry delay)
//
void initTimers(void)
{
    // Timer 0: 10ms tick (2,000,000 CPU cycles at 200MHz)
    CPUTimer_setPeriod(CPUTIMER0_BASE, 2000000 - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);

    // Timer 1: 100us delay tick (20,000 CPU cycles at 200MHz)
    CPUTimer_setPeriod(CPUTIMER1_BASE, 20000 - 1);
    CPUTimer_setPreScaler(CPUTIMER1_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
    CPUTimer_stopTimer(CPUTIMER1_BASE);
}
