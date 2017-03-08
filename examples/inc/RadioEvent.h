#ifndef __RADIO_EVENT_H__
#define __RADIO_EVENT_H__

#include "dot_util.h"
#include "mDotEvent.h"

class RadioEvent : public mDotEvent
{

public:
    RadioEvent() {
        target = new RawSerial(D1, D0);
        target->baud(115200);
    }

    virtual ~RadioEvent() {}

    virtual void JoinAccept(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
        logInfo("JoinAccept");

        target->putc(0x03); // msg ID
        target->putc('\n');
    }

    virtual void JoinFailed(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
        logInfo("JoinFailed");
    }


    virtual void PacketRx(uint8_t port, uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr, lora::DownlinkControl ctrl, uint8_t slot, uint8_t retries=0) {
        logInfo("PacketRx port=%d, size=%d, rssi=%d, FPending=%d", port, size, rssi, ctrl.Bits.FPending);

        target->putc(0x01); // msg ID
        target->putc(port);
        target->putc((size << 8) & 0xff);
        target->putc(size & 0xff);
        target->putc(ctrl.Bits.FPending);

        for (size_t ix = 0; ix < size; ix++) {
            target->putc(payload[ix]);
        }

        target->putc('\n');
    }

    virtual void RxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr, lora::DownlinkControl ctrl, uint8_t slot) {
        logInfo("RxDone size=%d, rssi=%d, FPending=%d", size, rssi, ctrl.Bits.FPending);

        target->putc(0x02); // msg ID
        target->putc((size << 8) & 0xff);
        target->putc(size & 0xff);
        target->putc(ctrl.Bits.FPending);
        target->putc('\n');
    }

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
                // print RX data as hexadecimal
                //printf("Rx data: %s\r\n", mts::Text::bin2hexString(info->RxBuffer, info->RxBufferSize).c_str());

                // print RX data as string
                std::string rx((const char*)info->RxBuffer, info->RxBufferSize);
                printf("Rx data: %s\r\n", rx.c_str());
            }
        }
    }

private:
    RawSerial* target;
};

#endif

