#include <stdint.h>
#include <pthread.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "statistics_task.h"

#include "nvs_config.h"
#include "power.h"
#include "connect.h"
#include "vcore.h"
#include "power_management_module.h"
#include "system_module.h"

#define DEFAULT_POLL_RATE 1000 // Collect data every second

static const char * TAG = "statistics_task";

static StatisticsNodePtr statisticsDataStart = NULL;
static StatisticsNodePtr statisticsDataEnd = NULL;
static pthread_mutex_t statisticsDataLock = PTHREAD_MUTEX_INITIALIZER;

static const uint16_t maxDataCount = 720;
static uint16_t currentDataCount;
static uint16_t statsFrequency;

typedef struct {
    int64_t timestamp;
    double hashrate;
    double avghashrate;
    float chipTemperature;
    float vrTemperature;
    float power;
    float voltage;
    uint16_t frequency;
    float current;
    int16_t coreVoltageActual;
    float coreVoltage;
    uint16_t fanSpeed;
    uint16_t fanRPM;
    int8_t wifiRSSI;
    uint32_t freeHeap;
    double hashrate_no_error;
    double hashrate_error;
} StatisticsDataAccumulator;

static StatisticsDataAccumulator accData = {};
static uint16_t accCount = 0;

StatisticsNodePtr addStatisticData(StatisticsNodePtr data)
{
    if ((NULL == data) || (0 == statsFrequency)) {
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
    if ((NULL == nodeIn) || (NULL == dataOut) || (0 == statsFrequency)) {
        return NULL;
    }

    StatisticsNextNodePtr nextNode = NULL;

    pthread_mutex_lock(&statisticsDataLock);

    *dataOut = *nodeIn;
    nextNode = nodeIn->next;

    pthread_mutex_unlock(&statisticsDataLock);

    return nextNode;
}

void statistics_init()
{
    STATISTICS_MODULE.statisticsList = &statisticsDataStart;
}

void statistics_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    TickType_t taskWakeTime = xTaskGetTickCount();

    while (1) {
        const int64_t currentTime = esp_timer_get_time() / 1000;
        statsFrequency = nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY, 0) * 1000;

        if ((0 != statsFrequency) && (accCount < statsFrequency / DEFAULT_POLL_RATE)) {
            int8_t wifiRSSI = -90;
            get_wifi_current_rssi(&wifiRSSI);

            accData.timestamp += currentTime;
            accData.hashrate += SYSTEM_MODULE.current_hashrate;
            accData.chipTemperature += POWER_MANAGEMENT_MODULE.chip_temp_avg;
            accData.vrTemperature += POWER_MANAGEMENT_MODULE.vr_temp;
            accData.power += POWER_MANAGEMENT_MODULE.power;
            accData.voltage += POWER_MANAGEMENT_MODULE.voltage;
            accData.current += Power_get_current();
            accData.coreVoltageActual += VCORE_get_voltage_mv();
            accData.coreVoltage += POWER_MANAGEMENT_MODULE.core_voltage;
            accData.fanSpeed += POWER_MANAGEMENT_MODULE.fan_perc;
            accData.fanRPM += POWER_MANAGEMENT_MODULE.fan_rpm;
            accData.wifiRSSI += wifiRSSI;
            accData.freeHeap += esp_get_free_heap_size();
            accData.frequency += POWER_MANAGEMENT_MODULE.frequency_value;
            accData.avghashrate += SYSTEM_MODULE.avg_hashrate;
            accData.hashrate_no_error += SYSTEM_MODULE.hashrate_no_error;
            accData.hashrate_error += SYSTEM_MODULE.hashrate_error;
            //ESP_LOGI(TAG, "Accumulating coreVoltage: %f", POWER_MANAGEMENT_MODULE.core_voltage);

            accCount++;
        } else {
            if (accCount > 0) {
                struct StatisticsData avgData = {};
                avgData.timestamp = accData.timestamp / accCount;
                avgData.hashrate = accData.hashrate / accCount;
                avgData.avghashrate = accData.avghashrate / accCount;
                avgData.chipTemperature = accData.chipTemperature / accCount;
                avgData.vrTemperature = accData.vrTemperature / accCount;
                avgData.power = accData.power / accCount;
                avgData.voltage = accData.voltage / accCount;
                avgData.current = accData.current / accCount;
                avgData.coreVoltageActual = accData.coreVoltageActual / accCount;
                avgData.coreVoltage = accData.coreVoltage / accCount;
                avgData.fanSpeed = accData.fanSpeed / accCount;
                avgData.fanRPM = accData.fanRPM / accCount;
                avgData.wifiRSSI = accData.wifiRSSI / accCount;
                avgData.freeHeap = accData.freeHeap / accCount;
                avgData.frequency = accData.frequency / accCount;
                avgData.avghashrate = accData.avghashrate / accCount;
                avgData.hashrate_no_error = accData.hashrate_no_error / accCount;
                avgData.hashrate_error = accData.hashrate_error / accCount;

                //ESP_LOGI(TAG, "Adding statistic data: coreVoltage=%f", avgData.coreVoltage);

                addStatisticData(&avgData);

                // Reset accumulator
                accData.timestamp = 0;
                accData.hashrate = 0;
                accData.avghashrate = 0;
                accData.chipTemperature = 0;
                accData.vrTemperature = 0;
                accData.power = 0;
                accData.voltage = 0;
                accData.current = 0;
                accData.coreVoltageActual = 0;
                accData.coreVoltage = 0;
                accData.fanSpeed = 0;
                accData.fanRPM = 0;
                accData.wifiRSSI = 0;
                accData.freeHeap = 0;
                accData.frequency = 0;
                accData.avghashrate = 0;
                accData.hashrate_no_error = 0;
                accData.hashrate_error = 0;

                accCount = 0;
            }
        }

        vTaskDelayUntil(&taskWakeTime, DEFAULT_POLL_RATE / portTICK_PERIOD_MS); // taskWakeTime is automatically updated
    }
}