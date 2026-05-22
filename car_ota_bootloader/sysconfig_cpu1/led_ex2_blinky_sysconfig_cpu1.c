//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu1.c
//
// TITLE:  Car OTA Bootloader - CPU1 Main Code
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "F021_F2838x_C28x.h"
#include "../car_ota_shared.h"

// OTA structures mapped to Shared RAM
#pragma DATA_SECTION(ota_cmd_buf, "ramgs0")
volatile OtaCmdBuffer ota_cmd_buf;

#pragma DATA_SECTION(ota_status_buf, "ramgs1")
volatile OtaStatusBuffer ota_status_buf;

// State control flags
volatile bool new_cmd_pending = false;
Fapi_StatusType oReturnCheck;
Fapi_FlashStatusWordType oFlashStatusWord;

// Function Prototypes
void init_flash_api(void);
void process_ota_start(void);
void process_ota_program(void);
void process_ota_verify(void);
void process_ota_run(void);
uint32_t calculate_crc32_c28x(const uint16_t *data, uint32_t length_words);

//
// IPC Interrupt Service Routine (CM to CPU1)
//
__interrupt void ipc_cm_to_cpu1_isr(void)
{
    // A command is sent from CM. Set pending flag.
    new_cmd_pending = true;

    // Clear the IPC interrupt flag
    IPC_ackFlagRtoL(IPC_CPU1_L_CM_R, IPC_FLAG0);

    // Acknowledge PIE Group
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// Main
//
void main(void)
{
    // Initialize device clock and peripherals
    Device_init();

    // Assign RAMGS0 to CPU1 (CM behaves as master to write, CPU1 reads)
    MemCfg_setGSRAMControllerSel(MEMCFG_SECT_GS0, MEMCFG_GSRAMCONTROLLER_CPU1);

    // Assign RAMGS1 to CPU1 (CPU1 owns it for writing status)
    MemCfg_setGSRAMControllerSel(MEMCFG_SECT_GS1, MEMCFG_GSRAMCONTROLLER_CPU1);

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

    // Register IPC Interrupt for CM to CPU1
    IPC_registerInterrupt(IPC_CPU1_L_CM_R, IPC_INT0, &ipc_cm_to_cpu1_isr);

    // Sync CPUs
    IPC_sync(IPC_CPU1_L_CPU2_R, IPC_SYNC);

    // Initialize OTA Status Buffer
    ota_status_buf.status = OTA_STATUS_IDLE;
    ota_status_buf.error_code = 0;
    ota_status_buf.ack_chunk_idx = 0xFFFFFFFFU;

    // Initialize Flash API
    init_flash_api();

    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    EINT;
    ERTM;

    uint32_t led_counter = 0;

    for(;;)
    {
        // Check for new command from CM
        if (new_cmd_pending)
        {
            new_cmd_pending = false;
            uint32_t cmd = ota_cmd_buf.command;

            switch(cmd)
            {
                case OTA_CMD_START:
                    process_ota_start();
                    break;

                case OTA_CMD_PROGRAM:
                    process_ota_program();
                    break;

                case OTA_CMD_VERIFY:
                    process_ota_verify();
                    break;

                case OTA_CMD_RUN:
                    process_ota_run();
                    break;

                default:
                    break;
            }
        }

        // CPU1 Heartbeat LED toggling
        led_counter++;
        if (led_counter >= 500)
        {
            led_counter = 0;
            GPIO_togglePin(CPU1_LED);
        }

        DEVICE_DELAY_US(1000); // 1ms loop
    }
}

//
// Initialize Flash API modules and parameters
//
void init_flash_api(void)
{
    // Initialize Flash module wait-states (3 wait-states for 200MHz clock)
    Flash_initModule(FLASH0CTRL_BASE, FLASH0ECC_BASE, 3U);

    // Claim the pump semaphore
    Flash_claimPumpSemaphore(FLASH_CPU1_WRAPPER);

    // Initialize the API with CPU clock frequency (200 MHz)
    oReturnCheck = Fapi_initializeAPI(F021_CPU0_BASE_ADDRESS, 200U);
    if(oReturnCheck != Fapi_Status_Success)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_ERASE;
        ota_status_buf.error_code = oReturnCheck;
        Flash_releasePumpSemaphore();
        return;
    }

    // Set Active Flash Bank to Bank 1 (where the update is programmed)
    oReturnCheck = Fapi_setActiveFlashBank(Fapi_FlashBank1);
    if(oReturnCheck != Fapi_Status_Success)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_ERASE;
        ota_status_buf.error_code = oReturnCheck;
        Flash_releasePumpSemaphore();
        return;
    }

    // Release the pump semaphore
    Flash_releasePumpSemaphore();
}

//
// Erase Bank 1 Sector 0 (simulating boot sector update preparation)
//
void process_ota_start(void)
{
    ota_status_buf.status = OTA_STATUS_ERASING;
    ota_status_buf.error_code = 0;

    // Claim pump access
    Flash_claimPumpSemaphore(FLASH_CPU1_WRAPPER);

    // Issue Erase Sector command for Sector 0 of Bank 1 (starts at 0x0C0000)
    oReturnCheck = Fapi_issueAsyncCommandWithAddress(Fapi_EraseSector, (uint32_t *)0x0C0000U);

    // Wait for the Flash State Machine (FSM) to finish erase
    while (Fapi_checkFsmForReady() != Fapi_Status_FsmReady);

    if(oReturnCheck != Fapi_Status_Success || Fapi_getFsmStatus() != 0)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_ERASE;
        ota_status_buf.error_code = Fapi_getFsmStatus();
        Flash_releasePumpSemaphore();
        return;
    }

    // Blank check the sector: Sector 0 size is 16KB = 4K 32-bit words (0x1000)
    oReturnCheck = Fapi_doBlankCheck((uint32_t *)0x0C0000U, 0x1000U, &oFlashStatusWord);
    if(oReturnCheck != Fapi_Status_Success)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_ERASE;
        ota_status_buf.error_code = 0x9999U; // Blank check failure
        Flash_releasePumpSemaphore();
        return;
    }

    // Release pump access
    Flash_releasePumpSemaphore();

    ota_status_buf.status = OTA_STATUS_ERASE_DONE;
    ota_status_buf.ack_chunk_idx = 0xFFFFFFFFU; // Signifies ready for chunk 0
}

//
// Program 128-byte chunk into Flash Bank 1
//
void process_ota_program(void)
{
    ota_status_buf.status = OTA_STATUS_PROGRAMMING;
    ota_status_buf.error_code = 0;

    uint32_t chunk_idx = ota_cmd_buf.chunk_idx;
    uint32_t chunk_size = ota_cmd_buf.chunk_size;

    // Calculate flash word address (each byte is 0.5 C28x 16-bit words)
    uint32_t target_addr = 0x0C0000U + chunk_idx * (chunk_size / 2U);

    // Claim pump access
    Flash_claimPumpSemaphore(FLASH_CPU1_WRAPPER);

    uint16_t *src_ptr = (uint16_t *)ota_cmd_buf.payload;
    bool prog_failed = false;

    // Program 128 bytes in 8 slices of 8 words (16 bytes)
    uint32_t i;
    for(i = 0; i < 64U; i += 8U)
    {
        oReturnCheck = Fapi_issueProgrammingCommand((uint32_t *)(target_addr + i),
                                                    src_ptr + i,
                                                    8U, 0, 0,
                                                    Fapi_AutoEccGeneration);

        // Wait until FSM is ready
        while(Fapi_checkFsmForReady() == Fapi_Status_FsmBusy);

        if(oReturnCheck != Fapi_Status_Success || Fapi_getFsmStatus() != 0)
        {
            prog_failed = true;
            ota_status_buf.error_code = Fapi_getFsmStatus();
            break;
        }
    }

    if(prog_failed)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_PROGRAM;
        Flash_releasePumpSemaphore();
        return;
    }

    // Verify written chunk (length in 32-bit words = chunk_size / 4)
    oReturnCheck = Fapi_doVerify((uint32_t *)target_addr,
                                 chunk_size / 4U,
                                 (uint32_t *)ota_cmd_buf.payload,
                                 &oFlashStatusWord);

    if(oReturnCheck != Fapi_Status_Success)
    {
        ota_status_buf.status = OTA_STATUS_ERROR_PROGRAM;
        ota_status_buf.error_code = 0x8888U; // Verification failed
        Flash_releasePumpSemaphore();
        return;
    }

    // Release pump access
    Flash_releasePumpSemaphore();

    ota_status_buf.status = OTA_STATUS_PROGRAM_DONE;
    ota_status_buf.ack_chunk_idx = chunk_idx;
}

//
// Verify total binary CRC32
//
void process_ota_verify(void)
{
    ota_status_buf.status = OTA_STATUS_VERIFYING;
    ota_status_buf.error_code = 0;

    uint32_t total_size = ota_cmd_buf.total_size;
    uint32_t expected_crc = ota_cmd_buf.expected_crc;

    // CRC check length in 16-bit words
    uint32_t word_count = total_size / 2U;

    uint32_t calc_crc = calculate_crc32_c28x((const uint16_t *)0x0C0000U, word_count);

    if(calc_crc == expected_crc)
    {
        ota_status_buf.status = OTA_STATUS_VERIFY_SUCCESS;
    }
    else
    {
        ota_status_buf.status = OTA_STATUS_ERROR_CRC;
        ota_status_buf.error_code = calc_crc; // Report calculated CRC
    }
}

//
// Swap code bank and soft reset/run
//
void process_ota_run(void)
{
    uint32_t blink;

    // Rapid blink CPU1 LED to indicate swap validation and launch sequence
    for(blink = 0; blink < 20U; blink++)
    {
        GPIO_togglePin(CPU1_LED);
        DEVICE_DELAY_US(50000);
    }

    // Trigger soft device reset to run bootloader check and boot new image
    SysCtl_resetDevice();
}

//
// Custom standard CRC32 (IEEE 802.3) implementation unpacked at byte boundaries
//
uint32_t calculate_crc32_c28x(const uint16_t *data, uint32_t length_words)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    for (i = 0; i < length_words; i++)
    {
        uint16_t word = data[i];

        // Lower byte
        uint8_t byte = word & 0xFFU;
        crc ^= byte;
        int bit;
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1U) ^ 0xEDB88320U;
            else
                crc = (crc >> 1U);
        }

        // Upper byte
        byte = (word >> 8U) & 0xFFU;
        crc ^= byte;
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1U) ^ 0xEDB88320U;
            else
                crc = (crc >> 1U);
        }
    }
    return ~crc;
}
