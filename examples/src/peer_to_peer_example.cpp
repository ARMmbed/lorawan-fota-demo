#include "dot_util.h"
#include "RadioEvent.h"
 
#if ACTIVE_EXAMPLE == PEER_TO_PEER_EXAMPLE

/////////////////////////////////////////////////////////////////////////////
// -------------------- DOT LIBRARY REQUIRED ------------------------------//
// * Because these example programs can be used for both mDot and xDot     //
//     devices, the LoRa stack is not included. The libmDot library should //
//     be imported if building for mDot devices. The libxDot library       //
//     should be imported if building for xDot devices.                    //
// * https://developer.mbed.org/teams/MultiTech/code/libmDot-dev-mbed5/    //
// * https://developer.mbed.org/teams/MultiTech/code/libmDot-mbed5/        //
// * https://developer.mbed.org/teams/MultiTech/code/libxDot-dev-mbed5/    //
// * https://developer.mbed.org/teams/MultiTech/code/libxDot-mbed5/        //
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////
// * these options must match between the two devices in   //
//   order for communication to be successful
/////////////////////////////////////////////////////////////
static uint8_t network_address[] = { 0x01, 0x02, 0x03, 0x04 };
static uint8_t network_session_key[] = { 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };
static uint8_t data_session_key[] = { 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };

mDot* dot = NULL;

Serial pc(USBTX, USBRX);

#if defined(TARGET_XDOT_L151CC)
I2C i2c(I2C_SDA, I2C_SCL);
ISL29011 lux(i2c);
#else
AnalogIn lux(XBEE_AD0);
#endif

int main() {
    // Custom event handler for automatically displaying RX data
    RadioEvent events;
    uint32_t tx_frequency;
    uint8_t tx_datarate;
    uint8_t tx_power;
    uint8_t frequency_band;

    pc.baud(115200);

    mts::MTSLog::setLogLevel(mts::MTSLog::TRACE_LEVEL);
    
    dot = mDot::getInstance();

    logInfo("mbed-os library version: %d", MBED_LIBRARY_VERSION);

    // start from a well-known state
    logInfo("defaulting Dot configuration");
    dot->resetConfig();

    // make sure library logging is turned on
    dot->setLogLevel(mts::MTSLog::INFO_LEVEL);

    // attach the custom events handler
    dot->setEvents(&events);

    // update configuration if necessary
    if (dot->getJoinMode() != mDot::PEER_TO_PEER) {
        logInfo("changing network join mode to PEER_TO_PEER");
        if (dot->setJoinMode(mDot::PEER_TO_PEER) != mDot::MDOT_OK) {
            logError("failed to set network join mode to PEER_TO_PEER");
        }
    }
    frequency_band = dot->getFrequencyBand();
    switch (frequency_band) {
        case mDot::FB_EU868:
            // 250kHz channels achieve higher throughput
            // DR6 : SF7 @ 250kHz
            // DR0 - DR5 (125kHz channels) available but much slower
            tx_frequency = 869850000;
            tx_datarate = mDot::DR6;
            // the 869850000 frequency is 100% duty cycle if the total power is under 7 dBm - tx power 4 + antenna gain 3 = 7
            tx_power = 4;
            break;
        case mDot::FB_US915:
        case mDot::FB_AU915:
        default:
            // 500kHz channels achieve highest throughput
            // DR8 : SF12 @ 500kHz
            // DR9 : SF11 @ 500kHz
            // DR10 : SF10 @ 500kHz
            // DR11 : SF9 @ 500kHz
            // DR12 : SF8 @ 500kHz
            // DR13 : SF7 @ 500kHz
            // DR0 - DR3 (125kHz channels) available but much slower
            tx_frequency = 915500000;
            tx_datarate = mDot::DR13;
            // 915 bands have no duty cycle restrictions, set tx power to max
            tx_power = 20;
            break;
    }
    // in PEER_TO_PEER mode there is no join request/response transaction
    // as long as both Dots are configured correctly, they should be able to communicate
    update_peer_to_peer_config(network_address, network_session_key, data_session_key, tx_frequency, tx_datarate, tx_power);

    // save changes to configuration
    logInfo("saving configuration");
    if (!dot->saveConfig()) {
        logError("failed to save configuration");
    }

    // display configuration
    display_config();

#if defined(TARGET_XDOT_L151CC)
    // configure the ISL29011 sensor on the xDot-DK for continuous ambient light sampling, 16 bit conversion, and maximum range
    lux.setMode(ISL29011::ALS_CONT);
    lux.setResolution(ISL29011::ADC_16BIT);
    lux.setRange(ISL29011::RNG_64000);
#endif

    while (true) {
        uint16_t light;
        std::vector<uint8_t> tx_data;

        // join network if not joined
        if (!dot->getNetworkJoinStatus()) {
            join_network();
        }

#if defined(TARGET_XDOT_L151CC)
        // get the latest light sample and send it to the gateway
        light = lux.getData();
        tx_data.push_back((light >> 8) & 0xFF);
        tx_data.push_back(light & 0xFF);
        logInfo("light: %lu [0x%04X]", light, light);
        send_data(tx_data);
#else 
        // get some dummy data and send it to the gateway
        light = lux.read_u16();
        tx_data.push_back((light >> 8) & 0xFF);
        tx_data.push_back(light & 0xFF);
        logInfo("light: %lu [0x%04X]", light, light);
        send_data(tx_data);
#endif

        // the Dot can't sleep in PEER_TO_PEER mode
        // it must be waiting for data from the other Dot
        // send data every 5 seconds
        logInfo("waiting for 5s");
        wait(5);
    }
 
    return 0;
}

#endif

