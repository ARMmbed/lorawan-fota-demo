#include "dot_util.h"
#include "RadioEvent.h"
 
#if ACTIVE_EXAMPLE == MANUAL_EXAMPLE

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
// * these options must match the settings on your gateway //
// * edit their values to match your configuration         //
// * frequency sub band is only relevant for the 915 bands //
/////////////////////////////////////////////////////////////
static uint8_t network_address[] = { 0x01, 0x02, 0x03, 0x04 };
static uint8_t network_session_key[] = { 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };
static uint8_t data_session_key[] = { 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };
static uint8_t frequency_sub_band = 6;
static bool public_network = false;
static uint8_t ack = 1;

// deepsleep consumes slightly less current than sleep
// in sleep mode, IO state is maintained, RAM is retained, and application will resume after waking up
// in deepsleep mode, IOs float, RAM is lost, and application will start from beginning after waking up
// if deep_sleep == true, device will enter deepsleep mode
static bool deep_sleep = true;

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

    pc.baud(115200);

    mts::MTSLog::setLogLevel(mts::MTSLog::TRACE_LEVEL);
    
    dot = mDot::getInstance();

    // attach the custom events handler
    dot->setEvents(&events);

    if (!dot->getStandbyFlag()) {
        logInfo("mbed-os library version: %d", MBED_LIBRARY_VERSION);

        // start from a well-known state
        logInfo("defaulting Dot configuration");
        dot->resetConfig();
        dot->resetNetworkSession();

        // make sure library logging is turned on
        dot->setLogLevel(mts::MTSLog::INFO_LEVEL);

        // update configuration if necessary
        if (dot->getJoinMode() != mDot::MANUAL) {
            logInfo("changing network join mode to MANUAL");
            if (dot->setJoinMode(mDot::MANUAL) != mDot::MDOT_OK) {
                logError("failed to set network join mode to MANUAL");
            }
        }
        // in MANUAL join mode there is no join request/response transaction
        // as long as the Dot is configured correctly and provisioned correctly on the gateway, it should be able to communicate
        // network address - 4 bytes (00000001 - FFFFFFFE)
        // network session key - 16 bytes
        // data session key - 16 bytes
        // to provision your Dot with a Conduit gateway, follow the following steps
        //   * ssh into the Conduit
        //   * provision the Dot using the lora-query application: http://www.multitech.net/developer/software/lora/lora-network-server/
        //      lora-query -a 01020304 A 0102030401020304 <your Dot's device ID> 01020304010203040102030401020304 01020304010203040102030401020304
        //   * if you change the network address, network session key, or data session key, make sure you update them on the gateway
        // to provision your Dot with a 3rd party gateway, see the gateway or network provider documentation
        update_manual_config(network_address, network_session_key, data_session_key, frequency_sub_band, public_network, ack);

        // save changes to configuration
        logInfo("saving configuration");
        if (!dot->saveConfig()) {
            logError("failed to save configuration");
        }

        // display configuration
        display_config();
    } else {
        // restore the saved session if the dot woke from deepsleep mode
        // useful to use with deepsleep because session info is otherwise lost when the dot enters deepsleep
        logInfo("restoring network session from NVM");
        dot->restoreNetworkSession();
    }
    

    while (true) {
        uint16_t light;
        std::vector<uint8_t> tx_data;

#if defined(TARGET_XDOT_L151CC)
        // configure the ISL29011 sensor on the xDot-DK for continuous ambient light sampling, 16 bit conversion, and maximum range
        lux.setMode(ISL29011::ALS_CONT);
        lux.setResolution(ISL29011::ADC_16BIT);
        lux.setRange(ISL29011::RNG_64000);

        // get the latest light sample and send it to the gateway
        light = lux.getData();
        tx_data.push_back((light >> 8) & 0xFF);
        tx_data.push_back(light & 0xFF);
        logInfo("light: %lu [0x%04X]", light, light);
        send_data(tx_data);

        // put the LSL29011 ambient light sensor into a low power state
        lux.setMode(ISL29011::PWR_DOWN);
#else 
        // get some dummy data and send it to the gateway
        light = lux.read_u16();
        tx_data.push_back((light >> 8) & 0xFF);
        tx_data.push_back(light & 0xFF);
        logInfo("light: %lu [0x%04X]", light, light);
        send_data(tx_data);
#endif

        // if going into deepsleep mode, save the session so we don't need to join again after waking up
        // not necessary if going into sleep mode since RAM is retained
        if (deep_sleep) {
            logInfo("saving network session to NVM");
            dot->saveNetworkSession();
        }

        // ONLY ONE of the three functions below should be uncommented depending on the desired wakeup method
        //sleep_wake_rtc_only(deep_sleep);
        //sleep_wake_interrupt_only(deep_sleep);
        sleep_wake_rtc_or_interrupt(deep_sleep);
    }
 
    return 0;
}

#endif
