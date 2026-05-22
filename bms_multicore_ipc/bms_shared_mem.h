#ifndef BMS_SHARED_MEM_H
#define BMS_SHARED_MEM_H

#include <stdint.h>

// BMS FSM 狀態定義
#define BMS_STATE_INIT       0
#define BMS_STATE_STANDBY    1
#define BMS_STATE_PRECHARGE  2
#define BMS_STATE_RUN        3
#define BMS_STATE_FAULT      4

// 確保 32-bit 自然對齊，防範通訊數據錯位
// 總大小：512 Bytes (在 C28x 中佔 256 字長/words，在 CM ARM 核心中佔 512 Bytes)
typedef struct {
    uint32_t timestamp;                     // 時間戳 (4 Bytes / 2 words)
    uint32_t bms_status;                    // BMS FSM 狀態 (4 Bytes / 2 words)
    float cell_voltages[96];                // 96 顆電芯電壓 (96 * 4 = 384 Bytes / 192 words)
    float ntc_temperatures[12];             // 12 路 NTC 溫度 (12 * 4 = 48 Bytes / 24 words)
    float total_voltage;                    // 總電壓 (4 Bytes / 2 words)
    float total_current;                    // 總電流 (4 Bytes / 2 words)
    uint32_t checksum;                      // 累加和校驗碼 (4 Bytes / 2 words)
    uint32_t padding[15];                   // 填充位 (15 * 4 = 60 Bytes / 30 words)，補齊至 512 Bytes
} BMS_Data_Packet;

#endif // BMS_SHARED_MEM_H
