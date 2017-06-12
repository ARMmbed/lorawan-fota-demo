#include "dot_util.h"
#include "RadioEvent.h"

using namespace std;

// EventQueue queue(8 * EVENTS_EVENT_SIZE);
// Thread ev_thread(osPriorityNormal, 1 * 1024);

static uint8_t network_id[] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 };
static uint8_t network_key[] = { 0x72, 0xEF, 0x3F, 0xDE, 0x77, 0x53, 0x60, 0x69, 0x49, 0x25, 0x73, 0xF5, 0x7E, 0x6C, 0x9F, 0xE8 };
static uint8_t frequency_sub_band = 0;
static bool public_network = true;
static uint8_t ack = 1;

// deepsleep consumes slightly less current than sleep
// in sleep mode, IO state is maintained, RAM is retained, and application will resume after waking up
// in deepsleep mode, IOs float, RAM is lost, and application will start from beginning after waking up
// if deep_sleep == true, device will enter deepsleep mode
static bool deep_sleep = false;

mDot* dot = NULL;

Serial pc(USBTX, USBRX);

// // fwd declaration
void send_mac_msg(uint8_t port, vector<uint8_t>* data);
void class_switch(char cls);

// Custom event handler for automatically displaying RX data
RadioEvent radio_events(/*&queue, */ &send_mac_msg, &class_switch);

typedef struct {
    uint8_t port;
    bool is_mac;
    std::vector<uint8_t>* data;
} UplinkMessage;

vector<UplinkMessage*>* message_queue = new vector<UplinkMessage*>();
static bool in_class_c_mode = false;

void get_current_credentials(LoRaWANCredentials_t* creds) {
    memcpy(creds->DevAddr, &(dot->getNetworkAddress()[0]), 4);
    memcpy(creds->NwkSKey, &(dot->getNetworkSessionKey()[0]), 16);
    memcpy(creds->AppSKey, &(dot->getDataSessionKey()[0]), 16);

    creds->UplinkCounter = dot->getUpLinkCounter();
    creds->DownlinkCounter = dot->getDownLinkCounter();

    creds->TxDataRate = dot->getTxDataRate();
    creds->RxDataRate = dot->getRxDataRate();
}

void set_class_c_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassCCredentials();

    // logInfo("Switching to class C (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

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

    dot->setTxDataRate(credentials->TxDataRate);
    dot->setRxDataRate(credentials->RxDataRate);

    dot->setUpLinkCounter(credentials->UplinkCounter);
    dot->setDownLinkCounter(credentials->DownlinkCounter);

    update_network_link_check_config(0, 0);

    // fake MAC command to switch to DR5
    std::vector<uint8_t> mac_cmd;
    mac_cmd.push_back(0x05);
    mac_cmd.push_back(credentials->RxDataRate);
    // todo: set the actual freq instead hard code to 8695250 ((b[2] << 16) + (b[1] << 8) + b[0]).
    mac_cmd.push_back(0xd2);
    mac_cmd.push_back(0xad);
    mac_cmd.push_back(0x84);
    dot->injectMacCommand(mac_cmd);

    dot->setClass("C");

    printf("Switched to class C\n");

    radio_events.switchedToClassC();
}

void set_class_a_creds() {
    LoRaWANCredentials_t* credentials = radio_events.GetClassACredentials();

    // logInfo("Switching to class A (DevAddr=%s)", mts::Text::bin2hexString(credentials->DevAddr, 4).c_str());

    std::vector<uint8_t> address(credentials->DevAddr, credentials->DevAddr + 4);
    std::vector<uint8_t> nwkskey(credentials->NwkSKey, credentials->NwkSKey + 16);
    std::vector<uint8_t> appskey(credentials->AppSKey, credentials->AppSKey + 16);

    dot->setNetworkAddress(address);
    dot->setNetworkSessionKey(nwkskey);
    dot->setDataSessionKey(appskey);

    dot->setTxDataRate(credentials->TxDataRate);
    dot->setRxDataRate(credentials->RxDataRate);

    dot->setUpLinkCounter(credentials->UplinkCounter);
    dot->setDownLinkCounter(credentials->DownlinkCounter);

    update_network_link_check_config(3, 5);

    dot->setClass("A");

    printf("Switched to class A\n");
}

void send_packet(UplinkMessage* message) {
    if (message_queue->size() > 0 && !message->is_mac) {
        // logInfo("MAC messages in queue, dropping this packet");
        free(message->data);
        free(message);
    }
    else {
        // otherwise, add to queue
        message_queue->push_back(message);
    }

    // take the first item from the queue
    UplinkMessage* m = message_queue->at(0);

    // OK... soooooo we can only send in Class A
    bool switched_creds = false;
    if (in_class_c_mode) {
        if (m->port != 5) { // exception because TTN does not have class C. Fake it on port 5...
            // switch to class A credentials
            set_class_a_creds();
            switched_creds = true;
        }
    }

    dot->setAppPort(m->port);
    dot->setTxDataRate(mDot::SF_7);
    dot->setRxDataRate(mDot::SF_7);

    // printf("[INFO] Going to send a message. port=%d, dr=%s, data=", m->port, dot->getDateRateDetails(dot->getTxDataRate()).c_str());
    // for (size_t ix = 0; ix < m->data->size(); ix++) {
    //     printf("%02x ", m->data->at(ix));
    // }
    // printf("\n");

    uint32_t ret;

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
        // logInfo("Removing first item from the queue");

        // remove message from the queue
        message_queue->erase(message_queue->begin());
        free(m->data);
        free(m);
    }

    // update credentials with the new counter
    LoRaWANCredentials_t* creds = in_class_c_mode ?
        radio_events.GetClassCCredentials() :
        radio_events.GetClassACredentials();

    creds->UplinkCounter = dot->getUpLinkCounter();
    creds->DownlinkCounter = dot->getDownLinkCounter();

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
    logInfo("class_switch to %c", cls);

    // in class A mode? then back up credentials and counters...
    if (!in_class_c_mode) {
        LoRaWANCredentials_t creds;
        get_current_credentials(&creds);
        radio_events.UpdateClassACredentials(&creds);
    }

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

static void start_filling_up() {
    void* buffer;
    uint32_t allocated = 0;

    uint32_t size = 1024;

    while (true) {
        if (size == 64) break;

        buffer = malloc(size);
        if (buffer == NULL) {
            size = size / 2;
            continue;
        }
        printf("Allocated %d bytes\n", size);
        allocated += size;
        wait_ms(10);
    }
    printf("Allocated %d bytes before failed\n", allocated);
}


int main() {
    pc.baud(115200);

    // ev_thread.start(callback(&queue, &EventQueue::dispatch_forever));

    mts::MTSLog::setLogLevel(mts::MTSLog::TRACE_LEVEL);

    dot = mDot::getInstance();
    // start_filling_up();

    // attach the custom events handler
    dot->setEvents(&radio_events);

    if (!dot->getStandbyFlag()) {
        logInfo("mbed-os library version: %d", MBED_LIBRARY_VERSION);

        // start from a well-known state
        logInfo("defaulting Dot configuration");
        dot->resetConfig();
        dot->resetNetworkSession();

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

        dot->setAdr(false);

        dot->setJoinRx2DataRate(mDot::DR3); // sf9

        dot->setDisableDutyCycle(true);

        // save changes to configuration
        logInfo("saving configuration");
        if (!dot->saveConfig()) {
            logError("failed to save configuration");
        }

        // display configuration
        display_config();

        dot->setLogLevel(mts::MTSLog::ERROR_LEVEL);
    } else {
        // restore the saved session if the dot woke from deepsleep mode
        // useful to use with deepsleep because session info is otherwise lost when the dot enters deepsleep
        logInfo("restoring network session from NVM");
        dot->restoreNetworkSession();
    }

    while (true) {
        if (!in_class_c_mode) {

            uint16_t light;

            // join network if not joined
            if (!dot->getNetworkJoinStatus()) {
                join_network();

                LoRaWANCredentials_t creds;
                get_current_credentials(&creds);
                radio_events.OnClassAJoinSucceeded(&creds);
            }

            // get some dummy data and send it to the gateway
            light = rand() % 65000;

            vector<uint8_t>* tx_data = new vector<uint8_t>();
            if (!in_class_c_mode) {
                tx_data->push_back((light >> 8) & 0xFF);
                tx_data->push_back(light & 0xFF);
                // logInfo("light: %lu [0x%04X]", light, light);
            }

            UplinkMessage* dummy = new UplinkMessage();
            dummy->port = 5;
            dummy->data = tx_data;

            send_packet(dummy);
        }

        // if going into deepsleep mode, save the session so we don't need to join again after waking up
        // not necessary if going into sleep mode since RAM is retained
        if (deep_sleep) {
            // logInfo("saving network session to NVM");
            dot->saveNetworkSession();
        }

        uint32_t sleep_time = calculate_actual_sleep_time(3 + (rand() % 8));
        // logInfo("going to wait %d seconds for duty-cycle...", sleep_time);

        // ONLY ONE of the three functions below should be uncommented depending on the desired wakeup method
        //sleep_wake_rtc_only(deep_sleep);
        //sleep_wake_interrupt_only(deep_sleep);
        // sleep_wake_rtc_or_interrupt(10, deep_sleep); // automatically waits at least for next TX window according to duty cycle

        // @todo: in class A can go to deepsleep, in class C cannot
        if (in_class_c_mode) {
            wait(sleep_time);
            continue; // for now just send as fast as possible
        }
        else {
            wait(sleep_time); // @todo, wait for all frames to be processed before going to sleep. need a wakelock.
            // sleep_wake_rtc_or_interrupt(10, deep_sleep);
        }
    }

    return 0;
}
