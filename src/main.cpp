#include "dot_util.h"
#include "RadioEvent.h"

using namespace std;

static uint8_t network_id[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xF0, 0x00, 0x3D, 0xAA };
static uint8_t network_key[] = { 0x72, 0xEF, 0x3F, 0xDE, 0x77, 0x53, 0x60, 0x69, 0x49, 0x25, 0x73, 0xF5, 0x7E, 0x6C, 0x9F, 0xE8 };
static uint8_t frequency_sub_band = 0;
static bool public_network = true;
static uint8_t ack = 0;
static bool disable_duty_cycle = false;

// deepsleep consumes slightly less current than sleep
// in sleep mode, IO state is maintained, RAM is retained, and application will resume after waking up
// in deepsleep mode, IOs float, RAM is lost, and application will start from beginning after waking up
// if deep_sleep == true, device will enter deepsleep mode
static bool deep_sleep = false;

mDot* dot = NULL;

Serial pc(USBTX, USBRX);

#if defined(TARGET_XDOT_L151CC)
I2C i2c(I2C_SDA, I2C_SCL);
ISL29011 lux(i2c);
#else
AnalogIn lux(XBEE_AD0);
#endif

void send_packet(std::vector<uint8_t> data) {
    uint32_t ret;

    ret = dot->send(data);
    if (ret != mDot::MDOT_OK) {
        logError("failed to send data to %s [%d][%s]", dot->getJoinMode() == mDot::PEER_TO_PEER ? "peer" : "gateway", ret, mDot::getReturnCodeString(ret).c_str());
    } else {
        logInfo("successfully sent data to %s", dot->getJoinMode() == mDot::PEER_TO_PEER ? "peer" : "gateway");
    }
}

int main() {
    // Custom event handler for automatically displaying RX data
    RadioEvent events;

    pc.baud(115200);

    mts::MTSLog::setLogLevel(mts::MTSLog::INFO_LEVEL);

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
        if (dot->getJoinMode() != mDot::OTA) {
            logInfo("changing network join mode to OTA");
            if (dot->setJoinMode(mDot::OTA) != mDot::MDOT_OK) {
                logError("failed to set network join mode to OTA");
            }
        }
        update_ota_config_id_key(network_id, network_key, frequency_sub_band, public_network, ack);

        // configure network link checks
        // network link checks are a good alternative to requiring the gateway to ACK every packet and should allow a single gateway to handle more Dots
        // check the link every count packets
        // declare the Dot disconnected after threshold failed link checks
        // for count = 3 and threshold = 5, the Dot will be considered disconnected after 15 missed packets in a row
        update_network_link_check_config(3, 5);

        if (disable_duty_cycle) {
            logInfo("disable duty cycle");
            if (dot->setDisableDutyCycle(true) != mDot::MDOT_OK) {
                logError("failed to disable duty cycle");
            }
        }

        logInfo("setting data rate to SF_7");
        if (dot->setTxDataRate(mDot::SF_7) != mDot::MDOT_OK) {
            logError("failed to set data rate");
        }

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

        // join network if not joined
        if (!dot->getNetworkJoinStatus()) {
            join_network();
        }

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
        send_packet(tx_data);

        // put the LSL29011 ambient light sensor into a low power state
        lux.setMode(ISL29011::PWR_DOWN);
#else
        // get some dummy data and send it to the gateway
        light = lux.read_u16();
        tx_data.push_back((light >> 8) & 0xFF);
        tx_data.push_back(light & 0xFF);
        logInfo("light: %lu [0x%04X]", light, light);
        send_packet(tx_data);
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
        sleep_wake_rtc_or_interrupt(1, deep_sleep); // automatically waits at least for next TX window according to duty cycle
    }

    return 0;
}
