#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include "helper.h"

FILE *debug, *errors;           // File descriptors for the two log files
Game *game;

void draw_outer_box() {
    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0);
    mvprintw(0, 1, "Dimension of the window: %d x %d", game->max_x, game->max_y);
    attroff(COLOR_PAIR(1));
}

void render_obstacles(Obstacle obstacles[]) {
    attron(COLOR_PAIR(3));
    for(int i=0; i< N_OBS; i++){
        mvprintw(obstacles[i].pos_y, obstacles[i].pos_x, "#");
    }
    attroff(COLOR_PAIR(3));
}

void render_drone(float x, float y) {
    attron(COLOR_PAIR(4));
    mvprintw(y, x, "+");
    attroff(COLOR_PAIR(4));
}

// Resize the input window
void resize_window() {
    endwin();
    refresh();
    clear();

    getmaxyx(stdscr, game->max_y, game->max_x);
    resize_term(game->max_x, game->max_y);

    clear();
    refresh();

    draw_outer_box();
}

// Handler for the signal SIGWINCH
void resize_handler(int sig, siginfo_t *info, void *context) {
    if (sig == SIGWINCH) {
        resize_window();
    }
    if (sig == SIGUSR1) {
        
    }
    if (sig == SIGUSR2){
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");
        // Close the files
        fclose(errors);
        fclose(debug);
        endwin();
        exit(EXIT_FAILURE);
    }
}

int main() {
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    initscr(); 
    start_color();
    cbreak(); 
    noecho();
    curs_set(0);

    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = resize_handler;
    sigemptyset(&sa.sa_mask);
    // Set the signal handler for SIGWINCH
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGWINCH)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Open the shared memory
    Drone *drone;
    const char *drone_shared_memory = "/drone_memory";
    int drone_mem_fd = shm_open(drone_shared_memory, O_RDWR, 0666);
    if (drone_mem_fd == -1) {
        perror("Opening the shared memory \n");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, sizeof(drone), PROT_READ | PROT_WRITE, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Map failed");
        LOG_TO_FILE(errors, "Map Failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    const char *game_shared_memory = "/game_memory";
    int game_mem_fd = shm_open(game_shared_memory, O_CREAT | O_RDWR, 0666);
    if (game_mem_fd == -1) {
        perror("Error opening shared memory for handle the game info");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the size of the shared memory
    if (ftruncate(game_mem_fd, sizeof(game)) == -1) {
        perror("Error truncating shared memory for terminal dimensions");
        LOG_TO_FILE(errors, "Error in setting the size of the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    game = (Game *)mmap(0, sizeof(game), PROT_READ | PROT_WRITE, MAP_SHARED, game_mem_fd, 0);
    if (game == MAP_FAILED) {
        perror("Error mapping shared memory for terminal dimensions");
        LOG_TO_FILE(errors, "Map failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    getmaxyx(stdscr, game->max_y, game->max_x);

    while(1) {
        clear();
        draw_outer_box();
        render_drone(drone->pos_x, drone->pos_y);
        refresh();
        usleep(50000);
    }
    endwin();

    // Unlink the shared memory, close the file descriptor, and unmap the shared memory region
    if (shm_unlink(drone_shared_memory) == -1) {
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error in removing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    if (close(drone_mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    munmap(drone, sizeof(drone));

    // Unlink the shared memory, close the file descriptor, and unmap the shared memory region
    if (shm_unlink(game_shared_memory) == -1) {
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error in removing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    if (close(game_mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    munmap(game, sizeof(game));

    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}
