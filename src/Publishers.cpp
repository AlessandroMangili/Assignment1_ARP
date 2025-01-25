/**
 * @file HelloWorldPublisher.cpp
 *
 */

#include "Generated/MessagePubSubTypes.hpp"

#include <chrono>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <random>

using namespace eprosima::fastdds::dds;

class Publishers
{
private:

    Message obstacleMessage_, targetMessage_;

    DomainParticipant* participant_;

    Publisher* publisher_;

    Topic* topicO_, *topicT_;

    DataWriter* writerO_, *writerT_;

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
                std::cout << "Publisher matched." << std::endl;
            }
            else if (info.current_count_change == -1)
            {
                matched_ = info.total_count;
                std::cout << "Publisher unmatched." << std::endl;
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

    Publishers()
        : participant_(nullptr)
        , publisher_(nullptr)
        , topicO_(nullptr)
        , topicT_(nullptr)
        , writerO_(nullptr)
        , writerT_(nullptr)
        , type_(new MessagePubSubType())
    {}

    virtual ~Publishers()
    {
        if (writerO_ != nullptr)
        {
            publisher_->delete_datawriter(writerO_);
        }
        if (writerT_ != nullptr)
        {
            publisher_->delete_datawriter(writerT_);
        }
        if (publisher_ != nullptr)
        {
            participant_->delete_publisher(publisher_);
        }
        if (topicO_ != nullptr)
        {
            participant_->delete_topic(topicO_);
        }
        if (topicT_ != nullptr)
        {
            participant_->delete_topic(topicT_);
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
    }

    //!Initialize the publisher
    bool init()
    {
        DomainParticipantQos participantQos;
        participantQos.name("Participant_publisher");
        participant_ = DomainParticipantFactory::get_instance()->create_participant(1, participantQos);

        if (participant_ == nullptr)
        {
            return false;
        }

        // Register the Type
        type_.register_type(participant_);

        // Create the publications Topic
        topicO_ = participant_->create_topic("ObstaclesTopic", type_.get_type_name(), TOPIC_QOS_DEFAULT);
        topicT_ = participant_->create_topic("TargetsTopic", type_.get_type_name(), TOPIC_QOS_DEFAULT);

        if (topicO_ == nullptr || topicT_ == nullptr)
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
        writerO_ = publisher_->create_datawriter(topicO_, DATAWRITER_QOS_DEFAULT, &listener_);
        writerT_ = publisher_->create_datawriter(topicT_, DATAWRITER_QOS_DEFAULT, &listener_);

        if (writerO_ == nullptr || writerT_ == nullptr)
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
            generate_object(10, 120, 120, true);
            writerO_->write(&obstacleMessage_);
            generate_object(10, 120, 120, false);
            writerT_->write(&targetMessage_);
            return true;
        }
        return false;
    }

    //!Run the Publisher
    void run(uint32_t samples)
    {
        uint32_t samples_sent = 0;
        while (samples_sent < samples)
        {
            if (publish())
            {
                samples_sent++;
                /*for (int i = 0; i < obstacleMessage_.x().size(); i++) {
                    std::cout <<"#1: " << obstacleMessage_.x().at(i)
                          <<" #2: "<< obstacleMessage_.y().at(i)
                          <<" SENT" << std::endl;
                }*/
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    void generate_object(int n_object, int map_x, int map_y, bool is_obstacle){
        std::random_device rd;                              // Seed for randomness
        std::mt19937 gen(rd());                             // Mersenne Twister generator
        std::uniform_int_distribution<> disX(1, map_x - 2); // Range [1, map_x-2]
        std::uniform_int_distribution<> disY(1, map_y - 2); // Range [1, map_y-2]

        if (is_obstacle) {
            obstacleMessage_.x().clear();
            obstacleMessage_.y().clear();
        } else {
            targetMessage_.x().clear();
            targetMessage_.y().clear();
        }

        for (int i = 0; i < n_object; i++){
            if (is_obstacle) {
                obstacleMessage_.x().push_back(disX(gen));
                obstacleMessage_.y().push_back(disY(gen));
            } else {
                targetMessage_.x().push_back(disX(gen));
                targetMessage_.y().push_back(disY(gen));
            }            
        }
    }
};

int main()
{
    std::cout << "Starting publisher." << std::endl;
    uint32_t samples = 3;

    Publishers* mypub = new Publishers();
    if(mypub->init())
    {
        mypub->run(samples);
    }

    delete mypub;
    return 0;
}