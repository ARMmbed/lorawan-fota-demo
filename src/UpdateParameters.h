/*
* PackageLicenseDeclared: Apache-2.0
* Copyright (c) 2017 ARM Limited
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
 * This file is shared between bootloader and the target application
 */

#ifndef _MBED_FOTA_UPDATE_PARAMS
#define _MBED_FOTA_UPDATE_PARAMS

// These values need to be the same between target application and bootloader!
#define     FOTA_INFO_PAGE         0x1800                       // The information page for the firmware update
#define     FOTA_UPDATE_PAGE       0x1801                       // The update starts at this page (and then continues)
#define     FOTA_DIFF_OLD_FW_PAGE  0x2100
#define     FOTA_DIFF_TARGET_PAGE  0x2500
#define     FOTA_SIGNATURE_LENGTH  sizeof(UpdateSignature_t)    // Length of ECDSA signature + class UUIDs + diff struct (5 bytes) -> matches sizeof(UpdateSignature_t)

// This structure is shared between the bootloader and the target application
// it contains information on whether there's an update pending, and the hash of the update
struct UpdateParams_t {
    bool update_pending;                // whether there's a pending update
    size_t size;                        // size of the update
    uint32_t offset;                    // Location of the patch in flash
    unsigned char sha256_hash[32];      // SHA256 hash of the update block

    uint32_t signature;                 // the value of MAGIC (to indicate that we actually wrote to this block)

    static const uint32_t MAGIC = 0x1BEAC000;
};

// This structure contains the update header (which is the first FOTA_SIGNATURE_LENGTH bytes of a package)
typedef struct __attribute__((__packed__)) {
    uint8_t signature_length;           // Length of the ECDSA/SHA256 signature
    unsigned char signature[72];        // ECDSA/SHA256 signature, signed with private key of the firmware (after applying patching)
    uint8_t manufacturer_uuid[16];      // Manufacturer UUID
    uint8_t device_class_uuid[16];      // Device Class UUID

    uint32_t diff_info;                 // first byte indicates whether this is a diff, last three bytes are the size of the *old* file
} UpdateSignature_t;

#endif
