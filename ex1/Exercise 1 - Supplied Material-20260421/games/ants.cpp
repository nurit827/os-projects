#include "uthreads.h"
#include <iostream>
#include <vector>
#include <unistd.h>
#include <cstdlib>

// --- SETTINGS ---
const int BOARD_SIZE = 20; 
const int ANT_NUMBER = 4;
const int SPEED_US = 150000; // 0.15 seconds delay

// --- VISUALS ---
const char* COLOR_RESET = "\033[0m";
const char* COLOR_FROZEN = "\033[1;34m"; // Blue for frozen
const char* COLOR_ACTIVE = "\033[1;32m"; // Green for active
const char* COLOR_BOARD  = "\033[1;30m"; // Dark Gray for trails

// --- GAME STATE ---
// 0=Up, 1=Right, 2=Down, 3=Left
const int DIR_X[] = {-1, 0, 1, 0};
const int DIR_Y[] = {0, 1, 0, -1};

static volatile char BOARD[BOARD_SIZE][BOARD_SIZE];
static volatile int ANT_POS[ANT_NUMBER][2];     // [id][row, col]
static volatile int ANT_DIR[ANT_NUMBER];        // Current direction
static volatile int ANT_TIDS[ANT_NUMBER];       // Store Thread IDs to block/resume
static volatile bool ANT_FROZEN[ANT_NUMBER];    // Track who is frozen

void init_game() {
    // 1. Clear Board
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            BOARD[i][j] = '.';

    // 2. Center Start (Harmonic)
    int mid = BOARD_SIZE / 2;
    for(int i=0; i<ANT_NUMBER; i++) {
        ANT_POS[i][0] = mid;
        ANT_POS[i][1] = mid;
        ANT_DIR[i] = i % 4; // Each faces a different way
        ANT_FROZEN[i] = false;
    }
}

void print_board() {
    std::system("clear");
    
    // Top Border
    std::cout << " +";
    for(int k=0; k<BOARD_SIZE; k++) std::cout << "--";
    std::cout << "+" << std::endl;

    for (int i = 0; i < BOARD_SIZE; ++i) {
        std::cout << " |";
        for (int j = 0; j < BOARD_SIZE; ++j) {
            int ant_here = -1;
            
            // Check for ants
            for (int k = 0; k < ANT_NUMBER; k++) {
                if (ANT_POS[k][0] == i && ANT_POS[k][1] == j) {
                    ant_here = k;
                    // If multiple ants are here, prioritize showing the active one
                    if (!ANT_FROZEN[k]) break; 
                }
            }

            if (ant_here != -1) {
                if (ANT_FROZEN[ant_here]) {
                    std::cout << COLOR_FROZEN << "X " << COLOR_RESET;
                } else {
                    std::cout << COLOR_ACTIVE << "O " << COLOR_RESET;
                }
            } else {
                // Draw trails
                if (BOARD[i][j] == '#') std::cout << COLOR_BOARD << "# " << COLOR_RESET;
                else std::cout << "  ";
            }
        }
        std::cout << "|" << std::endl;
    }
    
    // Bottom Border
    std::cout << " +";
    for(int k=0; k<BOARD_SIZE; k++) std::cout << "--";
    std::cout << "+" << std::endl;
    std::cout << "Status: " << COLOR_ACTIVE << "O (Active) " << COLOR_RESET 
              << COLOR_FROZEN << "X (Frozen)" << COLOR_RESET << std::endl;
}

void ant() {
    int tid = uthread_get_tid();
    
    // Find my internal ID (0..3) based on TID
    int id = -1;
    for(int i=0; i<ANT_NUMBER; i++) {
        if (ANT_TIDS[i] == tid) { id = i; break; }
    }
    if (id == -1) exit(-1); // Should never happen

    while (true) {
        int r = ANT_POS[id][0];
        int c = ANT_POS[id][1];
        int d = ANT_DIR[id];

        // --- 1. MOVE LOGIC (Langton's Ant) ---
        if (BOARD[r][c] == '.') {
            d = (d + 1) % 4;       // Turn Right
            BOARD[r][c] = '#';     // Mark Board
        } else {
            d = (d + 3) % 4;       // Turn Left
            BOARD[r][c] = '.';     // Clear Board
        }
        ANT_DIR[id] = d;

        // Calculate Next Step
        int next_r = r + DIR_X[d];
        int next_c = c + DIR_Y[d];

        // Wrap around (Torus world)
        if (next_r < 0) next_r = BOARD_SIZE - 1;
        if (next_r >= BOARD_SIZE) next_r = 0;
        if (next_c < 0) next_c = BOARD_SIZE - 1;
        if (next_c >= BOARD_SIZE) next_c = 0;

        // --- 2. INTERACTION LOGIC (Freeze Tag) ---
        for (int k = 0; k < ANT_NUMBER; k++) {
            if (k == id) continue; // Don't tag yourself

            // If another ant is on my target square
            if (ANT_POS[k][0] == next_r && ANT_POS[k][1] == next_c) {
                if (ANT_FROZEN[k]) {
                    // It's frozen: RESUME it
                    ANT_FROZEN[k] = false;
                    uthread_resume(ANT_TIDS[k]);
                } else {
                    // It's moving: BLOCK it
                    ANT_FROZEN[k] = true;
                    uthread_block(ANT_TIDS[k]);
                }
            }
        }

        // --- 3. EXECUTE MOVE ---
        ANT_POS[id][0] = next_r;
        ANT_POS[id][1] = next_c;

        uthread_sleep(0); // Yield
    }
}

int main() {
    uthread_init(100000); // Quantum size
    init_game();

    // Spawn and record TIDs
    for (int i = 0; i < ANT_NUMBER; i++) {
        ANT_TIDS[i] = uthread_spawn(ant);
    }

    while (true) {
        print_board();
        usleep(SPEED_US);
        
        // Main thread yields to let ants run
        // If we don't yield, the ants might starve depending on your scheduler
        uthread_sleep(0); 
    }
    return 0;
}