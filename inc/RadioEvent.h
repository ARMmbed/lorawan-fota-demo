#ifndef __RADIO_EVENT_H__
#define __RADIO_EVENT_H__

#include "dot_util.h"
#include "mDotEvent.h"
#include "ProtocolLayer.h"
#include "aes.h"
#include <sys/time.h>

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
        EventQueue* aevent_queue,
        Callback<void(uint8_t, std::vector<uint8_t>*)> asend_msg_cb,
        Callback<void(char)> aclass_switch_cb
    ) : event_queue(aevent_queue), send_msg_cb(asend_msg_cb), class_switch_cb(aclass_switch_cb)
    {
        target = new RawSerial(D1, D0);
        target->baud(9600);

        read_from_uart_thread.start(callback(this, &RadioEvent::uart_main));
    }

    virtual ~RadioEvent() {}

    /*!
     * MAC layer event callback prototype.
     *
     * \param [IN] flags Bit field indicating the MAC events occurred
     * \param [IN] info  Details about MAC events occurred
     */
    virtual void MacEvent(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        // Process on the events thread which has bigger stack
        event_queue->call(callback(this, &RadioEvent::HandleMacEvent), flags, info);
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

    void OnClassAJoinSucceeded(LoRaWANCredentials_t* credentials) {
        UpdateClassACredentials(credentials);

        printf("ClassAJoinSucceeded:\n");
        printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_a_credentials.DevAddr, 4).c_str());
        printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.NwkSKey, 16).c_str());
        printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.AppSKey, 16).c_str());
        printf("\tUplinkCounter: %li\n", class_a_credentials.UplinkCounter);
        printf("\tDownlinkCounter: %li\n", class_a_credentials.DownlinkCounter);
        printf("\tTxDataRate: %d\n", class_a_credentials.TxDataRate);
        printf("\tRxDataRate: %d\n", class_a_credentials.RxDataRate);

        char* data = (char*)malloc(1);
        data[0] = 0x03;

        sendOverUart(data, 1); // JoinAccept message through to target MCU
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
        char serial_buffer[128] = { 0 };
        uint8_t serial_ix = 0;

        while (1) {
            char c = target->getc();

            if (c == '\n') {
                printf("Received from target MCU: %s\n", mts::Text::bin2hexString((uint8_t*)serial_buffer, serial_ix).c_str());

                switch (serial_buffer[0]) {
                    case 0x01: // Datablock is complete
                    // 1 byte FragIndex
                    // 8 bytes BlockHash

                    // Switch to class A...
                    InvokeClassASwitch();

                    // Then queue the DataBlockAuthReq
                    std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                    ack->push_back(DATA_BLOCK_AUTH_REQ);
                    ack->push_back(serial_buffer[2]); // FragIndex
                    ack->push_back(serial_buffer[3]); // BlockHash
                    ack->push_back(serial_buffer[4]); // BlockHash
                    ack->push_back(serial_buffer[5]); // BlockHash
                    ack->push_back(serial_buffer[6]); // BlockHash
                    ack->push_back(serial_buffer[7]); // BlockHash
                    ack->push_back(serial_buffer[8]); // BlockHash
                    ack->push_back(serial_buffer[9]); // BlockHash
                    ack->push_back(serial_buffer[10]); // BlockHash
                    send_msg_cb(201, ack);
                    break;
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

    void sendOverUart(char* data, size_t size) {
        printf("Sending to target MCU (%li bytes): [ ", size);
        for (size_t ix = 0; ix < size; ix++) {
            printf("%02x ", data[ix]);
        }
        printf("]\n\n");

        for (size_t ix = 0; ix < size; ix++) {
            target->putc(data[ix]);
        }
        free(data);
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

                    // aes_128
                    const unsigned char nwk_input[16] = { 0x01, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
                    const unsigned char app_input[16] = { 0x02, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

                    mbedtls_aes_context ctx;
                    mbedtls_aes_setkey_enc(&ctx, class_c_group_params.McKey, 128);
                    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, nwk_input, class_c_credentials.NwkSKey);
                    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, app_input, class_c_credentials.AppSKey);

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
                        printf("Going to switch to class C in %d seconds\n", switch_to_class_c_t);

                        if (switch_to_class_c_t < 0) {
                            switch_to_class_c_t = 1;
                        }

                        class_c_timeout.attach(event_queue->event(this, &RadioEvent::InvokeClassCSwitch), switch_to_class_c_t);

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
                char* data = (char*)malloc(data_size);

                data[0] = (0x01); // msg ID
                data[1] = (info->RxPort);
                data[2] = ((info->RxBufferSize << 8) & 0xff);
                data[3] = (info->RxBufferSize & 0xff);
                data[4] = (0 /* FPending 0 for now... Not in this info struct */);

                for (size_t ix = 0; ix < info->RxBufferSize; ix++) {
                    data[5 + ix] = info->RxBuffer[ix];
                }

                sendOverUart(data, data_size); // Fwd to target MCU
            }
        }
    }


    RawSerial* target;
    EventQueue* event_queue;
    Callback<void(uint8_t, std::vector<uint8_t>*)> send_msg_cb;
    Callback<void(char)> class_switch_cb;
    UplinkEvent_t uplinkEvents[10];

    McClassCSessionParams_t class_c_session_params;
    McGroupSetParams_t class_c_group_params;
    FTMPackageParams_t frag_params;

    LoRaWANCredentials_t class_a_credentials;
    LoRaWANCredentials_t class_c_credentials;

    Timeout class_c_timeout;

    Thread read_from_uart_thread;
};

#endif

