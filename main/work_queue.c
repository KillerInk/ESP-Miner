#include "work_queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mining.h"

void queue_init(work_queue *queue, size_t size) {
    *queue = xQueueCreate(QUEUE_SIZE, size);
}

void queue_enqueue(work_queue *queue, void *new_work) {
    if (queue != NULL && new_work != NULL) {
        xQueueSend(*queue, &new_work, portMAX_DELAY);
    }
}

void *queue_dequeue(work_queue *queue) {
    void *dequeued_work = NULL;
    if (queue != NULL) {
        xQueueReceive(*queue, &dequeued_work, portMAX_DELAY);
    }
    return dequeued_work;
}

void queue_clear(work_queue *queue) {
    void *item;
    while (xQueueReceive(*queue, &item, 0) == pdPASS) {
        free(item);
    }
}

void ASIC_jobs_queue_clear(work_queue *queue) {
    bm_job *item;
    while (xQueueReceive(*queue, &item, 0) == pdPASS) {
        free(item->jobid);
        free(item->extranonce2);
        free(item);
    }
}


