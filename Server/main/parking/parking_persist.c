#include "esp_log.h"
#include "nvs.h"

#include "parking_persist.h"
#include "rfid_session.h"

#define NVS_NAMESPACE "parking"
#define NVS_KEY "store"

static const char *TAG = "PERSIST";

void parking_persist_load(void)
{
    static rfid_store_t saved; /* ~800 B: keep it off the main task stack */
    size_t len = sizeof(saved);
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return; /* first boot: nothing saved yet */
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY, &saved, &len);
    nvs_close(nvs);
    if (err == ESP_OK && len == sizeof(saved) && saved.version == RFID_STORE_VERSION)
    {
        rfid_session_restore(&saved);
        ESP_LOGI(TAG, "Restored RFID sessions and log (clock %s)",
                 saved.clock_synced ? "Unix time, waiting for a browser to correct downtime" : "not set yet");
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW(TAG, "Saved state ignored (%s, %u bytes)", esp_err_to_name(err), (unsigned)len);
}

/* Called after every scan, clock change and every few minutes; NVS spreads the writes. */
void parking_persist_save(void)
{
    rfid_store_t snapshot;
    rfid_session_snapshot(&snapshot);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK)
    {
        err = nvs_set_blob(nvs, NVS_KEY, &snapshot, sizeof(snapshot));
        if (err == ESP_OK)
            err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Saving state failed: %s", esp_err_to_name(err));
}
