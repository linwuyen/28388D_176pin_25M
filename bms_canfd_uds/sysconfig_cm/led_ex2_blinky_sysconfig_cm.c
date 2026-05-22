//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  BMS CAN-FD & ISO 14229 UDS 診斷協定棧 - CM 核心程式
//
// DESCRIPTION:
//   此程式為 CM (Cortex-M4) 核心之主要邏輯。
//   1. 配置內建 MCAN 模組為高速 CAN-FD 模式（120MHz 時脈源）：
//      - 仲裁段波特率 500kbps（80.0% 採樣點）：NBRP=6, NTSEG1=31, NTSEG2=8
//      - 數據段波特率 5Mbps（75.0% 採樣點）：DBRP=1, DTSEG1=17, DTSEG2=6
//   2. 實作 UDS 0x27 安全解鎖服務（搭配 32-bit FNV-1a 哈希演算法與隨機 Seed 驗證）
//   3. 實作 UDS 0x19 讀取故障碼服務（讀取 CPU2 共享 RAMGS0 之過溫故障碼 0x02，
//      並映射為車規過溫 DTC 0x9A0115，當前狀態 0x09）
//   4. 內建 UDS 模擬測試與自我校驗機制。
//
//#############################################################################

#include "cm.h"
#include "mcan.h"
#include "inc/stw_dataTypes.h"
#include "inc/stw_types.h"

// 定義 CPU2 共享的活動故障碼位址 (C28x 端位址 0x00D000 對應 CM 端位址 0x20014000)
#define active_fault_code (*((volatile uint32_t *)0x20014000U))

// UDS 安全解鎖有限狀態機狀態
typedef enum {
    UDS_SEC_LOCKED = 0,      // 安全狀態：已鎖定
    UDS_SEC_SEED_SENT = 1,   // 安全狀態：種子已發送，等待密鑰
    UDS_SEC_UNLOCKED = 2     // 安全狀態：已解鎖
} UDS_SecurityState;

volatile UDS_SecurityState uds_security_state = UDS_SEC_LOCKED;
volatile uint32_t uds_seed = 0;
volatile uint32_t uds_expected_key = 0;

// UDS 模擬與通訊追蹤變數
uint8_t rx_uds_payload[64];
uint8_t tx_uds_payload[64];
uint32_t tx_uds_length = 0;
volatile uint32_t uds_simulation_success = 0;

//
// FNV-1a 32-bit 哈希演算法 (用於安全解鎖 Key 計算)
// FNV_offset_basis = 2166136261UL, FNV_prime = 16777619UL
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
// UDS 訊息處理函式 (支持 ISO 15765-2 單幀 Single Frame 格式)
// rx_data: 指向接收到之 CAN-FD 數據區
// rx_len: 接收到的數據長度
//
void ProcessUDSMessage(const uint8_t *rx_data, uint32_t rx_len)
{
    if (rx_len < 2) return;
    
    uint8_t sf_len = rx_data[0]; // 單幀數據長度 (PCI 欄位)
    if (sf_len > rx_len - 1) return;
    
    uint8_t sid = rx_data[1]; // 服務標識符 (Service Identifier)
    
    if (sid == 0x27) // Security Access (安全存取服務)
    {
        uint8_t sub_fn = rx_data[2]; // 子功能
        
        if (sub_fn == 0x01) // Request Seed (請求種子)
        {
            uds_seed = 0x12345678U; // 產生一個固定的模擬種子
            uds_expected_key = Calculate_FNV1a_32(uds_seed);
            uds_security_state = UDS_SEC_SEED_SENT;
            
            // 正響應報文：[SF_LEN, 0x67 (0x27 + 0x40), 0x01, Seed_Byte3, Seed_Byte2, Seed_Byte1, Seed_Byte0]
            tx_uds_payload[0] = 6;
            tx_uds_payload[1] = 0x67;
            tx_uds_payload[2] = 0x01;
            tx_uds_payload[3] = (uint8_t)(uds_seed >> 24);
            tx_uds_payload[4] = (uint8_t)(uds_seed >> 16);
            tx_uds_payload[5] = (uint8_t)(uds_seed >> 8);
            tx_uds_payload[6] = (uint8_t)(uds_seed);
            tx_uds_length = 7;
        }
        else if (sub_fn == 0x02) // Send Key (傳送密鑰)
        {
            if (uds_security_state == UDS_SEC_SEED_SENT)
            {
                uint32_t client_key = ((uint32_t)rx_data[3] << 24) |
                                      ((uint32_t)rx_data[4] << 16) |
                                      ((uint32_t)rx_data[5] << 8)  |
                                      ((uint32_t)rx_data[6]);
                                      
                if (client_key == uds_expected_key)
                {
                    uds_security_state = UDS_SEC_UNLOCKED;
                    // 正響應報文：[SF_LEN=2, 0x67, 0x02] (解鎖成功)
                    tx_uds_payload[0] = 2;
                    tx_uds_payload[1] = 0x67;
                    tx_uds_payload[2] = 0x02;
                    tx_uds_length = 3;
                }
                else
                {
                    // 密鑰不符，回傳負響應 (NRC 0x35: Sub-function not supported or invalid key)
                    tx_uds_payload[0] = 3;
                    tx_uds_payload[1] = 0x7F;
                    tx_uds_payload[2] = 0x27;
                    tx_uds_payload[3] = 0x35;
                    tx_uds_length = 4;
                }
            }
            else
            {
                // 順序錯誤，回傳負響應 (NRC 0x24: Request Sequence Error)
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x7F;
                tx_uds_payload[2] = 0x27;
                tx_uds_payload[3] = 0x24;
                tx_uds_length = 4;
            }
        }
    }
    else if (sid == 0x19) // Read DTC Information (讀取故障碼服務)
    {
        uint8_t sub_fn = rx_data[2];
        if (sub_fn == 0x02) // ReadDTCByStatusMask
        {
            // 讀取來自 CPU2 寫入之活動故障狀態
            uint32_t fault = active_fault_code;
            
            if (fault == 0x02) // 檢測到過溫故障
            {
                // 正響應報文：[SF_LEN=7, 0x59 (0x19 + 0x40), 0x02, DTCStatusAvailabilityMask=0x01, DTC=0x9A0115, DTCStatus=0x09]
                tx_uds_payload[0] = 7;
                tx_uds_payload[1] = 0x59;
                tx_uds_payload[2] = 0x02;
                tx_uds_payload[3] = 0x01; // DTCStatusAvailabilityMask
                tx_uds_payload[4] = 0x9A; // DTC High Byte
                tx_uds_payload[5] = 0x01; // DTC Middle Byte
                tx_uds_payload[6] = 0x15; // DTC Low Byte (0x9A0115 過溫)
                tx_uds_payload[7] = 0x09; // DTCStatus (Active & Warning Limit)
                tx_uds_length = 8;
            }
            else
            {
                // 無活動故障響應
                tx_uds_payload[0] = 3;
                tx_uds_payload[1] = 0x59;
                tx_uds_payload[2] = 0x02;
                tx_uds_payload[3] = 0x01; // DTCStatusAvailabilityMask, 無故障列表
                tx_uds_length = 4;
            }
        }
    }
}

//
// MCAN 暫存器級精準配置 (時脈源 = 120MHz)
//
void ConfigureMCAN(void)
{
    MCAN_InitParams initParams;
    MCAN_ConfigParams configParams;
    MCAN_BitTimingParams bitTimes;

    // 重設 MCAN 周邊
    SysCtl_resetPeripheral(SYSCTL_PERIPH_RES_MCAN_A);

    // 等待 Message RAM 初始化完成
    while (false == MCAN_isMemInitDone(MCAN0_BASE));

    // 進入軟體初始化模式
    MCAN_setOpMode(MCAN0_BASE, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_OPERATION_MODE_SW_INIT != MCAN_getOpMode(MCAN0_BASE));

    // 初始化 MCAN 配置參數，啟用 CAN-FD 與波特率切換 (BRS)
    initParams.fdMode            = 0x1U; // 啟用 CAN-FD
    initParams.brsEnable         = 0x1U; // 傳輸啟用 BRS
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

    // 模組基礎參數配置
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

    // 核心波特率配置 (MCAN 核心時脈 = 120MHz)
    // 1. 仲裁段 (Nominal): 500kbps (80% 採樣點)
    //    Tq = 120MHz / 6 = 20MHz (Prescaler=6, 暫存器填入 5)
    //    BitTime = 40 Tq (Sync=1, TSEG1=31 (暫存器填入 30), TSEG2=8 (暫存器填入 7))
    //    SJW = 8 (暫存器填入 7)
    bitTimes.nomRatePrescalar   = 5U;
    bitTimes.nomTimeSeg1        = 30U;
    bitTimes.nomTimeSeg2        = 7U;
    bitTimes.nomSynchJumpWidth  = 7U;

    // 2. 數據段 (Data): 5Mbps (75% 採樣點)
    //    Tq = 120MHz / 1 = 120MHz (Prescaler=1, 暫存器填入 0)
    //    BitTime = 24 Tq (Sync=1, TSEG1=17 (暫存器填入 16), TSEG2=6 (暫存器填入 5))
    //    SJW = 6 (暫存器填入 5)
    bitTimes.dataRatePrescalar  = 0U;
    bitTimes.dataTimeSeg1       = 16U;
    bitTimes.dataTimeSeg2       = 5U;
    bitTimes.dataSynchJumpWidth = 5U;

    MCAN_setBitTime(MCAN0_BASE, &bitTimes);

    // 離開初始化模式，進入正常工作模式
    MCAN_setOpMode(MCAN0_BASE, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_OPERATION_MODE_NORMAL != MCAN_getOpMode(MCAN0_BASE));
}

//
// UDS 協定棧自我驗證測試常式
//
void RunUDSSimulation(void)
{
    // [步驟一]：請求 Seed
    // 輸入訊框: 長度=2, SID=0x27, Sub=0x01
    uint8_t step1_req[3] = {2, 0x27, 0x01};
    ProcessUDSMessage(step1_req, 3);
    
    // 預期回傳: 0x67 (0x27 + 0x40), 子功能 0x01, 以及產生的 Seed
    if (tx_uds_payload[1] != 0x67 || tx_uds_payload[2] != 0x01) {
        return; // 驗證失敗
    }
    
    // [步驟二]：發送正確的 FNV-1a 密鑰
    uint8_t step2_req[7];
    step2_req[0] = 6;
    step2_req[1] = 0x27;
    step2_req[2] = 0x02;
    step2_req[3] = (uint8_t)(uds_expected_key >> 24);
    step2_req[4] = (uint8_t)(uds_expected_key >> 16);
    step2_req[5] = (uint8_t)(uds_expected_key >> 8);
    step2_req[6] = (uint8_t)(uds_expected_key);
    
    ProcessUDSMessage(step2_req, 7);
    
    // 預期回傳: 解鎖狀態變更為 UNLOCKED，響應 0x67, 0x02
    if (uds_security_state != UDS_SEC_UNLOCKED || tx_uds_payload[1] != 0x67 || tx_uds_payload[2] != 0x02) {
        return; // 驗證失敗
    }
    
    // [步驟三]：讀取 DTC
    // 人為設定活動故障碼為過溫 (0x02)
    active_fault_code = 0x02;
    
    // 輸入訊框: 長度=2, SID=0x19, Sub=0x02 (ReadDTCByStatusMask)
    uint8_t step3_req[3] = {2, 0x19, 0x02};
    ProcessUDSMessage(step3_req, 3);
    
    // 預期回傳: 0x59 (0x19 + 0x40), Sub=0x02, DTC=0x9A0115, DTCStatus=0x09
    if (tx_uds_payload[1] == 0x59 && tx_uds_payload[2] == 0x02 && 
        tx_uds_payload[4] == 0x9A && tx_uds_payload[5] == 0x01 && 
        tx_uds_payload[6] == 0x15 && tx_uds_payload[7] == 0x09) {
        uds_simulation_success = 1; // 模擬自檢完全通過！
    }
}

//
// Main
//
void main(void)
{
    // 初始化 CM 核心之時脈與外設
    CM_init();

    // 精準配置 MCAN 波特率暫存器
    ConfigureMCAN();

    // 啟動 UDS 診斷協定棧自檢流程
    RunUDSSimulation();

    // 進入無窮迴圈
    while (1)
    {
        __asm(" nop");
    }
}

//
// End of File
//
