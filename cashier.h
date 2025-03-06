#include "structs.h"
#include <pthread.h>

void* cashier_function(void* arg) {
    Cashier* cashier = (Cashier*)arg;
    
    while (simulation_running) {
        Customer* c = dequeue(cashier->queue);
        
        if (c == NULL) break;
        
        pthread_mutex_lock(&visualization_lock);
        c->visual_state = BEING_SERVED;
        c->cashier_id = cashier->id;
        c->target_x = cashier->x;
        c->target_y = cashier->y + (TABLE_HEIGHT/2) - 20; 
        c->has_reached_cashier = false;  
        cashier->is_serving = true;
        cashier->current_customer = c;
        pthread_mutex_unlock(&visualization_lock);
        
        bool customer_ready = false;
        while (!customer_ready && simulation_running) {
            pthread_mutex_lock(&visualization_lock);
            customer_ready = c->has_reached_cashier;
            pthread_mutex_unlock(&visualization_lock);
            usleep(50000); 
        }
        
        if (!simulation_running) break;
        
        pthread_mutex_lock(&visualization_lock);
        c->service_start_time = SDL_GetTicks();
        pthread_mutex_unlock(&visualization_lock);
        
        float actual_service_time = cashier->avg_service_time_per_item * c->items;
        int service_time_ms = (int)(actual_service_time * 1000);
        
        for (int elapsed = 0; elapsed < service_time_ms && simulation_running; elapsed += 100) {
            usleep(100000); 
        }
        
        cashier->total_items_processed += c->items;
        cashier->total_customers_served++;
        
        pthread_mutex_lock(&visualization_lock);
        c->visual_state = LEAVING;
        cashier->is_serving = false;
        cashier->current_customer = NULL;
        pthread_mutex_unlock(&visualization_lock);
        
        usleep(200000);
        
        pthread_mutex_lock(&customers_served_lock);
        customers_served++;
        if (customers_served >= total_customers) {
            all_customers_served = 1;
            simulation_end_time = SDL_GetTicks();
        }
        pthread_mutex_unlock(&customers_served_lock);
    }
    
    return NULL;
}