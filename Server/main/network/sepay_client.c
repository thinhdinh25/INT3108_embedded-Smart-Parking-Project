#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "../barrier/servo.h"
#include "../display/lcd_display.h"
#include "../parking/parking_persist.h"
#include "../parking/payment.h"
#include "../parking/rfid_session.h"
#include "sepay_client.h"

#define SEPAY_URL "https://my.sepay.vn/userapi/transactions/list?account_number=" CONFIG_PARKING_PAY_ACCOUNT
#define BODY_SIZE 6144 /* since_id keeps replies to the few newest transactions */

static const char *TAG = "SEPAY";
static char *body;

/* GET the transaction list with extra query parameters into `body`; true on HTTP 200. */
static bool sepay_get(const char *query)
{
    char url[192];
    snprintf(url, sizeof(url), SEPAY_URL "&%s", query);
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return false;
    esp_http_client_set_header(client, "Authorization", "Bearer " CONFIG_PARKING_SEPAY_TOKEN);

    int len = 0, status = 0;
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0)
    {
        status = esp_http_client_get_status_code(client);
        int n;
        while (len < BODY_SIZE - 1 && (n = esp_http_client_read(client, body + len, BODY_SIZE - 1 - len)) > 0)
            len += n;
    }
    body[len] = '\0';
    esp_http_client_cleanup(client);
    if (status != 200)
        ESP_LOGW(TAG, "SePay request failed (HTTP %d)", status);
    return status == 200;
}

static void sepay_task(void *arg)
{
    (void)arg;
    char watching[RFID_PAY_CODE_LEN + 1] = "";
    uint32_t newest_before = 0; /* transactions up to this id existed before the QR was shown */

    for (;;)
    {
        rfid_payment_t pay;
        rfid_session_get_payment(&pay);
        if (!pay.active)
        {
            watching[0] = '\0';
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (strcmp(watching, pay.code) != 0)
        {
            /* New payment: remember the newest existing transaction so only later ones count. */
            if (!sepay_get("limit=1"))
            {
                vTaskDelay(pdMS_TO_TICKS(CONFIG_PARKING_PAY_POLL_MS));
                continue;
            }
            if (!sepay_latest_id(body, &newest_before))
                newest_before = 0;
            strcpy(watching, pay.code);
            ESP_LOGI(TAG, "Waiting for %lu VND with note %s", (unsigned long)pay.fee_vnd, pay.code);
        }

        char query[48];
        snprintf(query, sizeof(query), "since_id=%lu&limit=20", (unsigned long)newest_before + 1);
        if (sepay_get(query) && sepay_has_payment(body, pay.code, pay.fee_vnd) &&
            rfid_session_payment_done(pay.code))
        {
            ESP_LOGI(TAG, "Payment %s received, opening exit barrier", pay.code);
            esp_err_t err = servo_open_for(SERVO_GATE_OUT, 3000);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to open exit barrier: %s", esp_err_to_name(err));
            parking_persist_save();
            lcd_display_notify();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_PARKING_PAY_POLL_MS));
    }
}

esp_err_t sepay_client_start(void)
{
    if (CONFIG_PARKING_PAY_ACCOUNT[0] == '\0')
    {
        ESP_LOGW(TAG, "QR payment off: no receiving account in menuconfig, exits open at once");
        return ESP_OK;
    }
    char probe[160];
    if (vietqr_payload(probe, sizeof(probe), CONFIG_PARKING_PAY_BANK_BIN, CONFIG_PARKING_PAY_ACCOUNT, 1000, "PKTEST00") == 0)
    {
        ESP_LOGE(TAG, "QR payment off: bank BIN must be 6 digits and the account letters/digits only");
        return ESP_OK;
    }
    /* The QR needs no network. Without Internet the operator confirms the transfer on the dashboard. */
    rfid_session_enable_payment(CONFIG_PARKING_PAY_TIMEOUT_S);
    if (CONFIG_PARKING_SEPAY_TOKEN[0] == '\0')
    {
        ESP_LOGW(TAG, "QR payment on, confirmed by the operator (no SePay token for auto-check)");
        return ESP_OK;
    }
    body = malloc(BODY_SIZE);
    if (!body)
        return ESP_ERR_NO_MEM;
    /* TLS needs a deep stack for the handshake. */
    if (xTaskCreate(sepay_task, "sepay", 8192, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "QR payment on, auto-checked with SePay (timeout %d s)", CONFIG_PARKING_PAY_TIMEOUT_S);
    return ESP_OK;
}
