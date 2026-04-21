#include "uthreads.h"
#include <iostream>
#include <vector>
#include <unistd.h>
#include <cstdlib>
#include <termios.h>
#include <fcntl.h>

// --- GAME SETTINGS ---
const int WIDTH = 40;
const int HEIGHT = 20;
const int NUM_ENEMIES = 3;

// --- VISUALS ---
const char* CLR_RESET  = "\033[0m";
const char* CLR_PLAYER = "\033[1;32m"; // Bright Green
const char* CLR_ENEMY  = "\033[1;31m"; // Bright Red
const char* CLR_WALL   = "\033[1;34m"; // Blue

// --- SHARED STATE ---
volatile char BOARD[HEIGHT][WIDTH];
volatile bool GAME_OVER = false;
volatile char PLAYER_INPUT = ' '; // Shared between Main and Player Thread

struct Bike {
    int id;
    int r, c;
    int dr, dc; // Direction vector
    bool is_player;
    bool alive;
};

// Global bike states
Bike bikes[NUM_ENEMIES + 1]; 

// --- KEYBOARD HANDLING (Non-blocking) ---
int kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

void init_game() {
    // 1. Clear Board
    for(int i=0; i<HEIGHT; i++) {
        for(int j=0; j<WIDTH; j++) {
            if (i==0 || i==HEIGHT-1 || j==0 || j==WIDTH-1) 
                BOARD[i][j] = '#'; // Walls
            else 
                BOARD[i][j] = ' '; // Empty
        }
    }

    // 2. Setup Player (Green)
    bikes[0] = {0, HEIGHT/2, 2, 0, 1, true, true}; // Start left, moving right
    
    // 3. Setup Enemies (Red)
    // Enemy 1: Top, moving Down
    bikes[1] = {1, 2, WIDTH/2, 1, 0, false, true};
    // Enemy 2: Bottom, moving Up
    bikes[2] = {2, HEIGHT-3, WIDTH/2, -1, 0, false, true};
    // Enemy 3: Right, moving Left
    bikes[3] = {3, HEIGHT/2, WIDTH-3, 0, -1, false, true};
}

void print_game() {
    std::system("clear");
    for(int i=0; i<HEIGHT; i++) {
        for(int j=0; j<WIDTH; j++) {
            char cell = BOARD[i][j];
            if (cell == '#') std::cout << CLR_WALL << "#" << CLR_RESET;
            else if (cell == '@') std::cout << CLR_PLAYER << "@" << CLR_RESET; // Player Trail
            else if (cell == '*') std::cout << CLR_ENEMY << "*" << CLR_RESET;  // Enemy Trail
            else if (cell == 'X') std::cout << "X"; // Crash
            else std::cout << " ";
        }
        std::cout << std::endl;
    }
    std::cout << "Controls: W/A/S/D to move. Don't crash!" << std::endl;
}

// --- THREAD LOGIC ---

void run_bike() {
    int tid = uthread_get_tid();
    // Map TID to bike index (TIDs start at 1)
    int id = tid - 1; 
    
    while (bikes[id].alive && !GAME_OVER) {
        int r = bikes[id].r;
        int c = bikes[id].c;

        // 1. UPDATE DIRECTION
        if (bikes[id].is_player) {
            // Player reads global input
            if (PLAYER_INPUT == 'w' && bikes[id].dr == 0) { bikes[id].dr = -1; bikes[id].dc = 0; }
            if (PLAYER_INPUT == 's' && bikes[id].dr == 0) { bikes[id].dr = 1;  bikes[id].dc = 0; }
            if (PLAYER_INPUT == 'a' && bikes[id].dc == 0) { bikes[id].dr = 0;  bikes[id].dc = -1;}
            if (PLAYER_INPUT == 'd' && bikes[id].dc == 0) { bikes[id].dr = 0;  bikes[id].dc = 1; }
            PLAYER_INPUT = ' '; // Reset input
        } else {
            // AI Logic: Simple Collision Avoidance
            int next_r = r + bikes[id].dr;
            int next_c = c + bikes[id].dc;
            
            // If facing a wall, try to turn
            if (BOARD[next_r][next_c] != ' ') {
                // Try turning 90 degrees (simple right-hand rule)
                int temp = bikes[id].dr;
                bikes[id].dr = bikes[id].dc;
                bikes[id].dc = -temp;
                
                // If that's also blocked, try the other way
                if (BOARD[r + bikes[id].dr][c + bikes[id].dc] != ' ') {
                    bikes[id].dr = -bikes[id].dr;
                    bikes[id].dc = -bikes[id].dc;
                }
            }
        }

        // 2. MOVE
        int next_r = r + bikes[id].dr;
        int next_c = c + bikes[id].dc;

        // 3. COLLISION CHECK
        if (BOARD[next_r][next_c] != ' ') {
            bikes[id].alive = false; // CRASH!
            if (bikes[id].is_player) GAME_OVER = true;
        } else {
            // Mark trail
            BOARD[next_r][next_c] = bikes[id].is_player ? '@' : '*';
            bikes[id].r = next_r;
            bikes[id].c = next_c;
        }

        // 4. YIELD
        uthread_sleep(0); 
    }
    uthread_terminate(tid);
}

int main() {
    uthread_init(100000); // 100ms quantum
    init_game();

    // Spawn 4 threads (1 Player, 3 Enemies)
    for(int i=0; i<NUM_ENEMIES + 1; i++) {
        uthread_spawn(run_bike);
    }

    while (!GAME_OVER) {
        // 1. Handle Input (Main thread acts as I/O controller)
        if (kbhit()) {
            char c = getchar();
            if (c == 'w' || c == 'a' || c == 's' || c == 'd') {
                PLAYER_INPUT = c;
            }
            if (c == 'q') GAME_OVER = true;
        }

        // 2. Render
        print_game();
        
        // 3. Wait/Speed Control
        usleep(250000); // 0.25s per frame

        // 4. Yield to let threads calculate moves
        uthread_sleep(0);

        // 5. Check for victory
        bool enemies_alive = false;
        for (int i = 1; i <= NUM_ENEMIES; ++i) {
            if (bikes[i].alive) {
                enemies_alive = true;
                break;
            }
        }
        if (!enemies_alive) {
            GAME_OVER = true;
        }
    }
    std::cout << CLR_RESET << "\nGAME OVER! ";
    if (!bikes[0].alive) std::cout << "You crashed!" << std::endl;
    else std::cout << "You survived!" << std::endl;

    uthread_terminate(0);
    return 0;
}
