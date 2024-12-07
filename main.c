#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include "cJSON/cJSON.h"
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

    /*  INTRODUCTION */
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

    /* IMPORT CONFIGURATION FROM JSON FILE */
    char jsonBuffer[1024];
    FILE *file = fopen("appsettings.json", "r");
    if (file == NULL) {
        perror("Error opening the file");
        return EXIT_FAILURE;//1
    }
    int len = fread(jsonBuffer, 1, sizeof(jsonBuffer), file); 
    fclose(file);

    cJSON *json = cJSON_Parse(jsonBuffer);
    if (json == NULL) {
        perror("Error parsing the file");
        return EXIT_FAILURE;
    }

    char n_obs[10], n_target[10];
    snprintf(n_obs, sizeof(n_obs), "%d", cJSON_GetObjectItemCaseSensitive(json, "NumObstacles")->valueint);
    snprintf(n_target, sizeof(n_target), "%d", cJSON_GetObjectItemCaseSensitive(json, "NumTargets")->valueint);

    cJSON *initial_position = cJSON_GetObjectItemCaseSensitive(json,"DroneInitialPosition");
    cJSON *position = cJSON_GetObjectItem(initial_position, "Position");
    cJSON *velocity = cJSON_GetObjectItem(initial_position, "Velocity");
    cJSON *force = cJSON_GetObjectItem(initial_position, "Force");
    
    float pos[2], vel[2], f[2];
    for (int i = 0; i < cJSON_GetArraySize(position); ++i) {
        cJSON *el_pos = cJSON_GetArrayItem(position, i);
        cJSON *el_vel = cJSON_GetArrayItem(velocity, i);
        cJSON *el_force = cJSON_GetArrayItem(force, i);
        if (cJSON_IsNumber(el_pos)) pos[i] = el_pos->valuedouble;
        if (cJSON_IsNumber(el_vel)) vel[i] = el_vel->valuedouble;
        if (cJSON_IsNumber(el_force)) f[i] = el_force->valuedouble;
    }
    cJSON_Delete(json);
    char pos_str[20], vel_str[20], force_str[20];
    snprintf(pos_str, sizeof(pos_str), "%f,%f", pos[0], pos[1]);
    snprintf(vel_str, sizeof(vel_str), "%f,%f", vel[0], vel[1]);
    snprintf(force_str, sizeof(force_str), "%f,%f", f[0], f[1]);

    int drone_map_fds[2], drone_key_fds[2], input_pipe_fds[2], obstacle_position_fds[2], target_position_fds[2], obstacle_map_fds[2], target_map_fds[2], server_obstacles_fds[2], server_targets_fds[2];
    if (pipe(drone_map_fds) == -1) {
        perror("Error creating the pipe for the drone");
        LOG_TO_FILE(errors, "Error creating the pipe for the drone-map");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(drone_key_fds) == -1) {
        perror("Error creating the pipe for the drone");
        LOG_TO_FILE(errors, "Error creating the pipe for the drone-key");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(input_pipe_fds) == -1) {
        perror("Error creating the pipe for the input");
        LOG_TO_FILE(errors, "Error creating the pipe for the input");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(obstacle_position_fds) == -1) {
        perror("Error creating the pipe for the obstacle");
        LOG_TO_FILE(errors, "Error creating the pipe for the obstacle");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(obstacle_map_fds) == -1) {
        perror("Error creating the pipe for the obstacle");
        LOG_TO_FILE(errors, "Error creating the pipe for the obstacle");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(target_position_fds) == -1) {
        perror("Error creating the pipe for the target");
        LOG_TO_FILE(errors, "Error creating the pipe for the target");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(target_map_fds) == -1) {
        perror("Error creating the pipe for the target");
        LOG_TO_FILE(errors, "Error creating the pipe for the target");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(server_obstacles_fds) == -1) {
        perror("Error creating the pipe for the target");
        LOG_TO_FILE(errors, "Error creating the pipe for the target");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(server_targets_fds) == -1) {
        perror("Error creating the pipe for the target");
        LOG_TO_FILE(errors, "Error creating the pipe for the target");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* CONVERT INTO STRING ALL THE FILE DESCRIPTOR */
    char drone_write_map_fd_str[10], drone_write_key_fd_str[10], input_write_fd_str[10];
    char drone_read_map_fd_str[10], drone_read_key_fd_str[10], input_read_fd_str[10];
    char obstacle_write_position_fd_str[10], obstacle_read_position_fd_str[10];
    char target_write_position_fd_str[10], target_read_position_fd_str[10];
    char obstacle_write_map_fd_str[10], obstacle_read_map_fd_str[10];
    char target_write_map_fd_str[10], target_read_map_fd_str[10];
    char server_write_obstacles_fd_str[10], server_read_obstacles_fd_str[10];
    char server_write_targets_fd_str[10], server_read_targets_fd_str[10];

    snprintf(obstacle_write_map_fd_str, sizeof(obstacle_write_map_fd_str), "%d", obstacle_map_fds[1]);
    snprintf(target_write_map_fd_str, sizeof(target_write_map_fd_str), "%d", target_map_fds[1]);
    snprintf(obstacle_write_position_fd_str, sizeof(obstacle_write_position_fd_str), "%d", obstacle_position_fds[1]);
    snprintf(target_write_position_fd_str, sizeof(target_write_position_fd_str), "%d", target_position_fds[1]);
    snprintf(drone_write_map_fd_str, sizeof(drone_write_map_fd_str), "%d", drone_map_fds[1]);
    snprintf(drone_write_key_fd_str, sizeof(drone_write_key_fd_str), "%d", drone_key_fds[1]);
    snprintf(input_write_fd_str, sizeof(drone_write_map_fd_str), "%d", input_pipe_fds[1]);
    snprintf(drone_read_map_fd_str, sizeof(drone_read_map_fd_str), "%d", drone_map_fds[0]);
    snprintf(drone_read_key_fd_str, sizeof(drone_read_key_fd_str), "%d", drone_key_fds[0]);
    snprintf(input_read_fd_str, sizeof(drone_read_map_fd_str), "%d", input_pipe_fds[0]);
    snprintf(obstacle_read_position_fd_str, sizeof(obstacle_read_position_fd_str), "%d", obstacle_position_fds[0]);
    snprintf(target_read_position_fd_str, sizeof(target_read_position_fd_str), "%d", target_position_fds[0]);
    snprintf(obstacle_read_map_fd_str, sizeof(obstacle_read_map_fd_str), "%d", obstacle_map_fds[0]);
    snprintf(target_read_map_fd_str, sizeof(target_read_map_fd_str), "%d", target_map_fds[0]);
    snprintf(server_write_obstacles_fd_str, sizeof(server_write_obstacles_fd_str), "%d", server_obstacles_fds[1]);
    snprintf(server_read_obstacles_fd_str, sizeof(server_read_obstacles_fd_str), "%d", server_obstacles_fds[0]);
    snprintf(server_write_targets_fd_str, sizeof(server_write_targets_fd_str), "%d", server_targets_fds[1]);
    snprintf(server_read_targets_fd_str, sizeof(server_read_targets_fd_str), "%d", server_targets_fds[0]);

    /* LAUNCH THE SERVER AND THE DRONE */
    pid_t pids[N_PROCS], wd;
    char *inputs[N_PROCS - 1][16] = {
        {"./server", drone_write_map_fd_str, drone_write_key_fd_str, input_read_fd_str, obstacle_write_map_fd_str, obstacle_read_position_fd_str, target_write_map_fd_str, target_read_position_fd_str, server_write_obstacles_fd_str, server_write_targets_fd_str, pos_str, vel_str, force_str, n_obs, n_target, NULL}, 
        {"./drone", drone_read_map_fd_str, drone_read_key_fd_str, server_read_obstacles_fd_str, server_read_targets_fd_str, NULL},
        {"./obstacle", obstacle_write_position_fd_str, obstacle_read_map_fd_str, n_obs, NULL},
        {"./target", target_write_position_fd_str, target_read_map_fd_str, n_target, n_target, NULL}
    };
    for (int i = 0; i < N_PROCS - 1; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("Error forking");
            LOG_TO_FILE(errors, "Error forking");
            // Close the files
            fclose(debug);
            fclose(errors);
            exit(EXIT_FAILURE);
        } else if (pids[i] == 0) {
            execvp(inputs[i][0], inputs[i]);
            perror("Failed to execute to launch one of the main process");
            LOG_TO_FILE(errors, "Failed to execute to launch one of the main process");
            // Close the files
            fclose(debug);
            fclose(errors);
            exit(EXIT_FAILURE);
        }
        usleep(500000);
    }

    /* LAUNCH THE INPUT */
    pid_t konsole = fork();
    char *keyboard_input[] = {"konsole", "-e", "./keyboard_manager", input_write_fd_str, NULL};
    if (konsole < 0) {
        perror("Error forking the keyboard manager");
        LOG_TO_FILE(errors, "Error forking the keyboard manager");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else if (konsole == 0) {
        execvp(keyboard_input[0], keyboard_input);
        perror("Failed to execute to launch the keyboard manager");
        LOG_TO_FILE(errors, "Failed to execute to launch the keyboard manager");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else {
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
        perror("Error forking the watchdog");
        LOG_TO_FILE(errors, "Error forking the watchdog");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else if (wd == 0) {
        execvp(wd_input[0], wd_input);
        perror("Failed to execute to launch the watchdog");
        LOG_TO_FILE(errors, "Failed to execute to launch the watchdog");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < N_PROCS; i++) {
        wait(NULL);
    }
    wait(NULL);

    /* END PROGRAM */

    // Close the files
    fclose(debug);
    fclose(errors);

    return 0;
}