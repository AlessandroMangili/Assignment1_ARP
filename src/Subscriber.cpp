
/**
 * @file HelloWorldSubscriber.cpp
 *
 */

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

using namespace eprosima::fastdds::dds;

class Subscribers
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
                    std::cout << "Received data from " << currentTopic << ":" << std::endl;

                    // Use the correct data based on the topic
                    for (int i = 0; i < currentMessage->x().size(); i++) {
                        std::cout << "Index: " << i + 1
                                << " X: " << currentMessage->x().at(i)
                                << " Y: " << currentMessage->y().at(i)
                                << " SENT" << std::endl;
                    }
                    std::cout << std::endl;
                }
            }
        }


        Message obstacleMessage_, targetMessage_;

        std::atomic_int samples_;

    } listener_;

public:

    Subscribers()
        : participant_(nullptr)
        , subscriber_(nullptr)
        , topicO_(nullptr)
        , topicT_(nullptr)
        , readerO_(nullptr)
        , readerT_(nullptr)
        , type_(new MessagePubSubType())
    {
    }

    virtual ~Subscribers()
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
    void run()
    {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

};

int main()
{
    std::cout << "Starting subscriber." << std::endl;

    Subscribers* mysub = new Subscribers();
    if (mysub->init())
    {
        mysub->run();
    }

    delete mysub;
    return 0;
}