#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <float.h>  
#include "structs.h"


Queue* create_queue();
void enqueue(Queue* q, Customer* c);
Customer* dequeue(Queue* q);
float calculate_cashier_score(Cashier* cashier, Customer* customer);
float calculate_kiosk_score(SelfCheckout* kiosk, Customer* customer);
CheckoutOption get_best_checkout_option(Customer* customer);
void* cashier_function(void* arg);
void* selfcheckout_function(void* arg);
SDL_Texture* renderText(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color);
int getEmulationSpecs(SDL_Renderer *renderer, TTF_Font *font, const char *prompt, int minValue);
Customer* create_customer(int id);
bool init_visualization();
void cleanup();
void draw_tables_and_lanes(SDL_Renderer* renderer, Cashier** cashiers, int cashier_count);
void update_customers();
void render();


int main(int argc, char *argv[]) {
    if (!init_visualization()) {
        printf("Failed to initialize visualization!\n");
        return -1;
    }
    
    cashier_count = getEmulationSpecs(renderer, font, "Enter the number of cashiers:", 1);
    if (cashier_count <= 0) {
        cleanup();
        return 0;
    }
    
    total_customers = getEmulationSpecs(renderer, font, "Enter the number of customers:", 1);
    if (total_customers <= 0) {
        cleanup();
        return 0;
    }
    
    selfcheckout_count = getEmulationSpecs(renderer, font, "Enter the number of self-checkout kiosks:", 0);
    if (selfcheckout_count < 0) { 
        cleanup();
        return 0;
    }
    
    srand(time(NULL));
    
    all_customers = (Customer**)malloc(sizeof(Customer*) * total_customers);
    for (int i = 0; i < total_customers; i++) {
        all_customers[i] = create_customer(i + 1);
    }
    
    float cashier_area_width = (selfcheckout_count > 0) ? SCREEN_WIDTH * 3/4 : SCREEN_WIDTH;
    
    all_cashiers = (Cashier**)malloc(sizeof(Cashier*) * cashier_count);
    for (int i = 0; i < cashier_count; i++) {
        all_cashiers[i] = (Cashier*)malloc(sizeof(Cashier));
        all_cashiers[i]->id = i + 1;
        all_cashiers[i]->queue = create_queue();
        all_cashiers[i]->is_serving = false;
        all_cashiers[i]->current_customer = NULL;
        all_cashiers[i]->total_items_processed = 0;
        all_cashiers[i]->total_customers_served = 0;
        
        all_cashiers[i]->avg_service_time_per_item = 0.5f + ((float)rand() / RAND_MAX);
        
        all_cashiers[i]->x = ((i + 1) * cashier_area_width) / (cashier_count + 1);
        all_cashiers[i]->y = SCREEN_HEIGHT / 3;
    }
    
    if (selfcheckout_count > 0) {
        all_kiosks = (SelfCheckout**)malloc(sizeof(SelfCheckout*) * selfcheckout_count);
        for (int i = 0; i < selfcheckout_count; i++) {
            all_kiosks[i] = (SelfCheckout*)malloc(sizeof(SelfCheckout));
            all_kiosks[i]->id = i + 1;
            all_kiosks[i]->queue = create_queue();
            all_kiosks[i]->is_serving = false;
            all_kiosks[i]->current_customer = NULL;
            all_kiosks[i]->total_items_processed = 0;
            all_kiosks[i]->total_customers_served = 0;
            
            all_kiosks[i]->avg_service_time_per_item = 0.8f;
            
            all_kiosks[i]->x = SCREEN_WIDTH * 7/8; 
            int available_height = SCREEN_HEIGHT - 150;
            int row_height = available_height / selfcheckout_count;
            all_kiosks[i]->y = 120 + (row_height * i) + (row_height / 2);
        }
    }
    
    simulation_running = true;
    simulation_start_time = SDL_GetTicks();
    
    for (int i = 0; i < cashier_count; i++) {
        pthread_create(&all_cashiers[i]->thread, NULL, cashier_function, all_cashiers[i]);
    }
    
    for (int i = 0; i < selfcheckout_count; i++) {
        pthread_create(&all_kiosks[i]->thread, NULL, selfcheckout_function, all_kiosks[i]);
    }
    
    int current_customer = 0;
    
    for (int i = 0; i < total_customers && i < (cashier_count + selfcheckout_count) * 3; i++) {  
        Customer* c = all_customers[current_customer++];
        
        CheckoutOption best_option = get_best_checkout_option(c);
        
        if (best_option.type == CASHIER) {
            Cashier* cashier = all_cashiers[best_option.index];
            
            int queuePosition = 0;
            for (int j = 0; j < total_customers; j++) {
                if (all_customers[j]->is_active && 
                    all_customers[j]->visual_state == QUEUED && 
                    all_customers[j]->cashier_id == cashier->id) {
                    queuePosition++;
                }
            }
            
            c->target_x = cashier->x;
            c->target_y = cashier->y + TABLE_HEIGHT/2 + 20 + (queuePosition * CUSTOMER_SIZE);
            c->cashier_id = cashier->id;
            c->kiosk_id = -1;
            
            enqueue(cashier->queue, c);
        } else if (best_option.type == KIOSK) {
            SelfCheckout* kiosk = all_kiosks[best_option.index];
            
            int queuePosition = 0;
            for (int j = 0; j < total_customers; j++) {
                if (all_customers[j]->is_active && 
                    all_customers[j]->visual_state == QUEUED && 
                    all_customers[j]->kiosk_id == kiosk->id) {
                    queuePosition++;
                }
            }
            
            int row_center_y = kiosk->y;
            c->target_x = SCREEN_WIDTH * 3/4 + 20 + (queuePosition * CUSTOMER_SIZE);
            c->target_y = row_center_y;
            c->cashier_id = -1;
            c->kiosk_id = kiosk->id;
            
            enqueue(kiosk->queue, c);
        }
    }
    
    bool running = true;
    Uint32 next_customer_time = SDL_GetTicks() + (rand() % 2000) + 1000;  
    
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
        
        if (current_customer < total_customers && SDL_GetTicks() > next_customer_time) {
            Customer* c = all_customers[current_customer++];
            
            CheckoutOption best_option = get_best_checkout_option(c);
            
            if (best_option.type == CASHIER) {
                Cashier* cashier = all_cashiers[best_option.index];
                
                int queuePosition = 0;
                for (int j = 0; j < total_customers; j++) {
                    if (all_customers[j]->is_active && 
                        all_customers[j]->visual_state == QUEUED && 
                        all_customers[j]->cashier_id == cashier->id) {
                        queuePosition++;
                    }
                }
                
                c->target_x = cashier->x;
                c->target_y = cashier->y + TABLE_HEIGHT/2 + 20 + (queuePosition * CUSTOMER_SIZE);
                c->cashier_id = cashier->id;
                c->kiosk_id = -1;
                
                enqueue(cashier->queue, c);
            } else if (best_option.type == KIOSK) {
                SelfCheckout* kiosk = all_kiosks[best_option.index];
                
                int queuePosition = 0;
                for (int j = 0; j < total_customers; j++) {
                    if (all_customers[j]->is_active && 
                        all_customers[j]->visual_state == QUEUED && 
                        all_customers[j]->kiosk_id == kiosk->id) {
                        queuePosition++;
                    }
                }
                
                int row_center_y = kiosk->y;
                c->target_x = SCREEN_WIDTH * 3/4 + 20 + (queuePosition * CUSTOMER_SIZE);
                c->target_y = row_center_y;
                c->cashier_id = -1;
                c->kiosk_id = kiosk->id;
                
                enqueue(kiosk->queue, c);
            }
            
            next_customer_time = SDL_GetTicks() + (rand() % 2000) + 1000;
        }
        
        update_customers();
        
        render();
        
        SDL_Delay(30);  
    }
    
    simulation_running = false;
    all_customers_served = 1;
    
    for (int i = 0; i < cashier_count; i++) {
        pthread_cond_broadcast(&all_cashiers[i]->queue->cond);
    }
    
    for (int i = 0; i < selfcheckout_count; i++) {
        pthread_cond_broadcast(&all_kiosks[i]->queue->cond);
    }
    
    for (int i = 0; i < cashier_count; i++) {
        pthread_join(all_cashiers[i]->thread, NULL);
    }
    
    for (int i = 0; i < selfcheckout_count; i++) {
        pthread_join(all_kiosks[i]->thread, NULL);
    }
    
    cleanup();
    
    return 0;
}