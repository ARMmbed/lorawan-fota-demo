#ifndef __DOT_UTIL_H__
#define __DOT_UTIL_H__

#include "mbed.h"
#include "mDot.h"
#include "MTSLog.h"
#include "MTSText.h"

extern mDot* dot;

void display_config();

void update_ota_config_name_phrase(std::string network_name, std::string network_passphrase, uint8_t frequency_sub_band, bool public_network, uint8_t ack);

void update_ota_config_id_key(uint8_t *network_id, uint8_t *network_key, uint8_t frequency_sub_band, bool public_network, uint8_t ack);

void update_manual_config(uint8_t *network_address, uint8_t *network_session_key, uint8_t *data_session_key, uint8_t frequency_sub_band, bool public_network, uint8_t ack);

void update_peer_to_peer_config(uint8_t *network_address, uint8_t *network_session_key, uint8_t *data_session_key, uint32_t tx_frequency, uint8_t tx_datarate, uint8_t tx_power);

void update_network_link_check_config(uint8_t link_check_count, uint8_t link_check_threshold);

void join_network();

uint32_t calculate_actual_sleep_time(uint32_t delay_s);

void sleep_wake_rtc_only(uint32_t delay_s, bool deepsleep);

void sleep_wake_interrupt_only(bool deepsleep);

void sleep_wake_rtc_or_interrupt(uint32_t delay_s, bool deepsleep);

void sleep_save_io();

void sleep_configure_io();

void sleep_restore_io();

void send_data(std::vector<uint8_t> data);

#endif
