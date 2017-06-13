#ifndef __RADIO_EVENT_H__
#define __RADIO_EVENT_H__

#include "mbed.h"
#include "dot_util.h"
#include "mDotEvent.h"
#include "ProtocolLayer.h"
#include <sys/time.h>
#include "base64.h"

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
} LoRaWANCredentials_t;

class RadioEvent : public mDotEvent
{

public:
    RadioEvent(
        // EventQueue* aevent_queue,
        Callback<void(uint8_t, std::vector<uint8_t>*)> asend_msg_cb,
        Callback<void(char)> aclass_switch_cb
    ) : /*event_queue(aevent_queue), */send_msg_cb(asend_msg_cb), class_switch_cb(aclass_switch_cb)
    {
        read_from_uart_thread = new Thread(osPriorityNormal, 1 * 1024);

        target = new RawSerial(PA_9, PA_10);
        target->baud(9600);

        join_succeeded = false;

        read_from_uart_thread->start(callback(this, &RadioEvent::uart_main));
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
        unsigned char data[] = { 0x09 };

        sendOverUart(data, sizeof(data)); // Class C mode
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

        unsigned char data[] = { 0x03 };
        sendOverUart(data, sizeof(data)); // JoinAccept message through to target MCU
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

    void uart_main() {
        unsigned char serial_buffer[128] = { 0 };
        unsigned char decode_buffer[181] = { 0 }; // 33% overhead + 10 extra bytes just to be sure

        uint8_t serial_ix = 0;

        while (1) {
            char c = target->getc();

            if (c == '\n') {
                // clear buffer... not sure if base64_decode does this
                memset(decode_buffer, 0, sizeof(decode_buffer));

                size_t decode_buffer_size;
                int res = mbedtls_base64_decode(decode_buffer, sizeof(decode_buffer), &decode_buffer_size,
                    const_cast<const unsigned char*>(serial_buffer), serial_ix);
                if (res != 0) {
                    printf("[ERR] Base64 decode failed... res=%d\n", res);

                    // clear buffer...
                    memset(serial_buffer, 0, sizeof(serial_buffer));
                    serial_ix = 0;

                    break;
                }

                // printf("Received from target MCU: %s\n", mts::Text::bin2hexString((uint8_t*)decode_buffer, decode_buffer_size).c_str());

                switch (decode_buffer[0]) {
                    case 0x01: // Datablock is complete
                    {
                        if (decode_buffer_size < 11) break;

                        // 1 byte FragIndex
                        // 8 bytes BlockHash

                        // Switch to class A...
                        InvokeClassASwitch();

                        // Then queue the DataBlockAuthReq
                        std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                        ack->push_back(DATA_BLOCK_AUTH_REQ);
                        ack->push_back(decode_buffer[2]); // FragIndex
                        ack->push_back(decode_buffer[3]); // BlockHash
                        ack->push_back(decode_buffer[4]); // BlockHash
                        ack->push_back(decode_buffer[5]); // BlockHash
                        ack->push_back(decode_buffer[6]); // BlockHash
                        ack->push_back(decode_buffer[7]); // BlockHash
                        ack->push_back(decode_buffer[8]); // BlockHash
                        ack->push_back(decode_buffer[9]); // BlockHash
                        ack->push_back(decode_buffer[10]); // BlockHash
                        send_msg_cb(201, ack);
                        break;
                    }

                    case 0x04: // Data is relayed from target MCU
                    {
                        if (decode_buffer_size < 2) break;

                        std::vector<uint8_t>* data = new std::vector<uint8_t>();

                        for (size_t ix = 2; ix < decode_buffer_size; ix++) {
                            data->push_back(decode_buffer[ix]);
                        }

                        send_msg_cb(decode_buffer[1], data);

                        break;
                    }

                    case 0x05: // Request join status
                    {
                        if (join_succeeded) {
                            unsigned char data[] = { 0x03 };
                            sendOverUart(data, sizeof(data)); // JoinAccept message through to target MCU

                            InvokeClassASwitch(); // just to be sure, switch back to class A again
                        }
                        break;
                    }

                    case 0x08: // Key sign response
                    {
                        if (decode_buffer_size < 33) break;

                        memcpy(class_c_credentials.NwkSKey, decode_buffer + 1, 16);
                        memcpy(class_c_credentials.AppSKey, decode_buffer + 17, 16);

                        printf("ClassCCredentials (AES Key Sign Response):\n");
                        printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_c_credentials.DevAddr, 4).c_str());
                        printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.NwkSKey, 16).c_str());
                        printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.AppSKey, 16).c_str());

                        break;
                    }
                }

                memset(serial_buffer, 0, sizeof(serial_buffer));
                serial_ix = 0;
            }
            else {
                serial_buffer[serial_ix] = c;
                serial_ix++;
            }
        }
    }

    void sendOverUart(unsigned char* buffer, size_t buffer_size) {
        for (size_t ix = 0; ix < buffer_size; ix++) {
            target->putc(buffer[ix]);
        }

        // size_t dest_buffer_size = (size_t)(static_cast<float>(buffer_size) * 1.35f) + 10; // 35% overhead just to be safe, and then some extra bytes just for cause
        // unsigned char* dest_buffer = (unsigned char*)calloc(dest_buffer_size, 1);
        // if (!dest_buffer) {
        //     printf("[ERR] malloc base64 destination buffer failed...\n");
        //     return;
        // }

        // // printf("send_over_uart... buffer_size=%u, dest_buffer_size=%u\n",
        // //     buffer_size, dest_buffer_size);

        // size_t encoded_size;

        // int res = mbedtls_base64_encode(dest_buffer, dest_buffer_size, &encoded_size,
        //     (const unsigned char*)buffer, buffer_size);
        // if (res != 0) {
        //     printf("[ERR] base64 encode failed... %d\n", res);
        //     free(dest_buffer);
        //     return;
        // }

        // // send to radio module
        // // printf("Sending to target MCU (%u bytes)\n", encoded_size);

        // for (size_t ix = 0; ix < encoded_size; ix++) {
        //     // printf("%c", dest_buffer[ix]);
        //     if (target) {
        //         target->putc(dest_buffer[ix]);
        //     }
        // }
        // // printf("\n");
        // if (target) {
        //     target->putc('\n');
        // }

        // free(dest_buffer);
    }

    void processFragmentationMacCommand(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        switch(info->RxBuffer[0]) {
            case FRAG_SESSION_SETUP_REQ:
            {
                if(info->RxBufferSize != FRAG_SESSION_SETUP_REQ_LENGTH) {
                    logError("Invalid FRAG_SESSION_SETUP_REQ command");
                    return;
                }

                // @todo: fragsession is bit 5 and 6 on byte 1
                // @todo: extract mc group from byte 1
                frag_params.FragSession = info->RxBuffer[1];
                frag_params.NbFrag = ( info->RxBuffer[3] << 8 ) + info->RxBuffer[2];
                frag_params.FragSize = info->RxBuffer[4] ;
                frag_params.Encoding = info->RxBuffer[5];
                frag_params.Padding = info->RxBuffer[6];
                frag_params.Redundancy = REDUNDANCYMAX-1;

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
            }
            break;
        }
    }

    void processMulticastMacCommand(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        switch (info->RxBuffer[0]) {
            case MC_GROUP_SETUP_REQ:
                {
                    // 0201a61e012600112233445566778899aabbccddeeff0000000003e8
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

                    // aes_128, cannot run on xdot because memory limitations
                    const unsigned char nwk_input[16] = { 0x01, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
                    const unsigned char app_input[16] = { 0x02, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

                    unsigned char data[1 + (16 * 3)];
                    data[0] = 0x08;
                    memcpy(data + 1, class_c_group_params.McKey, 16);
                    memcpy(data + 1 + 16, nwk_input, 16);
                    memcpy(data + 1 + 16 + 16, app_input, 16);

                    sendOverUart(data, sizeof(data)); // AES-128 request

                    // mbedtls_aes_context ctx;
                    // mbedtls_aes_setkey_enc(&ctx, class_c_group_params.McKey, 128);
                    // mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, nwk_input, class_c_credentials.NwkSKey);
                    // mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, app_input, class_c_credentials.AppSKey);

                    printf("ClassCCredentials:\n");
                    printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_c_credentials.DevAddr, 4).c_str());
                    // printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.NwkSKey, 16).c_str());
                    // printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_c_credentials.AppSKey, 16).c_str());

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
                    printf("\tTimeOut: %d\n", class_c_session_params.TimeOut);
                    printf("\tTimeToStart: %li\n", class_c_session_params.TimeToStart);
                    printf("\tUlFCountRef: %d\n", class_c_session_params.UlFCountRef);
                    printf("\tDLFrequencyClassCSession: %li\n", class_c_session_params.DLFrequencyClassCSession);
                    printf("\tDataRateClassCSession: %d\n", class_c_session_params.DataRateClassCSession);

                    class_c_credentials.TxDataRate = class_c_session_params.DataRateClassCSession;
                    class_c_credentials.RxDataRate = class_c_session_params.DataRateClassCSession;

                    class_c_credentials.UplinkCounter = 0;
                    class_c_credentials.DownlinkCounter = 0;

                    // @todo: set frequency

                    std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                    ack->push_back(MC_CLASSC_SESSION_ANS);

                    uint8_t status = class_c_session_params.McGroupIDHeader;

                    // @todo: switch back to class C after the timeout!

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

                        unsigned char data[4];
                        data[0] = 0x04;
                        data[1] = (switch_to_class_c_t >> 16) & 0xff;
                        data[2] = (switch_to_class_c_t >> 8) & 0xff;
                        data[3] = switch_to_class_c_t & 0xff;

                        sendOverUart(data, sizeof(data));

                        // class_c_timeout.attach(event_queue->event(this, &RadioEvent::InvokeClassCSwitch), switch_to_class_c_t);
                        class_c_timeout.attach(callback(this, &RadioEvent::InvokeClassCSwitch), switch_to_class_c_t);

                        // timetostart in seconds
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
        class_switch_cb('C');
    }

    void InvokeClassASwitch() {
        class_switch_cb('A');
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

                size_t data_size = 1 + 1 + 2 + 1 + info->RxBufferSize;
                unsigned char* data = (unsigned char*)malloc(data_size);

                data[0] = (0x01); // msg ID
                data[1] = (info->RxPort);
                data[2] = ((info->RxBufferSize << 8) & 0xff);
                data[3] = (info->RxBufferSize & 0xff);
                data[4] = (0 /* FPending 0 for now... Not in this info struct */);

                for (size_t ix = 0; ix < info->RxBufferSize; ix++) {
                    data[5 + ix] = info->RxBuffer[ix];
                }

                sendOverUart(data, data_size); // Fwd to target MCU

                free(data);
            }
        }
    }


    RawSerial* target;
    // EventQueue* event_queue;
    Callback<void(uint8_t, std::vector<uint8_t>*)> send_msg_cb;
    Callback<void(char)> class_switch_cb;
    UplinkEvent_t uplinkEvents[10];

    McClassCSessionParams_t class_c_session_params;
    McGroupSetParams_t class_c_group_params;
    FTMPackageParams_t frag_params;

    LoRaWANCredentials_t class_a_credentials;
    LoRaWANCredentials_t class_c_credentials;

    Timeout class_c_timeout;

    Thread* read_from_uart_thread;

    bool join_succeeded;
};

#endif

