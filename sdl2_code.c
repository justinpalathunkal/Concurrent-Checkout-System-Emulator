#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>

// Screen dimensions
#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 600

// Customer properties
#define CUSTOMER_SIZE 50
#define CUSTOMER_SPEED 2  // Movement speed

// Table properties
#define TABLE_WIDTH 100
#define TABLE_HEIGHT 200

int main(int argc, char *argv[]) {
    int tableCount, customerLimit, totalCustomersGenerated = 0;

    // Accept input from command line arguments
    if (argc != 3) {
        printf("Usage: %s <number_of_tables> <number_of_customers>\n", argv[0]);
        return -1;
    }

    tableCount = atoi(argv[1]);
    customerLimit = atoi(argv[2]);

    if (tableCount <= 0 || customerLimit <= 0) {
        printf("Invalid input. Tables and customers must be greater than 0.\n");
        return -1;
    }

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        SDL_Quit();
        return -1;
    }

    if (TTF_Init() == -1) {
        printf("TTF_Init failed! SDL_ttf Error: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    TTF_Font *font = TTF_OpenFont("arial.ttf", 24);
    if (!font) {
        printf("Failed to load font! SDL_ttf Error: %s\n", TTF_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("SDL2 Customers Moving Up",
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
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_Texture *userTexture = IMG_LoadTexture(renderer, "user.png");
    if (!userTexture) {
        printf("Failed to load user image! SDL_image Error: %s\n", IMG_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    SDL_Texture *clerkTexture = IMG_LoadTexture(renderer, "clerk.png");
    if (!clerkTexture) {
        printf("Failed to load clerk image! SDL_image Error: %s\n", IMG_GetError());
        SDL_DestroyTexture(userTexture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    srand(time(NULL));

    // Allocate tables and customers dynamically
    SDL_Rect *tables = malloc(sizeof(SDL_Rect) * tableCount);
    SDL_Rect *customers = malloc(sizeof(SDL_Rect) * tableCount);
    SDL_Rect *clerks = malloc(sizeof(SDL_Rect) * tableCount); // Clerk positions
    int *customerNumbers = malloc(sizeof(int) * tableCount);
    bool *waiting = malloc(sizeof(bool) * tableCount);
    Uint32 *waitStartTime = malloc(sizeof(Uint32) * tableCount);
    bool *moveFast = malloc(sizeof(bool) * tableCount);
    Uint32 *nextStartTime = malloc(sizeof(Uint32) * tableCount);
    bool *isActive = malloc(sizeof(bool) * tableCount);

    if (!tables || !customers || !clerks || !customerNumbers || !waiting || !waitStartTime || !moveFast || !nextStartTime || !isActive) {
        printf("Memory allocation failed!\n");
        SDL_DestroyTexture(userTexture);
        SDL_DestroyTexture(clerkTexture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return -1;
    }

    int globalCounter = 1;
    for (int i = 0; i < tableCount; i++) {
        tables[i].x = (SCREEN_WIDTH / (tableCount + 1)) * (i + 1) - TABLE_WIDTH / 2;
        tables[i].y = SCREEN_HEIGHT / 2;
        tables[i].w = TABLE_WIDTH;
        tables[i].h = TABLE_HEIGHT;

        customers[i].x = tables[i].x - CUSTOMER_SIZE - 10;
        customers[i].y = SCREEN_HEIGHT - CUSTOMER_SIZE;
        customers[i].w = CUSTOMER_SIZE;
        customers[i].h = CUSTOMER_SIZE;

        // Clerk positions (on top of the table)
        clerks[i].x = tables[i].x + TABLE_WIDTH / 2 - CUSTOMER_SIZE / 2; // Centered on the table
        clerks[i].y = tables[i].y - CUSTOMER_SIZE; // Above the table
        clerks[i].w = CUSTOMER_SIZE;
        clerks[i].h = CUSTOMER_SIZE;

        // Initialize customer numbers and active status
        if (totalCustomersGenerated < customerLimit) {
            customerNumbers[i] = globalCounter++;
            totalCustomersGenerated++; // Increment counter immediately
            isActive[i] = true;
        } else {
            customerNumbers[i] = 0;
            isActive[i] = false;
        }

        waiting[i] = false;
        waitStartTime[i] = 0;
        moveFast[i] = false;
        nextStartTime[i] = SDL_GetTicks() + (rand() % 3000);
    }

    SDL_Rect tickRect = {10, 10, 200, 40};
    bool running = true;
    SDL_Event event;

    bool allCustomersServed = false;
    Uint32 allServedTime = 0;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        // Customer movement logic
        for (int i = 0; i < tableCount; i++) {
            if (!isActive[i]) continue;

            if (SDL_GetTicks() < nextStartTime[i]) {
                continue;
            }

            if (!waiting[i] && !moveFast[i]) {
                if (customers[i].y + CUSTOMER_SIZE > tables[i].y + TABLE_HEIGHT / 2) {
                    customers[i].y -= CUSTOMER_SPEED;
                } else {
                    waiting[i] = true;
                    waitStartTime[i] = SDL_GetTicks();
                }
            } else if (waiting[i]) {
                if (SDL_GetTicks() - waitStartTime[i] >= 5000) {
                    waiting[i] = false;
                    moveFast[i] = true;
                }
            } else if (moveFast[i]) {
                customers[i].y -= CUSTOMER_SPEED * 3;
            }

            if (customers[i].y + CUSTOMER_SIZE < 0) {
                if (totalCustomersGenerated < customerLimit) {
                    customers[i].y = SCREEN_HEIGHT;
                    moveFast[i] = false;
                    waiting[i] = false;
                    nextStartTime[i] = SDL_GetTicks() + (rand() % 3000);
                    customerNumbers[i] = globalCounter++;
                    totalCustomersGenerated++; // Increment counter immediately
                } else {
                    isActive[i] = false; // Stop generating new customers
                }
            }
        }

        // Check if all customers have been served
        if (!allCustomersServed) {
            allCustomersServed = true;
            for (int i = 0; i < tableCount; i++) {
                if (isActive[i]) {
                    allCustomersServed = false;
                    break;
                }
            }
            if (allCustomersServed) {
                allServedTime = SDL_GetTicks(); // Record the time when all customers are served
            }
        }

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        // Tick Counter
        char tickText[30];
        if (allCustomersServed) {
            sprintf(tickText, "Time Elapsed: %d", allServedTime / 1000); // Freeze the clock
        } else {
            sprintf(tickText, "Time Elapsed: %d", SDL_GetTicks() / 1000); // Update the clock
        }
        SDL_Surface *tickSurface = TTF_RenderText_Solid(font, tickText, (SDL_Color){0, 0, 0, 255});
        SDL_Texture *tickTexture = SDL_CreateTextureFromSurface(renderer, tickSurface);
        SDL_RenderCopy(renderer, tickTexture, NULL, &tickRect);
        SDL_FreeSurface(tickSurface);
        SDL_DestroyTexture(tickTexture);

        // Display "ALL CUSTOMERS SERVED" message
        if (allCustomersServed) {
            char servedText[50];
            sprintf(servedText, "ALL CUSTOMERS SERVED IN %d SECONDS", (allServedTime / 1000));
            SDL_Surface *servedSurface = TTF_RenderText_Solid(font, servedText, (SDL_Color){0, 0, 0, 255});
            SDL_Texture *servedTexture = SDL_CreateTextureFromSurface(renderer, servedSurface);
            SDL_Rect servedRect = {SCREEN_WIDTH / 2 - 150, 10, 300, 40};
            SDL_RenderCopy(renderer, servedTexture, NULL, &servedRect);
            SDL_FreeSurface(servedSurface);
            SDL_DestroyTexture(servedTexture);
        }

        // Draw tables as grey conveyor belts
        for (int i = 0; i < tableCount; i++) {
            // Draw the table in grey
            SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); // Grey color
            SDL_RenderFillRect(renderer, &tables[i]);

            // Add horizontal lines to simulate a conveyor belt
            SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255); // Dark grey for lines
            for (int y = tables[i].y; y < tables[i].y + tables[i].h; y += 10) {
                SDL_RenderDrawLine(renderer, tables[i].x, y, tables[i].x + tables[i].w, y);
            }

            // Reset color to grey for the next table
            SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
        }

        // Draw clerks
        for (int i = 0; i < tableCount; i++) {
            SDL_RenderCopy(renderer, clerkTexture, NULL, &clerks[i]);
        }

        // Draw customers
        for (int i = 0; i < tableCount; i++) {
            if (isActive[i]) {
                SDL_RenderCopy(renderer, userTexture, NULL, &customers[i]);

                char customerNum[10];
                sprintf(customerNum, "%d", customerNumbers[i]);
                SDL_Surface *textSurface = TTF_RenderText_Solid(font, customerNum, (SDL_Color){0, 0, 0, 255});
                SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                SDL_Rect textPos = {customers[i].x + 15, customers[i].y - 30, textSurface->w, textSurface->h};
                SDL_RenderCopy(renderer, textTexture, NULL, &textPos);
                SDL_FreeSurface(textSurface);
                SDL_DestroyTexture(textTexture);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    // Free allocated memory
    free(tables);
    free(customers);
    free(clerks);
    free(customerNumbers);
    free(waiting);
    free(waitStartTime);
    free(moveFast);
    free(nextStartTime);
    free(isActive);

    // Cleanup SDL
    SDL_DestroyTexture(userTexture);
    SDL_DestroyTexture(clerkTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_CloseFont(font);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}