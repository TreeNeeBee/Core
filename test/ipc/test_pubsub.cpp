#include "../../source/inc/ipc/Publisher.hpp"
#include "../../source/inc/ipc/Subscriber.hpp"
#include <iostream>
#include <vector>
#include <memory>

using namespace lap::core::ipc;
using namespace lap::core;

struct Data { UInt64 value; };

int main() {
    PublisherConfig pub_cfg;
    pub_cfg.max_chunks = 16;
    pub_cfg.chunk_size = sizeof(Data);
    
    SubscriberConfig sub_cfg;
    
    std::cout << "Creating publisher..." << std::endl;
    auto pub_result = Publisher<Data>::Create("test", pub_cfg);
    if (!pub_result.HasValue()) {
        std::cerr << "Publisher failed" << std::endl;
        return 1;
    }
    auto publisher = std::make_shared<Publisher<Data>>(std::move(pub_result).Value());
    std::cout << "Publisher created" << std::endl;
    
    std::cout << "Creating subscribers..." << std::endl;
    std::vector<std::shared_ptr<Subscriber<Data>>> subscribers;
    for (int i = 0; i < 3; ++i) {
        auto sub_result = Subscriber<Data>::Create("test", sub_cfg);
        if (!sub_result.HasValue()) {
            std::cerr << "Subscriber " << i << " failed" << std::endl;
            continue;
        }
        auto subscriber = std::make_shared<Subscriber<Data>>(std::move(sub_result).Value());
        subscribers.push_back(subscriber);
        std::cout << "Subscriber " << i << " created" << std::endl;
    }
    
    std::cout << "Loaning..." << std::endl;
    auto loan_result = publisher->Loan();
    if (!loan_result.HasValue()) {
        std::cerr << "Loan failed" << std::endl;
        return 1;
    }
    std::cout << "Loan succeeded!" << std::endl;
    
    return 0;
}

#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
