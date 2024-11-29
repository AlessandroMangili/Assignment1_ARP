#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> 
#include <ncurses.h>
#include <signal.h>
#include <unistd.h>
#include "helper.h"

WINDOW *input_window, *info_window, *windows[3][3]; 
FILE *debug, *errors;                               // File descriptors for the two log files
Drone drone;
pid_t wd_pid;
const char *symbols[3][3] = {                       // Symbols for the keyboard
    {"\\", "^", "/"},
    {"<", "S", ">"},
    {"/", "v", "\\"}
};

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
    draw_box(win, symbol);
}

// Update the information window
void update_info_window(Drone drone) {
    werase(info_window);
    box(info_window, 0, 0);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int middle_col = cols / 4;
    int middle_row = rows / 4;
    mvwprintw(info_window, middle_row - 2, middle_col - 7, "position {");
    mvwprintw(info_window, middle_row - 1, middle_col - 6, "x: %.6f", drone.pos_x);
    mvwprintw(info_window, middle_row, middle_col - 6, "y: %.6f", drone.pos_y);
    mvwprintw(info_window, middle_row + 1, middle_col - 7, "}");

    mvwprintw(info_window, middle_row + 3, middle_col - 7, "velocity {");
    mvwprintw(info_window, middle_row + 4, middle_col - 6, "x: %.6f", drone.vel_x);
    mvwprintw(info_window, middle_row + 5, middle_col - 6, "y: %.6f", drone.vel_y);
    mvwprintw(info_window, middle_row + 6, middle_col - 7, "}");

    mvwprintw(info_window, middle_row + 8, middle_col - 7, "force {");
    mvwprintw(info_window, middle_row + 9, middle_col - 6, "x: %.6f", drone.force_x);
    mvwprintw(info_window, middle_row + 10, middle_col - 6, "y: %.6f", drone.force_y);
    mvwprintw(info_window, middle_row + 11, middle_col - 7, "}");
    wrefresh(info_window);
}

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
    
    box(info_window, 0, 0);
    update_info_window(drone);
    mvwprintw(info_window, 0, 2, "Info output");
    wrefresh(info_window);
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
    LOG_TO_FILE(debug, "INCOMING SIGNAL");
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
    
    LOG_TO_FILE(debug, "Process started");

    initscr();
    start_color();
    cbreak();
    noecho();
    curs_set(0);

    // Inizializza i colori
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
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("sigaction");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        exit(EXIT_FAILURE);
    }

    int ch;
    while ((ch = getch()) != 'p' && ch != 'P') {
        LOG_TO_FILE(debug, "ASK");
        switch (ch) {
            case 'w': case 'W':
                handle_key_pressed(windows[0][0], symbols[0][0]);
                break;
            case 'e': case 'E':
                handle_key_pressed(windows[0][1], symbols[0][1]);
                break;
            case 'r': case 'R':
                handle_key_pressed(windows[0][2], symbols[0][2]);
                break;
            case 's': case 'S':
                handle_key_pressed(windows[1][0], symbols[1][0]);
                break;
            case 'd': case 'D':
                handle_key_pressed(windows[1][1], symbols[1][1]);
                break;
            case 'f': case 'F':
                handle_key_pressed(windows[1][2], symbols[1][2]);
                break;
            case 'x': case 'X':
                handle_key_pressed(windows[2][0], symbols[2][0]);
                break;
            case 'c': case 'C':
                handle_key_pressed(windows[2][1], symbols[2][1]);
                break;
            case 'v': case 'V':
                handle_key_pressed(windows[2][2], symbols[2][2]);
                break;
            default:
                break;
        }
    }
    LOG_TO_FILE(debug, "CLOSING");
    // Send the termination signal to the watchdog
    kill(wd_pid, SIGUSR2);

    // Clear the windows
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            delwin(windows[i][j]);
        }
    }
    delwin(input_window);
    delwin(info_window);
    endwin();

    // Close the files
    fclose(debug);
    fclose(errors);
    
    return 0;
}