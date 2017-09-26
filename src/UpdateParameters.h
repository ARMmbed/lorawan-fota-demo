#ifndef _MBED_FOTA_UPDATE_PARAMS
#define _MBED_FOTA_UPDATE_PARAMS

// This structure is shared between the bootloader and the target application

struct UpdateParams_t {
    bool update_pending;
    size_t size;
    uint32_t signature;
    uint64_t hash;

    static const uint32_t MAGIC = 0x1BEAC000;
};

#endif
