//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  Car OTA Bootloader - CM Gateway Code
//
//#############################################################################

#include "cm.h"
#include "ipc.h"
#include "../car_ota_shared.h"

// Map GS0 and GS1 shared buffers
#define ota_cmd_buf     (*((volatile OtaCmdBuffer *)0x20014000U))
#define ota_status_buf  (*((volatile OtaStatusBuffer *)0x20016000U))

#define MOCK_FW_SIZE    512U
#define CHUNK_SIZE      128U
#define NUM_CHUNKS      (MOCK_FW_SIZE / CHUNK_SIZE)

// Simulated Firmware Image
uint8_t mock_firmware[MOCK_FW_SIZE];

// Telemetry and Status variables for debugging
volatile uint32_t ota_stage = 0;      // 0: Idle, 1: Erase, 2: Program, 3: Verify, 4: Success, 5: Failed
volatile uint32_t ota_error_code = 0; // Stores last error code
volatile uint32_t current_chunk = 0;  // Index of chunk being processed

// Function Prototypes
uint32_t calculate_crc32_bytes(const uint8_t *data, uint32_t length_bytes);
void delay_ms(uint32_t ms);

//
// Main
//
void main(void)
{
    // Initialize CM Core Clock
    CM_init();

    // Populate simulated firmware with an incrementing pattern (e.g. 0 to 255 repeat)
    uint32_t i;
    for(i = 0; i < MOCK_FW_SIZE; i++)
    {
        mock_firmware[i] = (uint8_t)(i & 0xFFU);
    }

    // Calculate expected total binary CRC32
    uint32_t expected_total_crc = calculate_crc32_bytes(mock_firmware, MOCK_FW_SIZE);

    // Initial delay to let CPU1 boot and initialize its Flash API
    delay_ms(100);

    // ==========================================
    // Step 1: Start OTA (Erase Bank 1)
    // ==========================================
    ota_stage = 1;
    ota_cmd_buf.command = OTA_CMD_START;
    ota_cmd_buf.total_size = MOCK_FW_SIZE;
    ota_cmd_buf.expected_crc = expected_total_crc;
    ota_cmd_buf.chunk_idx = 0;
    ota_cmd_buf.chunk_size = 0;
    ota_cmd_buf.chunk_crc = 0;

    // Trigger CPU1 IPC Interrupt
    IPC_setFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG0);

    // Wait for CPU1 to complete the erase operation
    while(ota_status_buf.status == OTA_STATUS_IDLE || ota_status_buf.status == OTA_STATUS_ERASING)
    {
        delay_ms(1);
    }

    if(ota_status_buf.status != OTA_STATUS_ERASE_DONE)
    {
        ota_stage = 5; // Failed
        ota_error_code = ota_status_buf.error_code;
        while(1);
    }

    // ==========================================
    // Step 2: Loop and Program Chunks
    // ==========================================
    ota_stage = 2;
    for(current_chunk = 0; current_chunk < NUM_CHUNKS; current_chunk++)
    {
        // Copy chunk bytes into the GS0 payload buffer safely (128 bytes)
        uint8_t *payload_dst = (uint8_t *)ota_cmd_buf.payload;
        uint32_t b;
        for(b = 0; b < CHUNK_SIZE; b++)
        {
            payload_dst[b] = mock_firmware[current_chunk * CHUNK_SIZE + b];
        }

        // Calculate chunk CRC
        uint32_t chunk_crc = calculate_crc32_bytes(&mock_firmware[current_chunk * CHUNK_SIZE], CHUNK_SIZE);

        ota_cmd_buf.chunk_idx = current_chunk;
        ota_cmd_buf.chunk_size = CHUNK_SIZE;
        ota_cmd_buf.chunk_crc = chunk_crc;
        ota_cmd_buf.command = OTA_CMD_PROGRAM;

        // Trigger CPU1 IPC Interrupt to write chunk
        IPC_setFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG0);

        // Wait for programming to finish
        while(ota_status_buf.status == OTA_STATUS_PROGRAMMING || ota_status_buf.status == OTA_STATUS_ERASE_DONE)
        {
            delay_ms(1);
        }

        if(ota_status_buf.status != OTA_STATUS_PROGRAM_DONE || ota_status_buf.ack_chunk_idx != current_chunk)
        {
            ota_stage = 5; // Failed
            ota_error_code = ota_status_buf.error_code;
            while(1);
        }
    }

    // ==========================================
    // Step 3: Verify the entire firmware image
    // ==========================================
    ota_stage = 3;
    ota_cmd_buf.command = OTA_CMD_VERIFY;

    // Trigger CPU1 IPC Interrupt to run CRC verification
    IPC_setFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG0);

    // Wait for validation
    while(ota_status_buf.status == OTA_STATUS_VERIFYING || ota_status_buf.status == OTA_STATUS_PROGRAM_DONE)
    {
        delay_ms(1);
    }

    if(ota_status_buf.status != OTA_STATUS_VERIFY_SUCCESS)
    {
        ota_stage = 5; // Failed
        ota_error_code = ota_status_buf.error_code; // Contains computed CRC32 on failure
        while(1);
    }

    // ==========================================
    // Step 4: Run updated firmware
    // ==========================================
    ota_stage = 4; // OTA Success!
    ota_cmd_buf.command = OTA_CMD_RUN;

    // Trigger CPU1 IPC Interrupt to jump/reset
    IPC_setFlagLtoR(IPC_CM_L_CPU1_R, IPC_FLAG0);

    // Sit in success loop
    while(1)
    {
        delay_ms(100);
    }
}

//
// Standard IEEE 802.3 CRC32 calculation over bytes
//
uint32_t calculate_crc32_bytes(const uint8_t *data, uint32_t length_bytes)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    for (i = 0; i < length_bytes; i++)
    {
        crc ^= data[i];
        int bit;
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc = (crc >> 1U);
            }
        }
    }
    return ~crc;
}

//
// Simple delay loop
//
void delay_ms(uint32_t ms)
{
    // At 120MHz CM clock, 1ms is approx 120,000 cycles.
    // A simple loop with 3 cycles per iteration.
    uint32_t count = ms * 40000U;
    while(count--)
    {
        __asm(" nop");
    }
}
