#ifndef _APPLICATION_CONFIG_H
#define _APPLICATION_CONFIG_H

#include "mDot.h"

#define CONFIG_NVM_OFFSET           0x50

typedef struct {

    uint32_t dispenses_left;
    uint32_t tx_interval;
    uint8_t  initialized;
    bool     low_battery;

} ApplicationConfigData;


class ApplicationConfig {
public:
    ApplicationConfig(mDot* dotInstance, BlockDevice* bdInstance) : dot(dotInstance), bd(bdInstance) {
        bd->read(&data, CONFIG_NVM_OFFSET * bd->get_read_size(), sizeof(ApplicationConfig));
        printf("initialized %d, left = %d\n", data.initialized, data.dispenses_left);

        if (data.initialized != 1 || data.dispenses_left > 1000) {
            data.dispenses_left = 1000;
            data.tx_interval = 60;
            data.initialized = 1;
        }
    }

    uint32_t get_dispenses_left() {
        return data.dispenses_left;
    }

    uint32_t get_tx_interval_s() {
        return data.tx_interval;
    }

    uint32_t get_battery_status() {
        return data.low_battery;
    }

    void decrease_dispenses_left() {
        data.dispenses_left--;
    }

    void reset_dispenses_left() {
        data.dispenses_left = 1000;
        persist();
    }

    void alert_low_battery() {
        data.low_battery = true;
        persist();
    }

    void alert_stable_battery() {
        data.low_battery = false;
        persist();
    }

    void set_tx_interval_s(uint32_t tx_interval_s) {
        data.tx_interval = tx_interval_s;
        persist();
    }

    void persist() {
        bd->program(&data, CONFIG_NVM_OFFSET * bd->get_read_size(), sizeof(ApplicationConfig));
    }

private:

    mDot* dot;
    BlockDevice* bd;
    ApplicationConfigData data;
};

#endif // _APPLICATION_CONFIG_H
