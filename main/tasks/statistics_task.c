#include <stdint.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "power.h"
#include "connect.h"
#include "vcore.h"

#define DEFAULT_POLL_RATE 5000
#define MAX_DATA_COUNT 720
#define MAX_DURATION 720
#define MIN_DURATION 1

static const char * TAG = "statistics_task";

static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static uint16_t maxDataCount = 0;
static uint16_t currentDataCount = 0;
static uint16_t duration = 0;

StatisticsNodePtr addStatisticData(StatisticsNodePtr data)
{
    if ((NULL == data) || (0 == maxDataCount)) {
        return NULL;
    }

    StatisticsNodePtr newData = NULL;

    // create new data block or reuse first data block
    if (currentDataCount < maxDataCount) {
        newData = (StatisticsNodePtr)malloc(sizeof(struct StatisticsData));
        currentDataCount++;
    } else {
        newData = statisticsDataStart;
    }

    // set data
    if (NULL != newData) {
        pthread_mutex_lock(&statisticsDataLock);

        if (NULL == statisticsDataStart) {
            statisticsDataStart = newData; // set first new data block
        } else {
            if ((statisticsDataStart == newData) && (NULL != statisticsDataStart->next)) {
                statisticsDataStart = statisticsDataStart->next; // move DataStart to next (first data block reused)
            }
        }

        *newData = *data;
        newData->next = NULL;

        if ((NULL != statisticsDataEnd) && (newData != statisticsDataEnd)) {
            statisticsDataEnd->next = newData; // link data block
        }
        statisticsDataEnd = newData; // set DataEnd to new data

        pthread_mutex_unlock(&statisticsDataLock);
    }

    return newData;
}

StatisticsNextNodePtr statisticData(StatisticsNodePtr nodeIn, StatisticsNodePtr dataOut)
{
    if ((NULL == nodeIn) || (NULL == dataOut) || (0 == maxDataCount)) {
        return NULL;
    }

    StatisticsNextNodePtr nextNode = NULL;

    pthread_mutex_lock(&statisticsDataLock);

    *dataOut = *nodeIn;
    nextNode = nodeIn->next;

    pthread_mutex_unlock(&statisticsDataLock);

    return nextNode;
}

void statistics_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    GLOBAL_STATE->STATISTICS_MODULE.statisticsList = &statisticsDataStart;
}

void statistics_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    maxDataCount = nvs_config_get_u16(NVS_CONFIG_STATISTICS_LIMIT, 0);
    if (MAX_DATA_COUNT < maxDataCount) {
        maxDataCount = MAX_DATA_COUNT;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_LIMIT, maxDataCount);
    }
    duration = nvs_config_get_u16(NVS_CONFIG_STATISTICS_DURATION, 1);
    if (MAX_DURATION < duration) {
        duration = MAX_DURATION;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_DURATION, duration);
    }
    if (MIN_DURATION > duration) {
        duration = MIN_DURATION;
        nvs_config_set_u16(NVS_CONFIG_STATISTICS_DURATION, duration);
    }

    if (0 != maxDataCount) {
        const TickType_t pollRate = DEFAULT_POLL_RATE * duration * (MAX_DATA_COUNT / maxDataCount);
        struct StatisticsData statsData;

        ESP_LOGI(TAG, "Ready!");
        TickType_t taskWakeTime = xTaskGetTickCount();
        while (1) {
            int8_t wifiRSSI = -90;
            get_wifi_current_rssi(&wifiRSSI);

            statsData.timestamp = esp_timer_get_time() / 1000;
            statsData.hashrate = SYSTEM_MODULE.current_hashrate;
            statsData.chipTemperature = POWER_MANAGEMENT_MODULE.chip_temp_avg;
            statsData.vrTemperature = POWER_MANAGEMENT_MODULE.vr_temp;
            statsData.power = POWER_MANAGEMENT_MODULE.power;
            statsData.voltage = POWER_MANAGEMENT_MODULE.voltage;
            statsData.current = Power_get_current();
            statsData.coreVoltageActual = VCORE_get_voltage_mv();
            statsData.coreVoltage = POWER_MANAGEMENT_MODULE.core_voltage;
            statsData.fanSpeed = POWER_MANAGEMENT_MODULE.fan_perc;
            statsData.fanRPM = POWER_MANAGEMENT_MODULE.fan_rpm;
            statsData.wifiRSSI = wifiRSSI;
            statsData.freeHeap = esp_get_free_heap_size();
            statsData.frequency = POWER_MANAGEMENT_MODULE.frequency_value;
            statsData.avghashrate = SYSTEM_MODULE.avg_hashrate;

            addStatisticData(&statsData);

            // looper:
            vTaskDelayUntil(&taskWakeTime, pollRate / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
        }
    } else {
        ESP_LOGI(TAG, "Disabled!");
        while (1) {
            vTaskDelay(DEFAULT_POLL_RATE / portTICK_PERIOD_MS);
        }
    }
}
