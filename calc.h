#include "structs.h"
#include <pthread.h>

Cashier* get_least_busy_cashier() {
    int min_size = INT_MAX;
    Cashier* best_cashier = NULL;

    for (int i = 0; i < cashier_count; i++) {
        pthread_mutex_lock(&all_cashiers[i]->queue->lock);
        if (all_cashiers[i]->queue->size < min_size) {
            min_size = all_cashiers[i]->queue->size;
            best_cashier = all_cashiers[i];
        }
        pthread_mutex_unlock(&all_cashiers[i]->queue->lock);
    }
    return best_cashier;
}

float calculate_cashier_score(Cashier* cashier, Customer* customer) {
    float estimated_service_time = cashier->avg_service_time_per_item * customer->items;
    float queue_waiting_time = 0.0;
    
    pthread_mutex_lock(&cashier->queue->lock);
    Node* current = cashier->queue->front;
    while (current != NULL) {
        Customer* queued_customer = current->data;
        queue_waiting_time += cashier->avg_service_time_per_item * queued_customer->items;
        current = current->next;
    }
    pthread_mutex_unlock(&cashier->queue->lock);
    
    if (cashier->is_serving && cashier->current_customer != NULL) {
        Uint32 elapsed_time_ms = SDL_GetTicks() - cashier->current_customer->service_start_time;
        float elapsed_time = elapsed_time_ms / 1000.0f;
        float total_service_time = cashier->avg_service_time_per_item * cashier->current_customer->items;
        float remaining_time = total_service_time - elapsed_time;
        if (remaining_time > 0) {
            queue_waiting_time += remaining_time;
        }
    }
    
    float total_time = queue_waiting_time + estimated_service_time;
    
    return total_time;
}

float calculate_kiosk_score(SelfCheckout* kiosk, Customer* customer) {
    
    float estimated_service_time = kiosk->avg_service_time_per_item * customer->items;
    float queue_waiting_time = 0.0;
    
    pthread_mutex_lock(&kiosk->queue->lock);
    Node* current = kiosk->queue->front;
    while (current != NULL) {
        Customer* queued_customer = current->data;
        queue_waiting_time += kiosk->avg_service_time_per_item * queued_customer->items;
        current = current->next;
    }
    pthread_mutex_unlock(&kiosk->queue->lock);
    
    if (kiosk->is_serving && kiosk->current_customer != NULL) {
        Uint32 elapsed_time_ms = SDL_GetTicks() - kiosk->current_customer->service_start_time;
        float elapsed_time = elapsed_time_ms / 1000.0f;
        float total_service_time = kiosk->avg_service_time_per_item * kiosk->current_customer->items;
        float remaining_time = total_service_time - elapsed_time;
        if (remaining_time > 0) {
            queue_waiting_time += remaining_time;
        }
    }
    
    float total_time = queue_waiting_time + estimated_service_time;
    return total_time;
}

CheckoutOption get_best_checkout_option(Customer* customer) {
    float best_score = FLT_MAX;
    CheckoutOption best_option;
    best_option.type = NONE;
    best_option.index = -1;
    
    for (int i = 0; i < cashier_count; i++) {
        float score = calculate_cashier_score(all_cashiers[i], customer);
        
        if (score < best_score) {
            best_score = score;
            best_option.type = CASHIER;
            best_option.index = i;
        }
    }
    
    for (int i = 0; i < selfcheckout_count; i++) {
        float score = calculate_kiosk_score(all_kiosks[i], customer);
        
        if (score < best_score) {
            best_score = score;
            best_option.type = KIOSK;
            best_option.index = i;
        }
    }
    
    return best_option;
}