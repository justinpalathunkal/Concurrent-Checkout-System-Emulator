# Concurrent Cashier System Simulator

## Motivation

Current shopping systems go for a sequential approach to how they divide customers which leaves room for optimization. This project aims to simulate real life scenarios where there are multiple cashiers, self checkout kiosks, and customers interacting concurrently with heuristics guiding customer traffic to the most optimal checkout times for all customers.

## Setup

There are various ways to setup SDL2 locally but my approach involved these steps:

1. Need to download MYSYS2 for the mingw in a linux environment
2. Download all SDL2 packages 
3. Run gcc src.c -o test.exe `pkg-config --cflags --libs sdl2 SDL2_ttf SDL2_image`
4. The simulator window should pop up and be ready to be run

## Demo

Take a look at the video within the repository to understand how the simulation plays 

https://github.com/user-attachments/assets/fe071b84-0a47-4830-8ce3-7ca06a3454af

