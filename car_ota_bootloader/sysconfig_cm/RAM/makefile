# BMS Practice Project - Project 1 CM Core Makefile

SHELL = cmd.exe

CG_TOOL_ROOT := C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-arm_20.2.7.LTS
CC := "$(CG_TOOL_ROOT)/bin/armcl"
LNK := "$(CG_TOOL_ROOT)/bin/armcl"

# Compiler Flags
# -mv7M4: Cortex-M4
# --code_state=16: Thumb mode
# --float_support=FPv4SPD16: Single-precision FPU support
# -me: Little Endian
CFLAGS := -mv7M4 --code_state=16 --float_support=FPv4SPD16 -me --opt_level=off -g \
          --define=DEBUG --define=CM --define=RAM \
          --diag_warning=225 --diag_wrap=off --display_error_number \
          --abi=eabi \
          -I"d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cm" \
          -I"d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cm/device" \
          -I"C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib_cm" \
          -I"$(CG_TOOL_ROOT)/include"

# Linker Flags
LFLAGS := -z -m"sysconfig_cm.map" --heap_size=0x400 --stack_size=0x200 \
          --warn_sections \
          -i"$(CG_TOOL_ROOT)/lib" \
          -i"$(CG_TOOL_ROOT)/include" \
          --reread_libs --rom_model

OBJS := led_ex2_blinky_sysconfig_cm.obj startup_cm.obj device_cm.obj

LIBS := "C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib_cm/ccs/Debug/driverlib_cm.lib" -llibc.a

all: sysconfig_cm.out

sysconfig_cm.out: $(OBJS) ../2838x_RAM_lnk_cm.cmd
	@echo Building target: $@
	@echo Invoking: ARM Linker
	$(LNK) $(CFLAGS) $(LFLAGS) -o $@ $(OBJS) ../2838x_RAM_lnk_cm.cmd $(LIBS)
	@echo Finished building target: $@
	@echo.

led_ex2_blinky_sysconfig_cm.obj: ../led_ex2_blinky_sysconfig_cm.c
	@echo Compiling: $<
	$(CC) $(CFLAGS) --preproc_with_compile --output_file=$@ $<

startup_cm.obj: ../startup_cm.c
	@echo Compiling: $<
	$(CC) $(CFLAGS) --preproc_with_compile --output_file=$@ $<

device_cm.obj: ../device/cm.c
	@echo Compiling: $<
	$(CC) $(CFLAGS) --preproc_with_compile --output_file=$@ $<

clean:
	-del /F $(OBJS) sysconfig_cm.out sysconfig_cm.map
	@echo Finished clean

.PHONY: all clean

