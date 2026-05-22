//#############################################################################
//
// FILE:   device.h
//
// TITLE:  Device setup for examples.
//
//#############################################################################
//
// Copyright (C) 2014-2022 Texas Instruments Incorporated - http://www.ti.com/
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
#ifndef __DEVICE_H__
#define __DEVICE_H__
//
// Included Files
//
#include "driverlib.h"
#include "clocktree.h"

#if (!defined(CPU1) && !defined(CPU2))
#error "You must define CPU1 or CPU2 in your project properties.  Otherwise, \
the offsets in your header files will be inaccurate."
#endif

#if (defined(CPU1) && defined(CPU2))
#error "You have defined both CPU1 and CPU2 in your project properties.  Only \
a single CPU should be defined."
#endif


//*****************************************************************************
//
// Macro to call SysCtl_delay() to achieve a delay in microseconds. The macro
// will convert the desired delay in microseconds to the count value expected
// by the function. \b x is the number of microseconds to delay.
//
//*****************************************************************************
#define DEVICE_DELAY_US(x) SysCtl_delay(((((long double)(x)) / (1000000.0L /  \
                                (long double)DEVICE_SYSCLK_FREQ)) - 9.0L) / 5.0L)

//*****************************************************************************
//
// Macros related to booting CPU2 and CM. These can be used while invoking the
// functions Device_bootCPU2() and Device_bootCM(). 
//
// Note that the macros CM_BOOT_FREQ_125MHZ and CPU2_BOOT_FREQ_200MHZ are used
// in the functions Device_bootCM() and Device_bootCPU2() respectively. Please
// update the function and the macros if you are using a different clock
// frequency.
//
//*****************************************************************************
#ifdef CPU1
#define BOOT_KEY                                0x5A000000UL
#define CM_BOOT_FREQ_125MHZ                     0x7D00U
#define CPU2_BOOT_FREQ_200MHZ                   0xC800U

#define BOOTMODE_BOOT_TO_FLASH_SECTOR0          0x03U
#define BOOTMODE_BOOT_TO_FLASH_SECTOR4          0x23U
#define BOOTMODE_BOOT_TO_FLASH_SECTOR8          0x43U
#define BOOTMODE_BOOT_TO_FLASH_SECTOR13         0x63U
#define BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR0   0x0AU
#define BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR4   0x2AU
#define BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR8   0x4AU
#define BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR13  0x6AU
#define BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_M1RAM  0x0CU
#define BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_S0RAM  0x0CU
#define BOOTMODE_BOOT_TO_M0RAM                  0x05U
#define BOOTMODE_BOOT_TO_S0RAM                  0x05U
#define BOOTMODE_BOOT_TO_USEROTP                0x0BU

#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_100W    0x10000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_200W    0x20000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_300W    0x30000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_400W    0x40000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_500W    0x50000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_600W    0x60000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_700W    0x70000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_800W    0x80000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_900W    0x90000U
#define BOOTMODE_IPC_MSGRAM_COPY_LENGTH_1000W   0xA0000U
#endif

//*****************************************************************************
//
// Defines, Globals, and Header Includes related to Flash Support
//
//*****************************************************************************
#ifdef _FLASH
#include <stddef.h>

#ifndef CMDTOOL
extern uint16_t RamfuncsLoadStart;
extern uint16_t RamfuncsLoadEnd;
extern uint16_t RamfuncsLoadSize;
extern uint16_t RamfuncsRunStart;
extern uint16_t RamfuncsRunEnd;
extern uint16_t RamfuncsRunSize;
#endif

#define DEVICE_FLASH_WAITSTATES 3

#endif

//*****************************************************************************
//
// Function Prototypes
//
//*****************************************************************************
//*****************************************************************************
//
//! \addtogroup device_api
//! @{
//
//*****************************************************************************
//*****************************************************************************
//
//! Function to initialize the device. Primarily initializes system 
//! control to a known state by disabling the watchdog, setting up the
//! SYSCLKOUT frequency, and enabling the clocks to the peripherals.
//!
//! \param None.
//! \return None.
//
//*****************************************************************************
extern void Device_init(void);


//*****************************************************************************
//!
//!
//! @brief Function to turn on all peripherals, enabling reads and writes to the
//! peripherals' registers.
//!
//! Note that to reduce power, unused peripherals should be disabled.
//!
//! @param None
//! @return None
//
//*****************************************************************************
extern void Device_enableAllPeripherals(void);
//*****************************************************************************
//!
//!
//! @brief Function to disable pin locks on GPIOs.
//!
//! @param None
//! @return None
//
//*****************************************************************************
extern void Device_initGPIO(void);

//*****************************************************************************
//!
//! @brief Function to enable pullups for the unbonded GPIOs on the 176PTP package:
//! GPIOs     Grp Bits
//! 95-132    C   31
//!           D   31:0
//!           E   4:0
//! 134-168   E   31:6
//!           F   8:0
//!
//! @param None
//! @return None
//
//*****************************************************************************
extern void Device_enableUnbondedGPIOPullupsFor176Pin(void);
//*****************************************************************************
//!
//! @brief Function to enable pullups for the unbonded GPIOs on the
//! 176PTP package.
//!
//! @param None
//! @return None
//
//*****************************************************************************
extern void Device_enableUnbondedGPIOPullups(void);


#ifdef CPU1

//*****************************************************************************
//
//! @brief Function to boot CPU2.
//!
//! \param bootmode is the mode in which CPU2 should boot.
//!
//! Available bootmodes :
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR0
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR4
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR8
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR13
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR0
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR4
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR8
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR13
//!      - BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_M1RAM
//!      - BOOTMODE_BOOT_TO_M0RAM
//!      - BOOTMODE_BOOT_TO_USEROTP
//!
//! Note that while using BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_M1RAM,
//! BOOTMODE_IPC_MSGRAM_COPY_LENGTH_xxxW must be ORed with the bootmode parameter
//!
//! This function must be called after Device_init function
//! \return None.
//
//*****************************************************************************
extern void Device_bootCPU2(uint32_t bootmode);

//*****************************************************************************
//
//! @brief Function to boot CM
//!
//! \param bootmode is the mode in which CM should boot.
//!
//! Available bootmodes :
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR0
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR4
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR8
//!      - BOOTMODE_BOOT_TO_FLASH_SECTOR13
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR0
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR4
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR8
//!      - BOOTMODE_BOOT_TO_SECURE_FLASH_SECTOR13
//!      - BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_S0RAM
//!      - BOOTMODE_BOOT_TO_S0RAM
//!      - BOOTMODE_BOOT_TO_USEROTP
//!
//! Note that while using BOOTMODE_IPC_MSGRAM_COPY_BOOT_TO_M1RAM,
//! BOOTMODE_IPC_MSGRAM_COPY_LENGTH_xxxW must be ORed with the bootmode parameter
//!
//! This function must be called after Device_init function
//! \return None.
//
//*****************************************************************************
extern void Device_bootCM(uint32_t bootmode);

//*****************************************************************************
//
//!
//! @brief Function to verify the XTAL frequency
//! \param freq is the XTAL frequency in MHz
//! \return The function return true if the the actual XTAL frequency matches with the
//! input value
//
//*****************************************************************************
extern bool Device_verifyXTAL(float freq);
#endif

//*****************************************************************************
//!
//! @brief Error handling function to be called when an ASSERT is violated
//!
//! @param *filename File name in which the error has occurred
//! @param line Line number within the file
//! @return None
//
//*****************************************************************************
extern void __error__(const char *filename, uint32_t line);
//*****************************************************************************
//
// Close the Doxygen group.
//! @}
//
//*****************************************************************************
#endif // __DEVICE_H__
