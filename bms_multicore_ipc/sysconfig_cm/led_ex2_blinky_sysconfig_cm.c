//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cm.c
//
// TITLE:  三核心孤立型 BMS - CM 核心通訊程式
//
// DESCRIPTION:
//   此程式為 CM (Cortex-M4) 核心之主要邏輯。
//   透過 IPC2 中斷接收 CPU2 傳送之 BMS 96 顆電芯電壓與狀態數據。
//   在中斷服務常式中讀取 IPC 暫存器取得 C28x 實體位址，轉換為 CM 內部
//   M4 位元組位址後，以 32-bit 指針高速拷貝 512 字節 (128 個 uint32_t) 
//   資料至本地緩衝區，並清除中斷標誌。
//
//#############################################################################

#include "cm.h"
#include "ipc.h"
#include "../bms_shared_mem.h"

// 本地緩衝區，用於接收來自 CPU2 的 BMS 數據
#pragma DATA_ALIGN(local_bms_data, 4)
BMS_Data_Packet local_bms_data;

// 計數器，便於 Debug 觀察
volatile uint32_t ipc2_interrupt_count = 0;
volatile uint32_t checksum_error_count = 0;

//
// IPC2 中斷服務常式 (C28x CPU2 to CM)
//
__interrupt void ipc2_isr(void)
{
    ipc2_interrupt_count++;

    // 讀取 CPU2 寫入之位址暫存器 (CPU2 端寫入 IPCSENDDATA，CM 端讀取 IPC_RECVDATA)
    // 為了最大相容性與魯棒性，若 IPC_RECVDATA 為 0，則讀取 IPC_RECVADDR
    uint32_t c28x_addr = IPC_Instance[IPC_CM_L_CPU2_R].IPC_RecvCmd_Reg->IPC_RECVDATA;
    if(c28x_addr == 0)
    {
        c28x_addr = IPC_Instance[IPC_CM_L_CPU2_R].IPC_RecvCmd_Reg->IPC_RECVADDR;
    }

    if(c28x_addr != 0)
    {
        // 將 C28x 的字位址 (Word Address) 轉換為 CM 的位元組位址 (Byte Address)
        // 轉換公式：CM_Addr = 0x20014000U + (C28x_Addr - 0x00D000U) * 2U
        uint32_t cm_addr = 0x20014000U + (c28x_addr - 0x00D000U) * 2U;

        uint32_t *src = (uint32_t *)cm_addr;
        uint32_t *dst = (uint32_t *)&local_bms_data;
        int i;

        // 高速複製 512 字節 (128 個 32-bit uint32_t)
        for(i = 0; i < 128; i++)
        {
            dst[i] = src[i];
        }

        // 驗證 Checksum (前 112 個 uint32_t/float 元素之累加和)
        uint32_t sum = 0;
        for(i = 0; i < 112; i++)
        {
            sum += dst[i];
        }

        if(sum != local_bms_data.checksum)
        {
            checksum_error_count++;
        }
    }

    // 清除 IPC2 旗標 (ACK)，使 CPU2 能夠繼續寫入下一個緩衝區
    IPC_ackFlagRtoL(IPC_CM_L_CPU2_R, IPC_FLAG2);
}

//
// Main
//
void main(void)
{
    // 初始化 CM 核心之時脈與外設
    CM_init();

    // 清除所有舊的 IPC 旗標
    IPC_clearFlagLtoR(IPC_CM_L_CPU2_R, IPC_FLAG_ALL);

    // 註冊並啟用 IPC2 中斷服務常式
    IPC_registerInterrupt(IPC_CM_L_CPU2_R, IPC_INT2, ipc2_isr);

    // 進入無窮迴圈，等待 IPC2 中斷觸發
    while(1)
    {
        // 這裡可加入低功耗待機或主線通訊處理邏輯
        __asm(" nop");
    }
}

//
// End of File
//
