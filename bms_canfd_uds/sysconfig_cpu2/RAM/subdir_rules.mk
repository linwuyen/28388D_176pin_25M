################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.obj: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C2000 Compiler'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --idiv_support=idiv0 --tmu_support=tmu0 --vcu_support=vcrc -Ooff --fp_mode=relaxed --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2" --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2/device" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib" --include_path="C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/include" --define=CPU2 --define=RAM --define=DEBUG --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2/RAM/syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-1934004607: ../led_ex2_blinky_sysconfig_cpu2.syscfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: SysConfig'
	"C:/ti/ccs1281/ccs/utils/sysconfig_1.21.0/sysconfig_cli.bat" --script "d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu1/led_ex2_blinky_sysconfig_cpu1.syscfg" --context "CPU1" --script "d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2/led_ex2_blinky_sysconfig_cpu2.syscfg" -o "syscfg" -s "C:/ti/c2000/C2000Ware_5_01_00_00/.metadata/sdk.json" -d "F2838x" --context "CPU2" --compiler ccs
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/board.c: build-1934004607 ../led_ex2_blinky_sysconfig_cpu2.syscfg
syscfg/board.h: build-1934004607
syscfg/board.cmd.genlibs: build-1934004607
syscfg/board.opt: build-1934004607
syscfg/pinmux.csv: build-1934004607
syscfg/c2000ware_libraries.cmd.genlibs: build-1934004607
syscfg/c2000ware_libraries.opt: build-1934004607
syscfg/c2000ware_libraries.c: build-1934004607
syscfg/c2000ware_libraries.h: build-1934004607
syscfg/clocktree.h: build-1934004607
syscfg: build-1934004607

syscfg/%.obj: ./syscfg/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C2000 Compiler'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --idiv_support=idiv0 --tmu_support=tmu0 --vcu_support=vcrc -Ooff --fp_mode=relaxed --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2" --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2/device" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib" --include_path="C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/include" --define=CPU2 --define=RAM --define=DEBUG --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="syscfg/$(basename $(<F)).d_raw" --include_path="d:/GITHUB/28388D_176pin_25M/bms_canfd_uds/sysconfig_cpu2/RAM/syscfg" --obj_directory="syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


