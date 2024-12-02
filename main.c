#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include "helper.h"

FILE *debug, *errors;       // File descriptors for the two log files

// Gets the pid of the process running on the Konsole terminal
int get_konsole_child(pid_t konsole) {
    char cmd[100];
    sprintf(cmd, "ps --ppid %d -o pid= 2>/dev/null", konsole);

    FILE *pipe = popen(cmd, "r");
    if (pipe == NULL) {
        perror("popen");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    int pid;
    fscanf(pipe, "%d", &pid);

    pclose(pipe);
    return pid;
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

    /* INTRO */
    char key;
    bool forward = false;
    printf("\n\n\t\t   In this thrilling drone control challenge, you’ll need to navigate\n");
    printf("\t\t   through an obstacle-filled environment. Your skills will be tested,\n");
    printf("\t\t   and only the best will be able to complete the mission.\n");
    printf("\t\t   Use the controls wisely and stay sharp!\n\n");

    printf("\t\t  ###########################\n");
    printf("\t\t  #  COMMANDS AND CONTROLS  #\n");
    printf("\t\t  ###########################\n");
    printf("\n");
    printf("\t\t  Move Up                : E\n");
    printf("\t\t  Move Down              : C\n");
    printf("\t\t  Move Left              : S\n");
    printf("\t\t  Move Right             : F\n");
    printf("\t\t  Move Up-Right          : R\n");
    printf("\t\t  Move Up-Left           : W\n");
    printf("\t\t  Move Down-Right        : V\n");
    printf("\t\t  Move Down-Left         : X\n");
    printf("\n");
    printf("\t\t  Remove All Forces      : D\n");
    printf("\t\t  Brake                  : B\n");
    printf("\t\t  Reset the Drone        : U\n");
    printf("\t\t  Quit the Game          : P\n");
    printf("\n");
    printf("\t\t  ###########################\n");

    printf("\nEnter 's' to start the game or 'p' to quit\n");
    scanf("%c", &key);
    do {
        switch (key) {
            case 's':
                forward = true;
                printf("\n\n\t\t    ****************************************\n");
                printf("\t\t    *          GAME STARTED!            *\n");
                printf("\t\t    *    Get ready to control the drone!   *\n");
                printf("\t\t    ****************************************\n\n");
                break;
            case 'p':
                printf("\n\t\t    ****************************************\n");
                printf("\t\t    *     You have quit the game. Goodbye!  *\n");
                printf("\t\t    ****************************************\n");
                exit(EXIT_SUCCESS);
            default:
                printf("\nInvalid input. Enter 's' to start or 'p' to quit\n");
                scanf("%c", &key);
                break;
        }
    } while (!forward);

    int drone_pipe_fds[2], drone2_pipe_fds[2], input_pipe_fds[2];
    if (pipe(drone_pipe_fds) == -1) {
        perror("pipe");
        LOG_TO_FILE(errors, "Error in creating the pipe for the server-drone");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(drone2_pipe_fds) == -1) {
        perror("pipe");
        LOG_TO_FILE(errors, "Error in creating the pipe for the server-drone");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(input_pipe_fds) == -1) {
        perror("pipe");
        LOG_TO_FILE(errors, "Error in creating the pipe for the server-input");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* LAUNCH THE SERVER AND THE DRONE */
    char write_drone_fd_str[10], write_drone2_fd_str[10], write_input_fd_str[10];
    char read_drone_fd_str[10], read_drone2_fd_str[10], read_input_fd_str[10];
    snprintf(write_drone_fd_str, sizeof(write_drone_fd_str), "%d", drone_pipe_fds[1]);
    snprintf(write_drone2_fd_str, sizeof(write_drone2_fd_str), "%d", drone2_pipe_fds[1]);
    snprintf(write_input_fd_str, sizeof(write_drone_fd_str), "%d", input_pipe_fds[1]);
    snprintf(read_drone_fd_str, sizeof(read_drone_fd_str), "%d", drone_pipe_fds[0]);
    snprintf(read_drone2_fd_str, sizeof(read_drone2_fd_str), "%d", drone2_pipe_fds[0]);
    snprintf(read_input_fd_str, sizeof(read_drone_fd_str), "%d", input_pipe_fds[0]);

    pid_t pids[N_PROCS], wd;
    char *inputs[N_PROCS - 1][5] = {{"./server", write_drone_fd_str, write_drone2_fd_str, read_input_fd_str, NULL}, {"./drone", read_drone_fd_str, read_drone2_fd_str, NULL}};
    for (int i = 0; i < N_PROCS - 1; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("Fork");
            LOG_TO_FILE(errors, "Unable to fork");
            // Close the files
            fclose(debug);
            fclose(errors);
            exit(EXIT_FAILURE);
        }
        if (pids[i] == 0) {
            execvp(inputs[i][0], inputs[i]);
            perror("Exec failed");
            LOG_TO_FILE(errors, "Unable to exec a process");
            // Close the files
            fclose(debug);
            fclose(errors);
            exit(EXIT_FAILURE);
        }
        usleep(500000);
    }

    /* LAUNCH THE INPUT */
    pid_t konsole;
    konsole = fork();
    char *keyboard_input[] = {"konsole", "-e", "./keyboard_manager", write_input_fd_str, NULL};
    if (konsole < 0) {
        perror("Fork");
        LOG_TO_FILE(errors, "Unable to fork");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (konsole == 0) {
        execvp(keyboard_input[0], keyboard_input);
        perror("Exec failed");
        LOG_TO_FILE(errors, "Unable to exec a process");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (konsole != 0) {
        usleep(500000);    
        pids[N_PROCS - 1] = get_konsole_child(konsole);
    }
    
    usleep(500000);
    /* LAUNCH THE WATCHDOG */
    char pids_string[N_PROCS][50];
    char *wd_input[N_PROCS + 2];
    wd_input[0] = "./watchdog";
    for(int i = 0; i < N_PROCS; i++) {
        sprintf(pids_string[i], "%d", pids[i]);
        wd_input[i + 1] = pids_string[i];
    }
    wd_input[N_PROCS + 1] = NULL;
    wd = fork();
    if (wd < 0) {
        perror("Fork");
        LOG_TO_FILE(errors, "Unable to fork");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (wd == 0) {
        execvp(wd_input[0], wd_input);
        perror("Exec failed");
        LOG_TO_FILE(errors, "Unable to exec a process");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < N_PROCS; i++) {
        wait(NULL);
    }
    wait(NULL);

    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}