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
} LoRaWANCredentials_t;

class RadioEvent : public mDotEvent
{

public:
    RadioEvent(Callback<void(uint8_t, std::vector<uint8_t>*)> asend_msg_cb) : send_msg_cb(asend_msg_cb) {
        target = new RawSerial(D1, D0);
        target->baud(115200);
    }

    virtual ~RadioEvent() {}

    /*!
     * MAC layer event callback prototype.
     *
     * \param [IN] flags Bit field indicating the MAC events occurred
     * \param [IN] info  Details about MAC events occurred
     */
    virtual void MacEvent(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {

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
                logInfo("PacketRx port=%d, size=%d, rssi=%d, FPending=%d", info->RxPort, info->RxBufferSize, info->RxRssi, 0);
                logInfo("Rx data: %s", mts::Text::bin2hexString(info->RxBuffer, info->RxBufferSize).c_str());

                target->putc(0x01); // msg ID
                target->putc(info->RxPort);
                target->putc((info->RxBufferSize << 8) & 0xff);
                target->putc(info->RxBufferSize & 0xff);
                target->putc(0 /* FPending 0 for now... Not in this info struct */);

                for (size_t ix = 0; ix < info->RxBufferSize; ix++) {
                    target->putc(info->RxBuffer[ix]);
                }

                // Process MAC events ourselves
                if (info->RxPort == 200) {
                    processMulticastMacCommand(flags, info);
                }

            }
        }
    }

    void OnTx(uint32_t uplinkCounter) {
        // move all one up
        uplinkEvents[0] = uplinkEvents[1];
        uplinkEvents[1] = uplinkEvents[2];

        uplinkEvents[2].uplinkCounter = uplinkCounter;
        uplinkEvents[2].time = time(NULL);

        logInfo("UplinkEvents are:");
        printf("\t[0] %li %li\n", uplinkEvents[0].uplinkCounter, uplinkEvents[0].time);
        printf("\t[1] %li %li\n", uplinkEvents[1].uplinkCounter, uplinkEvents[1].time);
        printf("\t[2] %li %li\n", uplinkEvents[2].uplinkCounter, uplinkEvents[2].time);
    }

    void OnClassAJoinSucceeded(vector<uint8_t> devAddr, vector<uint8_t> networkSessionKey, vector<uint8_t> appSessionKey) {
        memcpy(class_a_credentials.DevAddr, &devAddr[0], 4);
        memcpy(class_a_credentials.NwkSKey, &networkSessionKey[0], 16);
        memcpy(class_a_credentials.AppSKey, &appSessionKey[0], 16);

        logInfo("ClassAJoinSucceeded");
        printf("\tDevAddr: %s\n", mts::Text::bin2hexString(class_a_credentials.DevAddr, 4).c_str());
        printf("\tNwkSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.NwkSKey, 16).c_str());
        printf("\tAppSKey: %s\n", mts::Text::bin2hexString(class_a_credentials.AppSKey, 16).c_str());

        target->putc(0x03); // JoinAccept message through to target MCU
    }

private:
    void processMulticastMacCommand(LoRaMacEventFlags* flags, LoRaMacEventInfo* info) {
        switch (info->RxBuffer[0]) {
            case MC_GROUP_SETUP_REQ:
                {
                    // 020126011ea600112233445566778899aabbccddeeff0000000003e8
                    class_c_group_params.McGroupIDHeader = info->RxBuffer[1];
                    class_c_group_params.McAddr = (info->RxBuffer[2] << 24 ) + ( info->RxBuffer[3] << 16 ) + ( info->RxBuffer[4] << 8 ) + info->RxBuffer[5];
                    memcpy(class_c_group_params.McKey, info->RxBuffer + 6, 16);

                    class_c_group_params.McCountMSB = (info->RxBuffer[22] << 8) + info->RxBuffer[23];
                    class_c_group_params.Validity = (info->RxBuffer[24] << 24 ) + ( info->RxBuffer[25] << 16 ) + ( info->RxBuffer[26] << 8 ) + info->RxBuffer[27];

                    logInfo("MC_GROUP_SETUP_REQ:\n");
                    printf("\tMcGroupIDHeader: %d\n", class_c_group_params.McGroupIDHeader);
                    printf("\tMcAddr: %08x\n", class_c_group_params.McAddr);
                    printf("\tMcKey: %s\n", mts::Text::bin2hexString(class_c_group_params.McKey, 16).c_str());
                    printf("\tMcCountMSB: %d\n", class_c_group_params.McCountMSB);
                    printf("\tValidity: %li\n", class_c_group_params.Validity);

                    memcpy(class_c_credentials.DevAddr, &class_c_group_params.McAddr, 4);

                    // aes_128
                    const unsigned char nwk_input[16] = { 0x01, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
                    const unsigned char app_input[16] = { 0x02, class_c_credentials.DevAddr[0], class_c_credentials.DevAddr[1], class_c_credentials.DevAddr[2], class_c_credentials.DevAddr[3], 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

                    printf("nwk_input is %s\n", mts::Text::bin2hexString(nwk_input, 16).c_str());
                    printf("app_input is %s\n", mts::Text::bin2hexString(app_input, 16).c_str());

                    unsigned char output[16];

                    mbedtls_aes_context aes_ctx;
                    mbedtls_aes_setkey_enc(&aes_ctx, class_c_group_params.McKey, 128);
                    printf("setKey happened\n");
                    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, nwk_input, output);
                    // mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, app_input, class_c_credentials.AppSKey);

                    logInfo("ClassCCredentials");
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
                    class_c_session_params.TimeOut = info->RxBuffer[2] >> 4;
                    class_c_session_params.TimeToStart = ((info->RxBuffer[2] & 0x0F ) << 16 ) + ( info->RxBuffer[3] << 8 ) +  info->RxBuffer[4];
                    class_c_session_params.UlFCountRef = info->RxBuffer[5];
                    class_c_session_params.DLFrequencyClassCSession = (info->RxBuffer[6] << 16 ) + ( info->RxBuffer[7] << 8 ) +  info->RxBuffer[8];
                    class_c_session_params.DataRateClassCSession  = info->RxBuffer[9];

                    printf("MC_CLASSC_SESSION_REQ came in...:\n");
                    printf("\tMcGroupIDHeader: %d\n", class_c_session_params.McGroupIDHeader);
                    printf("\tTimeOut: %d\n", class_c_session_params.TimeOut);
                    printf("\tTimeToStart: %li\n", class_c_session_params.TimeToStart);
                    printf("\tUlFCountRef: %d\n", class_c_session_params.UlFCountRef);
                    printf("\tDLFrequencyClassCSession: %li\n", class_c_session_params.DLFrequencyClassCSession);
                    printf("\tDataRateClassCSession: %d\n", class_c_session_params.DataRateClassCSession);

                    std::vector<uint8_t>* ack = new std::vector<uint8_t>();
                    ack->push_back(MC_CLASSC_SESSION_ANS);

                    uint8_t status = class_c_session_params.McGroupIDHeader;

                    // so time to start depends on the UlFCountRef...
                    UplinkEvent_t ulEvent;
                    if (class_c_session_params.UlFCountRef == uplinkEvents[0].uplinkCounter) {
                        ulEvent = uplinkEvents[0];
                    }
                    else if (class_c_session_params.UlFCountRef == uplinkEvents[1].uplinkCounter) {
                        ulEvent = uplinkEvents[1];
                    }
                    else if (class_c_session_params.UlFCountRef == uplinkEvents[0].uplinkCounter) {
                        ulEvent = uplinkEvents[2];
                    }
                    else {
                        logError("UlFCountRef not found in uplinkEvents array");
                        status += 0b100;
                    }

                    // going to switch to class C in... ulEvent.time + params.TimeToStart
                    time_t switch_to_class_c_t = ulEvent.time + class_c_session_params.TimeToStart - time(NULL);
                    logInfo("Going to switch to class C in %d seconds\n", switch_to_class_c_t);

                    ack->push_back(status);

                    // timetostart in seconds
                    ack->push_back(switch_to_class_c_t >> 16 & 0xff);
                    ack->push_back(switch_to_class_c_t >> 8 & 0xff);
                    ack->push_back(switch_to_class_c_t & 0xff);

                    send_msg_cb(200, ack);
                }

                break;

            default:
                printf("Got MAC command, but ignoring... %d\n", info->RxBuffer[0]);
                break;
        }
    }


    RawSerial* target;
    Callback<void(uint8_t, std::vector<uint8_t>*)> send_msg_cb;
    UplinkEvent_t uplinkEvents[3];

    McClassCSessionParams_t class_c_session_params;
    McGroupSetParams_t class_c_group_params;

    LoRaWANCredentials_t class_a_credentials;
    LoRaWANCredentials_t class_c_credentials;
};

#endif

