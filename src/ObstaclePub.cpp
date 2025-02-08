#include "Generated/ObjectsPubSubTypes.hpp"

#include <chrono>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <random>
#include "../helper.h"
#include <signal.h>
#include <semaphore.h>

using namespace eprosima::fastdds::dds;

FILE *debug, *errors;
pid_t wd_pid;
bool matched = false;
sem_t *sync_sem;

class ObstaclePub
{
private:

    Objects message_;

    DomainParticipant* participant_;

    Publisher* publisher_;

    Topic* topic_;

    DataWriter* writer_;

    TypeSupport type_;

    class PubListener : public DataWriterListener
    {
    public:

        PubListener(): matched_(0) {}

        ~PubListener() override {}

        void on_publication_matched(
                DataWriter*,
                const PublicationMatchedStatus& info) override
        {
            if (info.current_count_change == 1)
            {
                matched_ = info.total_count;
                std::cout << "Publisher Obstacle matched." << std::endl;
                sem_wait(sync_sem); // Wait until the other publisher is also ready to publish
                std::this_thread::sleep_for(std::chrono::seconds(3));
                matched = true;
            }
            else if (info.current_count_change == -1)
            {
                matched_ = info.total_count;
                matched = false;
                std::cout << "Publisher Obstacle unmatched." << std::endl;
            }
            else
            {
                std::cout << info.current_count_change
                        << " is not a valid value for PublicationMatchedStatus current count change." << std::endl;
            }
        }

        std::atomic_int matched_;

    } listener_;

public:
    int n_obs;
    int map_x, map_y;

    ObstaclePub()
        : participant_(nullptr)
        , publisher_(nullptr)
        , topic_(nullptr)
        , writer_(nullptr)
        , type_(new ObjectsPubSubType())
    {}

    virtual ~ObstaclePub()
    {
        if (writer_ != nullptr)
        {
            publisher_->delete_datawriter(writer_);
        }
        if (publisher_ != nullptr)
        {
            participant_->delete_publisher(publisher_);
        }
        if (topic_ != nullptr)
        {
            participant_->delete_topic(topic_);
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
    }

    //!Initialize the publisher
    bool init()
    {
        DomainParticipantQos participantQos;

        participantQos.name("Participant_publisher");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(2, participantQos);

        if (participant_ == nullptr)
        {
            return false;
        }

        // Register the Type
        type_.register_type(participant_);

        // Create the publications Topic
        topic_ = participant_->create_topic("ObstaclesTopic", type_.get_type_name(), TOPIC_QOS_DEFAULT);

        if (topic_ == nullptr)
        {
            return false;
        }

        // Create the Publisher
        publisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);

        if (publisher_ == nullptr)
        {
            return false;
        }

        // Create the DataWriter
        writer_ = publisher_->create_datawriter(topic_, DATAWRITER_QOS_DEFAULT, &listener_);

        if (writer_ == nullptr)
        {
            return false;
        }

        return true;
    }

    //!Send a publication
    bool publish()
    {
        if (listener_.matched_ > 0)
        {
            generate_object(n_obs, map_x, map_y);
            writer_->write(&message_);
            return true;
        }
        return false;
    }

    //!Run the Publisher
    void run()
    {
        while (true)
        {
            if (matched) {
                publish();
                std::this_thread::sleep_for(std::chrono::seconds(15));
            }
        }
    }

    void generate_object(int n_object, int map_x, int map_y){
        std::random_device rd;                              // Seed for randomness
        std::mt19937 gen(rd());                             // Mersenne Twister generator
        std::uniform_int_distribution<> disX(1, map_x - 2); // Range [1, map_x-2]
        std::uniform_int_distribution<> disY(1, map_y - 2); // Range [1, map_y-2]

        message_.x().clear();
        message_.y().clear();
        message_.objects_number() = n_object;

        for (int i = 0; i < n_object; i++){
            message_.x().push_back(disX(gen));
            message_.y().push_back(disY(gen));          
        }
    }
};

ObstaclePub* mypub;

void signal_handler(int sig, siginfo_t* info, void *context) {
    if (sig == SIGUSR1) {
        wd_pid = info->si_pid;
        LOG_TO_FILE(debug, "Signal SIGUSR1 received from WATCHDOG");
        kill(wd_pid, SIGUSR1);
    }

    if (sig == SIGUSR2) {
        LOG_TO_FILE(debug, "Shutting down by the WATCHDOG");
        printf("Obstacle shutting down by the WATCHDOG: %d\n", getpid());
        delete mypub;
        // Close the files
        fclose(errors);
        fclose(debug);
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char* argv[])
{
    std::cout << "Starting Obstacle publisher" << std::endl;
    ObstaclePub* mypub = new ObstaclePub();

    debug = fopen("debug.log", "a");
    if (debug == NULL) {
        perror("[OBSTACLE]: Error opening the debug file");
        exit(EXIT_FAILURE);
    }
    errors = fopen("errors.log", "a");
    if (errors == NULL) {
        perror("[OBSTACLE]: Error opening the errors file");
        exit(EXIT_FAILURE);
    }

    if (argc < 3) {
        LOG_TO_FILE(errors, "Invalid number of parameters");
        // Close the files
        fclose(debug);
        fclose(errors); 
        exit(EXIT_FAILURE);
    }

    LOG_TO_FILE(debug, "Process started");

    /* Opens the semaphore for child process synchronization */
    sem_t *exec_sem = sem_open("/exec_semaphore", 0);
    if (exec_sem == SEM_FAILED) {
        perror("[OBSTACLE]: Failed to open the semaphore for the exec");
        LOG_TO_FILE(errors, "Failed to open the semaphore for the exec");
        exit(EXIT_FAILURE);
    }
    sem_post(exec_sem);  // Releases the resource to proceed with the launch of other child processes
    sem_close(exec_sem);
    /* Open the semaphore to synchronize the two publishers */
    sync_sem = sem_open("/sync_semaphore", 0);
    if (sync_sem == SEM_FAILED) {
        perror("[OBSTACLE]: Failed to open the sync semaphore");
        LOG_TO_FILE(errors, "Failed to open the semaphore");
        exit(EXIT_FAILURE);
    }

    mypub->n_obs = atoi(argv[1]);
    mypub->map_x = atoi(argv[2]);
    mypub->map_y = atoi(argv[3]);

    /* SETTING THE SIGNALS */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Set the signal handler for SIGUSR1
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[OBSTACLE]: Error in sigaction(SIGURS1)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS1)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }
    // Set the signal handler for SIGUSR2
    if(sigaction(SIGUSR2, &sa, NULL) == -1){
        perror("[OBSTACLE]: Error in sigaction(SIGURS2)");
        LOG_TO_FILE(errors, "Error in sigaction(SIGURS2)");
        // Close the files
        fclose(debug);
        fclose(errors);
        exit(EXIT_FAILURE);
    }

    // Add sigmask to block all signals execpt SIGURS1, SIGURS2 and SIGTERM
    sigset_t sigset;
    sigfillset(&sigset);
    sigdelset(&sigset, SIGUSR1);
    sigdelset(&sigset, SIGUSR2);
    sigdelset(&sigset, SIGTERM);
    sigprocmask(SIG_SETMASK, &sigset, NULL);
    
    if (mypub->init())
    {
        mypub->run();
    }

    /* END PROGRAM */
    sem_close(sync_sem);

    delete mypub;
    // Close the files
    fclose(debug);
    fclose(errors);
    return 0;
}