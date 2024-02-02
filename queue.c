#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include "queue.h"

//queue structure
struct queue {
    void **array;
    pthread_mutex_t mutex;
    sem_t empty;
    sem_t full;
    int front, back, size;
};

//creates new queue
queue_t *queue_new(int size) {
    queue_t *queue = (queue_t *) malloc(sizeof(queue_t));
    if (queue) {
        pthread_mutex_init(&queue->mutex, NULL);
        sem_init(&queue->empty, 0, 0);
        sem_init(&queue->full, 0, size);
        queue->size = size;
        queue->front = 0;
        queue->back = size - 1;
        queue->array = (void **) malloc(size * sizeof(void *));
        if (!queue->array) {
            free(queue->array);
            queue->array = NULL;
            return NULL;
        }

        return queue;
    }
    free(queue);
    queue = NULL;
    return NULL;
}

//deletes the queue
void queue_delete(queue_t **q) {
    if (*q && (*q)->array) {
        free((*q)->array);
        free(*q);
        *q = NULL;
    }
    return;
}

//pushes element onto queue
bool queue_push(queue_t *q, void *elem) {
    if (elem == NULL) {
        return false;
    }

    sem_wait(&q->full);
    pthread_mutex_lock(&q->mutex);

    q->back = (q->back + 1) % q->size;
    q->array[q->back] = elem;

    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->empty);

    return true;
}

//pops element off queue
bool queue_pop(queue_t *q, void **elem) {
    sem_wait(&q->empty);
    pthread_mutex_lock(&q->mutex);

    *elem = q->array[q->front];
    q->front = (q->front + 1) % q->size;

    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->full);

    if (*elem == NULL) {
        return false;
    }
    return true;
}
