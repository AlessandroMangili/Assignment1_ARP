#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <sys/time.h>

// Number of processes
#define NUM_CLIENTS 4
// Shared memory
int shared_memory[2] = {0};
int shared_memory_readers[2] = {0};

typedef struct {
    int client_get_fd; // Used by server for reading messages from client
    int client_put_fd; // Used by client for writing messages to the server
    int server_get_fd; // Used by client for reading messages from server
    int server_put_fd; // Used by server for writing messages to the client
} Pipe;

void server(Pipe pipes[], int shared_memory[]) {
    printf("SERVER STARTED\n\n");
    // Close useless file descriptor
    for (int i = 0; i < NUM_CLIENTS; i++) {
        close(pipes[i].client_put_fd);
        close(pipes[i].server_get_fd);
    }

    fd_set read_fds; // Set of file descriptor for the 'select'
    struct timeval timeout;
    int max_fd = -1;
    // Find the maximum file descriptor to set the select
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (pipes[i].client_get_fd > max_fd) {
            max_fd = pipes[i].client_get_fd;
        }
    }

    char state = 'w';
    while(1) {  
        FD_ZERO(&read_fds);
        // Add all file descriptors to monitor requests from processes
        for (int i = 0; i < NUM_CLIENTS; i++) {
            FD_SET(pipes[i].client_get_fd, &read_fds);
        }

        // Timeout of 5 seconds
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("Error in the select");
            exit(EXIT_FAILURE);
        } else if (activity) {
            for (int i = 0; i < NUM_CLIENTS; i++) {
                int fd[2] = {-1};
                switch (i) {
                    case 0:
                        if (shared_memory[0] == 0 && shared_memory[1] == 0 || shared_memory[0] <= shared_memory[1]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 1:
                        if (shared_memory[1] <= shared_memory[0]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 2:
                        if (shared_memory[0] != shared_memory_readers[0]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                    case 3:
                        if (shared_memory[1] != shared_memory_readers[1]) {
                            fd[0] = pipes[i].client_get_fd;
                            fd[1] = pipes[i].server_put_fd;
                        }
                        break;
                }
                
                if(FD_ISSET(fd[0],&read_fds) && state == 'w') {
                    state = 'e';
                    char request;
                    read(fd[0], &request, 1);
                    if (request == '?') {
                        printf("[SERVER]: processing writer%d\n", i + 1);
                        // Send the ack to the writer
                        write(fd[1], "OK", 2);  
                        
                        // Waits the number to store
                        char number_str[256];
                        read(fd[0], number_str, sizeof(number_str));
                        int number = atoi(number_str);

                        // Store the number into the shared memory
                        shared_memory[i] = number;
                        printf("[SERVER]: stored number: [%d] from writer%d\n", number, i + 1);
                    } else if (request == '!') {
                        printf("[SERVER]: processing reader%d\n", (i%2) + 1);
                        char response[256];
                        snprintf(response, sizeof(response), "%d", shared_memory[i%2]);
                        shared_memory_readers[i%2] = shared_memory[i%2];
                        // Send the number stored to the reader
                        write(fd[1], response, strlen(response));
                        printf("[SERVER]: sent number: [%d] to the reader%d\n", shared_memory[i%2], (i%2) + 1);
                    }
                    state = 'w';
                }
                //sleep(1);
            }
        } else {
            printf("No process are ready to write\n");
            sleep(2);
        }
    }
    for (int i = 0; i < NUM_CLIENTS; i++) {
        close(pipes[i].client_get_fd);
        close(pipes[i].server_put_fd);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 16) {
        fprintf(stderr, "[SERVER]: invalid number of parameters for the server\n");
        exit(EXIT_FAILURE);
    }

    Pipe pipes[NUM_CLIENTS];
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pipes[i].client_get_fd = atoi(argv[i * NUM_CLIENTS]);
        pipes[i].client_put_fd = atoi(argv[1 + i * NUM_CLIENTS]);
        pipes[i].server_get_fd = atoi(argv[2 + i * NUM_CLIENTS]);
        pipes[i].server_put_fd = atoi(argv[3 + i * NUM_CLIENTS]);
    }
    server(pipes, shared_memory);

    return 0;
}