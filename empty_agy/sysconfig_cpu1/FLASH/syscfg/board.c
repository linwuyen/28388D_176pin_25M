/*
 * Copyright (c) 2020 Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "board.h"

//*****************************************************************************
//
// Board Configurations
// Initializes the rest of the modules. 
// Call this function in your application if you wish to do all module 
// initialization.
// If you wish to not use some of the initializations, instead of the 
// Board_init use the individual Module_inits
//
//*****************************************************************************
void Board_init()
{
	EALLOW;

	PinMux_init();
	GPIO_init();
	IPC_SYSCFG_init();

	EDIS;
}

//*****************************************************************************
//
// PINMUX Configurations
//
//*****************************************************************************
void PinMux_init()
{
	//
	// PinMux for modules assigned to CPU1
	//
	
	// GPIO0 -> CPU1_LED Pinmux
	GPIO_setPinConfig(GPIO_0_GPIO0);
	// GPIO1 -> CPU2_LED Pinmux
	GPIO_setPinConfig(GPIO_1_GPIO1);
	//
	// PinMux for modules assigned to CPU2
	//
	

}

//*****************************************************************************
//
// GPIO Configurations
//
//*****************************************************************************
void GPIO_init(){
	CPU1_LED_init();
	CPU2_LED_init();
}

void CPU1_LED_init(){
	GPIO_setPadConfig(CPU1_LED, GPIO_PIN_TYPE_STD);
	GPIO_setQualificationMode(CPU1_LED, GPIO_QUAL_SYNC);
	GPIO_setDirectionMode(CPU1_LED, GPIO_DIR_MODE_OUT);
	GPIO_setControllerCore(CPU1_LED, GPIO_CORE_CPU1);
}
void CPU2_LED_init(){
	GPIO_setPadConfig(CPU2_LED, GPIO_PIN_TYPE_STD);
	GPIO_setQualificationMode(CPU2_LED, GPIO_QUAL_SYNC);
	GPIO_setDirectionMode(CPU2_LED, GPIO_DIR_MODE_OUT);
	GPIO_setControllerCore(CPU2_LED, GPIO_CORE_CPU2);
}

//*****************************************************************************
//
// IPC Configurations
//
//*****************************************************************************
void IPC_SYSCFG_init(){
    //
    // Paste the following line in your main() function after device_init, if you would like CPU2 to boot
    // Device_bootCPU2(BOOT_MODE_CPU2);           
    //
}
