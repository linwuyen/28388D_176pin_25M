#ifndef CAR_PIPELINE_SHARED_H
#define CAR_PIPELINE_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Raw sensor data written by CPU1 to GS0, read by CPU2
// Aligned to 32-bit boundary for multi-core compatibility
typedef struct {
    volatile uint32_t total_voltage_mv; // Simulated pack voltage in mV
    volatile int32_t total_current_ma;  // Simulated pack current in mA
    volatile uint32_t timestamp_us;     // Timestamp in microseconds
} RawSensorData;

// Processed state data written by CPU2 to GS1, read by CM
// Aligned to 32-bit boundary for multi-core compatibility
typedef struct {
    volatile uint32_t avg_voltage_mv;   // Moving average voltage
    volatile int32_t avg_current_ma;    // Moving average current
    volatile uint32_t state_flags;      // Bit 0: Over-voltage, Bit 1: Over-current
    volatile uint32_t update_counter;   // Output packet counter
} ProcessedData;

#ifdef __cplusplus
}
#endif

#endif // CAR_PIPELINE_SHARED_H
