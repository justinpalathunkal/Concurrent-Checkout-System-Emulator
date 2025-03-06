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

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
TTF_Font* font = NULL;
SDL_Texture* customer_texture = NULL;
SDL_Texture* cashier_texture = NULL;
SDL_Texture* selfcheckout_texture = NULL;  

Customer** all_customers = NULL;
Cashier** all_cashiers = NULL;
SelfCheckout** all_kiosks = NULL;  

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

Queue* create_queue() {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

void enqueue(Queue* q, Customer* c) {
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

Customer* dequeue(Queue* q) {
    pthread_mutex_lock(&q->lock);
    while (q->size == 0) {
        if (all_customers_served) {  
            pthread_mutex_unlock(&q->lock);
            return NULL;  
        }
        pthread_cond_wait(&q->cond, &q->lock);
    }

    Node* temp = q->front;
    Customer* c = temp->data;
    q->front = q->front->next;
    if (!q->front) q->rear = NULL;
    q->size--;

    free(temp);
    pthread_mutex_unlock(&q->lock);
    return c;
}

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

void* selfcheckout_function(void* arg) {
    SelfCheckout* kiosk = (SelfCheckout*)arg;
    
    while (simulation_running) {
        Customer* c = dequeue(kiosk->queue);
        
        if (c == NULL) break;
        
        pthread_mutex_lock(&visualization_lock);
        c->visual_state = BEING_SERVED;
        c->kiosk_id = kiosk->id;
        c->cashier_id = -1; 
        c->has_reached_kiosk = false; 
        c->target_x = kiosk->x;
        c->target_y = kiosk->y;
        kiosk->is_serving = true;
        kiosk->current_customer = c;
        pthread_mutex_unlock(&visualization_lock);
        
        bool customer_ready = false;
        while (!customer_ready && simulation_running) {
            pthread_mutex_lock(&visualization_lock);
            customer_ready = c->has_reached_kiosk;
            pthread_mutex_unlock(&visualization_lock);
            usleep(50000); 
        }
        
        if (!simulation_running) break;
        
        pthread_mutex_lock(&visualization_lock);
        c->service_start_time = SDL_GetTicks();
        pthread_mutex_unlock(&visualization_lock);
        
        float actual_service_time = kiosk->avg_service_time_per_item * c->items;
        int service_time_ms = (int)(actual_service_time * 1000);
        
        for (int elapsed = 0; elapsed < service_time_ms && simulation_running; elapsed += 100) {
            usleep(100000); 
        }
        
        kiosk->total_items_processed += c->items;
        kiosk->total_customers_served++;
        
        pthread_mutex_lock(&visualization_lock);
        c->visual_state = LEAVING;
        kiosk->is_serving = false;
        kiosk->current_customer = NULL;
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

SDL_Texture* renderText(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
    if (!surface) {
        return NULL;
    }
    
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

int getEmulationSpecs(SDL_Renderer *renderer, TTF_Font *font, const char *prompt, int minValue) {
    char inputText[16] = "";
    bool inputActive = true;
    int inputValue = 0;
    
    SDL_Color textColor = {0, 0, 0, 255};               
    SDL_Color promptColor = {0, 0, 0, 255};             
    SDL_Color bgColor = {173, 216, 230, 255};           
    SDL_Color panelColor = {255, 255, 255, 255};        
    SDL_Color inputBgColor = {240, 240, 240, 255};      
    SDL_Color borderColor = {100, 100, 100, 255};      
    
    SDL_StartTextInput();
    
    while (inputActive) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_StopTextInput();
                return -1;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_RETURN) {
                    inputValue = atoi(inputText);
                    if (inputValue >= minValue) {
                        inputActive = false;
                    }
                } else if (event.key.keysym.sym == SDLK_BACKSPACE && strlen(inputText) > 0) {
                    inputText[strlen(inputText) - 1] = '\0';
                }
            } else if (event.type == SDL_TEXTINPUT) {
                if (event.text.text[0] >= '0' && event.text.text[0] <= '9' && strlen(inputText) < 10) {
                    strcat(inputText, event.text.text);
                }
            }
        }
        
        SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        SDL_RenderClear(renderer);
        
        int panelWidth = 500;
        int panelHeight = 300;
        SDL_Rect panelRect = {(SCREEN_WIDTH - panelWidth) / 2, (SCREEN_HEIGHT - panelHeight) / 2, panelWidth, panelHeight};
        SDL_SetRenderDrawColor(renderer, panelColor.r, panelColor.g, panelColor.b, panelColor.a);
        SDL_RenderFillRect(renderer, &panelRect);
        
        SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
        SDL_RenderDrawRect(renderer, &panelRect);
        
        // Draw prompt inside panel
        SDL_Texture *promptTexture = renderText(renderer, font, prompt, promptColor);
        int promptWidth, promptHeight;
        SDL_QueryTexture(promptTexture, NULL, NULL, &promptWidth, &promptHeight);
        SDL_Rect promptRect = {(SCREEN_WIDTH - promptWidth) / 2, 
                             panelRect.y + 50, 
                             promptWidth, promptHeight};
        SDL_RenderCopy(renderer, promptTexture, NULL, &promptRect);
        SDL_DestroyTexture(promptTexture);
        
        SDL_Rect inputRect = {(SCREEN_WIDTH - INPUT_WIDTH) / 2, 
                            promptRect.y + promptHeight + 30, 
                            INPUT_WIDTH, INPUT_HEIGHT};
        SDL_SetRenderDrawColor(renderer, inputBgColor.r, inputBgColor.g, inputBgColor.b, inputBgColor.a);
        SDL_RenderFillRect(renderer, &inputRect);
        SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
        SDL_RenderDrawRect(renderer, &inputRect);
        
        if (strlen(inputText) > 0) {
            SDL_Texture *inputTexture = renderText(renderer, font, inputText, textColor);
            int textWidth, textHeight;
            SDL_QueryTexture(inputTexture, NULL, NULL, &textWidth, &textHeight);
            SDL_Rect textRect = {(SCREEN_WIDTH - textWidth) / 2, 
                               inputRect.y + (inputRect.h - textHeight) / 2, 
                               textWidth, textHeight};
            SDL_RenderCopy(renderer, inputTexture, NULL, &textRect);
            SDL_DestroyTexture(inputTexture);
        }
        
        const char *instructions = "Enter a positive number and press Enter";
        SDL_Texture *instructionsTexture = renderText(renderer, font, instructions, textColor);
        int instWidth, instHeight;
        SDL_QueryTexture(instructionsTexture, NULL, NULL, &instWidth, &instHeight);
        SDL_Rect instRect = {(SCREEN_WIDTH - instWidth) / 2, 
                           inputRect.y + inputRect.h + 30, 
                           instWidth, instHeight};
        SDL_RenderCopy(renderer, instructionsTexture, NULL, &instRect);
        SDL_DestroyTexture(instructionsTexture);
        
        if (inputValue > 0 && inputValue < minValue) {
            char errorMsg[50];
            sprintf(errorMsg, "Please enter a number >= %d", minValue);
            SDL_Texture *errorTexture = renderText(renderer, font, errorMsg, (SDL_Color){255, 0, 0, 255});
            int errorWidth, errorHeight;
            SDL_QueryTexture(errorTexture, NULL, NULL, &errorWidth, &errorHeight);
            SDL_Rect errorRect = {(SCREEN_WIDTH - errorWidth) / 2, 
                                instRect.y + instRect.h + 20, 
                                errorWidth, errorHeight};
            SDL_RenderCopy(renderer, errorTexture, NULL, &errorRect);
            SDL_DestroyTexture(errorTexture);
        }
        
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    
    SDL_StopTextInput();
    return inputValue;
}

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

bool init_visualization() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return false;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        SDL_Quit();
        return false;
    }

    if (TTF_Init() == -1) {
        printf("TTF_Init failed! SDL_ttf Error: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    font = TTF_OpenFont("arial.ttf", 24);
    if (!font) {
        printf("Failed to load font! SDL_ttf Error: %s\n", TTF_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    window = SDL_CreateWindow("Concurrent Cashier System Emulator",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              SCREEN_WIDTH, SCREEN_HEIGHT,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    customer_texture = IMG_LoadTexture(renderer, "user.png");
    if (!customer_texture) {
        printf("Failed to load user image! SDL_image Error: %s\n", IMG_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    cashier_texture = IMG_LoadTexture(renderer, "clerk.png");
    if (!cashier_texture) {
        printf("Failed to load clerk image! SDL_image Error: %s\n", IMG_GetError());
        SDL_DestroyTexture(customer_texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    
    selfcheckout_texture = IMG_LoadTexture(renderer, "selfcheckout.png");
    if (!selfcheckout_texture) {
        printf("Failed to load self-checkout image! SDL_image Error: %s\n", IMG_GetError());
        printf("Will continue without self-checkout visualization.\n");
    }

    return true;
}

void cleanup() {
    if (all_customers) {
        for (int i = 0; i < total_customers; i++) {
            if (all_customers[i]) {
                free(all_customers[i]);
            }
        }
        free(all_customers);
    }

    if (all_cashiers) {
        for (int i = 0; i < cashier_count; i++) {
            if (all_cashiers[i]) {
                if (all_cashiers[i]->queue) {
                    pthread_mutex_destroy(&all_cashiers[i]->queue->lock);
                    pthread_cond_destroy(&all_cashiers[i]->queue->cond);
                    free(all_cashiers[i]->queue);
                }
                free(all_cashiers[i]);
            }
        }
        free(all_cashiers);
    }
    
    if (all_kiosks) {
        for (int i = 0; i < selfcheckout_count; i++) {
            if (all_kiosks[i]) {
                if (all_kiosks[i]->queue) {
                    pthread_mutex_destroy(&all_kiosks[i]->queue->lock);
                    pthread_cond_destroy(&all_kiosks[i]->queue->cond);
                    free(all_kiosks[i]->queue);
                }
                free(all_kiosks[i]);
            }
        }
        free(all_kiosks);
    }

    SDL_DestroyTexture(customer_texture);
    SDL_DestroyTexture(cashier_texture);
    if (selfcheckout_texture) {
        SDL_DestroyTexture(selfcheckout_texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void create_tables_lanes(SDL_Renderer* renderer, Cashier** cashiers, int cashier_count) {
    for (int i = 0; i < cashier_count; i++) {
        Cashier* cashier = cashiers[i];
        SDL_Rect tableRect = {cashier->x - TABLE_WIDTH/2, cashier->y, TABLE_WIDTH, TABLE_HEIGHT};
        
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); 
        SDL_RenderFillRect(renderer, &tableRect);

        SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255); 
        for (int y = tableRect.y; y < tableRect.y + tableRect.h; y += 10) {
            SDL_RenderDrawLine(renderer, tableRect.x, y, tableRect.x + tableRect.w, y);
        }
        
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); 
        
        int x_left = cashier->x - TABLE_WIDTH/2 - 20;
        int x_right = cashier->x + TABLE_WIDTH/2 + 20;
        int y_start = cashier->y + TABLE_HEIGHT;
        int y_end = SCREEN_HEIGHT;
        
        for (int y = y_start; y < y_end; y += 10) {
            if (i > 0) { 
                SDL_RenderDrawLine(renderer, x_left, y, x_left, y + 5);
            }
            
            if (i < cashier_count - 1) { 
                SDL_RenderDrawLine(renderer, x_right, y, x_right, y + 5);
            }
        }
    }
    
    if (selfcheckout_texture && selfcheckout_count > 0) {
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderDrawLine(renderer, 
                          SCREEN_WIDTH * 3/4, 50,
                          SCREEN_WIDTH * 3/4, SCREEN_HEIGHT - 50);
        
        SDL_Color labelColor = {0, 0, 0, 255};
        SDL_Texture* labelTexture = renderText(renderer, font, "Self-Checkout Area", labelColor);
        int labelWidth, labelHeight;
        SDL_QueryTexture(labelTexture, NULL, NULL, &labelWidth, &labelHeight);
        SDL_Rect labelRect = {SCREEN_WIDTH * 7/8 - labelWidth/2, 50, labelWidth, labelHeight};
        SDL_RenderCopy(renderer, labelTexture, NULL, &labelRect);
        SDL_DestroyTexture(labelTexture);
        
        int available_height = SCREEN_HEIGHT - 150; 
        int row_height = available_height / selfcheckout_count;
        
        for (int i = 0; i < selfcheckout_count; i++) {
            int kiosk_center_x = SCREEN_WIDTH * 7/8; 
            int row_center_y = 120 + (row_height * i) + (row_height / 2);
            int kiosk_x = kiosk_center_x - SELFCHECKOUT_WIDTH/2;
            int kiosk_y = row_center_y - SELFCHECKOUT_HEIGHT/2;
            
            SDL_Rect kioskRect = {kiosk_x, kiosk_y, SELFCHECKOUT_WIDTH, SELFCHECKOUT_HEIGHT};
            SDL_RenderCopy(renderer, selfcheckout_texture, NULL, &kioskRect);
            
            char kioskText[20];
            sprintf(kioskText, "Kiosk %d", i + 1);
            SDL_Texture* kioskTextTexture = renderText(renderer, font, kioskText, (SDL_Color){0, 0, 0, 255});
            int textWidth, textHeight;
            SDL_QueryTexture(kioskTextTexture, NULL, NULL, &textWidth, &textHeight);
            SDL_Rect textRect = {kiosk_x - textWidth - 20, 
                               row_center_y - textHeight/2, 
                               textWidth, textHeight};
            SDL_RenderCopy(renderer, kioskTextTexture, NULL, &textRect);
            SDL_DestroyTexture(kioskTextTexture);
            
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255); 
            
            if (i > 0) {
                int row_top = row_center_y - row_height/2;
                for (int x = SCREEN_WIDTH * 3/4 + 20; x < SCREEN_WIDTH - 20; x += 10) {
                    SDL_RenderDrawLine(renderer, x, row_top, x + 5, row_top);
                }
            }
            
            if (i < selfcheckout_count - 1) {
                int row_bottom = row_center_y + row_height/2;
                for (int x = SCREEN_WIDTH * 3/4 + 20; x < SCREEN_WIDTH - 20; x += 10) {
                    SDL_RenderDrawLine(renderer, x, row_bottom, x + 5, row_bottom);
                }
            }
            
            int queue_start_x = SCREEN_WIDTH * 3/4 + 20;
            int queue_end_x = kiosk_x - 30; 
            
            for (int x = queue_start_x; x < queue_end_x; x += 10) {
                SDL_RenderDrawLine(renderer, 
                                  x, row_center_y - 20, 
                                  x, row_center_y - 15);
                                  
                SDL_RenderDrawLine(renderer, 
                                  x, row_center_y + 20, 
                                  x, row_center_y + 15);
            }
        }
    }
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

void render() {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    
    Uint32 current_time;
    if (all_customers_served) {
        current_time = simulation_end_time - simulation_start_time;
    } else {
        current_time = SDL_GetTicks() - simulation_start_time;
    }
    
    char tickText[50];
    sprintf(tickText, "Time Elapsed: %d seconds", current_time / 1000);
    SDL_Texture* tickTexture = renderText(renderer, font, tickText, (SDL_Color){0, 0, 0, 255});
    SDL_Rect tickRect = {10, 10, 200, 40};
    SDL_RenderCopy(renderer, tickTexture, NULL, &tickRect);
    SDL_DestroyTexture(tickTexture);
    
    char paramsText[100];
    sprintf(paramsText, "Cashiers: %d | Customers: %d | Self-Checkout: %d", 
            cashier_count, total_customers, selfcheckout_count);
    SDL_Texture* paramsTexture = renderText(renderer, font, paramsText, (SDL_Color){0, 0, 0, 255});
    SDL_Rect paramsRect = {SCREEN_WIDTH - 400, 10, 390, 40};
    SDL_RenderCopy(renderer, paramsTexture, NULL, &paramsRect);
    SDL_DestroyTexture(paramsTexture);
    
    char servedText[50];
    sprintf(servedText, "Customers Served: %d / %d", customers_served, total_customers);
    SDL_Texture* servedTexture = renderText(renderer, font, servedText, (SDL_Color){0, 0, 0, 255});
    SDL_Rect servedRect = {SCREEN_WIDTH / 2 - 150, 10, 300, 40};
    SDL_RenderCopy(renderer, servedTexture, NULL, &servedRect);
    SDL_DestroyTexture(servedTexture);
    
    if (all_customers_served) {
        char statsText[100];
        sprintf(statsText, "SIMULATION COMPLETE - ALL CUSTOMERS SERVED IN %d SECONDS", (current_time / 1000));
        SDL_Texture* completedTexture = renderText(renderer, font, statsText, (SDL_Color){0, 128, 0, 255});
        SDL_Rect completedRect = {SCREEN_WIDTH / 2 - 350, 70, 700, 40};
        SDL_RenderCopy(renderer, completedTexture, NULL, &completedRect);
        SDL_DestroyTexture(completedTexture);
        
        int totalEntities = cashier_count + selfcheckout_count;
        int availableHeight = SCREEN_HEIGHT - 220;
        
        TTF_Font* statFont = font;
        int rowHeight = 40;
        int statsPerColumn = 0;
        int numColumns = 1;
        
        TTF_Font* smallFont = NULL;
        if (totalEntities > 10) {
            smallFont = TTF_OpenFont("arial.ttf", 18);
            statFont = smallFont ? smallFont : font;
            rowHeight = 30;
        }
        
        statsPerColumn = availableHeight / rowHeight;
        if (statsPerColumn < 1) statsPerColumn = 1;
        
        numColumns = (totalEntities + statsPerColumn - 1) / statsPerColumn;
        if (numColumns < 1) numColumns = 1;
        if (numColumns > 3) numColumns = 3; 
        
        int columnWidth = SCREEN_WIDTH / (numColumns + 1);
        
        const char* statsTitle = "CASHIER PERFORMANCE STATISTICS";
        SDL_Texture* statsTitleTexture = renderText(renderer, font, statsTitle, (SDL_Color){0, 0, 0, 255});
        SDL_Rect statsTitleRect = {SCREEN_WIDTH / 2 - 200, 120, 400, 30};
        SDL_RenderCopy(renderer, statsTitleTexture, NULL, &statsTitleRect);
        SDL_DestroyTexture(statsTitleTexture);
        
        const char* tableHeader = "Cashier ID | Service Speed | Customers Served | Items Processed | Avg Items/Customer";
        SDL_Texture* headerTexture = renderText(renderer, statFont, tableHeader, (SDL_Color){50, 50, 50, 255});
        SDL_Rect headerRect = {SCREEN_WIDTH / 2 - columnWidth/2, 160, columnWidth, 30};
        SDL_RenderCopy(renderer, headerTexture, NULL, &headerRect);
        SDL_DestroyTexture(headerTexture);
        
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawLine(renderer, SCREEN_WIDTH / 2 - columnWidth/2, 195, 
                          SCREEN_WIDTH / 2 + columnWidth/2, 195);
        
        int total_customers_processed = 0;
        int total_items_processed = 0;
        float fastest_cashier_speed = FLT_MAX;
        float slowest_cashier_speed = 0;
        int fastest_cashier_id = 0;
        int slowest_cashier_id = 0;
        
        for (int col = 0; col < numColumns && col * statsPerColumn < cashier_count; col++) {
            int startIndex = col * statsPerColumn;
            int endIndex = startIndex + statsPerColumn;
            if (endIndex > cashier_count) endIndex = cashier_count;
            
            int colX = (SCREEN_WIDTH / (numColumns + 1)) * (col + 1);
            int yPos = 210;
            
            for (int i = startIndex; i < endIndex; i++) {
                char cashierStatsText[200];
                float avg_items_per_customer = 0;
                if (all_cashiers[i]->total_customers_served > 0) {
                    avg_items_per_customer = (float)all_cashiers[i]->total_items_processed / all_cashiers[i]->total_customers_served;
                }
                
                total_customers_processed += all_cashiers[i]->total_customers_served;
                total_items_processed += all_cashiers[i]->total_items_processed;
                
                if (all_cashiers[i]->avg_service_time_per_item < fastest_cashier_speed) {
                    fastest_cashier_speed = all_cashiers[i]->avg_service_time_per_item;
                    fastest_cashier_id = all_cashiers[i]->id;
                }
                
                if (all_cashiers[i]->avg_service_time_per_item > slowest_cashier_speed) {
                    slowest_cashier_speed = all_cashiers[i]->avg_service_time_per_item;
                    slowest_cashier_id = all_cashiers[i]->id;
                }
                
                sprintf(cashierStatsText, "%5d      |   %.2f sec/item  |      %3d         |      %4d        |      %.1f", 
                        all_cashiers[i]->id, 
                        all_cashiers[i]->avg_service_time_per_item,
                        all_cashiers[i]->total_customers_served,
                        all_cashiers[i]->total_items_processed,
                        avg_items_per_customer);
                        
                SDL_Texture* statsTexture = renderText(renderer, statFont, cashierStatsText, (SDL_Color){0, 0, 128, 255});
                SDL_Rect statsRect = {colX - columnWidth/2, yPos, columnWidth, rowHeight};
                SDL_RenderCopy(renderer, statsTexture, NULL, &statsRect);
                SDL_DestroyTexture(statsTexture);
                
                yPos += rowHeight;
            }
        }
        
        if (selfcheckout_count > 0) {
            int kioskStartY = 210 + ((cashier_count + numColumns - 1) / numColumns) * rowHeight + 30;
            int yPos = kioskStartY;
            
            char kioskTitle[100];
            sprintf(kioskTitle, "SELF-CHECKOUT KIOSK STATISTICS:");
            SDL_Texture* kioskTitleTexture = renderText(renderer, statFont, kioskTitle, (SDL_Color){0, 0, 0, 255});
            SDL_Rect kioskTitleRect = {SCREEN_WIDTH / 2 - 200, kioskStartY, 400, 30};
            SDL_RenderCopy(renderer, kioskTitleTexture, NULL, &kioskTitleRect);
            SDL_DestroyTexture(kioskTitleTexture);
            kioskStartY += rowHeight;
            
            for (int col = 0; col < numColumns && col * statsPerColumn < selfcheckout_count; col++) {
                int startIndex = col * statsPerColumn;
                int endIndex = startIndex + statsPerColumn;
                if (endIndex > selfcheckout_count) endIndex = selfcheckout_count;
                
                int colX = (SCREEN_WIDTH / (numColumns + 1)) * (col + 1);
                int yPos = kioskStartY;
                
                for (int i = startIndex; i < endIndex; i++) {
                    char kioskStatsText[200];
                    float avg_items_per_customer = 0;
                    if (all_kiosks[i]->total_customers_served > 0) {
                        avg_items_per_customer = (float)all_kiosks[i]->total_items_processed / all_kiosks[i]->total_customers_served;
                    }
                    
                    sprintf(kioskStatsText, "Kiosk %d: %d customers, %d items (%.1f items/customer), %.1f sec/item", 
                            all_kiosks[i]->id, 
                            all_kiosks[i]->total_customers_served,
                            all_kiosks[i]->total_items_processed,
                            avg_items_per_customer,
                            all_kiosks[i]->avg_service_time_per_item);
                            
                    SDL_Texture* statsTexture = renderText(renderer, statFont, kioskStatsText, (SDL_Color){0, 0, 128, 255});
                    SDL_Rect statsRect = {colX - columnWidth/2, yPos, columnWidth, rowHeight};
                    SDL_RenderCopy(renderer, statsTexture, NULL, &statsRect);
                    SDL_DestroyTexture(statsTexture);
                    
                    yPos += rowHeight;
                }
            }
            
            yPos = kioskStartY + ((selfcheckout_count + numColumns - 1) / numColumns) * rowHeight + 20;
            
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderDrawLine(renderer, SCREEN_WIDTH / 2 - 400, yPos, SCREEN_WIDTH / 2 + 400, yPos);
            yPos += 30;
            
            char summaryTitle[100];
            sprintf(summaryTitle, "SUMMARY STATISTICS:");
            SDL_Texture* summaryTitleTexture = renderText(renderer, statFont, summaryTitle, (SDL_Color){0, 0, 0, 255});
            SDL_Rect summaryTitleRect = {SCREEN_WIDTH / 2 - 200, yPos, 400, rowHeight-10};
            SDL_RenderCopy(renderer, summaryTitleTexture, NULL, &summaryTitleRect);
            SDL_DestroyTexture(summaryTitleTexture);
            yPos += rowHeight;
            
            if (yPos + 3*rowHeight < SCREEN_HEIGHT - 60) {
                char fastestCashier[100];
                sprintf(fastestCashier, "Fastest Cashier: #%d (%.2f sec/item)", fastest_cashier_id, fastest_cashier_speed);
                SDL_Texture* fastestTexture = renderText(renderer, statFont, fastestCashier, (SDL_Color){0, 128, 0, 255});
                SDL_Rect fastestRect = {SCREEN_WIDTH / 2 - 200, yPos, 400, rowHeight-10};
                SDL_RenderCopy(renderer, fastestTexture, NULL, &fastestRect);
                SDL_DestroyTexture(fastestTexture);
                yPos += rowHeight;
                
                char slowestCashier[100];
                sprintf(slowestCashier, "Slowest Cashier: #%d (%.2f sec/item)", slowest_cashier_id, slowest_cashier_speed);
                SDL_Texture* slowestTexture = renderText(renderer, statFont, slowestCashier, (SDL_Color){128, 0, 0, 255});
                SDL_Rect slowestRect = {SCREEN_WIDTH / 2 - 200, yPos, 400, rowHeight-10};
                SDL_RenderCopy(renderer, slowestTexture, NULL, &slowestRect);
                SDL_DestroyTexture(slowestTexture);
                yPos += rowHeight;
                
                char avgServiceTime[100];
                float avg_time_per_item = 0;
                if (total_items_processed > 0) {
                    avg_time_per_item = ((float)current_time / 1000.0f) / total_items_processed;
                }
                sprintf(avgServiceTime, "Average Time Per Item: %.2f seconds", avg_time_per_item);
                SDL_Texture* avgTimeTexture = renderText(renderer, statFont, avgServiceTime, (SDL_Color){0, 0, 0, 255});
                SDL_Rect avgTimeRect = {SCREEN_WIDTH / 2 - 200, yPos, 400, rowHeight-10};
                SDL_RenderCopy(renderer, avgTimeTexture, NULL, &avgTimeRect);
                SDL_DestroyTexture(avgTimeTexture);
            }
        } else {
            int summaryY = 210 + (cashier_count * rowHeight) + 30;
            
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderDrawLine(renderer, SCREEN_WIDTH / 2 - 400, summaryY, SCREEN_WIDTH / 2 + 400, summaryY);
            summaryY += 30;
            
            char summaryTitle[100];
            sprintf(summaryTitle, "SUMMARY STATISTICS:");
            SDL_Texture* summaryTitleTexture = renderText(renderer, statFont, summaryTitle, (SDL_Color){0, 0, 0, 255});
            SDL_Rect summaryTitleRect = {SCREEN_WIDTH / 2 - 200, summaryY, 400, rowHeight-10};
            SDL_RenderCopy(renderer, summaryTitleTexture, NULL, &summaryTitleRect);
            SDL_DestroyTexture(summaryTitleTexture);
            summaryY += rowHeight;
            
            if (summaryY + 3*rowHeight < SCREEN_HEIGHT - 60) {
                char fastestCashier[100];
                sprintf(fastestCashier, "Fastest Cashier: #%d (%.2f sec/item)", fastest_cashier_id, fastest_cashier_speed);
                SDL_Texture* fastestTexture = renderText(renderer, statFont, fastestCashier, (SDL_Color){0, 128, 0, 255});
                SDL_Rect fastestRect = {SCREEN_WIDTH / 2 - 200, summaryY, 400, rowHeight-10};
                SDL_RenderCopy(renderer, fastestTexture, NULL, &fastestRect);
                SDL_DestroyTexture(fastestTexture);
                summaryY += rowHeight;
                
                char slowestCashier[100];
                sprintf(slowestCashier, "Slowest Cashier: #%d (%.2f sec/item)", slowest_cashier_id, slowest_cashier_speed);
                SDL_Texture* slowestTexture = renderText(renderer, statFont, slowestCashier, (SDL_Color){128, 0, 0, 255});
                SDL_Rect slowestRect = {SCREEN_WIDTH / 2 - 200, summaryY, 400, rowHeight-10};
                SDL_RenderCopy(renderer, slowestTexture, NULL, &slowestRect);
                SDL_DestroyTexture(slowestTexture);
                summaryY += rowHeight;
                
                char avgServiceTime[100];
                float avg_time_per_item = 0;
                if (total_items_processed > 0) {
                    avg_time_per_item = ((float)current_time / 1000.0f) / total_items_processed;
                }
                sprintf(avgServiceTime, "Average Time Per Item: %.2f seconds", avg_time_per_item);
                SDL_Texture* avgTimeTexture = renderText(renderer, statFont, avgServiceTime, (SDL_Color){0, 0, 0, 255});
                SDL_Rect avgTimeRect = {SCREEN_WIDTH / 2 - 200, summaryY, 400, rowHeight-10};
                SDL_RenderCopy(renderer, avgTimeTexture, NULL, &avgTimeRect);
                SDL_DestroyTexture(avgTimeTexture);
            }
        }
        
        const char* closingNote = "Close this window to exit the simulation.";
        SDL_Texture* noteTexture = renderText(renderer, font, closingNote, (SDL_Color){100, 100, 100, 255});
        int noteWidth, noteHeight;
        SDL_QueryTexture(noteTexture, NULL, NULL, &noteWidth, &noteHeight);
        SDL_Rect noteRect = {SCREEN_WIDTH / 2 - noteWidth/2, SCREEN_HEIGHT - 40, noteWidth, 30};
        SDL_RenderCopy(renderer, noteTexture, NULL, &noteRect);
        SDL_DestroyTexture(noteTexture);
        
        if (smallFont) {
            TTF_CloseFont(smallFont);
        }
    } 
    else {
        pthread_mutex_lock(&visualization_lock);
        
        draw_tables_and_lanes(renderer, all_cashiers, cashier_count);
        
        for (int i = 0; i < cashier_count; i++) {
            Cashier* cashier = all_cashiers[i];
            
            SDL_Rect cashierRect = {cashier->x - CUSTOMER_SIZE/2, cashier->y - CUSTOMER_SIZE, CUSTOMER_SIZE, CUSTOMER_SIZE};
            SDL_RenderCopy(renderer, cashier_texture, NULL, &cashierRect);
            
            char cashierIdText[50];
            sprintf(cashierIdText, "%d (%.1f s/item)", cashier->id, cashier->avg_service_time_per_item);
            SDL_Texture* cashierIdTexture = renderText(renderer, font, cashierIdText, (SDL_Color){0, 0, 0, 255});
            SDL_Rect cashierIdRect = {cashier->x - 60, cashier->y - CUSTOMER_SIZE - 30, 120, 20};
            SDL_RenderCopy(renderer, cashierIdTexture, NULL, &cashierIdRect);
            SDL_DestroyTexture(cashierIdTexture);
            
            int visualQueueSize = 0;
            for (int j = 0; j < total_customers; j++) {
                if (all_customers[j]->is_active && 
                    all_customers[j]->visual_state == QUEUED && 
                    all_customers[j]->cashier_id == cashier->id) {
                    visualQueueSize++;
                }
            }
            
            char queueText[20];
            sprintf(queueText, "Queue: %d", visualQueueSize);
            SDL_Texture* queueTexture = renderText(renderer, font, queueText, (SDL_Color){0, 0, 0, 255});
            SDL_Rect queueRect = {cashier->x - 40, cashier->y + TABLE_HEIGHT + 10, 80, 20};
            SDL_RenderCopy(renderer, queueTexture, NULL, &queueRect);
            SDL_DestroyTexture(queueTexture);
        }
        
        for (int i = 0; i < total_customers; i++) {
            Customer* c = all_customers[i];
            if (!c->is_active) continue;
            
            SDL_Rect customerRect = {c->x - CUSTOMER_SIZE/2, c->y - CUSTOMER_SIZE, CUSTOMER_SIZE, CUSTOMER_SIZE};
            SDL_RenderCopy(renderer, customer_texture, NULL, &customerRect);
            
            char customerIdText[20];
            sprintf(customerIdText, "%d (%d items)", c->id, c->items);
            SDL_Texture* customerIdTexture = renderText(renderer, font, customerIdText, (SDL_Color){0, 0, 0, 255});
            SDL_Rect customerIdRect = {c->x - 50, c->y - CUSTOMER_SIZE - 20, 100, 20};
            SDL_RenderCopy(renderer, customerIdTexture, NULL, &customerIdRect);
            SDL_DestroyTexture(customerIdTexture);
            
            if ((c->visual_state == BEING_SERVED && c->has_reached_cashier) || 
                (c->visual_state == BEING_SERVED && c->has_reached_kiosk)) {
                Uint32 time_elapsed = SDL_GetTicks() - c->service_start_time;
                Uint32 time_remaining = 0;
                
                if (c->cashier_id > 0 && c->cashier_id <= cashier_count) {
                    Cashier* cashier = all_cashiers[c->cashier_id - 1];
                    float total_service_time = cashier->avg_service_time_per_item * c->items * 1000;
                    if (time_elapsed < total_service_time) {
                        time_remaining = (Uint32)(total_service_time - time_elapsed);
                    }
                } else if (c->kiosk_id > 0 && c->kiosk_id <= selfcheckout_count) {
                    SelfCheckout* kiosk = all_kiosks[c->kiosk_id - 1];
                    float total_service_time = kiosk->avg_service_time_per_item * c->items * 1000;
                    if (time_elapsed < total_service_time) {
                        time_remaining = (Uint32)(total_service_time - time_elapsed);
                    }
                }
                
                if (time_remaining > 0) {
                    char timeText[20];
                    sprintf(timeText, "%d s", (time_remaining / 1000) + 1);
                    SDL_Texture* timeTexture = renderText(renderer, font, timeText, (SDL_Color){255, 0, 0, 255});
                    SDL_Rect timeRect = {c->x - 15, c->y - CUSTOMER_SIZE - 40, 30, 20};
                    SDL_RenderCopy(renderer, timeTexture, NULL, &timeRect);
                    SDL_DestroyTexture(timeTexture);
                }
            }
        }
        
        pthread_mutex_unlock(&visualization_lock);
    }
    
    SDL_RenderPresent(renderer);
}

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