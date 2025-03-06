#include <pthread.h>

typedef enum {
    WAITING_TO_ENTER,
    MOVING_TO_QUEUE,
    QUEUED,
    BEING_SERVED,
    LEAVING,
    EXITED
} CustomerVisualState;

typedef enum {
    NONE,
    CASHIER,
    KIOSK
} CheckoutType;

typedef enum {
    INPUT_CASHIERS,
    INPUT_CUSTOMERS,
    SIMULATION_RUNNING
} ProgramState;

struct Node;
struct Queue;
struct Cashier;
struct SelfCheckout;
struct Customer;

typedef struct {
    CheckoutType type;
    int index;
} CheckoutOption;

typedef struct Customer {
    int id;
    int service_time;
    int items;
    CustomerVisualState visual_state;
    float x, y;                 
    float target_x, target_y;  
    int cashier_id;             
    int kiosk_id;               
    Uint32 service_start_time;  
    bool is_active;
    bool has_reached_cashier;   
    bool has_reached_kiosk;     
} Customer;

typedef struct Node {
    Customer* data;
    struct Node* next;
} Node;

typedef struct Queue {
    Node* front;
    Node* rear;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Queue;

typedef struct Cashier {
    int id;
    pthread_t thread;
    Queue* queue;
    float x, y;              
    bool is_serving;         
    Customer* current_customer; 
    float avg_service_time_per_item; 
    int total_items_processed;  
    int total_customers_served; 
} Cashier;

typedef struct SelfCheckout {
    int id;
    pthread_t thread;
    Queue* queue;
    float x, y;              
    bool is_serving;         
    Customer* current_customer; 
    float avg_service_time_per_item; 
    int total_items_processed; 
    int total_customers_served; 
} SelfCheckout;