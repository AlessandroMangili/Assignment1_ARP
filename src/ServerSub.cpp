#include "Generated/MessagePubSubTypes.hpp"

#include <chrono>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include "../helper.h"
#include <signal.h>
#include <sys/mman.h>
#include <sys/select.h>

using namespace eprosima::fastdds::dds;

FILE *debug, *errors;       // File descriptors for the two log files
pid_t wd_pid, map_pid;
Drone *drone;
int n_obs;
int n_targ;
float *score;

int drone_write_targets_fd, map_write_target_fd;
int drone_write_obstacles_fd, map_write_obstacle_fd;

class ServerSub
{
private:

    DomainParticipant* participant_;

    Subscriber* subscriber_;

    DataReader* readerO_, *readerT_;

    Topic* topicO_, *topicT_;

    TypeSupport type_;

    class SubListener : public DataReaderListener
    {
    public:

        SubListener() : samples_(0) {}

        ~SubListener() override {}

        void on_subscription_matched(
                DataReader*,
                const SubscriptionMatchedStatus& info) override
        {
            if (info.current_count_change == 1)
            {
                std::cout << "Subscriber matched." << std::endl;
            }
            else if (info.current_count_change == -1)
            {
                std::cout << "Subscriber unmatched." << std::endl;
            }
            else
            {
                std::cout << info.current_count_change
                          << " is not a valid value for SubscriptionMatchedStatus current count change" << std::endl;
            }
        }

        void on_data_available(
                DataReader* reader) override
        {
            SampleInfo info;
            auto topicName = reader->get_topicdescription()->get_name();
            
            // Decide which message to process based on the topic name
            Message* currentMessage = nullptr;
            std::string currentTopic = "";

            if (topicName == "ObstaclesTopic") {
                currentMessage = &obstacleMessage_;
                currentTopic = "ObstaclesTopic";
            }
            else if (topicName == "TargetsTopic") {
                currentMessage = &targetMessage_;
                currentTopic = "TargetsTopic";
            }

            // Process the message if it's a valid topic
            if (currentMessage != nullptr && reader->take_next_sample(currentMessage, &info) == eprosima::fastdds::dds::RETCODE_OK)
            {
                if (info.valid_data)
                {
                    std::cout << "Received data from " << currentTopic /*<< ":" */ << std::endl;

                    // Use the correct data based on the topic
                    std::vector<Object> objects(currentMessage->x().size());
                    std::string objectStr;
                    for (int i = 0; i < currentMessage->x().size(); i++) {
                        objects[i] = {currentMessage->x()[i], currentMessage->y()[i], (topicName == "ObstaclesTopic" ? 'o' : 't'), false};

                        objectStr += std::to_string(objects[i].pos_x) + "," + std::to_string(objects[i].pos_y) + "," + objects[i].type + "," + std::to_string(objects[i].hit ? 1 : 0);
                        if (i + 1 < currentMessage->x().size()) 
                            objectStr += "|";

                        /*std::cout << "Index: " << i + 1
                                << " X: " << currentMessage->x().at(i)
                                << " Y: " << currentMessage->y().at(i)
                                << " SENT" << std::endl;*/
                    }

                    if (topicName == "ObstaclesTopic") {
                        write(drone_write_obstacles_fd, objectStr.c_str(), objectStr.size());
                        write(map_write_obstacle_fd, objectStr.c_str(), objectStr.size());
                    }
                    else if (topicName == "TargetsTopic") {
                        write(drone_write_targets_fd, objectStr.c_str(), objectStr.size());
                        write(map_write_target_fd, objectStr.c_str(), objectStr.size());
                    }
                }
            }
        }

        Message obstacleMessage_, targetMessage_;

        std::atomic_int samples_;

    } listener_;

public:

    ServerSub()
        : participant_(nullptr)
        , subscriber_(nullptr)
        , topicO_(nullptr)
        , topicT_(nullptr)
        , readerO_(nullptr)
        , readerT_(nullptr)
        , type_(new MessagePubSubType())
    {
    }

    virtual ~ServerSub()
    {
        if (readerO_ != nullptr)
        {
            subscriber_->delete_datareader(readerO_);
        }
        if (readerT_ != nullptr)
        {
            subscriber_->delete_datareader(readerT_);
        }
        if (topicO_ != nullptr)
        {
            participant_->delete_topic(topicO_);
        }
        if (topicT_ != nullptr)
        {
            participant_->delete_topic(topicT_);
        }
        if (subscriber_ != nullptr)
        {
            participant_->delete_subscriber(subscriber_);
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
    }

    //!Initialize the subscriber
    bool init()
    {
        DomainParticipantQos participantQos;
        
        participantQos.name("Participant_subscriber");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(1, participantQos);

        if (participant_ == nullptr)
        {
            return false;
        }

        // Register the Type
        type_.register_type(participant_);

        // Create the subscriptions Topic
        topicO_ = participant_->create_topic("ObstaclesTopic", type_.get_type_name(), TOPIC_QOS_DEFAULT);
        topicT_ = participant_->create_topic("TargetsTopic", type_.get_type_name(), TOPIC_QOS_DEFAULT);

        if (topicO_ == nullptr || topicT_ == nullptr)
        {
            return false;
        }

        // Create the Subscriber
        subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);

        if (subscriber_ == nullptr)
        {
            return false;
        }

        // Create the DataReader
        readerO_ = subscriber_->create_datareader(topicO_, DATAREADER_QOS_DEFAULT, &listener_);
        readerT_ = subscriber_->create_datareader(topicT_, DATAREADER_QOS_DEFAULT, &listener_);

        if (readerO_ == nullptr || readerT_ == nullptr)
        {
            return false;
        }

        return true;
    }

    //!Run the Subscriber
    void run(int drone_write_size_fd, int drone_write_key_fd, int input_read_key_fd, int map_read_size_fd)
    {
        char buffer[2048];
        fd_set read_fds;
        struct timeval timeout;

        int max_fd = -1;
        if (map_read_size_fd > max_fd) {
            max_fd = map_read_size_fd;
        }
        if(input_read_key_fd > max_fd) {
            max_fd = input_read_key_fd;
        }

        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(input_read_key_fd, &read_fds);
            FD_SET(map_read_size_fd, &read_fds);

            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int activity;
            do {
                activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
            } while(activity == -1 && errno == EINTR);

            if (activity < 0) {
                perror("Error in the server's select");
                LOG_TO_FILE(errors, "Error in select which pipe reads");
                break;
            } else if (activity > 0) {
                memset(buffer, '\0', sizeof(buffer));
                // Check if the map process has sent him the map size
                if (FD_ISSET(map_read_size_fd, &read_fds)) {
                    ssize_t bytes_read = read(map_read_size_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0'; // End the string
                        write(drone_write_size_fd, buffer, strlen(buffer));
                    }
                }
                // Check if the input process has sent him a key that was pressed
                if (FD_ISSET(input_read_key_fd, &read_fds)) {
                    ssize_t bytes_read = read(input_read_key_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        write(drone_write_key_fd, buffer, strlen(buffer));
                    }
                }
            }
        }    
        // Close file descriptor
        close(drone_write_size_fd);
        close(drone_write_key_fd);
        close(map_read_size_fd);
        close(input_read_key_fd);   
    }

};

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }
    if (sig == SIGUSR2) {
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");

        // Unlink the shared memory
        if (shm_unlink(DRONE_SHARED_MEMORY) == -1) {
            perror("Unlink shared memory");
            LOG_TO_FILE(errors, "Error unlinking the shared memory");
            // Close the files
            fclose(debug);
            fclose(errors); 
            exit(EXIT_FAILURE);
        }

        // Close the semaphore and unlink it
        sem_close(drone->sem);
        sem_unlink("drone_sem");

        if (kill(map_pid, SIGUSR2) == -1) {
            perror("Error sending SIGTERM signal to the MAP");
            LOG_TO_FILE(errors, "Error sending SIGTERM signal to the MAP");
            exit(EXIT_FAILURE);
        }
        // Close the files
        fclose(errors);
        fclose(debug);
        
        exit(EXIT_SUCCESS);
    }
}

int create_drone_shared_memory() {
    int drone_mem_fd = shm_open(DRONE_SHARED_MEMORY, O_CREAT | O_RDWR, 0666);
    if (drone_mem_fd == -1) {
        perror("Error opening the shared memory");
        LOG_TO_FILE(errors, "Error opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    
    // Set the size of the shared memory
    if(ftruncate(drone_mem_fd, sizeof(Drone)) == -1){
        perror("Error setting the size of the shared memory");
        LOG_TO_FILE(errors, "Error setting the size of the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Map the shared memory into a drone objects
    drone = (Drone *)mmap(0, sizeof(Drone), PROT_READ | PROT_WRITE, MAP_SHARED, drone_mem_fd, 0);
    if (drone == MAP_FAILED) {
        perror("Error mapping the shared memory");
        LOG_TO_FILE(errors, "Error mapping the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    LOG_TO_FILE(debug, "Created and opened the drone shared memory");
    return drone_mem_fd;
}

int create_score_shared_memory() {
    int score_mem_fd = shm_open(SCORE_SHARED_MEMORY, O_CREAT | O_RDWR, 0666);
    if (score_mem_fd == -1) {
        perror("Error opening the shared memory");
        LOG_TO_FILE(errors, "Error opening the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    
    // Set the size of the shared memory
    if(ftruncate(score_mem_fd, sizeof(float)) == -1){
        perror("Error setting the size of the shared memory");
        LOG_TO_FILE(errors, "Error setting the size of the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }

    // Map the shared memory into a drone objects
    score = (float *)mmap(0, sizeof(float), PROT_READ | PROT_WRITE, MAP_SHARED, score_mem_fd, 0);
    if (score == MAP_FAILED) {
        perror("Error mapping the shared memory");
        LOG_TO_FILE(errors, "Error mapping the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors);   
        exit(EXIT_FAILURE);
    }
    *score = 0;
    LOG_TO_FILE(debug, "Created and opened the score shared memory");
    return score_mem_fd;
}

int main(int argc, char* argv[])
{
    std::cout << "Starting Server subscriber" << std::endl;
    ServerSub* mysub = new ServerSub();

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

    if (argc < 10) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    sem_t *exec_sem = sem_open("/exec_semaphore", 0);
    if (exec_sem == SEM_FAILED) {
        LOG_TO_FILE(errors, "Failed to open the semaphore for the exec");
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    sem_post(exec_sem);

    /* CREATE AND SETUP THE PIPES */
    int drone_write_size_fd = atoi(argv[1]), 
        drone_write_key_fd = atoi(argv[2]), 
        input_read_key_fd = atoi(argv[3]);

    drone_write_obstacles_fd = atoi(argv[4]), 
    drone_write_targets_fd = atoi(argv[5]); 

    int pipe_fd[2], pipe2_fd[2], pipe3_fd[2];
    if (pipe(pipe_fd) == -1) {
        perror("Error creating the pipe for the map");
        LOG_TO_FILE(errors, "Error creating the pipe");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(pipe2_fd) == -1) {
        perror("Error creating the pipe 2 for the map");
        LOG_TO_FILE(errors, "Error creating the pipe 2");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    if (pipe(pipe3_fd) == -1) {
        perror("Error creating the pipe 3 for the map");
        LOG_TO_FILE(errors, "Error creating the pipe 3");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    int map_read_size_fd = pipe_fd[0];
    map_write_obstacle_fd = pipe2_fd[1];
    map_write_target_fd = pipe3_fd[1];
    char map_write_size_fd_str[10], map_read_obstacle_fd_str[10], map_read_target_fd_str[10];
    snprintf(map_write_size_fd_str, sizeof(map_write_size_fd_str), "%d", pipe_fd[1]);
    snprintf(map_read_obstacle_fd_str, sizeof(map_read_obstacle_fd_str), "%d", pipe2_fd[0]);
    snprintf(map_read_target_fd_str, sizeof(map_read_target_fd_str), "%d", pipe3_fd[0]);

    /* CREATE THE SHARED MEMORY */
    int drone_mem_fd = create_drone_shared_memory();
    int score_mem_fd = create_score_shared_memory();

    /* CREATE THE SEMAPHORE */
    sem_unlink("drone_sem");
    drone->sem = sem_open("drone_sem", O_CREAT | O_RDWR, 0666, 1);
    if (drone->sem == SEM_FAILED) {
        perror("Error creating the semaphore for the drone");
        LOG_TO_FILE(errors, "Error creating the semaphore for the drone");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    /* SET THE INITIAL CONFIGURATION */   
    // Lock
    sem_wait(drone->sem);
    // Setting the initial position
    LOG_TO_FILE(debug, "Initialized initial position to the drone");
    sscanf(argv[6], "%f,%f", &drone->pos_x, &drone->pos_y);
    sscanf(argv[7], "%f,%f", &drone->vel_x, &drone->vel_y);
    sscanf(argv[8], "%f,%f", &drone->force_x, &drone->force_y);

    n_obs = atoi(argv[9]);
    n_targ = atoi(argv[10]);

    char n_obs_str[10];
    snprintf(n_obs_str, sizeof(n_obs_str), "%d", n_obs);

    char n_targ_str[10];
    snprintf(n_targ_str, sizeof(n_targ_str), "%d", n_targ);

    // Unlock
    sem_post(drone->sem);

    /* LAUNCH THE MAP WINDOW */
    // Fork to create the map window process
    char *map_window_path[] = {"konsole", "-e", "./map_window", map_write_size_fd_str, map_read_obstacle_fd_str, map_read_target_fd_str, n_obs_str, n_targ_str, NULL};
    map_pid = fork();
    if (map_pid ==-1){
        perror("Error forking the map file");
        LOG_TO_FILE(errors, "Error forking the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    } else if (map_pid == 0){
        execvp(map_window_path[0], map_window_path);
        perror("Failed to execute to launch the map file");
        LOG_TO_FILE(errors, "Failed to execute to launch the map file");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    
    /* SETTING THE SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
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
        perror("Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    usleep(50000);

    
    if (mysub->init())
    {
        mysub->run(drone_write_size_fd, drone_write_key_fd, input_read_key_fd, map_read_size_fd);
    }

        /* END PROGRAM */
    // Unlink the shared memory
    if (shm_unlink(DRONE_SHARED_MEMORY) == -1 || shm_unlink(SCORE_SHARED_MEMORY) == -1) {
        perror("Unlink shared memory");
        LOG_TO_FILE(errors, "Error unlinking the shared memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Close the file descriptor
    if (close(drone_mem_fd) == -1 || close(score_mem_fd) == -1) {
        perror("Close file descriptor");
        LOG_TO_FILE(errors, "Error closing the file descriptor of the memory");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }
    // Unmap the shared memory region
    munmap(drone, sizeof(Drone));
    munmap(score, sizeof(float));

    // Close the semaphore and unlink it
    sem_close(drone->sem);
    sem_unlink("drone_sem");

    // Close the files
    fclose(debug);
    fclose(errors); 

    delete mysub;
    return 0;
}