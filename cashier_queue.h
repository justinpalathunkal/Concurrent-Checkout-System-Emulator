#include <pthread.h>

Queue* create_queue() {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

void enqueue(Queue* q, Customer c) {
    Node* newNode = (Node*)malloc(sizeof(Node));
    newNode->data = c;
    newNode->next = NULL;

    pthread_mutex_lock(&q->lock);
    if (!q->rear) {
        q->front = q->rear = newNode;
    } else {
        q->rear->next = newNode;
        q->rear = newNode;
    }
    q->size++;
    pthread_cond_signal(&q->cond);  
    pthread_mutex_unlock(&q->lock);
}

Customer dequeue(Queue* q) {
    pthread_mutex_lock(&q->lock);
    while (q->size == 0) {
        if (all_customers_served) {  
            pthread_mutex_unlock(&q->lock);
            return (Customer){-1, 0, 0};  
        }
        pthread_cond_wait(&q->cond, &q->lock);
    }

    Node* temp = q->front;
    Customer c = temp->data;
    q->front = q->front->next;
    if (!q->front) q->rear = NULL;
    q->size--;

    free(temp);
    pthread_mutex_unlock(&q->lock);
    return c;
}
