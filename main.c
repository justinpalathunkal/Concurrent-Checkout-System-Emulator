#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>

int customers_served = 0;  
int total_customers;        
int all_customers_served = 0;  
pthread_mutex_t customers_served_lock = 0;  

typedef struct {
    int id;
    int service_time;
    int items;
} Customer;

typedef struct Node {
    Customer data;
    struct Node* next;
} Node;

typedef struct {
    Node* front;
    Node* rear;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Queue;

typedef struct {
    int id;
    pthread_t thread;
    Queue queue;
    double avg_service_time_per_item; 
} Cashier;

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

void* cashier_function(void* arg) {
    Cashier* cashier = (Cashier*)arg;
    while (1) {
        Customer c = dequeue(&cashier->queue);
        if (c.id == -1) break;
        printf("Cashier %d serving Customer %d (Items: %d, Service Time: %d sec)\n", 
                cashier->id, c.id, c.items, c.service_time);
        sleep(c.service_time);
        printf("Cashier %d finished serving Customer %d\n", cashier->id, c.id);

        pthread_mutex_lock(&customers_served_lock);
        customers_served++;
        if (customers_served >= total_customers) {
            all_customers_served = 1;
        }
        pthread_mutex_unlock(&customers_served_lock);
    }
    return NULL;
}

double calculate_wait_time(Cashier* cashier, int new_customer_items) {
    double estimated_time = 0.0;
    pthread_mutex_lock(&cashier->queue.lock);
    Node* current = cashier->queue.front;
    while (current) {
        estimated_time += current->data.items * cashier->avg_service_time_per_item;
        current = current->next;
    }
    estimated_time += new_customer_items * cashier->avg_service_time_per_item;
    pthread_mutex_unlock(&cashier->queue.lock);
    return estimated_time;
}

Cashier* get_best_cashier(Cashier cashiers[], int cashier_count, int customer_items) {
    double min_estimated_wait_time = __DBL_MAX__;
    Cashier* best_cashier = NULL;

    for (int i = 0; i < cashier_count; i++) {
        double wait_time = calculate_wait_time(&cashiers[i], customer_items);

        if (wait_time < min_estimated_wait_time) {
            min_estimated_wait_time = wait_time;
            best_cashier = &cashiers[i];
        }
    }
    return best_cashier;
}

void new_customer(int id, Cashier cashiers[], int cashier_count) {
    int service_time = (rand() % 5) + 1;
    int num_items = rand() % 20;
    Customer c = {id, service_time, num_items};
    Cashier* cashier = get_best_cashier(cashiers, cashier_count, num_items);
    enqueue(&cashier->queue, c);
    printf("Customer %d added to Cashier %d's queue (Service Time: %d sec, Items: %d)\n", 
            c.id, cashier->id, c.service_time, c.items);
}

int main() {
    int cashier_count;
    
    printf("How many cashiers are available? ");
    scanf("%d", &cashier_count);
    printf("How many customers are shopping? ");
    scanf("%d", &total_customers);

    Cashier cashiers[cashier_count];

    srand(time(NULL));

    for (int i = 0; i < cashier_count; i++) {
        cashiers[i].id = i + 1;
        cashiers[i].queue = *create_queue();
        cashiers[i].avg_service_time_per_item = ((rand() % 3) + 1) * 0.5;  
        pthread_create(&cashiers[i].thread, NULL, cashier_function, &cashiers[i]);
    }

    for (int i = 0; i < total_customers; i++) {
        new_customer(i + 1, cashiers, cashier_count);
        sleep(rand() % 3);
    }

    all_customers_served = 1;
    
    for (int i = 0; i < cashier_count; i++) {
        pthread_cond_broadcast(&cashiers[i].queue.cond);  
    }

    for (int i = 0; i < cashier_count; i++) {
        pthread_join(cashiers[i].thread, NULL);
    }

    printf("All %d customers have been served! Program exiting.\n", total_customers);
    return 0;
}
