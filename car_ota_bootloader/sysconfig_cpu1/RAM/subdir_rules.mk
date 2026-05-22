################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.obj: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C2000 Compiler'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --idiv_support=idiv0 --tmu_support=tmu0 --vcu_support=vcrc -Ooff --fp_mode=relaxed --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1" --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1/device" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib" --include_path="C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/include" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/libraries/flash_api/f2838x/c28x/include/FlashAPI" --define=CPU1 --define=RAM --define=DEBUG --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1/RAM/syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-245800513: ../led_ex2_blinky_sysconfig_cpu1.syscfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: SysConfig'
	"C:/ti/ccs1281/ccs/utils/sysconfig_1.21.0/sysconfig_cli.bat" --script "d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu2/led_ex2_blinky_sysconfig_cpu2.syscfg" --context "CPU2" --script "d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1/led_ex2_blinky_sysconfig_cpu1.syscfg" -o "syscfg" -s "C:/ti/c2000/C2000Ware_5_01_00_00/.metadata/sdk.json" -d "F2838x" --context "CPU1" --compiler ccs
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/board.c: build-245800513 ../led_ex2_blinky_sysconfig_cpu1.syscfg
syscfg/board.h: build-245800513
syscfg/board.cmd.genlibs: build-245800513
syscfg/board.opt: build-245800513
syscfg/pinmux.csv: build-245800513
syscfg/c2000ware_libraries.cmd.genlibs: build-245800513
syscfg/c2000ware_libraries.opt: build-245800513
syscfg/c2000ware_libraries.c: build-245800513
syscfg/c2000ware_libraries.h: build-245800513
syscfg/clocktree.h: build-245800513
syscfg: build-245800513

syscfg/%.obj: ./syscfg/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C2000 Compiler'
	"C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/bin/cl2000" -v28 -ml -mt --cla_support=cla2 --float_support=fpu64 --idiv_support=idiv0 --tmu_support=tmu0 --vcu_support=vcrc -Ooff --fp_mode=relaxed --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1" --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1/device" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/driverlib/f2838x/driverlib" --include_path="C:/ti/ccs1281/ccs/tools/compiler/ti-cgt-c2000_22.6.1.LTS/include" --include_path="C:/ti/c2000/C2000Ware_5_01_00_00/libraries/flash_api/f2838x/c28x/include/FlashAPI" --define=CPU1 --define=RAM --define=DEBUG --diag_suppress=10063 --diag_warning=225 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="syscfg/$(basename $(<F)).d_raw" --include_path="d:/GITHUB/28388D_176pin_25M/car_ota_bootloader/sysconfig_cpu1/RAM/syscfg" --obj_directory="syscfg" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '



