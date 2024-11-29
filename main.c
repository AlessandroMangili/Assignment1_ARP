#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include "helper.h"

FILE *debug, *errors;       // File descriptors for the two log files

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

    pid_t pids[N_PROCS], wd;
    char *inputs[N_PROCS][5] = {{"./server", NULL}, {"./drone", NULL}, {"konsole", "--hold", "-e", "./keyboard_manager", NULL}};

    /* INTRO */
    char key;
    bool forward = false;
    // Descrizione più accattivante del gioco
    printf("\n\n\t\t   In this thrilling drone control challenge, you’ll need to navigate\n");
    printf("\t\t   through an obstacle-filled environment. Your skills will be tested,\n");
    printf("\t\t   and only the best will be able to complete the mission.\n");
    printf("\t\t   Use the controls wisely and stay sharp!\n\n");

    // Nuovo riquadro con le istruzioni
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

    printf("\nEnter 's' to start the mission or 'p' to quit\n");
    scanf("%c", &key);
    do {
        switch (key) {
            case 's':
                forward = true;
                printf("\n\n\t\t    ****************************************\n");
                printf("\t\t    *          MISSION STARTED!            *\n");
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

    for (int i = 0; i < N_PROCS; i++) {
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
    usleep(500000);

    char pids_string[N_PROCS][50];
    for(int i = 0; i < N_PROCS; i++) {
        sprintf(pids_string[i], "%d", pids[i]);
    }
    char *wd_input[] = {"./watchdog", pids_string[0], pids_string[1], pids_string[2], NULL};
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