#include "dot_util.h"
#include "RadioEvent.h"
 
#if ACTIVE_EXAMPLE == CLASS_C_EXAMPLE

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
// * either the network name and passphrase can be used or //
//     the network ID (8 bytes) and KEY (16 bytes)         //
/////////////////////////////////////////////////////////////
static std::string network_name = "MultiTech";
static std::string network_passphrase = "MultiTech";
static uint8_t network_id[] = { 0x6C, 0x4E, 0xEF, 0x66, 0xF4, 0x79, 0x86, 0xA6 };
static uint8_t network_key[] = { 0x1F, 0x33, 0xA1, 0x70, 0xA5, 0xF1, 0xFD, 0xA0, 0xAB, 0x69, 0x7A, 0xAE, 0x2B, 0x95, 0x91, 0x6B };
static uint8_t frequency_sub_band = 0;
static bool public_network = false;
static uint8_t ack = 1;

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

    logInfo("mbed-os library version: %d", MBED_LIBRARY_VERSION);

    // start from a well-known state
    logInfo("defaulting Dot configuration");
    dot->resetConfig();
    dot->resetNetworkSession();

        // make sure library logging is turned on
    dot->setLogLevel(mts::MTSLog::INFO_LEVEL);

    // attach the custom events handler
    dot->setEvents(&events);

    // update configuration if necessary
    if (dot->getJoinMode() != mDot::OTA) {
        logInfo("changing network join mode to OTA");
        if (dot->setJoinMode(mDot::OTA) != mDot::MDOT_OK) {
            logError("failed to set network join mode to OTA");
        }
    }
    // in OTA and AUTO_OTA join modes, the credentials can be passed to the library as a name and passphrase or an ID and KEY
    // only one method or the other should be used!
    // network ID = crc64(network name)
    // network KEY = cmac(network passphrase)
    update_ota_config_name_phrase(network_name, network_passphrase, frequency_sub_band, public_network, ack);
    //update_ota_config_id_key(network_id, network_key, frequency_sub_band, public_network, ack);

    // configure the Dot for class C operation
    // the Dot must also be configured on the gateway for class C
    // use the lora-query application to do this on a Conduit: http://www.multitech.net/developer/software/lora/lora-network-server/
    // to provision your Dot for class C operation with a 3rd party gateway, see the gateway or network provider documentation
    logInfo("changing network mode to class C");
    if (dot->setClass("C") != mDot::MDOT_OK) {
        logError("failed to set network mode to class C");
    }
    
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

        // the Dot can't sleep in class C mode
        // it must be waiting for data from the gateway
        // send data every 30s
        logInfo("waiting for 30s");
        wait(30);
    }
 
    return 0;
}

#endif

