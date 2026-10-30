#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "http_server.h"
#include "sdkconfig.h"
#include "../parking/parking_persist.h"
#include "../parking/parking_state.h"
#include "../parking/rfid_session.h"
#include "../../barrier/servo.h"

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");
extern const unsigned char style_css_start[] asm("_binary_style_css_start");
extern const unsigned char style_css_end[] asm("_binary_style_css_end");

static const char *TAG = "HTTP_SERVER";

/* Every POST must carry the admin PIN in X-Parking-Key (the dashboard asks for it once). */
static bool authorized(httpd_req_t *req)
{
    char given[33] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Parking-Key", given, sizeof(given)) != ESP_OK)
        return false;
    const char *expected = CONFIG_PARKING_ADMIN_PIN;
    size_t len = strlen(expected);
    unsigned diff = strlen(given) ^ len;
    for (size_t i = 0; i < len && i < sizeof(given); ++i)
        diff |= (unsigned char)given[i] ^ (unsigned char)expected[i]; /* no early exit */
    return diff == 0;
}

static esp_err_t send_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_sendstr(req, "PIN required");
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)style_css_start,
                           style_css_end - style_css_start);
}

static esp_err_t parking_state_get_handler(httpd_req_t *req)
{
    parking_state_t state = {0};
    int free_count = 0;
    parking_state_get(&state, &free_count);
    char response[192];
    int temperature_tenths = (int)(state.temperature_c * 10.0f + (state.temperature_c >= 0.0f ? 0.5f : -0.5f));
    int water_tenths = (int)(state.water_mm * 10.0f + 0.5f);
    int length = snprintf(response, sizeof(response),
                          "{\"received\":%s,\"temperature_received\":%s,\"slots\":[%d,%d,%d,%d,%d],\"free\":%d,\"total\":5,\"temperature_c\":%s%d.%d,\"water_mm\":%d.%d,\"fire\":%s}",
                          state.received ? "true" : "false",
                          state.temperature_received ? "true" : "false", state.slots[0],
                          state.slots[1],
                          state.slots[2], state.slots[3], state.slots[4], free_count,
                          temperature_tenths < 0 ? "-" : "", abs(temperature_tenths) / 10,
                          abs(temperature_tenths % 10),
                          water_tenths / 10, water_tenths % 10,
                          state.fire ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, length);
}

static const char *rfid_result_name(rfid_scan_result_t result)
{
    switch (result)
    {
        case RFID_SCAN_ENTERED: return "in";
        case RFID_SCAN_EXITED: return "out";
        case RFID_SCAN_FULL: return "full";
        default: return "wrong_gate";
    }
}

/* Streams cars inside, the recent in/out log and the price in small chunks (no large buffer). */
static esp_err_t rfid_log_get_handler(httpd_req_t *req)
{
    rfid_active_t active[RFID_MAX_ACTIVE];
    rfid_log_entry_t log[RFID_LOG_SIZE];
    const size_t active_count = rfid_session_get_active(active, RFID_MAX_ACTIVE);
    const size_t log_count = rfid_session_get_log(log, RFID_LOG_SIZE);
    const uint32_t now_s = (uint32_t)(parking_clock_now_us() / 1000000LL);
    char line[160];

    httpd_resp_set_type(req, "application/json");
    snprintf(line, sizeof(line), "{\"now_s\":%lu,\"fee_per_hour\":%d,\"parked\":[",
             (unsigned long)now_s, PARKING_FEE_PER_HOUR_VND);
    esp_err_t err = httpd_resp_sendstr_chunk(req, line);

    /* card_id is restricted to [A-Z0-9_-] by the parser, so it needs no JSON escaping. */
    for (size_t i = 0; i < active_count && err == ESP_OK; ++i)
    {
        snprintf(line, sizeof(line), "%s{\"card\":\"%.31s\",\"in_s\":%lu,\"fee\":%lu}",
                 i ? "," : "", active[i].card_id, (unsigned long)active[i].entered_s,
                 (unsigned long)rfid_fee_for_seconds(now_s > active[i].entered_s ? now_s - active[i].entered_s : 0));
        err = httpd_resp_sendstr_chunk(req, line);
    }
    if (err == ESP_OK)
        err = httpd_resp_sendstr_chunk(req, "],\"log\":[");
    for (size_t i = 0; i < log_count && err == ESP_OK; ++i)
    {
        snprintf(line, sizeof(line),
                 "%s{\"card\":\"%.31s\",\"event\":\"%s\",\"gate\":\"%s\",\"at_s\":%lu,\"duration_s\":%lu,\"fee\":%lu}",
                 i ? "," : "", log[i].card_id, rfid_result_name(log[i].result),
                 log[i].gate == RFID_GATE_OUT ? "out" : "in", (unsigned long)log[i].at_s,
                 (unsigned long)log[i].duration_s, (unsigned long)log[i].fee_vnd);
        err = httpd_resp_sendstr_chunk(req, line);
    }
    if (err == ESP_OK)
        err = httpd_resp_sendstr_chunk(req, "]}");
    if (err == ESP_OK)
        err = httpd_resp_sendstr_chunk(req, NULL);
    return err;
}

typedef enum
{
    BARRIER_IN,
    BARRIER_OUT,
    BARRIER_ALL,
} barrier_target_t;

typedef struct
{
    barrier_target_t target;
    bool open;
} barrier_command_t;

/* One handler for every POST /api/barrier/{in,out,all}/{open,close}; the route carries the command. */
static esp_err_t barrier_command_handler(httpd_req_t *req)
{
    if (!authorized(req))
        return send_unauthorized(req);
    const barrier_command_t *cmd = req->user_ctx;
    esp_err_t err = cmd->target == BARRIER_IN    ? servo_set_in_open(cmd->open)
                    : cmd->target == BARRIER_OUT ? servo_set_out_open(cmd->open)
                                                 : servo_set_all_open(cmd->open);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Servo command failed");
    if (cmd->target == BARRIER_ALL)
        return httpd_resp_sendstr(req, cmd->open ? "ALL OPEN" : "ALL CLOSED");
    return httpd_resp_sendstr(req, cmd->open ? "OPEN" : "CLOSED");
}

/* POST /api/time, body = Unix seconds: the server has no RTC or internet, so the browser sets it. */
static esp_err_t time_set_handler(httpd_req_t *req)
{
    if (!authorized(req))
        return send_unauthorized(req);
    char body[16];
    int len = req->content_len < sizeof(body) ? httpd_req_recv(req, body, req->content_len) : -1;
    if (len <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unix seconds expected");
    body[len] = '\0';
    char *end;
    unsigned long long unix_s = strtoull(body, &end, 10);
    if (*end != '\0' || unix_s < 1700000000ULL || unix_s > 4000000000ULL)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unix seconds expected");
    rfid_session_set_clock(unix_s);
    parking_persist_save();
    return httpd_resp_sendstr(req, "OK");
}

static esp_err_t barrier_state_handler(httpd_req_t *req)
{
    char response[40];
    snprintf(response, sizeof(response), "{\"in_open\":%s,\"out_open\":%s}",
             servo_is_in_open() ? "true" : "false", servo_is_out_open() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12; /* one per route below */
    /* Browsers keep several keep-alive connections open and the dashboard polls every
     * 1-2 s; without purging, idle connections fill every slot and new clients get
     * "error in accept (23)". Budget: 3 internal + 10 clients + 1 UDP = 14 of 16 sockets. */
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return err;
    }

    static const barrier_command_t barrier_commands[] = {
        {BARRIER_IN, true}, {BARRIER_IN, false},
        {BARRIER_OUT, true}, {BARRIER_OUT, false},
        {BARRIER_ALL, true}, {BARRIER_ALL, false},
    };
    const httpd_uri_t routes[] = {
        {.uri = "/api/barrier", .method = HTTP_GET, .handler = barrier_state_handler},
        {.uri = "/api/barrier/in/open", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[0]},
        {.uri = "/api/barrier/in/close", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[1]},
        {.uri = "/api/barrier/out/open", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[2]},
        {.uri = "/api/barrier/out/close", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[3]},
        {.uri = "/api/barrier/all/open", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[4]},
        {.uri = "/api/barrier/all/close", .method = HTTP_POST, .handler = barrier_command_handler,
         .user_ctx = (void *)&barrier_commands[5]},
        {.uri = "/api/time", .method = HTTP_POST, .handler = time_set_handler},
        {.uri = "/api/rfid", .method = HTTP_GET, .handler = rfid_log_get_handler},
        {.uri = "/api/parking", .method = HTTP_GET, .handler = parking_state_get_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler},
        {.uri = "/", .method = HTTP_GET, .handler = root_get_handler},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
    {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register HTTP route %s", routes[i].uri);
            httpd_stop(server);
            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP server listening on port %d", config.server_port);
    return ESP_OK;
}
