#include "nvs_config.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define NVS_CONFIG_NAMESPACE "main"

#define FLOAT_STR_LEN 32

static const char * TAG = "nvs_config";

// ---------------------------------------------------------------------------
//  Queue Item Definition
// ---------------------------------------------------------------------------
typedef enum {
    NVS_ITEM_TYPE_U8,
    NVS_ITEM_TYPE_U16,
    NVS_ITEM_TYPE_I32,
    NVS_ITEM_TYPE_U64,
    NVS_ITEM_TYPE_BOOL,
    NVS_ITEM_TYPE_FLOAT,
    NVS_ITEM_TYPE_DOUBLE,
    NVS_ITEM_TYPE_STRING
} nvs_item_type_t;

typedef union {
    uint8_t u8;
    uint16_t u16;
    int32_t  i32;
    uint64_t u64;
    bool     b;
    float   f;
    double  d;
    char *  s;          // pointer to dynamically allocated string
} nvs_value_u;

typedef struct {
    const char *key;
    nvs_item_type_t type;
    nvs_value_u val;
} nvs_item_t;

// Global queue handle
static QueueHandle_t nvs_queue = NULL;

char * nvs_config_get_string(const char * key, const char * default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return strdup(default_value);
    }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK) {
        nvs_close(handle);
        return strdup(default_value);
    }

    char * out = malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK) {
        free(out);
        nvs_close(handle);
        return strdup(default_value);
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_string(const char * key, const char * value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %s", key, value);
    }

    nvs_close(handle);
}

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint16_t out;
    err = nvs_get_u16(handle, key, &out);
    nvs_close(handle);

    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_u16(const char * key, const uint16_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_u16(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %u", key, value);
    }

    nvs_close(handle);
}

int32_t nvs_config_get_i32(const char * key, const int32_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    int32_t out;
    err = nvs_get_i32(handle, key, &out);
    nvs_close(handle);

    if (err != ESP_OK) {
        return default_value;
    }
    return out;
}

void nvs_config_set_i32(const char * key, const int32_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %li", key, value);
    }

    nvs_close(handle);
}

uint64_t nvs_config_get_u64(const char * key, const uint64_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    uint64_t out;
    err = nvs_get_u64(handle, key, &out);

    if (err != ESP_OK) {
        nvs_close(handle);
        return default_value;
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_u64(const char * key, const uint64_t value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_u64(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %llu", key, value);
    }
    nvs_close(handle);
}

float nvs_config_get_float(const char *key, float default_value)
{
    char default_str[FLOAT_STR_LEN];
    snprintf(default_str, sizeof(default_str), "%.6f", default_value);

    char *str_value = nvs_config_get_string(key, default_str);

    char *endptr;
    float value = strtof(str_value, &endptr);
    if (endptr == str_value || *endptr != '\0') {
        ESP_LOGW(TAG, "Invalid float format for key %s: %s", key, str_value);
        value = default_value;
    }

    free(str_value);
    return value;
}

void nvs_config_set_float(const char *key, float value)
{
    char str_value[FLOAT_STR_LEN];
    snprintf(str_value, sizeof(str_value), "%.6f", value);

    nvs_config_set_string(key, str_value);
}

double nvs_config_get_double(const char * key, const double default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    double out;
    size_t size = sizeof(double);
    err = nvs_get_blob(handle, key, &out, &size);

    if (err != ESP_OK) {
        nvs_close(handle);
        return default_value;
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_double(const char * key, const double value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_blob(handle, key, &value, sizeof(double));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %f", key, value);
    }
    nvs_close(handle);
}

bool nvs_config_get_bool(const char * key, const bool default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_value;
    }

    bool out;
    size_t size = sizeof(bool);
    err = nvs_get_blob(handle, key, &out, &size);

    if (err != ESP_OK) {
        nvs_close(handle);
        return default_value;
    }

    nvs_close(handle);
    return out;
}

void nvs_config_set_bool(const char * key, const bool value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_set_blob(handle, key, &value, sizeof(bool));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not write nvs key: %s, value: %i", key, value);
    }
    nvs_close(handle);
}

void nvs_config_commit()
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open nvs");
        return;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not commit nvs");
    }
    nvs_close(handle);
}




// ---------------------------------------------------------------------------
//  Task that processes the queue
// ---------------------------------------------------------------------------
void nvs_write_task(void *pvParameters)
{
    (void) pvParameters;

    nvs_item_t item;
    for (;;) {
        if (xQueueReceive(nvs_queue, &item, portMAX_DELAY) == pdPASS) {
            switch (item.type) {
                case NVS_ITEM_TYPE_U8:
                    // Convert to u16 because no u8 setter
                    nvs_config_set_u16(item.key, (uint16_t)item.val.u8);
                    break;

                case NVS_ITEM_TYPE_U16:
                    nvs_config_set_u16(item.key, item.val.u16);
                    break;

                case NVS_ITEM_TYPE_I32:
                    nvs_config_set_i32(item.key, item.val.i32);
                    break;

                case NVS_ITEM_TYPE_U64:
                    nvs_config_set_u64(item.key, item.val.u64);
                    break;

                case NVS_ITEM_TYPE_BOOL:
                    nvs_config_set_bool(item.key, item.val.b);
                    break;

                case NVS_ITEM_TYPE_FLOAT:
                    nvs_config_set_float(item.key, item.val.f);
                    break;

                case NVS_ITEM_TYPE_DOUBLE:
                    nvs_config_set_double(item.key, item.val.d);
                    break;

                case NVS_ITEM_TYPE_STRING:
                    nvs_config_set_string(item.key, item.val.s);
                    free(item.val.s);          // clean up allocated string
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Queue and Task initialization helper
// ---------------------------------------------------------------------------
void init_and_start(void)
{
    // Create queue with a reasonable depth (e.g., 10 items)
    nvs_queue = xQueueCreate(10, sizeof(nvs_item_t));
    if (!nvs_queue) return;

    // Start the background task
    xTaskCreate(&nvs_write_task,
                "NVS_Write_Task",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);
}

// ---------------------------------------------------------------------------
//  Helper functions that enqueue various types
// ---------------------------------------------------------------------------
void enqueue_nvs_uint8(const char *key, uint8_t value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_U8 };
    item.val.u8 = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_uint16(const char *key, uint16_t value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_U16 };
    item.val.u16 = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_int32(const char *key, int32_t value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_I32 };
    item.val.i32 = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_uint64(const char *key, uint64_t value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_U64 };
    item.val.u64 = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_bool(const char *key, bool value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_BOOL };
    item.val.b = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_float(const char *key, float value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_FLOAT };
    item.val.f = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_double(const char *key, double value)
{
    if (!nvs_queue) init_and_start();
    nvs_item_t item = { key, NVS_ITEM_TYPE_DOUBLE };
    item.val.d = value;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}

void enqueue_nvs_string(const char *key, const char *value)
{
    if (!nvs_queue) init_and_start();
    char *copy = strdup(value);   // allocate copy for queue
    nvs_item_t item = { key, NVS_ITEM_TYPE_STRING };
    item.val.s = copy;
    xQueueSendToBack(nvs_queue, &item, portMAX_DELAY);
}


