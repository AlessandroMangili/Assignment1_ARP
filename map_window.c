#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <errno.h>
#include "helper.h"

FILE *debug, *errors;           // File descriptors for the two log files
Game game;
Drone *drone;
int server_write_size_fd;            // File descriptor for sending the size of the map to the server
int n_obs;
int n_targ;
float *score;

void draw_outer_box() {
    attron(COLOR_PAIR(1));
    box(stdscr, 0, 0);
    mvprintw(0, 1, "Dimension of the window: %d x %d - score: %.2f", game.max_x, game.max_y, *score);
    attroff(COLOR_PAIR(1));
    refresh();
}

void render_obstacles(Object obstacles[]) {
    attron(COLOR_PAIR(3));
    for(int i = 0; i < n_obs; i++){
        if (obstacles[i].pos_y <= 0 || obstacles[i].pos_x <= 0) continue;
        mvprintw(obstacles[i].pos_y, obstacles[i].pos_x, "O");
    }
    attroff(COLOR_PAIR(3));
    refresh();
}

void render_targets(Object targets[]) {
    attron(COLOR_PAIR(2));
    for(int i = 0; i < n_targ; i++){
        if (targets[i].pos_y <= 0 || targets[i].pos_x <= 0) continue;
        mvprintw(targets[i].pos_y, targets[i].pos_x, "T");
    }
    attroff(COLOR_PAIR(2));
    refresh();
}

void render_drone(float x, float y) {
    attron(COLOR_PAIR(4));
    mvprintw(y, x, "+");
    attroff(COLOR_PAIR(4));
    refresh();
}

void write_to_server() {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "%d, %d", game.max_x, game.max_y);
    write(server_write_size_fd, buffer, strlen(buffer));
}

// Resize the input window
void resize_window() {
    endwin();
    refresh();
    clear();

    getmaxyx(stdscr, game.max_y, game.max_x);
    resize_term(game.max_y, game.max_x);

    write_to_server();

    clear();
    refresh();

    draw_outer_box();
}

// Handler for the signal SIGWINCH
void resize_handler(int sig, siginfo_t *info, void *context) {
    if (sig == SIGWINCH) {
        resize_window();
    }
    
    if (sig == SIGUSR2){
        LOG_TO_FILE(debug, "Shutting down by the SERVER");
        endwin();
        // Close the files
        fclose(errors);
        fclose(debug);
        exit(EXIT_SUCCESS);
    }
}

int open_drone_shared_memory() {
    int drone_mem_fd = shm_open(DRONE_SHARED_MEMORY, O_RDONLY, 0666);
    if (drone_mem_fd == -1) {
        perror("[MAP]: Error opening the drone shared memory");
        LOG_TO_FILE(errors, "Error opening the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("[MAP]: Error mapping the drone shared memory");
        LOG_TO_FILE(errors, "Error mapping the drone shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Opened the drone shared memory");
    return drone_mem_fd;
}

int open_score_shared_memory() {
    int score_mem_fd = shm_open(SCORE_SHARED_MEMORY, O_RDONLY, 0666);
    if (score_mem_fd == -1) {
        perror("[MAP]: Error opening the score shared memory");
        LOG_TO_FILE(errors, "Error opening the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    score = (float *)mmap(0, sizeof(float), PROT_READ, MAP_SHARED, score_mem_fd, 0);
    if (score == MAP_FAILED) {
        perror("[MAP]: Error mapping the score shared memory");
        LOG_TO_FILE(errors, "Error mapping the score shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Opened the score shared memory");
    return score_mem_fd;
}

void map_render(Drone *drone, Object obstacles[], Object targets[]) {
    clear();
    draw_outer_box();
    render_drone(drone->pos_x, drone->pos_y);
    render_obstacles(obstacles);
    render_targets(targets);
}

int main(int argc, char *argv[]) {
    /* OPEN THE LOG FILES */
    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("[MAP]: Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("[MAP]: Error opening the errors file");
        exit(EXIT_FAILURE);
    }

    if (argc < 6) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* Opens the semaphore for server process synchronization */
    sem_t *map_sem = sem_open("/map_semaphore", 0);
    if (map_sem == SEM_FAILED) {
        perror("[MAP]: Failed to open the semaphore for the exec"); 
        LOG_TO_FILE(errors, "Failed to open the semaphore for the exec");
        exit(EXIT_FAILURE);
    }
    sem_post(map_sem);  

    /* SETUP THE PIPE */
    server_write_size_fd = atoi(argv[1]);
    int server_read_obstacle_fd = atoi(argv[2]), server_read_target_fd = atoi(argv[3]);
    n_obs = atoi(argv[3]);
    n_targ = atoi(argv[4]);

    /* SETUP NCURSE */
    initscr(); 
    start_color();
    cbreak(); 
    noecho();
    curs_set(0);

    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);

    /* SETUP SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = resize_handler;
    sigemptyset(&sa.sa_mask);
    // Set the signal handler for SIGWINCH
    if (sigaction(SIGWINCH, &sa, NULL) == -1) {
        perror("[MAP]: Error in sigaction(SIGWINCH)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGWINCH)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("[MAP]: Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Add sigmask to block all signals execpt SIGWINCH and SIGURS2
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGWINCH);
    sigdelset(&sigset, SIGUSR2);
    sigprocmask(SIG_SETMASK, &sigset, NULL);

    /* OPEN THE SHARED MEMORY */
    int drone_mem_fd = open_drone_shared_memory();
    int score_mem_fd = open_score_shared_memory();

    // Retrive the dimension of the terminal
    getmaxyx(stdscr, game.max_y, game.max_x);
    // Send to the server the dimension
    write_to_server();

    Object obstacles[n_obs], targets[n_targ];
    memset(obstacles, 0, sizeof(obstacles));
    memset(targets, 0, sizeof(targets));

    char buffer[256];
    fd_set read_fds;
    struct timeval timeout;
    int max_fd = -1;
    if (server_read_obstacle_fd > max_fd) {
        max_fd = server_read_obstacle_fd;
    }
    if (server_read_target_fd > max_fd) {
        max_fd = server_read_target_fd;
    }
    /* LAUNCH THE MAP */
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_read_obstacle_fd, &read_fds);
        FD_SET(server_read_target_fd, &read_fds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;
        int activity;
        do {
            activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        } while(activity == -1 && errno == EINTR);

        if (activity < 0) {
            perror("[MAP]: Error in the server's select");
            LOG_TO_FILE(errors, "Error in select which pipe reads");
            break;
        } else if (activity > 0) {
            // Check if the map process has sent him the map size
            if (FD_ISSET(server_read_obstacle_fd, &read_fds)) {
                ssize_t bytes_read = read(server_read_obstacle_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    char *token = strtok(buffer, "|");
                    int i = 0;
                    while (token != NULL) {
                        sscanf(token, "%d,%d,%c", &obstacles[i].pos_x, &obstacles[i].pos_y, &obstacles[i].type);
                        token = strtok(NULL, "|");
                        i++;
                    }
                    map_render(drone, obstacles, targets);
                }
            }
            if (FD_ISSET(server_read_target_fd, &read_fds)) {
                ssize_t bytes_read = read(server_read_target_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0'; // End the string
                    char *token = strtok(buffer, "|");
                    int i = 0;
                    while (token != NULL) {
                        sscanf(token, "%d,%d,%c", &targets[i].pos_x, &targets[i].pos_y, &targets[i].type);
                        token = strtok(NULL, "|");
                        i++;
                    }
                    map_render(drone, obstacles, targets);
                }
            }
        } else {
            map_render(drone, obstacles, targets);
        }
    }

    /* END PROGRAM*/
    endwin();
    // Close the file descriptor
    if (close(drone_mem_fd) == -1 || close(score_mem_fd) == -1) {
        perror("[MAP]: Error closing the file descriptor of shared memory");
        LOG_TO_FILE(errors, "Error closing the file descriptor of shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));
    munmap(score, sizeof(float));

    sem_close(map_sem);

    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}
