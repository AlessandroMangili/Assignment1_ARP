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
int term_rows, term_cols;

void render_obstacles(Obstacle obstacles[]) {
    for(int i=0; i< N_OBS; i++){
        mvprintw(obstacles[i].pos_y, obstacles[i].pos_x, "#");
    }
}

void render_drone(float x, float y) {
    mvprintw(y, x, "+");
}

// Handler per il segnale SIGWINCH
void resize_handler(int sig, siginfo_t *info, void *context) {
    endwin();  // Termina temporaneamente ncurses
    refresh(); // Rientra in modalità ncurses
    clear();   // Pulisci lo schermo

    // Ottieni le nuove dimensioni del terminale
    getmaxyx(stdscr, term_rows, term_cols);

    // Aggiorna le dimensioni di ncurses
    resize_term(term_rows, term_cols);

    // Rinfresca la finestra principale
    clear();
    refresh();

    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0); // Disegna un bordo lungo lo schermo
    mvprintw(0, 1, "Dimension of the window: %d x %d", term_rows, term_cols);
    attroff(COLOR_PAIR(1));
    refresh();
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

    // Definizione colori
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);

    // Registra l'handler per il ridimensionamento
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;        // Usa SA_SIGINFO per gestire segnali dettagliati
    sa.sa_sigaction = resize_handler; // Registra l'handler
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    Drone *drone;
    const char *shared_memory = "/drone_memory";
    const int SIZE = 4096;
    int i, mem_fd;
    mem_fd = shm_open(shared_memory, O_RDWR, 0666); // open shared memory segment for read and write
    if (mem_fd == -1) {
        perror("Opening the shared memory \n");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    drone = (Drone *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0); // protocol write
    if (drone == MAP_FAILED) {
        perror("Map failed");
        LOG_TO_FILE(errors, "Map Failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    getmaxyx(stdscr, term_rows, term_cols);

    // Disegna i bordi iniziali
    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0); // Disegna un bordo lungo lo schermo
    mvprintw(0, 1, "Dimension of the window: %d x %d", term_rows, term_cols);
    attroff(COLOR_PAIR(1));
    refresh();

    while(1) {
        render_drone(drone->pos_x, drone->pos_y);
    }

    endwin();

    if (shm_unlink(shared_memory) == -1) { // Remove shared memory segment.
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error in removing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    munmap(drone, SIZE);
    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}
