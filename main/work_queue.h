#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include "freertos/FreeRTOS.h"
#define QUEUE_SIZE 12
typedef QueueHandle_t work_queue;
void queue_init(work_queue *queue, size_t size);
void queue_enqueue(work_queue *queue, void *new_work);
void *queue_dequeue(work_queue *queue);
void queue_clear(work_queue *queue);
void ASIC_jobs_queue_clear(work_queue *queue);
#endif // WORK_QUEUE_H

