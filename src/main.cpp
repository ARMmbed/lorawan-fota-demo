#include "dot_util.h"
#include "RadioEvent.h"

using namespace std;

EventQueue queue;

static uint8_t network_id[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xF0, 0x00, 0x3D, 0xAA };
static uint8_t network_key[] = { 0x72, 0xEF, 0x3F, 0xDE, 0x77, 0x53, 0x60, 0x69, 0x49, 0x25, 0x73, 0xF5, 0x7E, 0x6C, 0x9F, 0xE8 };
static uint8_t frequency_sub_band = 0;
static bool public_network = true;
static uint8_t ack = 0;

// deepsleep consumes slightly less current than sleep
// in sleep mode, IO state is maintained, RAM is retained, and application will resume after waking up
// in deepsleep mode, IOs float, RAM is lost, and application will start from beginning after waking up
// if deep_sleep == true, device will enter deepsleep mode
static bool deep_sleep = false;

mDot* dot = NULL;

Serial pc(USBTX, USBRX);

// fwd declaration
void send_mac_msg(uint8_t port, vector<uint8_t>* data);
void class_switch(char cls);

// Custom event handler for automatically displaying RX data
RadioEvent radio_events(&queue, &send_mac_msg, &class_switch);

typedef struct {
    uint8_t port;
    bool is_mac;
    std::vector<uint8_t>* data;
} UplinkMessage;

#if defined(TARGET_XDOT_L151CC)
I2C i2c(I2C_SDA, I2C_SCL);
ISL29011 lux(i2c);
#else
AnalogIn lux(XBEE_AD0);
#endif

vector<UplinkMessage*>* message_queue = new vector<UplinkMessage*>();
static bool in_class_c_mode = false;

void set_class_c_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassCCredentials();

    logInfo("Switching to class C (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

    // @todo: this is weird, ah well...
    std::vector<uint8_t> address;
    address.push_back(credentials->DevAddr[3]);
    address.push_back(credentials->DevAddr[2]);
    address.push_back(credentials->DevAddr[1]);
    address.push_back(credentials->DevAddr[0]);
    std::vector<uint8_t> nwkskey(credentials->NwkSKey, credentials->NwkSKey + 16);
    std::vector<uint8_t> appskey(credentials->AppSKey, credentials->AppSKey + 16);

    dot->setNetworkAddress(address);
    dot->setNetworkSessionKey(nwkskey);
    dot->setDataSessionKey(appskey);
    // dot->setClass("C");

    logInfo("Switched to class C");
}

void set_class_a_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassACredentials();

    logInfo("Switching to class A (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

    std::vector<uint8_t> address(credentials->DevAddr, credentials->DevAddr + 4);
    std::vector<uint8_t> nwkskey(credentials->NwkSKey, credentials->NwkSKey + 16);
    std::vector<uint8_t> appskey(credentials->AppSKey, credentials->AppSKey + 16);

    dot->setNetworkAddress(address);
    dot->setNetworkSessionKey(nwkskey);
    dot->setDataSessionKey(appskey);
    dot->setClass("A");

    logInfo("Switched to class A");
}

void send_packet(UplinkMessage* message) {
    if (message_queue->size() > 0 && !message->is_mac) {
        logInfo("MAC messages in queue, dropping this packet");
        free(message->data);
        free(message);
    }
    else {
        // otherwise, add to queue
        message_queue->push_back(message);
    }

    // OK... soooooo we can only send in Class A
    bool switched_creds = false;
    if (in_class_c_mode) {
        if (message->port != 5) { // exception because TTN does not have class C. Fake it on port 5...
            // switch to class A credentials
            set_class_a_creds();
            switched_creds = true;
        }
    }

    // take the first item from the queue
    UplinkMessage* m = message_queue->at(0);

    printf("[INFO] Going to send a message. port=%d, data=", m->port);
    for (size_t ix = 0; ix < m->data->size(); ix++) {
        printf("%02x ", m->data->at(ix));
    }
    printf("\n");

    uint32_t ret;

    dot->setAppPort(m->port);

    radio_events.OnTx(dot->getUpLinkCounter() + 1);

    if (m->is_mac) {
        dot->setAck(true);
        ret = dot->send(*(m->data));
        dot->setAck(false);
    }
    else {
        dot->setAck(false);
        ret = dot->send(*(m->data));
    }

    if (ret != mDot::MDOT_OK) {
        logError("failed to send data to %s [%d][%s]", dot->getJoinMode() == mDot::PEER_TO_PEER ? "peer" : "gateway", ret, mDot::getReturnCodeString(ret).c_str());
    } else {
        logInfo("successfully sent data to %s", dot->getJoinMode() == mDot::PEER_TO_PEER ? "peer" : "gateway");
    }

    // Message was sent, or was not mac message? remove from queue
    if (ret == mDot::MDOT_OK || !m->is_mac) {
        logInfo("Removing first item from the queue");

        // remove message from the queue
        message_queue->erase(message_queue->begin());
        free(m->data);
        free(m);
    }

    // switch back
    if (switched_creds) {
        // switch to class A credentials
        set_class_c_creds();
    }
}

void send_mac_msg(uint8_t port, std::vector<uint8_t>* data) {
    UplinkMessage* m = new UplinkMessage();
    m->is_mac = true;
    m->data = data;
    m->port = port;

    message_queue->push_back(m);
}

void class_switch(char cls) {
    // @todo; make enum
    if (cls == 'C') {
        in_class_c_mode = true;
        set_class_c_creds();
    }
    else if (cls == 'A') {
        in_class_c_mode = false;
        set_class_a_creds();
    }
    else {
        logError("Cannot switch to class %c", cls);
    }
}

int main() {
    pc.baud(115200);

    Thread ev_thread(osPriorityNormal, 16 * 1024);
    ev_thread.start(callback(&queue, &EventQueue::dispatch_forever));

    mts::MTSLog::setLogLevel(mts::MTSLog::INFO_LEVEL);

    dot = mDot::getInstance();

    // attach the custom events handler
    dot->setEvents(&radio_events);

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

        logInfo("setting data rate to SF_7");
        if (dot->setTxDataRate(mDot::SF_7) != mDot::MDOT_OK) {
            logError("failed to set data rate");
        }

        dot->setAdr(true);

        dot->setDisableDutyCycle(true);

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

        // join network if not joined
        if (!dot->getNetworkJoinStatus()) {
            join_network();

            radio_events.OnClassAJoinSucceeded(dot->getNetworkAddress(), dot->getNetworkSessionKey(), dot->getDataSessionKey());
        }

        // get some dummy data and send it to the gateway
        light = lux.read_u16();

        vector<uint8_t>* tx_data = new vector<uint8_t>();
        if (!in_class_c_mode) {
            tx_data->push_back((light >> 8) & 0xFF);
            tx_data->push_back(light & 0xFF);
            logInfo("light: %lu [0x%04X]", light, light);
        }

        UplinkMessage* dummy = new UplinkMessage();
        dummy->port = 5;
        dummy->data = tx_data;

        send_packet(dummy);

        // if going into deepsleep mode, save the session so we don't need to join again after waking up
        // not necessary if going into sleep mode since RAM is retained
        if (deep_sleep) {
            logInfo("saving network session to NVM");
            dot->saveNetworkSession();
        }

        uint32_t sleep_time = calculate_actual_sleep_time(10);
        logInfo("going to wait %d seconds for duty-cycle...", sleep_time);

        // ONLY ONE of the three functions below should be uncommented depending on the desired wakeup method
        //sleep_wake_rtc_only(deep_sleep);
        //sleep_wake_interrupt_only(deep_sleep);
        // sleep_wake_rtc_or_interrupt(10, deep_sleep); // automatically waits at least for next TX window according to duty cycle

        // @todo: in class A can go to deepsleep, in class C cannot
        if (in_class_c_mode) {
            // wait(sleep_time); <-- this should go back when we have real class C on TTN
            continue; // for now just send as fast as possible
        }
        else {
            wait(sleep_time); // @todo, wait for all frames to be processed before going to sleep. need a wakelock.
            // sleep_wake_rtc_or_interrupt(10, deep_sleep);
        }
    }

    return 0;
}
