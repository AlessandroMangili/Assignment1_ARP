#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <ncurses.h>
#include <signal.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <pthread.h>
#include "helper.h"

WINDOW *input_window, *info_window, *windows[3][3]; 
FILE *debug, *errors;                               // File descriptors for the two log files
Drone *drone;
pid_t wd_pid;
const char *symbols[3][3] = {                       // Symbols for the keyboard
    {"\\", "^", "/"},
    {"<", "D", ">"},
    {"/", "v", "\\"}
};
pthread_mutex_t info_window_mutex;                  // Mutex for synchronizing ncurses

// Update the information window
void update_info_window() {
    werase(info_window);
    box(info_window, 0, 0);
    mvwprintw(info_window, 0, 2, "Info display");

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int middle_col = cols / 4;
    int middle_row = rows / 4;
    mvwprintw(info_window, middle_row - 2, middle_col - 7, "position {");
    mvwprintw(info_window, middle_row - 1, middle_col - 6, "x: %.6f", drone->pos_x);
    mvwprintw(info_window, middle_row, middle_col - 6, "y: %.6f", drone->pos_y);
    mvwprintw(info_window, middle_row + 1, middle_col - 7, "}");

    mvwprintw(info_window, middle_row + 3, middle_col - 7, "velocity {");
    mvwprintw(info_window, middle_row + 4, middle_col - 6, "x: %.6f", drone->vel_x);
    mvwprintw(info_window, middle_row + 5, middle_col - 6, "y: %.6f", drone->vel_y);
    mvwprintw(info_window, middle_row + 6, middle_col - 7, "}");

    mvwprintw(info_window, middle_row + 8, middle_col - 7, "force {");
    mvwprintw(info_window, middle_row + 9, middle_col - 6, "x: %.6f", drone->force_x);
    mvwprintw(info_window, middle_row + 10, middle_col - 6, "y: %.6f", drone->force_y);
    mvwprintw(info_window, middle_row + 11, middle_col - 7, "}");
    wrefresh(info_window);
}

// Continuously update the information window"
void *update_info_thread() {
    while (1) {
        pthread_mutex_lock(&info_window_mutex);
        update_info_window();
        pthread_mutex_unlock(&info_window_mutex);
        usleep(50000);
    }
}

// Draw the box for the key
void draw_box(WINDOW *win, const char *symbol) {
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", symbol);
    wrefresh(win);
}

// Change the color of the symbol for 0.2 seconds, simulating the key press effect
void handle_key_pressed(WINDOW *win, const char *symbol) {
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "%s", symbol);
    wrefresh(win);
    usleep(200000);
    wattroff(win, COLOR_PAIR(2));
    mvwprintw(win, 1, 2, "%s", symbol);
    wrefresh(win);
}

// Create the two windows and draw the keyboard
void create_keyboard_window(int rows, int cols) {
    input_window = newwin(rows, cols / 2, 0, 0);
    info_window = newwin(rows, cols / 2, 0, cols / 2);

    int start_y = (rows - (BOX_HEIGHT * 3)) / 2;
    int start_x = ((cols / 2) - (BOX_WIDTH * 3 )) / 2;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            windows[i][j] = derwin(input_window, BOX_HEIGHT, BOX_WIDTH, start_y + i * BOX_HEIGHT, start_x + j * BOX_WIDTH);
            refresh();
            draw_box(windows[i][j], symbols[i][j]);
        }
    }

    box(input_window, 0, 0);
    mvwprintw(input_window, 0, 2, "Input manager");
    mvwprintw(input_window, rows - 2, ((cols / 2) - 30) / 2, "Press 'P' to close the program");
    wrefresh(input_window);
}

// Resize the input window
void resize_windows() {
    endwin();

    refresh();
    clear();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    // Update the dimension of ncurse
    resize_term(rows, cols);

    clear();
    refresh();

    create_keyboard_window(rows, cols);
}

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGWINCH) {
        resize_windows();
    }
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }
    if (sig == SIGUSR2){
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");
        // Close the files
        fclose(errors);
        fclose(debug);
        // Clear the windows
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                delwin(windows[i][j]);
            }
        }
        delwin(input_window);
        delwin(info_window);
        endwin();
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    int rows, cols;
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

    if (argc < 2) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Converti il parametro passato in un file descriptor
    int pipe_fd = atoi(argv[1]);
    
    LOG_TO_FILE(debug, "Process started");

    // Open the shared memory
    const int SIZE = 4096;
    const char *shared_memory = "/drone_memory";
    int i, mem_fd;
    mem_fd = shm_open(shared_memory, O_RDONLY, 0666);
    if (mem_fd == -1) {
        perror("Opening the shared memory \n");
        LOG_TO_FILE(errors, "Error in opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, SIZE, PROT_READ, MAP_SHARED, mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Map failed");
        LOG_TO_FILE(errors, "Map Failed");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    initscr();
    start_color();
    cbreak();
    noecho();
    curs_set(0);

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_GREEN);

    getmaxyx(stdscr, rows, cols);
    create_keyboard_window(rows, cols);

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
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
    // Initialize and create the thread to continuously update the information window
    pthread_mutex_init(&info_window_mutex, NULL);
    pthread_t info_thread;
    if (pthread_create(&info_thread, NULL, update_info_thread, NULL) != 0) {
        perror("pthread_create");
        LOG_TO_FILE(errors, "Error on creating the pthread");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    int ch;
    while ((ch = getch()) != 'p' && ch != 'P') {
        if (ch != EOF) {
            write(pipe_fd, &ch, sizeof(ch));
        }
    }
    // Send the termination signal to the watchdog
    kill(wd_pid, SIGUSR2);

    // Join the thread and destroy the mutex
    pthread_join(info_thread, NULL);
    pthread_mutex_destroy(&info_window_mutex);

    // Clear the windows
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            delwin(windows[i][j]);
        }
    }
    delwin(input_window);
    delwin(info_window);
    endwin();

    // Unlink the shared memory, close the file descriptor, and unmap the shared memory region
    if (shm_unlink(shared_memory) == -1) {
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