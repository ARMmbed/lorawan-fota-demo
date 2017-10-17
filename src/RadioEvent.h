#ifndef __RADIO_EVENT_H__
#define __RADIO_EVENT_H__

#include "mbed.h"

#include "dot_util.h"
#include "mDotEvent.h"
#include "ProtocolLayer.h"
#include "base64.h"
#include "AT45BlockDevice.h"
#include "mbed_lorawan_frag_lib.h"
#include "tiny-aes.h"
#include "mbed_stats.h"
#include "UpdateParameters.h"
#include "UpdateCerts.h"
#include "BDFile.h"
#include "janpatch.h"
#include "mbed_delta_update.h"

typedef struct {
    uint32_t uplinkCounter;
    time_t time;
} UplinkEvent_t;

typedef struct {
    uint8_t DevAddr[4];
    uint8_t AppSKey[16];
    uint8_t NwkSKey[16];

    uint32_t UplinkCounter;
    uint32_t DownlinkCounter;

    uint8_t TxDataRate;
    uint8_t RxDataRate;

    uint32_t Rx2Frequency;
} LoRaWANCredentials_t;

static bool compare_buffers(uint8_t* buff1, const uint8_t* buff2, size_t size) {
    for (size_t ix = 0; ix < size; ix++) {
        if (buff1[ix] != buff2[ix]) return false;
    }
    return true;
}

static void calculate_sha256(BlockDevice* bd, size_t offset, size_t size, unsigned char sha_out_buffer[32]) {
    uint8_t sha_buffer[128];

    // SHA256 requires a large buffer, alloc on heap instead of stack
    FragmentationSha256* sha256 = new FragmentationSha256(bd, sha_buffer, sizeof(sha_buffer));

    sha256->calculate(
        offset,
        size,
        sha_out_buffer);

    delete sha256;
}

static void print_sha256(unsigned char sha_out_buffer[32]) {
    debug("SHA256 hash is: ");
    for (size_t ix = 0; ix < 32; ix++) {
        debug("%02x", sha_out_buffer[ix]);
    }
    debug("\n");
}

class RadioEvent : public mDotEvent
{

public:
    RadioEvent(
        // EventQueue* aevent_queue,
        Callback<void(uint8_t, std::vector<uint8_t>*)> asend_msg_cb,
        Callback<void(char)> aclass_switch_cb
    ) : /*event_queue(aevent_queue), */send_msg_cb(asend_msg_cb), class_switch_cb(aclass_switch_cb)
    {
        join_succeeded = false;
        cls = '0';
        has_received_frag_session = false;

        int ain;
        if ((ain = at45.init()) != BD_ERROR_OK) {
            printf("Failed to initialize AT45BlockDevice (%d)\n", ain);
        }
    }

    virtual ~RadioEvent() {}

    /*!
     * MAC layer event callback prototype.
     *
     * \param [IN] flags Bit field indicating the MAC events occurred
     * \param [IN] info  Details about MAC events occurred
     */
    virtual void MacEvent(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        HandleMacEvent(flags, info);

        // Process on the events thread which has bigger stack
        // event_queue->call(callback(this, &RadioEvent::HandleMacEvent), flags, info);
    }

    void OnTx(uint32_t uplinkCounter) {
        // move all one up
        uplinkEvents[0] = uplinkEvents[1];
        uplinkEvents[1] = uplinkEvents[2];
        uplinkEvents[2] = uplinkEvents[3];
        uplinkEvents[3] = uplinkEvents[4];
        uplinkEvents[4] = uplinkEvents[5];
        uplinkEvents[5] = uplinkEvents[6];
        uplinkEvents[6] = uplinkEvents[7];
        uplinkEvents[7] = uplinkEvents[8];
        uplinkEvents[8] = uplinkEvents[9];

        uplinkEvents[9].uplinkCounter = uplinkCounter;
        uplinkEvents[9].time = time(NULL);

        // logInfo("UplinkEvents are:");
        // printf("\t[0] %li %li\n", uplinkEvents[0].uplinkCounter, uplinkEvents[0].time);
        // printf("\t[1] %li %li\n", uplinkEvents[1].uplinkCounter, uplinkEvents[1].time);
        // printf("\t[2] %li %li\n", uplinkEvents[2].uplinkCounter, uplinkEvents[2].time);
    }

    void switchedToClassC() {
        cls = 'C';

        class_c_cancel_timeout.attach(callback(this, &RadioEvent::ClassCTimeout), class_c_cancel_s);
    }

    void switchedToClassA() {
        cls = 'A';

        class_c_cancel_timeout.detach();
    }

    void OnClassAJoinSucceeded(LoRaWANCredentials_t* credentials) {

        join_succeeded = true;

        UpdateClassACredentials(credentials);

        printf("ClassAJoinSucceeded:\n");
        printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_a_credentials.DevAddr, 4).c_str());
        printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.NwkSKey, 16).c_str());
        printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.AppSKey, 16).c_str());
        printf("\tUplinkCounter: %li\n", class_a_credentials.UplinkCounter);
        printf("\tDownlinkCounter: %li\n", class_a_credentials.DownlinkCounter);
        printf("\tTxDataRate: %d\n", class_a_credentials.TxDataRate);
        printf("\tRxDataRate: %d\n", class_a_credentials.RxDataRate);

        mbed_stats_heap_t heap_stats;
        mbed_stats_heap_get(&heap_stats);
        printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);
    }

    void UpdateClassACredentials(LoRaWANCredentials_t* credentials) {
        memcpy(&class_a_credentials, credentials, sizeof(LoRaWANCredentials_t));
    }

    LoRaWANCredentials_t* GetClassACredentials() {
        return &class_a_credentials;
    }

    LoRaWANCredentials_t* GetClassCCredentials() {
        return &class_c_credentials;
    }

private:

    void processFragmentationMacCommand(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        switch(info->RxBuffer[0]) {
            case FRAG_SESSION_SETUP_REQ:
            {
                mbed_stats_heap_t heap_stats;
                mbed_stats_heap_get(&heap_stats);
                printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);

                if(info->RxBufferSize != FRAG_SESSION_SETUP_REQ_LENGTH) {
                    logError("Invalid FRAG_SESSION_SETUP_REQ command");
                    return;
                }

                // @todo: fragsession is bit 5 and 6 on byte 1
                // @todo: extract mc group from byte 1
                frag_params.FragSession = (info->RxBuffer[1] >> 4) & 0x03;
                frag_params.NbFrag = ( info->RxBuffer[3] << 8 ) + info->RxBuffer[2];
                frag_params.FragSize = info->RxBuffer[4];
                frag_params.Encoding = info->RxBuffer[5];
                frag_params.Padding = info->RxBuffer[6];
                frag_params.Redundancy = REDUNDANCYMAX-1;

                // [2, 0, 26, 0, 204, 0, 184]

                printf("FRAG_SESSION_SETUP_REQ:\n");
                printf("\tFragSession: %d\n", frag_params.FragSession);
                printf("\tNbFrag: %d\n", frag_params.NbFrag);
                printf("\tFragSize: %d\n", frag_params.FragSize);
                printf("\tEncoding: %d\n", frag_params.Encoding);
                printf("\tRedundancy: %d\n", frag_params.Redundancy);
                printf("\tPadding: %d\n", frag_params.Padding);

                std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                ack->push_back(FRAG_SESSION_SETUP_ANS);
                ack->push_back(0b01000000);
                send_msg_cb(201, ack);

                frag_opts.NumberOfFragments = frag_params.NbFrag;
                frag_opts.FragmentSize = frag_params.FragSize;
                frag_opts.Padding = frag_params.Padding;

                // @todo: make this dependent on the space on the heap
                frag_opts.RedundancyPackets = MBED_CONF_APP_MAX_REDUNDANCY_PACKETS;

                frag_opts.FlashOffset = FOTA_UPDATE_PAGE * at45.get_read_size();

                if (frag_session != NULL) {
                    delete frag_session;
                }

                mbed_stats_heap_get(&heap_stats);
                printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);

                frag_session = new FragmentationSession(&at45, frag_opts);
                FragResult result = frag_session->initialize();
                if (result != FRAG_OK) {
                    printf("FragmentationSession could not initialize! %d %s\n", result, FragmentationSession::frag_result_string(result));
                    break;
                }

                printf("FragmentationSession initialized OK\n");

                has_received_frag_session = true;

                mbed_stats_heap_get(&heap_stats);
                printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);
            }
            break;

            case DATA_FRAGMENT:
            {
                uint16_t frameCounter = (info->RxBuffer[2] << 8) + info->RxBuffer[1];

                if (frag_session == NULL) return;

                FragResult result;

                if ((result = frag_session->process_frame(frameCounter, info->RxBuffer + 3, info->RxBufferSize - 3)) != FRAG_OK) {
                    if (result == FRAG_COMPLETE) {
                        printf("FragmentationSession is complete at frame %d\n", frameCounter);
                        delete frag_session;
                        frag_session = NULL;

                        mbed_stats_heap_t heap_stats;
                        mbed_stats_heap_get(&heap_stats);
                        printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);

                        InvokeClassASwitch();

                        // Calculate the CRC of the data in flash to see if the file was unpacked correctly
                        // CRC64 of the original file is 150eff2bcd891e18 (see fake-fw/test-crc64/main.cpp)
                        uint8_t crc_buffer[128];

                        FragmentationCrc64 crc64(&at45, crc_buffer, sizeof(crc_buffer));
                        uint64_t crc_res = crc64.calculate(frag_opts.FlashOffset, (frag_opts.NumberOfFragments * frag_opts.FragmentSize) - frag_opts.Padding);

                        printf("Hash is %08llx\n", crc_res);

                        // Write the parameters to flash; but don't set update_pending yet (only after verification by the network)
                        UpdateParams_t update_params;
                        update_params.update_pending = 0;
                        update_params.size = (frag_opts.NumberOfFragments * frag_opts.FragmentSize) - frag_opts.Padding - FOTA_SIGNATURE_LENGTH;
                        update_params.offset = frag_opts.FlashOffset + FOTA_SIGNATURE_LENGTH;
                        update_params.signature = UpdateParams_t::MAGIC;
                        at45.program(&update_params, FOTA_INFO_PAGE * at45.get_read_size(), sizeof(UpdateParams_t));

                        std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                        ack->push_back(DATA_BLOCK_AUTH_REQ);
                        ack->push_back(frag_params.FragSession); // fragindex

                        uint8_t* crc_buff = (uint8_t*)&crc_res;
                        ack->push_back(crc_buff[0]);
                        ack->push_back(crc_buff[1]);
                        ack->push_back(crc_buff[2]);
                        ack->push_back(crc_buff[3]);
                        ack->push_back(crc_buff[4]);
                        ack->push_back(crc_buff[5]);
                        ack->push_back(crc_buff[6]);
                        ack->push_back(crc_buff[7]);

                        send_msg_cb(201, ack);

                        break;
                    }
                    else {
                        printf("FragmentationSession process_frame %d failed: %s\n",
                            frameCounter, FragmentationSession::frag_result_string(result));
                        break;
                    }
                }

                printf("Processed frame with frame counter %d, packets lost %d\n", frameCounter, frag_session->get_lost_frame_count());
                break;
            }
            break;

            case DATA_BLOCK_AUTH_ANS:
            {
                // sanity check in case old DATA_BLOCK_AUTH_ANS is still in the queue
                if (!has_received_frag_session) return;

                printf("DATA_BLOCK_AUTH_ANS: ");
                for (size_t ix = 0; ix < info->RxBufferSize; ix++) {
                    printf("%02x ", info->RxBuffer[ix]);
                }
                printf("\n");



                // fragindex and success bit are on info->RxBuffer[1]
                if (info->RxBufferSize == 2) {
                    // not good!
                }
                else {
                    // do MIC check...
                    // if MIC check is OK, then start flashing the firmware
                    // TTN has MIC not implemented yet

                    UpdateParams_t update_params;
                    // read the current page (with offset and size info)
                    at45.read(&update_params, FOTA_INFO_PAGE * at45.get_read_size(), sizeof(UpdateParams_t));

                    // Read out the header of the package...
                    UpdateSignature_t* header = new UpdateSignature_t();
                    at45.read(header, update_params.offset - FOTA_SIGNATURE_LENGTH, FOTA_SIGNATURE_LENGTH);

                    if (!compare_buffers(header->manufacturer_uuid, UPDATE_CERT_MANUFACTURER_UUID, 16)) {
                        debug("Manufacturer UUID does not match\n");
                        return;
                    }

                    debug("Manufacturer UUID matches\n");

                    if (!compare_buffers(header->device_class_uuid, UPDATE_CERT_DEVICE_CLASS_UUID, 16)) {
                        debug("Device class UUID does not match\n");
                        return;
                    }

                    debug("Device class UUID matches\n");

                    // Is this a diff?
                    uint8_t* diff_info = (uint8_t*)&header->diff_info;

                    printf("Diff? %d, size=%d\n", diff_info[0], (diff_info[1] << 16) + (diff_info[2] << 8) + diff_info[3]);

                    if (diff_info[0] == 1) {
                        int old_size = (diff_info[1] << 16) + (diff_info[2] << 8) + diff_info[3];

                        int v;

                        // calculate sha256 hash for current fw & diff file (for debug purposes)
                        unsigned char sha_out_buff[32];
                        calculate_sha256(&at45, FOTA_DIFF_OLD_FW_PAGE * at45.get_read_size(), old_size, sha_out_buff);
                        debug("Current firmware hash: ");
                        print_sha256(sha_out_buff);

                        calculate_sha256(&at45, update_params.offset, update_params.size, sha_out_buff);
                        debug("Diff file hash: ");
                        print_sha256(sha_out_buff);

                        // so now use JANPatch
                        printf("source start=%llu size=%d\n", FOTA_DIFF_OLD_FW_PAGE * at45.get_read_size(), old_size);
                        BDFILE source(&at45, FOTA_DIFF_OLD_FW_PAGE * at45.get_read_size(), old_size);
                        printf("diff start=%lu size=%u\n", update_params.offset, update_params.size);
                        BDFILE diff(&at45, update_params.offset, update_params.size);
                        printf("target start=%llu\n", FOTA_DIFF_TARGET_PAGE * at45.get_read_size());
                        BDFILE target(&at45, FOTA_DIFF_TARGET_PAGE * at45.get_read_size(), 0);

                        v = apply_delta_update(&at45, 528, &source, &diff, &target);

                        if (v != MBED_DELTA_UPDATE_OK) {
                            debug("apply_delta_update failed %d\n", v);
                            return;
                        }

                        debug("Patched firmware length is %ld\n", target.ftell());

                        update_params.offset = FOTA_DIFF_TARGET_PAGE * at45.get_read_size();
                        update_params.size = target.ftell();
                    }


                    // Calculate the SHA256 hash of the file, and then verify whether the signature was signed with a trusted private key
                    unsigned char sha_out_buffer[32];
                    {
                        calculate_sha256(&at45, update_params.offset, update_params.size, sha_out_buffer);

                        debug("Patched firmware hash: ");
                        print_sha256(sha_out_buffer);

                        mbed_stats_heap_t heap_stats;
                        mbed_stats_heap_get(&heap_stats);
                        printf("Heap stats: Used %lu / %lu bytes\n", heap_stats.current_size, heap_stats.reserved_size);

                        // now check that the signature is correct...
                        {
                            printf("ECDSA signature is: ");
                            for (size_t ix = 0; ix < header->signature_length; ix++) {
                                printf("%02x ", header->signature[ix]);
                            }
                            printf("\n");

                            // ECDSA requires a large buffer, alloc on heap instead of stack
                            FragmentationEcdsaVerify* ecdsa = new FragmentationEcdsaVerify(UPDATE_CERT_PUBKEY, UPDATE_CERT_LENGTH);
                            bool valid = ecdsa->verify(sha_out_buffer, header->signature, header->signature_length);
                            if (!valid) {
                                debug("ECDSA verification of firmware failed\n");
                                free(header);
                                return;
                            }
                            else {
                                debug("ECDSA verification OK\n");
                            }

                            delete ecdsa;
                        }
                    }

                    free(header);

                    // Hash is matching, now populate the FOTA_INFO_PAGE with information about the update, so the bootloader can flash the update
                    if (1) {
                        update_params.update_pending = 1;
                        memcpy(update_params.sha256_hash, sha_out_buffer, sizeof(sha_out_buffer));
                        at45.program(&update_params, FOTA_INFO_PAGE * at45.get_read_size(), sizeof(UpdateParams_t));

                        debug("Stored the update parameters in flash on page 0x%x\n", FOTA_INFO_PAGE);
                    }
                    else {
                        debug("Has not stored update parameters in flash, override in RadioEvent.h\n");
                    }

                    // and now reboot the device...
                    printf("System going down for reset *NOW*!\n");
                    NVIC_SystemReset();
                }
            }
            break;
        }
    }

    void processMulticastMacCommand(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        switch (info->RxBuffer[0]) {
            case MC_GROUP_SETUP_REQ:
                {
                    class_c_group_params.McGroupIDHeader = info->RxBuffer[1];
                    class_c_group_params.McAddr = (info->RxBuffer[5] << 24 ) + ( info->RxBuffer[4] << 16 ) + ( info->RxBuffer[3] << 8 ) + info->RxBuffer[2];
                    memcpy(class_c_group_params.McKey, info->RxBuffer + 6, 16);

                    class_c_group_params.McCountMSB = (info->RxBuffer[23] << 8) + info->RxBuffer[22];
                    class_c_group_params.Validity = (info->RxBuffer[27] << 24 ) + ( info->RxBuffer[26] << 16 ) + ( info->RxBuffer[25] << 8 ) + info->RxBuffer[24];

                    printf("MC_GROUP_SETUP_REQ:\n");
                    printf("\tMcGroupIDHeader: %d\n", class_c_group_params.McGroupIDHeader);
                    printf("\tMcAddr: %s\n", mts::Text::bin2hexString((uint8_t*)&class_c_group_params.McAddr, 4).c_str());
                    printf("\tMcKey: %s\n", mts::Text::bin2hexString(class_c_group_params.McKey, 16).c_str());
                    printf("\tMcCountMSB: %d\n", class_c_group_params.McCountMSB);
                    printf("\tValidity: %li\n", class_c_group_params.Validity);

                    memcpy(class_c_credentials.DevAddr, &class_c_group_params.McAddr, 4);

                    const uint8_t nwk_input[16] = { 0x01, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
                    const uint8_t app_input[16] = { 0x02, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

                    AES_ECB_encrypt(nwk_input, class_c_group_params.McKey, class_c_credentials.NwkSKey, 16);
                    AES_ECB_encrypt(app_input, class_c_group_params.McKey, class_c_credentials.AppSKey, 16);

                    printf("ClassCCredentials:\n");
                    printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_c_credentials.DevAddr, 4).c_str());
                    printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.NwkSKey, 16).c_str());
                    printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.AppSKey, 16).c_str());

                    std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                    ack->push_back(MC_GROUP_SETUP_ANS);
                    ack->push_back(class_c_group_params.McGroupIDHeader);
                    send_msg_cb(200, ack);
                }
                break;

            case MC_CLASSC_SESSION_REQ:
                {
                    McClassCSessionParams_t class_c_session_params;
                    class_c_session_params.McGroupIDHeader = info->RxBuffer[1] & 0x03 ;
                    class_c_session_params.TimeOut = info->RxBuffer[4] >> 4;
                    class_c_session_params.TimeToStart = ((info->RxBuffer[4] & 0x0F ) << 16 ) + ( info->RxBuffer[3] << 8 ) +  info->RxBuffer[2];
                    class_c_session_params.UlFCountRef = info->RxBuffer[5];
                    class_c_session_params.DLFrequencyClassCSession = (info->RxBuffer[8] << 16 ) + ( info->RxBuffer[7] << 8 ) +  info->RxBuffer[6];
                    class_c_session_params.DataRateClassCSession  = info->RxBuffer[9];

                    printf("MC_CLASSC_SESSION_REQ:\n");
                    printf("\tMcGroupIDHeader: %d\n", class_c_session_params.McGroupIDHeader);
                    printf("\tTimeOut: %lu\n", class_c_session_params.TimeOut);
                    printf("\tTimeToStart: %li\n", class_c_session_params.TimeToStart);
                    printf("\tUlFCountRef: %d\n", class_c_session_params.UlFCountRef);
                    printf("\tDLFrequencyClassCSession: %li\n", class_c_session_params.DLFrequencyClassCSession);
                    printf("\tDataRateClassCSession: %d\n", class_c_session_params.DataRateClassCSession);

                    class_c_credentials.TxDataRate = class_c_session_params.DataRateClassCSession;
                    class_c_credentials.RxDataRate = class_c_session_params.DataRateClassCSession;

                    class_c_credentials.UplinkCounter = 0;
                    class_c_credentials.DownlinkCounter = 0;

                    class_c_credentials.Rx2Frequency = class_c_session_params.DLFrequencyClassCSession;

                    std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                    ack->push_back(MC_CLASSC_SESSION_ANS);

                    uint8_t status = class_c_session_params.McGroupIDHeader;

                    class_c_cancel_s = pow(2.0f, static_cast<int>(class_c_session_params.TimeOut));

                    bool switch_err = false;

                    // so time to start depends on the UlFCountRef...
                    UplinkEvent_t ulEvent;
                    bool foundUlEvent = false;

                    for (size_t ix = 0; ix < 10; ix++) {
                        if (class_c_session_params.UlFCountRef == uplinkEvents[ix].uplinkCounter) {
                            ulEvent = uplinkEvents[ix];
                            foundUlEvent = true;
                        }
                    }

                    if (!foundUlEvent) {
                        logError("UlFCountRef %d not found in uplinkEvents array", class_c_session_params.UlFCountRef);
                        status += 0b00000100;
                        switch_err = true;
                    }

                    ack->push_back(status);

                    // going to switch to class C in... ulEvent.time + params.TimeToStart
                    if (!switch_err) {
                        time_t switch_to_class_c_t = ulEvent.time + class_c_session_params.TimeToStart - time(NULL);
                        printf("Going to switch to class C in %li seconds\n", switch_to_class_c_t);

                        if (switch_to_class_c_t < 0) {
                            switch_to_class_c_t = 1;
                        }

                        // class_c_start_timeout.attach(event_queue->event(this, &RadioEvent::InvokeClassCSwitch), switch_to_class_c_t);
                        class_c_start_timeout.attach(callback(this, &RadioEvent::InvokeClassCSwitch), switch_to_class_c_t);

                        // timetostart in seconds
                        // @TODO: if this message fails to ack we should update this timing here otherwise the server is out of sync
                        ack->push_back(switch_to_class_c_t & 0xff);
                        ack->push_back(switch_to_class_c_t >> 8 & 0xff);
                        ack->push_back(switch_to_class_c_t >> 16 & 0xff);
                    }

                    send_msg_cb(200, ack);
                }

                break;

            default:
                printf("Got MAC command, but ignoring... %d\n", info->RxBuffer[0]);
                break;
        }
    }

    void InvokeClassCSwitch() {
        // no frag_session? abort
        if (frag_session == NULL) {
            logError("Refusing class C switch. No frag_session");
            return;
        }

        class_switch_cb('C');
    }

    void InvokeClassASwitch() {
        class_switch_cb('A');
    }

    void ClassCTimeout() {
        if (cls == 'C') {
            logInfo("Class C Timeout");

            InvokeClassASwitch();
        }
    }

    void HandleMacEvent(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {

        if (mts::MTSLog::getLogLevel() == mts::MTSLog::TRACE_LEVEL) {
            std::string msg = "OK";
            switch (info->Status) {
                case LORAMAC_EVENT_INFO_STATUS_ERROR:
                    msg = "ERROR";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT:
                    msg = "TX_TIMEOUT";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_RX_TIMEOUT:
                    msg = "RX_TIMEOUT";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_RX_ERROR:
                    msg = "RX_ERROR";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL:
                    msg = "JOIN_FAIL";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_DOWNLINK_FAIL:
                    msg = "DOWNLINK_FAIL";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL:
                    msg = "ADDRESS_FAIL";
                    break;
                case LORAMAC_EVENT_INFO_STATUS_MIC_FAIL:
                    msg = "MIC_FAIL";
                    break;
                default:
                    break;
            }
            logTrace("Event: %s", msg.c_str());

            logTrace("Flags Tx: %d Rx: %d RxData: %d RxSlot: %d LinkCheck: %d JoinAccept: %d",
                     flags->Bits.Tx, flags->Bits.Rx, flags->Bits.RxData, flags->Bits.RxSlot, flags->Bits.LinkCheck, flags->Bits.JoinAccept);
            logTrace("Info: Status: %d ACK: %d Retries: %d TxDR: %d RxPort: %d RxSize: %d RSSI: %d SNR: %d Energy: %d Margin: %d Gateways: %d",
                     info->Status, info->TxAckReceived, info->TxNbRetries, info->TxDatarate, info->RxPort, info->RxBufferSize,
                     info->RxRssi, info->RxSnr, info->Energy, info->DemodMargin, info->NbGateways);
        }

        if (flags->Bits.Rx) {

            logDebug("Rx %d bytes", info->RxBufferSize);
            if (info->RxBufferSize > 0) {
                if (cls == 'C') {
                    class_c_cancel_timeout.attach(callback(this, &RadioEvent::ClassCTimeout), class_c_cancel_s);
                }

                // Forward the data to the target MCU
                // logInfo("PacketRx port=%d, size=%d, rssi=%d, FPending=%d", info->RxPort, info->RxBufferSize, info->RxRssi, 0);
                // logInfo("Rx data: %s", mts::Text::bin2hexString(info->RxBuffer, info->RxBufferSize).c_str());

                // Process MAC events ourselves
                if (info->RxPort == 200) {
                    processMulticastMacCommand(flags, info);
                }
                else if (info->RxPort == 201) {
                    processFragmentationMacCommand(flags, info);
                }
            }
        }
    }


    Callback<void(uint8_t, std::vector<uint8_t>*)> send_msg_cb;
    Callback<void(char)> class_switch_cb;
    UplinkEvent_t uplinkEvents[10];

    McClassCSessionParams_t class_c_session_params;
    McGroupSetParams_t class_c_group_params;
    FTMPackageParams_t frag_params;

    LoRaWANCredentials_t class_a_credentials;
    LoRaWANCredentials_t class_c_credentials;

    Timeout class_c_start_timeout;
    Timeout class_c_cancel_timeout;
    uint32_t class_c_cancel_s;

    AT45BlockDevice at45;
    FragmentationSession* frag_session;
    FragmentationSessionOpts_t frag_opts;

    bool join_succeeded;
    char cls;
    bool has_received_frag_session;
};

#endif

