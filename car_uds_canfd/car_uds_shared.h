#ifndef CAR_UDS_SHARED_H
#define CAR_UDS_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Shared structure between CPU2 and CM located at the start of GS0 RAM
// Align variables to 32-bit boundary for C28x and CM compatibility
typedef struct {
    volatile uint32_t active_fault_code; // 0: Normal, 0x02: Over-temperature
    volatile uint32_t cell_voltage_mv;   // Real-time cell voltage in mV
    volatile uint32_t cell_temp_c;       // Real-time temperature in Celsius
} UDS_SharedData;

#ifdef __cplusplus
}
#endif

#endif // CAR_UDS_SHARED_H
