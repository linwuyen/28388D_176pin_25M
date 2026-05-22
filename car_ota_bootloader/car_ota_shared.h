#ifndef CAR_OTA_SHARED_H
#define CAR_OTA_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// OTA Commands from CM to CPU1 (written to GS0 by CM)
#define OTA_CMD_NONE         0U
#define OTA_CMD_START        1U // Triggers Erase of Bank 1
#define OTA_CMD_PROGRAM      2U // Triggers Program of a 128-byte chunk
#define OTA_CMD_VERIFY       3U // Triggers CRC32 verification of total image
#define OTA_CMD_RUN          4U // Triggers soft reset or jump to Bank 1

// OTA Statuses from CPU1 to CM (written to GS1 by CPU1)
#define OTA_STATUS_IDLE             0U
#define OTA_STATUS_ERASING          1U
#define OTA_STATUS_ERASE_DONE       2U
#define OTA_STATUS_PROGRAMMING      3U
#define OTA_STATUS_PROGRAM_DONE     4U
#define OTA_STATUS_VERIFYING        5U
#define OTA_STATUS_VERIFY_SUCCESS   6U
#define OTA_STATUS_ERROR_ERASE      0x81U
#define OTA_STATUS_ERROR_PROGRAM    0x82U
#define OTA_STATUS_ERROR_CRC        0x83U
#define OTA_STATUS_ERROR_SEQUENCE   0x84U

// OTA Command & Payload structure - placed in GS0 (CM Write / CPU1 Read)
// CM is the owner of GS0
typedef struct {
    volatile uint32_t command;       // Command from CM to CPU1
    volatile uint32_t chunk_idx;     // Current chunk index (0-based)
    volatile uint32_t chunk_size;    // Size of current chunk in bytes (normally 128)
    volatile uint32_t total_size;    // Total size of binary in bytes
    volatile uint32_t expected_crc;  // Expected CRC32 of total binary
    volatile uint32_t chunk_crc;     // CRC32 of current chunk
    volatile uint32_t payload[32];   // 128 bytes payload (32 * 32-bit words)
} OtaCmdBuffer;

// OTA Status structure - placed in GS1 (CPU1 Write / CM Read)
// CPU1 is the owner of GS1
typedef struct {
    volatile uint32_t status;        // Status from CPU1 to CM
    volatile uint32_t error_code;    // Detailed error or return code
    volatile uint32_t ack_chunk_idx; // Last processed chunk index
} OtaStatusBuffer;

#ifdef __cplusplus
}
#endif

#endif // CAR_OTA_SHARED_H
