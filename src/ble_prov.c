#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_prov.h"

void ble_store_config_init(void);

#define BLE_PROV_TAG "ble_prov"

/* Nordic UART Service (NUS) UUIDs — a plain RX/TX byte pipe that the phone
   writes lines to (RX) and receives lines from (TX, notify). */
static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t nus_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t nus_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

#define RX_LINE_MAX 512

static const char *TAG = BLE_PROV_TAG;

static ble_prov_rx_line_cb_t s_rx_cb = NULL;
static ble_prov_conn_cb_t s_conn_cb = NULL;

static char s_device_name[24];

static bool s_synced = false;
static bool s_want_adv = false;
static bool s_connected = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
/* The connection that has enabled notifications on the TX characteristic.
   Notifications must go to THIS handle, not the most recently connected one
   (the OS Bluetooth settings may hold a second connection that never
   subscribes). */
static uint16_t s_notify_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle = 0;

static uint8_t s_rx_line[RX_LINE_MAX];
static size_t s_rx_len = 0;

static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* ---------- RX line reassembly ---------- */

static void rx_append_byte(uint8_t byte)
{
    if (s_rx_len >= sizeof(s_rx_line) - 1)
    {
        s_rx_line[s_rx_len] = '\0';
        if (s_rx_cb)
        {
            s_rx_cb((const char *)s_rx_line, s_rx_len);
        }
        s_rx_len = 0;
    }

    if (byte == '\n')
    {
        s_rx_line[s_rx_len] = '\0';
        if (s_rx_cb)
        {
            s_rx_cb((const char *)s_rx_line, s_rx_len);
        }
        s_rx_len = 0;
    }
    else
    {
        s_rx_line[s_rx_len++] = byte;
    }
}

/* ---------- GATT ---------- */

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return 0;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    ESP_LOGI(TAG, "RX write received: op=%d len=%u", ctxt->op, len);
    if (len > 0)
    {
        uint8_t tmp[RX_LINE_MAX];
        uint16_t n = len > sizeof(tmp) ? sizeof(tmp) : len;
        os_mbuf_copydata(ctxt->om, 0, n, tmp);
        for (uint16_t i = 0; i < n; i++)
        {
            rx_append_byte(tmp[i]);
        }
    }
    return 0;
}

static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* TX is notify-only; reads return empty. */
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &nus_rx_uuid.u,
                .access_cb = rx_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &nus_tx_uuid.u,
                .access_cb = tx_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_CHR:
        if (ble_uuid_cmp(ctxt->chr.chr_def->uuid, &nus_tx_uuid.u) == 0)
        {
            s_tx_attr_handle = ctxt->chr.val_handle;
        }
        break;
    default:
        break;
    }
}

static int gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0)
    {
        return rc;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0)
    {
        return rc;
    }
    return 0;
}

/* ---------- Advertising ---------- */

static void start_advertising(void)
{
    if (!s_synced || !s_want_adv)
    {
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};

    /* Primary advertisement must stay within BLE's 31-byte limit:
       flags (3) + 128-bit service UUID (18) + short name "ESP32C3" (9)
       = 30 bytes. A name is included here so it is visible even to
       scanners that do not request a scan response. */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &nus_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    static const char short_name[] = "ESP32C3";
    fields.name = (uint8_t *)short_name;
    fields.name_len = sizeof(short_name) - 1;
    fields.name_is_complete = 0; /* shortened local name */

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    /* The full unique name (ESP32C3-<mac>) goes in the scan response, which
       most phones request during scanning. */
    const char *name = ble_svc_gap_device_name();
    if (name != NULL && name[0] != '\0')
    {
        rsp_fields.name = (uint8_t *)name;
        rsp_fields.name_len = strlen(name);
        rsp_fields.name_is_complete = 1;
    }
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising started as '%s'",
             name != NULL ? name : "(no name)");
}

static void stop_advertising(void)
{
    if (s_synced)
    {
        ble_gap_adv_stop();
    }
    ESP_LOGI(TAG, "Advertising stopped");
}

/* ---------- GAP ---------- */

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected");
            /* Keep advertising so the device stays findable for provisioning
               even while one client (e.g. the OS Bluetooth settings) holds a
               connection. */
            start_advertising();
        }
        else
        {
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (event->disconnect.conn.conn_handle == s_notify_conn_handle)
        {
            s_notify_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        if (s_conn_cb)
        {
            s_conn_cb(false);
        }
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event (attr=%u curr=%d conn=%u)",
                 event->subscribe.attr_handle, event->subscribe.cur_notify,
                 event->subscribe.conn_handle);
        if (event->subscribe.attr_handle == s_tx_attr_handle)
        {
            if (event->subscribe.cur_notify)
            {
                s_notify_conn_handle = event->subscribe.conn_handle;
                if (s_conn_cb)
                {
                    s_conn_cb(true);
                }
            }
            else if (event->subscribe.conn_handle == s_notify_conn_handle)
            {
                s_notify_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ---------- Host callbacks ---------- */

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Resetting state; reason=%d", reason);
}

static void on_sync(void)
{
    s_synced = true;

    /* Ensure the advertising name is ours; ble_svc_gap_init() may have reset
       it to the default when CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC is enabled. */
    if (s_device_name[0] != '\0')
    {
        ble_svc_gap_device_name_set(s_device_name);
    }

    uint8_t addr_type;
    int rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ble_hs_id_infer_auto failed: %d", rc);
    }
    start_advertising();
}

static void host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- Public API ---------- */

esp_err_t ble_prov_init(ble_prov_rx_line_cb_t rx_cb, ble_prov_conn_cb_t conn_cb)
{
    s_rx_cb = rx_cb;
    s_conn_cb = conn_cb;

    int rc = nimble_port_init();
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_name, sizeof(s_device_name), "ESP32C3-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    rc = gatt_svr_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "gatt_svr_init failed: %d", rc);
        return ESP_FAIL;
    }

    /* Set the name AFTER the GAP service is initialized: with
       CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC, ble_svc_gap_init() resets the device
       name to the default, overwriting anything set before it. */
    ble_svc_gap_device_name_set(s_device_name);

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE provisioning initialized, device name=%s", s_device_name);
    return ESP_OK;
}

esp_err_t ble_prov_start_adv(void)
{
    s_want_adv = true;
    start_advertising();
    return ESP_OK;
}

esp_err_t ble_prov_stop_adv(void)
{
    bool was_advertising = s_want_adv;
    s_want_adv = false;
    if (was_advertising)
    {
        stop_advertising();
    }
    return ESP_OK;
}

esp_err_t ble_prov_send(const char *line)
{
    if (s_notify_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t mtu = ble_att_mtu(s_notify_conn_handle);
    if (mtu < 23)
    {
        mtu = 23;
    }
    uint16_t chunk = mtu - 3;
    if (chunk > 200)
    {
        chunk = 200;
    }

    size_t len = strlen(line);
    size_t off = 0;
    int rc = 0;
    while (off < len)
    {
        size_t n = len - off;
        if (n > chunk)
        {
            n = chunk;
        }
        /* Retry a few times: during a burst the connection's TX queue can
           briefly be full; notify_custom then returns an error. */
        rc = BLE_HS_EUNKNOWN;
        for (int attempt = 0; attempt < 20; attempt++)
        {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(line + off, n);
            if (om == NULL)
            {
                rc = BLE_HS_ENOMEM;
                break;
            }
            rc = ble_gatts_notify_custom(s_notify_conn_handle, s_tx_attr_handle, om);
            if (rc == 0)
            {
                break;
            }
            ESP_LOGW(TAG, "notify retry %d (rc=%d)", attempt, rc);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (rc != 0)
        {
            ESP_LOGW(TAG, "notify failed: %d", rc);
            break;
        }
        off += n;
    }

    if (rc == 0)
    {
        struct os_mbuf *om = ble_hs_mbuf_from_flat("\n", 1);
        if (om)
        {
            ble_gatts_notify_custom(s_notify_conn_handle, s_tx_attr_handle, om);
        }
    }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

bool ble_prov_is_connected(void)
{
    return s_connected;
}
