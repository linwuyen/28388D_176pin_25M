//#############################################################################
//
// FILE:   led_ex2_blinky_sysconfig_cpu2.c
//
// TITLE: SysConfig LED Blinky Example
//
// <h1> LED Blinky Example (CPU2) </h1>
//
// This example demonstrates how to blink a LED using CPU2.
//
// \b External \b Connections \n
//  - None.
//
// \b Watch \b Variables \n
//  - None.
//
//
//#############################################################################
//
//
// $Copyright:
// Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.com
//
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions 
// are met:
// 
//   Redistributions of source code must retain the above copyright 
//   notice, this list of conditions and the following disclaimer.
// 
//   Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the 
//   documentation and/or other materials provided with the   
//   distribution.
// 
//   Neither the name of Texas Instruments Incorporated nor the names of
//   its contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// $
//#############################################################################

//
// Included Files
//
// Make sure to include "board.h" to use SysConfig
//
#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "C:/ti/c2000/C2000Ware_5_01_00_00/device_support/f2838x/headers/include/f2838x_device.h"
#include "../bms_shared_mem.h"

// 定義自訂 CPU2-to-CM IPC 暫存器結構，以完全滿足 IpcRegs.IPCSENDDATA 與 IpcRegs.IPCSET.bit.IPC2 語法
struct MY_IPC_REGS {
    union   CPU2TOCMIPCACK_REG               IPCACK;                       // 偏移 0
    union   CMTOCPU2IPCSTS_REG               IPCSTS;                       // 偏移 2
    union   CPU2TOCMIPCSET_REG               IPCSET;                       // 偏移 4
    union   CPU2TOCMIPCCLR_REG               IPCCLR;                       // 偏移 6
    union   CPU2TOCMIPCFLG_REG               IPCFLG;                       // 偏移 8
    uint16_t                                 rsvd1[2];                     // 偏移 10
    uint32_t                                 IPCCOUNTERL;                  // 偏移 12
    uint32_t                                 IPCCOUNTERH;                  // 偏移 14
    uint32_t                                 IPCSENDCOM;                   // 偏移 16
    uint32_t                                 IPCSENDADDR;                  // 偏移 18
    uint32_t                                 IPCSENDDATA;                  // 偏移 20
    uint32_t                                 IPCREPLY;                     // 偏移 22
    uint32_t                                 IPCRECVCOM;                   // 偏移 24
    uint32_t                                 IPCRECVADDR;                  // 偏移 26
    uint32_t                                 IPCRECVDATA;                  // 偏移 28
    uint32_t                                 IPCREPLY_2;                   // 偏移 30
};

#define IpcRegs (*((volatile struct MY_IPC_REGS *)0x0005CE40))

#define Cpu2toCpu1IpcRegs (*((volatile struct CPU1TOCPU2_IPC_REGS_CPU2VIEW *)0x0005CE00))
#define GpioDataRegs (*((volatile struct GPIO_DATA_REGS *)0x00007F00))
#define EPwm1Regs (*((volatile struct EPWM_REGS *)0x00004000))

// 配置變數 bms_ping_buffer 和 bms_pong_buffer，並將它們定位在 SHARERAM_GS0 (於連結檔中為 ramgs0) 記憶體段中
#pragma DATA_SECTION(bms_ping_buffer, "ramgs0")
BMS_Data_Packet bms_ping_buffer;

#pragma DATA_SECTION(bms_pong_buffer, "ramgs0")
BMS_Data_Packet bms_pong_buffer;

// Interrupt prototype
__interrupt void ipc0_isr(void);

//
// Main
//
void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Initialize settings from SysConfig
    //
    Board_init();

    //
    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    //
    Interrupt_initModule();

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    //
    Interrupt_initVectorTable();

    //
    // Register and enable CPU2 IPC0 Interrupt
    //
    IPC_registerInterrupt(IPC_CPU2_L_CPU1_R, IPC_INT0, ipc0_isr);

    //
    // Sync CPUs so the blinking starts at the same time, though the LEDs toggle at different frequency
    //
    IPC_sync(IPC_CPU2_L_CPU1_R, IPC_SYNC);

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // 狀態機與雙緩衝寫入相關變數
    uint32_t current_state = BMS_STATE_INIT;
    uint16_t state_timer = 0;
    uint32_t tx_counter = 0;
    uint16_t write_to_ping = 1;
    uint16_t led_counter = 0;

    //
    // Loop Forever (以 10ms 為週期)
    //
    for(;;)
    {
        // 1. BMS 有限狀態機 (FSM) 邏輯更新
        state_timer++;
        switch(current_state)
        {
            case BMS_STATE_INIT:
                if(state_timer >= 50) // 500ms
                {
                    current_state = BMS_STATE_STANDBY;
                    state_timer = 0;
                }
                break;
                
            case BMS_STATE_STANDBY:
                if(state_timer >= 50) // 500ms
                {
                    current_state = BMS_STATE_PRECHARGE;
                    state_timer = 0;
                }
                break;
                
            case BMS_STATE_PRECHARGE:
                if(state_timer >= 100) // 1000ms
                {
                    current_state = BMS_STATE_RUN;
                    state_timer = 0;
                }
                break;
                
            case BMS_STATE_RUN:
                if(state_timer >= 500) // 5000ms
                {
                    current_state = BMS_STATE_FAULT;
                    state_timer = 0;
                }
                break;
                
            case BMS_STATE_FAULT:
                if(state_timer >= 100) // 1000ms
                {
                    current_state = BMS_STATE_INIT;
                    state_timer = 0;
                }
                break;
                
            default:
                current_state = BMS_STATE_INIT;
                state_timer = 0;
                break;
        }

        // 2. 實作非阻塞寫入與 IPC 觸發
        // 檢查 CM 核心是否已讀完上一次的資料 (IPC2 旗標由 CM 清除為 0 時可寫入)
        if(IpcRegs.IPCFLG.bit.IPC2 == 0)
        {
            // 決定寫入哪一個緩衝區
            BMS_Data_Packet *active_buf = write_to_ping ? &bms_ping_buffer : &bms_pong_buffer;
            
            // 填寫封包內容
            active_buf->timestamp = tx_counter++;
            active_buf->bms_status = current_state;
            
            uint16_t idx;
            // 模擬 96 顆電芯電壓 (3.7V 到 3.8V 左右微幅增量)
            for(idx = 0; idx < 96; idx++)
            {
                active_buf->cell_voltages[idx] = 3.7f + (float)idx * 0.001f;
            }
            
            // 模擬 12 路溫度
            for(idx = 0; idx < 12; idx++)
            {
                active_buf->ntc_temperatures[idx] = 25.0f + (float)idx * 0.1f;
            }
            
            // 總電壓與總電流
            active_buf->total_voltage = 3.7f * 96.0f;
            active_buf->total_current = 15.5f;
            
            // 計算 Checksum (前 112 個 uint32_t/float 元素的總和)
            uint32_t sum = 0;
            uint32_t *ptr = (uint32_t *)active_buf;
            for(idx = 0; idx < 112; idx++)
            {
                sum += ptr[idx];
            }
            active_buf->checksum = sum;
            
            // 透過暫存器指令發送數據
            EALLOW;
            IpcRegs.IPCSENDDATA = (uint32_t)active_buf; // 傳送實體物理地址
            IpcRegs.IPCSET.bit.IPC2 = 1;                     // 觸發 CM 核心的 IPC2 中斷
            EDIS;
            
            // 切換緩衝區
            write_to_ping = !write_to_ping;
        }

        // 心跳燈 (每 500ms 切換一次)
        led_counter++;
        if(led_counter >= 50)
        {
            led_counter = 0;
            GpioDataRegs.GPATOGGLE.bit.GPIO1 = 1;
        }

        // 延遲 10ms 實現 100Hz 迴圈
        DEVICE_DELAY_US(10000);
    }
}

//
// IPC0 Interrupt Service Routine (ISR)
// Triggers when CPU1 sets IPC_FLAG0.
// Must be ultra-fast (limit execution under 500ns).
//
__interrupt void ipc0_isr(void)
{
    // Verify CPU1 set IPC_FLAG10 (START_DDS command flag)
    if(Cpu2toCpu1IpcRegs.CPU1TOCPU2IPCSTS.bit.IPC10 == 1)
    {
        // Read new frequency from CPU1_TO_CPU2_MSG_RAM (0x03A000)
        uint32_t freq_val = *((volatile uint32_t *)0x03A000);

        // Clear/ACK both IPC_FLAG10 and IPC_FLAG0 flags
        EALLOW;
        Cpu2toCpu1IpcRegs.CPU2TOCPU1IPCACK.all = (1UL << 10) | (1UL << 0);
        EDIS;

        // Write directly to ePWM1 TBPRD register (simulating DDS frequency update)
        EPwm1Regs.TBPRD = (uint16_t)freq_val;
    }

    // Acknowledge PIE Group 1 Interrupt
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//
// End of File
//
