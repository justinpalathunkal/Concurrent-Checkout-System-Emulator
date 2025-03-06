#include "structs.h"
#include <pthread.h>

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 900
#define CUSTOMER_SIZE 50
#define CUSTOMER_SPEED 2  
#define TABLE_WIDTH 100
#define TABLE_HEIGHT 200
#define INPUT_WIDTH 300
#define INPUT_HEIGHT 50
#define SELFCHECKOUT_WIDTH 120
#define SELFCHECKOUT_HEIGHT 100

int total_customers = 0;
int cashier_count = 0;
int selfcheckout_count = 0;  
int customers_served = 0;
int all_customers_served = 0;
pthread_mutex_t customers_served_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t visualization_lock = PTHREAD_MUTEX_INITIALIZER;
bool simulation_running = false;
Uint32 simulation_start_time = 0;
Uint32 simulation_end_time = 0;

Customer* create_customer(int id) {
    Customer* c = (Customer*)malloc(sizeof(Customer));
    c->id = id;
    c->service_time = (rand() % 5) + 3; 
    c->items = (rand() % 15) + 1;        
    c->visual_state = WAITING_TO_ENTER;
    
    c->x = SCREEN_WIDTH / 2 + (rand() % 200 - 100);
    c->y = SCREEN_HEIGHT + (rand() % 50);
    
    c->target_x = c->x;
    c->target_y = c->y;
    c->cashier_id = -1;
    c->kiosk_id = -1;
    c->service_start_time = 0;
    c->is_active = true;
    c->has_reached_cashier = false;
    c->has_reached_kiosk = false;
    
    return c;
}


void update_customers() {
    pthread_mutex_lock(&visualization_lock);
    
    for (int i = 0; i < total_customers; i++) {
        Customer* c = all_customers[i];
        if (!c->is_active) continue;
        
        switch (c->visual_state) {
            case WAITING_TO_ENTER:
                c->visual_state = MOVING_TO_QUEUE;
                break;
                
            case MOVING_TO_QUEUE:
                if (c->y > c->target_y) {
                    c->y -= CUSTOMER_SPEED;
                } else if (c->y < c->target_y) {
                    c->y += CUSTOMER_SPEED;
                }
                
                if (abs(c->x - c->target_x) > CUSTOMER_SPEED) {
                    if (c->x < c->target_x) c->x += CUSTOMER_SPEED;
                    else c->x -= CUSTOMER_SPEED;
                } else {
                    c->x = c->target_x;
                }
                
                if (abs(c->x - c->target_x) <= CUSTOMER_SPEED && 
                    abs(c->y - c->target_y) <= CUSTOMER_SPEED) {
                    c->x = c->target_x; 
                    c->y = c->target_y;
                    c->visual_state = QUEUED;
                }
                break;
                
            case QUEUED:
                if (c->cashier_id > 0 && c->cashier_id <= cashier_count) {
                    Cashier* cashier = all_cashiers[c->cashier_id - 1];
                    
                    int position_in_queue = 0;
                    for (int j = 0; j < total_customers; j++) {
                        if (all_customers[j]->is_active && 
                            all_customers[j]->cashier_id == c->cashier_id &&
                            all_customers[j]->visual_state == QUEUED &&
                            all_customers[j]->id < c->id) {
                            position_in_queue++;
                        }
                    }
                    
                    float new_target_x = cashier->x;
                    float new_target_y = cashier->y + TABLE_HEIGHT/2 + 20 + (position_in_queue * CUSTOMER_SIZE);
                    
                    if (abs(new_target_y - c->target_y) > 5) {
                        c->target_x = new_target_x;
                        c->target_y = new_target_y;
                        c->visual_state = MOVING_TO_QUEUE; 
                    }
                } else if (c->kiosk_id > 0 && c->kiosk_id <= selfcheckout_count) {
                    SelfCheckout* kiosk = all_kiosks[c->kiosk_id - 1];
                    
                    int position_in_queue = 0;
                    for (int j = 0; j < total_customers; j++) {
                        if (all_customers[j]->is_active && 
                            all_customers[j]->kiosk_id == c->kiosk_id &&
                            all_customers[j]->visual_state == QUEUED &&
                            all_customers[j]->id < c->id) {
                            position_in_queue++;
                        }
                    }
                    
                    float new_target_x = SCREEN_WIDTH * 3/4 + 20 + (position_in_queue * CUSTOMER_SIZE);
                    float new_target_y = kiosk->y;
                    
                    if (abs(new_target_x - c->target_x) > 5) {
                        c->target_x = new_target_x;
                        c->target_y = new_target_y;
                        c->visual_state = MOVING_TO_QUEUE; 
                    }
                }
                break;
                
            case BEING_SERVED:
                if (c->cashier_id > 0 && c->cashier_id <= cashier_count) {
                    Cashier* cashier = all_cashiers[c->cashier_id - 1];
                    
                    float service_x = cashier->x;
                    float service_y = cashier->y + (TABLE_HEIGHT/2) - 20; 
                    
                    if (abs(c->x - service_x) > 2 || abs(c->y - service_y) > 2) {
                        c->target_x = service_x;
                        c->target_y = service_y;
                        
                        if (c->y > c->target_y) {
                            c->y -= CUSTOMER_SPEED;
                        } else if (c->y < c->target_y) {
                            c->y += CUSTOMER_SPEED;
                        }
                        
                        if (abs(c->x - c->target_x) > CUSTOMER_SPEED) {
                            if (c->x < c->target_x) c->x += CUSTOMER_SPEED;
                            else c->x -= CUSTOMER_SPEED;
                        } else {
                            c->x = c->target_x;
                        }
                    } else {
                        c->x = service_x;
                        c->y = service_y;
                        
                        if (!c->has_reached_cashier) {
                            c->has_reached_cashier = true;
                        }
                    }
                } else if (c->kiosk_id > 0 && c->kiosk_id <= selfcheckout_count) {
                    SelfCheckout* kiosk = all_kiosks[c->kiosk_id - 1];
                    
                    float service_x = kiosk->x;
                    float service_y = kiosk->y;
                    
                    if (abs(c->x - service_x) > 2 || abs(c->y - service_y) > 2) {
                        c->target_x = service_x;
                        c->target_y = service_y;
                        
                        if (c->y > c->target_y) {
                            c->y -= CUSTOMER_SPEED;
                        } else if (c->y < c->target_y) {
                            c->y += CUSTOMER_SPEED;
                        }
                        
                        if (abs(c->x - c->target_x) > CUSTOMER_SPEED) {
                            if (c->x < c->target_x) c->x += CUSTOMER_SPEED;
                            else c->x -= CUSTOMER_SPEED;
                        } else {
                            c->x = c->target_x;
                        }
                    } else {
                        c->x = service_x;
                        c->y = service_y;
                        
                        if (!c->has_reached_kiosk) {
                            c->has_reached_kiosk = true;
                        }
                    }
                }
                break;
                
            case LEAVING:
                c->y -= CUSTOMER_SPEED * 1.5; 
                if (c->y + CUSTOMER_SIZE < 0) {
                    c->visual_state = EXITED;
                    c->is_active = false;
                }
                break;
                
            case EXITED:
                break;
        }
    }
    
    pthread_mutex_unlock(&visualization_lock);
}