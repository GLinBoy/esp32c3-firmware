#ifndef BLE_PROV_H
#define BLE_PROV_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Called with a complete, NUL-terminated line received from the phone over the
   BLE UART RX characteristic (the trailing '\n' is stripped). */
typedef void (*ble_prov_rx_line_cb_t)(const char *line, size_t len);

/* Called when a phone connects to / disconnects from the provisioning service. */
typedef void (*ble_prov_conn_cb_t)(bool connected);

/*
 * Initializes the NimBLE host and registers the provisioning GATT service.
 * Must be called after nvs_flash_init(). The host task is spawned here;
 * advertising is NOT started until ble_prov_start_adv() is called.
 */
esp_err_t ble_prov_init(ble_prov_rx_line_cb_t rx_cb, ble_prov_conn_cb_t conn_cb);

/* Start advertising the provisioning service. Safe to call before the host is
   synced; advertising then starts as soon as the host is ready. */
esp_err_t ble_prov_start_adv(void);

/* Stop advertising (e.g. after Wi-Fi credentials have been received). */
esp_err_t ble_prov_stop_adv(void);

/* Send a NUL-terminated string to the connected phone as a notification.
   Long strings are split into MTU-sized chunks and terminated with '\n'.
   Returns ESP_ERR_INVALID_STATE if no phone is connected. */
esp_err_t ble_prov_send(const char *line);

bool ble_prov_is_connected(void);

#endif /* BLE_PROV_H */
