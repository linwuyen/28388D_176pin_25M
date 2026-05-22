//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  Car UDS CAN FD - CM Core Diagnostic Stack
//
//#############################################################################

#include "cm.h"
#include "mcan.h"
#include "inc/stw_dataTypes.h"
#include "inc/stw_types.h"
#include "../car_uds_shared.h"

// Define pointer to GS0 Shared RAM (C28x CPU2 address 0x00D000 maps to CM 0x20014000)
#define uds_shared_data (*((volatile UDS_SharedData *)0x20014000U))

// UDS Session States
typedef enum {
    UDS_SESSION_DEFAULT = 0x01,
    UDS_SESSION_PROGRAMMING = 0x02,
    UDS_SESSION_EXTENDED = 0x03
} UDS_Session;

// UDS Security States
typedef enum {
    UDS_SEC_LOCKED = 0,
    UDS_SEC_SEED_SENT = 1,
    UDS_SEC_UNLOCKED = 2
} UDS_SecurityState;

// Protocol States
volatile UDS_Session uds_session = UDS_SESSION_DEFAULT;
volatile UDS_SecurityState uds_security_state = UDS_SEC_LOCKED;
volatile uint32_t uds_seed = 0;
volatile uint32_t uds_expected_key = 0;

// VIN storage (17 bytes + null terminator)
uint8_t uds_vin[18] = "TI_F28388D_CAR_01";

// UDS buffers
uint8_t tx_uds_payload[64];
uint32_t tx_uds_length = 0;
volatile uint32_t uds_simulation_success = 0;

//
// FNV-1a 32-bit Hash Algorithm (Security Access)
//
uint32_t Calculate_FNV1a_32(uint32_t seed)
{
    uint32_t hash = 2166136261UL;
    int i;
    for (i = 0; i < 4; ++i)
    {
        uint8_t byte = (uint8_t)(seed >> (i * 8));
        hash = hash ^ byte;
        hash = hash * 16777619UL;
    }
    return hash;
}

//
// Process UDS ISO 14229 Service
//
void ProcessUDSMessage(const uint8_t *rx_data, uint32_t rx_len)
{
    if (rx_len < 2) return;

    uint8_t sf_len = rx_data[0]; // Single Frame Length (PCI)
    if (sf_len > rx_len - 1) return;

    uint8_t sid = rx_data[1]; // Service ID

    tx_uds_length = 0;

    switch (sid)
    {
        case 0x10: // Diagnostic Session Control
        {
            if (sf_len < 2)
            {
                // NRC 0x13: Incorrect Message Length Or Invalid Format
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x10;
                tx_uds_payload[3] = 0x13;
                tx_uds_length = 4;
                break;
            }
            uint8_t req_session = rx_data[2];
            if (req_session == 0x01 || req_session == 0x02 || req_session == 0x03)
            {
                uds_session = (UDS_Session)req_session;
                // Whenever we switch sessions, relock security
                uds_security_state = UDS_SEC_LOCKED;

                // Positive response: [SF_LEN=2, 0x50, Session]
                tx_uds_payload[0] = 2;
                tx_uds_payload[1] = 0x50;
                tx_uds_payload[2] = req_session;
                tx_uds_length = 3;
            }
            else
            {
                // NRC 0x12: Sub-function Not Supported
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x10;
                tx_uds_payload[3] = 0x12;
                tx_uds_length = 4;
            }
            break;
        }

        case 0x22: // Read Data By Identifier (RDBI)
        {
            if (sf_len < 3)
            {
                // NRC 0x13
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x22;
                tx_uds_payload[3] = 0x13;
                tx_uds_length = 4;
                break;
            }
            uint16_t did = ((uint16_t)rx_data[2] << 8) | rx_data[3];
            if (did == 0xF190) // Read VIN
            {
                // Positive response: [SF_LEN=20, 0x62, 0xF1, 0x90, 17-bytes VIN]
                tx_uds_payload[0] = 20;
                tx_uds_payload[1] = 0x62;
                tx_uds_payload[2] = 0xF1;
                tx_uds_payload[3] = 0x90;
                int i;
                for (i = 0; i < 17; i++)
                {
                    tx_uds_payload[4 + i] = uds_vin[i];
                }
                tx_uds_length = 21;
            }
            else if (did == 0x0100) // Read cell voltage & temp from CPU2
            {
                uint32_t volt = uds_shared_data.cell_voltage_mv;
                uint32_t temp = uds_shared_data.cell_temp_c;

                // Positive response: [SF_LEN=11, 0x62, 0x01, 0x00, 4-byte Volt, 4-byte Temp]
                tx_uds_payload[0] = 11;
                tx_uds_payload[1] = 0x62;
                tx_uds_payload[2] = 0x01;
                tx_uds_payload[3] = 0x00;
                tx_uds_payload[4] = (uint8_t)(volt >> 24);
                tx_uds_payload[5] = (uint8_t)(volt >> 16);
                tx_uds_payload[6] = (uint8_t)(volt >> 8);
                tx_uds_payload[7] = (uint8_t)(volt);
                tx_uds_payload[8] = (uint8_t)(temp >> 24);
                tx_uds_payload[9] = (uint8_t)(temp >> 16);
                tx_uds_payload[10] = (uint8_t)(temp >> 8);
                tx_uds_payload[11] = (uint8_t)(temp);
                tx_uds_length = 12;
            }
            else
            {
                // NRC 0x31: Request Out Of Range
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x22;
                tx_uds_payload[3] = 0x31;
                tx_uds_length = 4;
            }
            break;
        }

        case 0x2E: // Write Data By Identifier (WDBI)
        {
            if (sf_len < 4)
            {
                // NRC 0x13
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x2E;
                tx_uds_payload[3] = 0x13;
                tx_uds_length = 4;
                break;
            }
            uint16_t did = ((uint16_t)rx_data[2] << 8) | rx_data[3];
            if (did == 0xF190) // Write VIN
            {
                // Conditions check: Must be in Extended session
                if (uds_session != UDS_SESSION_EXTENDED)
                {
                    // NRC 0x22: Conditions Not Correct
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x7F;
                    tx_uds_payload[2] = 0x2E;
                    tx_uds_payload[3] = 0x22;
                    tx_uds_length = 4;
                    break;
                }
                // Security check: Must be Unlocked
                if (uds_security_state != UDS_SEC_UNLOCKED)
                {
                    // NRC 0x33: Security Access Denied
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x7F;
                    tx_uds_payload[2] = 0x2E;
                    tx_uds_payload[3] = 0x33;
                    tx_uds_length = 4;
                    break;
                }
                // Verify length of payload
                if (sf_len < 21) // 3 (SID+DID) + 17 (VIN) + 1 (PCI length byte minimum)
                {
                    // NRC 0x13
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x7F;
                    tx_uds_payload[2] = 0x2E;
                    tx_uds_payload[3] = 0x13;
                    tx_uds_length = 4;
                    break;
                }

                // Copy VIN
                int i;
                for (i = 0; i < 17; i++)
                {
                    uds_vin[i] = rx_data[4 + i];
                }
                uds_vin[17] = '\0';

                // Positive response: [SF_LEN=3, 0x6E, 0xF1, 0x90]
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x6E;
                tx_uds_payload[2] = 0xF1;
                tx_uds_payload[3] = 0x90;
                tx_uds_length = 4;
            }
            else
            {
                // NRC 0x31
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x2E;
                tx_uds_payload[3] = 0x31;
                tx_uds_length = 4;
            }
            break;
        }

        case 0x27: // Security Access
        {
            uint8_t sub_fn = rx_data[2];
            if (sub_fn == 0x01) // Request Seed
            {
                uds_seed = 0x55AA3A5CUL; // Fixed diagnostic seed
                uds_expected_key = Calculate_FNV1a_32(uds_seed);
                uds_security_state = UDS_SEC_SEED_SENT;

                // Positive response: [SF_LEN=6, 0x67, 0x01, 4-byte Seed]
                tx_uds_payload[0] = 6;
                tx_uds_payload[1] = 0x67;
                tx_uds_payload[2] = 0x01;
                tx_uds_payload[3] = (uint8_t)(uds_seed >> 24);
                tx_uds_payload[4] = (uint8_t)(uds_seed >> 16);
                tx_uds_payload[5] = (uint8_t)(uds_seed >> 8);
                tx_uds_payload[6] = (uint8_t)(uds_seed);
                tx_uds_length = 7;
            }
            else if (sub_fn == 0x02) // Send Key
            {
                if (uds_security_state == UDS_SEC_SEED_SENT)
                {
                    if (sf_len < 6)
                    {
                        // NRC 0x13
                        tx_uds_payload[0] = 3;
                        tx_uds_payload[1] = 0x7F;
                        tx_uds_payload[2] = 0x27;
                        tx_uds_payload[3] = 0x13;
                        tx_uds_length = 4;
                        break;
                    }
                    uint32_t client_key = ((uint32_t)rx_data[3] << 24) |
                                          ((uint32_t)rx_data[4] << 16) |
                                          ((uint32_t)rx_data[5] << 8)  |
                                          ((uint32_t)rx_data[6]);

                    if (client_key == uds_expected_key)
                    {
                        uds_security_state = UDS_SEC_UNLOCKED;
                        // Positive response: [SF_LEN=2, 0x67, 0x02]
                        tx_uds_payload[0] = 2;
                        tx_uds_payload[1] = 0x67;
                        tx_uds_payload[2] = 0x02;
                        tx_uds_length = 3;
                    }
                    else
                    {
                        // NRC 0x35: Invalid Key
                        tx_uds_payload[0] = 3;
                        tx_uds_payload[1] = 0x7F;
                        tx_uds_payload[2] = 0x27;
                        tx_uds_payload[3] = 0x35;
                        tx_uds_length = 4;
                    }
                }
                else
                {
                    // NRC 0x24: Request Sequence Error
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x7F;
                    tx_uds_payload[2] = 0x27;
                    tx_uds_payload[3] = 0x24;
                    tx_uds_length = 4;
                }
            }
            else
            {
                // NRC 0x12
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x27;
                tx_uds_payload[3] = 0x12;
                tx_uds_length = 4;
            }
            break;
        }

        case 0x19: // Read DTC Information
        {
            uint8_t sub_fn = rx_data[2];
            if (sub_fn == 0x02) // ReadDTCByStatusMask
            {
                uint32_t fault = uds_shared_data.active_fault_code;
                if (fault == 0x02) // Over-temperature active
                {
                    // Positive response: [SF_LEN=7, 0x59, 0x02, Mask=0x01, DTC=0x9A0115, Status=0x09]
                    tx_uds_payload[0] = 7;
                    tx_uds_payload[1] = 0x59;
                    tx_uds_payload[2] = 0x02;
                    tx_uds_payload[3] = 0x01; // Status availability mask
                    tx_uds_payload[4] = 0x9A; // DTC High
                    tx_uds_payload[5] = 0x01; // DTC Mid
                    tx_uds_payload[6] = 0x15; // DTC Low (0x9A0115)
                    tx_uds_payload[7] = 0x09; // DTC Status (Active)
                    tx_uds_length = 8;
                }
                else
                {
                    // Positive response: [SF_LEN=3, 0x59, 0x02, Mask=0x01] (No active DTCs)
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x59;
                    tx_uds_payload[2] = 0x02;
                    tx_uds_payload[3] = 0x01;
                    tx_uds_length = 4;
                }
            }
            else
            {
                // NRC 0x12
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x19;
                tx_uds_payload[3] = 0x12;
                tx_uds_length = 4;
            }
            break;
        }

        default:
        {
            // NRC 0x11: Service Not Supported
            tx_uds_payload[0] = 3;
            tx_uds_payload[1] = 0x7F;
            tx_uds_payload[2] = sid;
            tx_uds_payload[3] = 0x11;
            tx_uds_length = 4;
            break;
        }
    }
}

//
// MCAN-FD configuration (120MHz CM Clock)
//
void ConfigureMCAN(void)
{
    MCAN_InitParams initParams;
    MCAN_ConfigParams configParams;
    MCAN_BitTimingParams bitTimes;

    // Reset MCAN
    SysCtl_resetPeripheral(SYSCTL_PERIPH_RES_MCAN_A);

    // Wait for message RAM initialization
    while (false == MCAN_isMemInitDone(MCAN0_BASE));

    // Put into Software Init mode
    MCAN_setOpMode(MCAN0_BASE, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_OPERATION_MODE_SW_INIT != MCAN_getOpMode(MCAN0_BASE));

    // Enable FD Mode & BRS
    initParams.fdMode            = 0x1U;
    initParams.brsEnable         = 0x1U;
    initParams.txpEnable         = 0x0U;
    initParams.efbi              = 0x0U;
    initParams.pxhddisable       = 0x0U;
    initParams.darEnable         = 0x0U;
    initParams.wkupReqEnable     = 0x1U;
    initParams.autoWkupEnable    = 0x1U;
    initParams.emulationEnable   = 0x1U;
    initParams.tdcEnable         = 0x1U;
    initParams.wdcPreload        = 0xFFU;
    initParams.tdcConfig.tdcf    = 0xAU;
    initParams.tdcConfig.tdco    = 0x6U;

    MCAN_init(MCAN0_BASE, &initParams);

    configParams.monEnable         = 0x0U;
    configParams.asmEnable         = 0x0U;
    configParams.tsPrescalar       = 0xFU;
    configParams.tsSelect          = 0x0U;
    configParams.timeoutSelect     = MCAN_TIMEOUT_SELECT_CONT;
    configParams.timeoutPreload    = 0xFFFFU;
    configParams.timeoutCntEnable  = 0x0U;
    configParams.filterConfig.rrfs = 0x1U;
    configParams.filterConfig.rrfe = 0x1U;
    configParams.filterConfig.anfe = 0x1U;
    configParams.filterConfig.anfs = 0x1U;

    MCAN_config(MCAN0_BASE, &configParams);

    // Nominal Bit Rate: 500kbps (80% sampling point)
    // Prescaler=6 (reg value = 5), Tq total = 40.
    bitTimes.nomRatePrescalar   = 5U;
    bitTimes.nomTimeSeg1        = 30U;
    bitTimes.nomTimeSeg2        = 7U;
    bitTimes.nomSynchJumpWidth  = 7U;

    // Data Bit Rate: 5Mbps (75% sampling point)
    // Prescaler=1 (reg value = 0), Tq total = 24.
    bitTimes.dataRatePrescalar  = 0U;
    bitTimes.dataTimeSeg1       = 16U;
    bitTimes.dataTimeSeg2       = 5U;
    bitTimes.dataSynchJumpWidth = 5U;

    MCAN_setBitTime(MCAN0_BASE, &bitTimes);

    // Enter Normal mode
    MCAN_setOpMode(MCAN0_BASE, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_OPERATION_MODE_NORMAL != MCAN_getOpMode(MCAN0_BASE));
}

//
// Extended Self-Test UDS Diagnostic Sequence
//
void RunUDSSimulation(void)
{
    // [Test 1] Read VIN (Default session) - should succeed
    uint8_t t1_req[4] = {3, 0x22, 0xF1, 0x90};
    ProcessUDSMessage(t1_req, 4);
    if (tx_uds_payload[1] != 0x62 || tx_uds_payload[2] != 0xF1 || tx_uds_payload[3] != 0x90) return;

    // [Test 2] Write VIN in Default session - should fail with NRC 0x22 (Conditions Not Correct)
    uint8_t t2_req[22] = {21, 0x2E, 0xF1, 0x90, 'N','E','W','_','V','I','N','_','T','E','S','T','_','0','0','1','\0'};
    ProcessUDSMessage(t2_req, 22);
    if (tx_uds_payload[1] != 0x7F || tx_uds_payload[2] != 0x2E || tx_uds_payload[3] != 0x22) return;

    // [Test 3] Switch to Extended Diagnostic Session (0x03)
    uint8_t t3_req[3] = {2, 0x10, 0x03};
    ProcessUDSMessage(t3_req, 3);
    if (uds_session != UDS_SESSION_EXTENDED || tx_uds_payload[1] != 0x50 || tx_uds_payload[2] != 0x03) return;

    // [Test 4] Write VIN in Extended session while Locked - should fail with NRC 0x33 (Security Access Denied)
    ProcessUDSMessage(t2_req, 22);
    if (tx_uds_payload[1] != 0x7F || tx_uds_payload[2] != 0x2E || tx_uds_payload[3] != 0x33) return;

    // [Test 5] Security Access: Request Seed (0x01)
    uint8_t t5_req[3] = {2, 0x27, 0x01};
    ProcessUDSMessage(t5_req, 3);
    if (uds_security_state != UDS_SEC_SEED_SENT || tx_uds_payload[1] != 0x67 || tx_uds_payload[2] != 0x01) return;

    // [Test 6] Security Access: Send Key (0x02)
    uint8_t t6_req[7];
    t6_req[0] = 6;
    t6_req[1] = 0x27;
    t6_req[2] = 0x02;
    t6_req[3] = (uint8_t)(uds_expected_key >> 24);
    t6_req[4] = (uint8_t)(uds_expected_key >> 16);
    t6_req[5] = (uint8_t)(uds_expected_key >> 8);
    t6_req[6] = (uint8_t)(uds_expected_key);
    ProcessUDSMessage(t6_req, 7);
    if (uds_security_state != UDS_SEC_UNLOCKED || tx_uds_payload[1] != 0x67 || tx_uds_payload[2] != 0x02) return;

    // [Test 7] Write VIN in Extended session while Unlocked - should succeed
    ProcessUDSMessage(t2_req, 22);
    if (tx_uds_payload[1] != 0x6E || tx_uds_payload[2] != 0xF1 || tx_uds_payload[3] != 0x90) return;

    // Verify written VIN
    ProcessUDSMessage(t1_req, 4);
    if (tx_uds_payload[1] != 0x62 || tx_uds_payload[4] != 'N' || tx_uds_payload[5] != 'E' || tx_uds_payload[6] != 'W') return;

    // [Test 8] Read DTC (with over-temp active)
    // Manually force over-temp to verify 0x19
    uds_shared_data.active_fault_code = 0x02;
    uint8_t t8_req[3] = {2, 0x19, 0x02};
    ProcessUDSMessage(t8_req, 3);
    if (tx_uds_payload[1] != 0x59 || tx_uds_payload[4] != 0x9A || tx_uds_payload[7] != 0x09) return;

    // All tests passed!
    uds_simulation_success = 1;
}

//
// Main
//
void main(void)
{
    // Initialize CM Core Clock
    CM_init();

    // Configure MCAN Module
    ConfigureMCAN();

    // Run diagnostic simulation self-test
    RunUDSSimulation();

    while (1)
    {
        // Blink a heartbeat by doing NOPs or state checking
        __asm(" nop");
    }
}

//
// End of File
//
