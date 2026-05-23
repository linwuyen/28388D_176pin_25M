//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  Car UDS CAN FD - CM Core Diagnostic Stack
//
//#############################################################################

#include "cm.h"
#include "can.h"
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
// ISO 15765-2 DoCAN multi-frame transport layer structures and states
//
typedef struct {
    uint8_t payload[256];
    uint32_t total_len;
    uint32_t assembled_len;
    uint8_t next_sn;
    bool in_progress;
} DoCAN_RxContext;

typedef struct {
    uint8_t payload[256];
    uint32_t total_len;
    uint32_t sent_len;
    uint8_t next_sn;
    bool waiting_for_fc;
} DoCAN_TxContext;

DoCAN_RxContext server_rx_ctx;
DoCAN_TxContext server_tx_ctx;

DoCAN_RxContext client_rx_ctx;
DoCAN_TxContext client_tx_ctx;

volatile uint32_t sim_in_progress = 0;
uint8_t client_received_response[256];
uint32_t client_received_response_len = 0;
volatile uint32_t client_response_ready = 0;

void DoCAN_ServerRxFrame(const uint8_t *frame);
void DoCAN_ClientRxFrame(const uint8_t *frame);
void DoCAN_TxFrameToServerBus(const uint8_t *frame);
void DoCAN_TxFrameToClientBus(const uint8_t *frame);
void DoCAN_ServerSendResponse(void);
void DoCAN_ServerSendFC(void);
void DoCAN_ServerSendCFs(void);
void DoCAN_ClientSendRequest(const uint8_t *payload, uint32_t len);
void DoCAN_ClientSendCFs(void);
void DoCAN_ClientSendFC(void);

//
// Router for server transmitted frames
//
void DoCAN_TxFrameToServerBus(const uint8_t *frame)
{
    if (sim_in_progress)
    {
        DoCAN_ClientRxFrame(frame);
    }
    else
    {
        CAN_sendMessage(CANB_BASE, 1, 8, frame);
    }
}

//
// Router for client transmitted frames
//
void DoCAN_TxFrameToClientBus(const uint8_t *frame)
{
    if (sim_in_progress)
    {
        DoCAN_ServerRxFrame(frame);
    }
    else
    {
        CAN_sendMessage(CANB_BASE, 2, 8, frame);
    }
}

//
// Server sending response payload (L bytes, from tx_uds_payload[1..L])
//
void DoCAN_ServerSendResponse(void)
{
    uint32_t L = tx_uds_payload[0];
    
    if (L <= 7)
    {
        // Send Single Frame (SF)
        uint8_t frame[8];
        frame[0] = L;
        int i;
        for (i = 0; i < L; i++)
        {
            frame[1 + i] = tx_uds_payload[1 + i];
        }
        // Pad
        for (i = L + 1; i < 8; i++)
        {
            frame[i] = 0xAA;
        }
        
        DoCAN_TxFrameToServerBus(frame);
    }
    else
    {
        // Send First Frame (FF)
        uint8_t frame[8];
        frame[0] = 0x10 | ((L >> 8) & 0x0F);
        frame[1] = L & 0xFF;
        int i;
        for (i = 0; i < 6; i++)
        {
            frame[2 + i] = tx_uds_payload[1 + i];
        }
        
        server_tx_ctx.total_len = L;
        server_tx_ctx.sent_len = 6;
        server_tx_ctx.next_sn = 1;
        server_tx_ctx.waiting_for_fc = true;
        
        DoCAN_TxFrameToServerBus(frame);
    }
}

//
// Server sending Flow Control (FC) frame
//
void DoCAN_ServerSendFC(void)
{
    uint8_t frame[8];
    frame[0] = 0x30; // FS = 0 (Continue to Send)
    frame[1] = 0x00; // BS = 0 (Send all)
    frame[2] = 0x00; // STmin = 0
    int i;
    for (i = 3; i < 8; i++) frame[i] = 0xAA;
    
    DoCAN_TxFrameToServerBus(frame);
}

//
// Server sending Consecutive Frames (CF)
//
void DoCAN_ServerSendCFs(void)
{
    if (!server_tx_ctx.waiting_for_fc) return;
    server_tx_ctx.waiting_for_fc = false;
    
    uint32_t L = server_tx_ctx.total_len;
    while (server_tx_ctx.sent_len < L)
    {
        uint8_t frame[8];
        frame[0] = 0x20 | server_tx_ctx.next_sn;
        server_tx_ctx.next_sn = (server_tx_ctx.next_sn + 1) & 0x0F;
        
        uint32_t rem = L - server_tx_ctx.sent_len;
        uint32_t copy_len = (rem > 7) ? 7 : rem;
        
        int i;
        for (i = 0; i < copy_len; i++)
        {
            frame[1 + i] = tx_uds_payload[1 + server_tx_ctx.sent_len + i];
        }
        for (i = copy_len + 1; i < 8; i++)
        {
            frame[i] = 0xAA;
        }
        
        server_tx_ctx.sent_len += copy_len;
        DoCAN_TxFrameToServerBus(frame);
    }
}

//
// Server Receiver Callback
//
void DoCAN_ServerRxFrame(const uint8_t *frame)
{
    uint8_t pci = frame[0] >> 4;
    
    if (pci == 0) // Single Frame (SF)
    {
        uint8_t len = frame[0] & 0x0F;
        if (len > 7) len = 7;
        
        server_rx_ctx.payload[0] = len;
        int i;
        for (i = 0; i < len; i++)
        {
            server_rx_ctx.payload[1 + i] = frame[1 + i];
        }
        
        ProcessUDSMessage(server_rx_ctx.payload, len + 1);
        
        if (tx_uds_length > 0)
        {
            DoCAN_ServerSendResponse();
        }
    }
    else if (pci == 1) // First Frame (FF)
    {
        uint16_t len = ((uint16_t)(frame[0] & 0x0F) << 8) | frame[1];
        server_rx_ctx.total_len = len;
        server_rx_ctx.assembled_len = 6;
        server_rx_ctx.next_sn = 1;
        server_rx_ctx.in_progress = true;
        
        int i;
        for (i = 0; i < 6; i++)
        {
            server_rx_ctx.payload[1 + i] = frame[2 + i];
        }
        
        DoCAN_ServerSendFC();
    }
    else if (pci == 2) // Consecutive Frame (CF)
    {
        if (!server_rx_ctx.in_progress) return;
        
        uint8_t sn = frame[0] & 0x0F;
        if (sn != server_rx_ctx.next_sn)
        {
            server_rx_ctx.in_progress = false;
            return;
        }
        
        server_rx_ctx.next_sn = (server_rx_ctx.next_sn + 1) & 0x0F;
        
        uint32_t rem = server_rx_ctx.total_len - server_rx_ctx.assembled_len;
        uint32_t copy_len = (rem > 7) ? 7 : rem;
        
        int i;
        for (i = 0; i < copy_len; i++)
        {
            server_rx_ctx.payload[1 + server_rx_ctx.assembled_len + i] = frame[1 + i];
        }
        
        server_rx_ctx.assembled_len += copy_len;
        
        if (server_rx_ctx.assembled_len >= server_rx_ctx.total_len)
        {
            server_rx_ctx.in_progress = false;
            server_rx_ctx.payload[0] = server_rx_ctx.total_len;
            
            ProcessUDSMessage(server_rx_ctx.payload, server_rx_ctx.total_len + 1);
            
            if (tx_uds_length > 0)
            {
                DoCAN_ServerSendResponse();
            }
        }
    }
    else if (pci == 3) // Flow Control (FC)
    {
        uint8_t fs = frame[0] & 0x0F;
        if (fs == 0) // Continue to Send
        {
            DoCAN_ServerSendCFs();
        }
    }
}

//
// Client (Tester) Simulation side
//
void DoCAN_ClientSendRequest(const uint8_t *payload, uint32_t len)
{
    client_response_ready = 0;
    client_received_response_len = 0;
    
    if (len <= 7)
    {
        uint8_t frame[8];
        frame[0] = len;
        int i;
        for (i = 0; i < len; i++)
        {
            frame[1 + i] = payload[i];
        }
        for (i = len + 1; i < 8; i++)
        {
            frame[i] = 0xAA;
        }
        
        DoCAN_TxFrameToClientBus(frame);
    }
    else
    {
        uint8_t frame[8];
        frame[0] = 0x10 | ((len >> 8) & 0x0F);
        frame[1] = len & 0xFF;
        int i;
        for (i = 0; i < 6; i++)
        {
            frame[2 + i] = payload[i];
        }
        
        client_tx_ctx.total_len = len;
        client_tx_ctx.sent_len = 6;
        client_tx_ctx.next_sn = 1;
        client_tx_ctx.waiting_for_fc = true;
        
        for (i = 0; i < len; i++)
        {
            client_tx_ctx.payload[i] = payload[i];
        }
        
        DoCAN_TxFrameToClientBus(frame);
    }
}

void DoCAN_ClientSendFC(void)
{
    uint8_t frame[8];
    frame[0] = 0x30; // FS = 0
    frame[1] = 0x00; // BS = 0
    frame[2] = 0x00; // STmin = 0
    int i;
    for (i = 3; i < 8; i++) frame[i] = 0xAA;
    
    DoCAN_TxFrameToClientBus(frame);
}

void DoCAN_ClientSendCFs(void)
{
    if (!client_tx_ctx.waiting_for_fc) return;
    client_tx_ctx.waiting_for_fc = false;
    
    uint32_t L = client_tx_ctx.total_len;
    while (client_tx_ctx.sent_len < L)
    {
        uint8_t frame[8];
        frame[0] = 0x20 | client_tx_ctx.next_sn;
        client_tx_ctx.next_sn = (client_tx_ctx.next_sn + 1) & 0x0F;
        
        uint32_t rem = L - client_tx_ctx.sent_len;
        uint32_t copy_len = (rem > 7) ? 7 : rem;
        
        int i;
        for (i = 0; i < copy_len; i++)
        {
            frame[1 + i] = client_tx_ctx.payload[client_tx_ctx.sent_len + i];
        }
        for (i = copy_len + 1; i < 8; i++)
        {
            frame[i] = 0xAA;
        }
        
        client_tx_ctx.sent_len += copy_len;
        DoCAN_TxFrameToClientBus(frame);
    }
}

void DoCAN_ClientRxFrame(const uint8_t *frame)
{
    uint8_t pci = frame[0] >> 4;
    
    if (pci == 0) // Single Frame (SF)
    {
        uint8_t len = frame[0] & 0x0F;
        if (len > 7) len = 7;
        
        client_received_response[0] = len;
        int i;
        for (i = 0; i < len; i++)
        {
            client_received_response[1 + i] = frame[1 + i];
        }
        client_received_response_len = len + 1;
        client_response_ready = 1;
    }
    else if (pci == 1) // First Frame (FF)
    {
        uint16_t len = ((uint16_t)(frame[0] & 0x0F) << 8) | frame[1];
        client_rx_ctx.total_len = len;
        client_rx_ctx.assembled_len = 6;
        client_rx_ctx.next_sn = 1;
        client_rx_ctx.in_progress = true;
        
        int i;
        for (i = 0; i < 6; i++)
        {
            client_rx_ctx.payload[1 + i] = frame[2 + i];
        }
        
        DoCAN_ClientSendFC();
    }
    else if (pci == 2) // Consecutive Frame (CF)
    {
        if (!client_rx_ctx.in_progress) return;
        
        uint8_t sn = frame[0] & 0x0F;
        if (sn != client_rx_ctx.next_sn)
        {
            client_rx_ctx.in_progress = false;
            return;
        }
        
        client_rx_ctx.next_sn = (client_rx_ctx.next_sn + 1) & 0x0F;
        
        uint32_t rem = client_rx_ctx.total_len - client_rx_ctx.assembled_len;
        uint32_t copy_len = (rem > 7) ? 7 : rem;
        
        int i;
        for (i = 0; i < copy_len; i++)
        {
            client_rx_ctx.payload[1 + client_rx_ctx.assembled_len + i] = frame[1 + i];
        }
        
        client_rx_ctx.assembled_len += copy_len;
        
        if (client_rx_ctx.assembled_len >= client_rx_ctx.total_len)
        {
            client_rx_ctx.in_progress = false;
            client_rx_ctx.payload[0] = client_rx_ctx.total_len;
            
            int i;
            for (i = 0; i < client_rx_ctx.total_len + 1; i++)
            {
                client_received_response[i] = client_rx_ctx.payload[i];
            }
            client_received_response_len = client_rx_ctx.total_len + 1;
            client_response_ready = 1;
        }
    }
    else if (pci == 3) // Flow Control (FC)
    {
        uint8_t fs = frame[0] & 0x0F;
        if (fs == 0) // Continue to Send
        {
            DoCAN_ClientSendCFs();
        }
    }
}

//
// CAN configuration (125MHz CM Clock, 500kbps Baud)
//
void ConfigureCAN(void)
{
    // Enable CANB Clock
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CAN_B);

    // Initialize CAN module
    CAN_initModule(CANB_BASE);

    // Set CANB baud rate to 500kbps using CM_CLK_FREQ
    CAN_setBitRate(CANB_BASE, CM_CLK_FREQ, 500000, 20);

    // Set up Mailbox 1 (TX) and Mailbox 2 (RX) using CAN_setupMessageObject.
    CAN_setupMessageObject(CANB_BASE, 1, 0x7E8, CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_TX, 0, CAN_MSG_OBJ_NO_FLAGS, 8);
    CAN_setupMessageObject(CANB_BASE, 2, 0x7E0, CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_RX, 0, CAN_MSG_OBJ_NO_FLAGS, 8);

    // Start CAN operations
    CAN_startModule(CANB_BASE);
}

//
// Extended Self-Test UDS Diagnostic Sequence
//
void RunUDSSimulation(void)
{
    sim_in_progress = 1;

    // [Test 1] Read VIN (Default session) - should succeed
    uint8_t t1_req[3] = {0x22, 0xF1, 0x90};
    DoCAN_ClientSendRequest(t1_req, 3);
    if (!client_response_ready) return;
    if (client_received_response[0] != 20) return;
    if (client_received_response[1] != 0x62 || client_received_response[2] != 0xF1 || client_received_response[3] != 0x90) return;

    // [Test 2] Write VIN in Default session - should fail with NRC 0x22 (Conditions Not Correct)
    uint8_t t2_req[21] = {0x2E, 0xF1, 0x90, 'N','E','W','_','V','I','N','_','T','E','S','T','_','0','0','1','\0'};
    DoCAN_ClientSendRequest(t2_req, 21);
    if (!client_response_ready) return;
    if (client_received_response[0] != 3) return;
    if (client_received_response[1] != 0x7F || client_received_response[2] != 0x2E || client_received_response[3] != 0x22) return;

    // [Test 3] Switch to Extended Diagnostic Session (0x03)
    uint8_t t3_req[2] = {0x10, 0x03};
    DoCAN_ClientSendRequest(t3_req, 2);
    if (!client_response_ready) return;
    if (uds_session != UDS_SESSION_EXTENDED || client_received_response[1] != 0x50 || client_received_response[2] != 0x03) return;

    // [Test 4] Write VIN in Extended session while Locked - should fail with NRC 0x33 (Security Access Denied)
    DoCAN_ClientSendRequest(t2_req, 21);
    if (!client_response_ready) return;
    if (client_received_response[1] != 0x7F || client_received_response[2] != 0x2E || client_received_response[3] != 0x33) return;

    // [Test 5] Security Access: Request Seed (0x01)
    uint8_t t5_req[2] = {0x27, 0x01};
    DoCAN_ClientSendRequest(t5_req, 2);
    if (!client_response_ready) return;
    if (uds_security_state != UDS_SEC_SEED_SENT || client_received_response[1] != 0x67 || client_received_response[2] != 0x01) return;

    // [Test 6] Security Access: Send Key (0x02)
    uint8_t t6_req[6];
    t6_req[0] = 0x27;
    t6_req[1] = 0x02;
    t6_req[2] = (uint8_t)(uds_expected_key >> 24);
    t6_req[3] = (uint8_t)(uds_expected_key >> 16);
    t6_req[4] = (uint8_t)(uds_expected_key >> 8);
    t6_req[5] = (uint8_t)(uds_expected_key);
    DoCAN_ClientSendRequest(t6_req, 6);
    if (!client_response_ready) return;
    if (uds_security_state != UDS_SEC_UNLOCKED || client_received_response[1] != 0x67 || client_received_response[2] != 0x02) return;

    // [Test 7] Write VIN in Extended session while Unlocked - should succeed
    DoCAN_ClientSendRequest(t2_req, 21);
    if (!client_response_ready) return;
    if (client_received_response[1] != 0x6E || client_received_response[2] != 0xF1 || client_received_response[3] != 0x90) return;

    // Verify written VIN
    DoCAN_ClientSendRequest(t1_req, 3);
    if (!client_response_ready) return;
    if (client_received_response[1] != 0x62 || client_received_response[4] != 'N' || client_received_response[5] != 'E' || client_received_response[6] != 'W') return;

    // [Test 8] Read DTC (with over-temp active)
    // Manually force over-temp to verify 0x19
    uds_shared_data.active_fault_code = 0x02;
    uint8_t t8_req[2] = {0x19, 0x02};
    DoCAN_ClientSendRequest(t8_req, 2);
    if (!client_response_ready) return;
    if (client_received_response[1] != 0x59 || client_received_response[4] != 0x9A || client_received_response[7] != 0x09) return;

    // All tests passed!
    sim_in_progress = 0;
    uds_simulation_success = 1;
}

//
// Main
//
void main(void)
{
    // Initialize CM Core Clock
    CM_init();

    // Configure CAN Module
    ConfigureCAN();

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
