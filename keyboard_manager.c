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

// Routine for continuously updating the information window
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

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                delwin(windows[i][j]);
            }
        }
        delwin(input_window);
        delwin(info_window);
        endwin();

        // Close the files
        fclose(errors);
        fclose(debug);

        exit(EXIT_FAILURE);
    }
}

int open_shared_memory() {
    int mem_fd = shm_open(DRONE_SHARED_MEMORY, O_RDONLY, 0666);
    if (mem_fd == -1) {
        perror("Error opening the shared memory");
        LOG_TO_FILE(errors, "Error opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ, MAP_SHARED, mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Error mapping the shared memory");
        LOG_TO_FILE(errors, "Error mapping the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    return mem_fd;
}

void keyboard_manager(int server_write_fd) {
    int ch;
    while ((ch = getch()) != 'p' && ch != 'P') {
        if (ch != EOF) {
            write(server_write_fd, &ch, sizeof(ch));
        }
    }
}

int main(int argc, char* argv[]) {
    /* OPEN THE LOG FILES */
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("Error opening the errors file");
        exit(EXIT_FAILURE);
    }

    if (argc < 2) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* SETUP THE PIPE */
    int server_write_fd = atoi(argv[1]);

    /* OPEN SHARED MEMORY */
    int mem_fd = open_shared_memory();

    /* SETUP NCURSE */
    initscr();
    start_color();
    cbreak();
    noecho();
    curs_set(0);

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLACK, COLOR_GREEN);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    create_keyboard_window(rows, cols);

    /* SETUP SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Set the signal handler for SIGWINCH
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        perror("Error in sigaction(SIGWINCH)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGWINCH)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("Error in sigaction(SIGURS1)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("rror in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    
    /* START THREAD */
    // Initialize and create the thread to continuously update the information window
    pthread_mutex_init(&info_window_mutex, NULL);
    pthread_t info_thread;
    if (pthread_create(&info_thread, NULL, update_info_thread, NULL) != 0) {
        perror("Error creating the thread for update the info window");
        LOG_TO_FILE(errors, "Error creating the thread for update the info window");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    /* LAUNCH THE INPUT PROCESS */
    keyboard_manager(server_write_fd);

    /* END PROGRAM */
    // Send the termination signal to the watchdog
    kill(wd_pid, SIGUSR2);

    // Join the thread and destroy the mutex
    pthread_join(info_thread, NULL);
    pthread_mutex_destroy(&info_window_mutex);

    // Delete all the windows
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            delwin(windows[i][j]);
        }
    }
    delwin(input_window);
    delwin(info_window);
    endwin();

    // Close the file descriptor
    if (close(mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error in closing the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));

    // Close the files
    fclose(debug);
    fclose(errors);
    
    return 0;
}