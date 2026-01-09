#include "../../source/inc/ipc/Publisher.hpp"
#include "../../source/inc/ipc/Subscriber.hpp"
#include <iostream>

using namespace lap::core::ipc;
using namespace lap::core;

struct Data { UInt64 value; };

int main() {
    PublisherConfig pub_cfg;
    pub_cfg.max_chunks = 16;
    
    std::cout << "Creating publisher..." << std::endl;
    auto pub_result = Publisher<Data>::Create("test", pub_cfg);
    if (!pub_result.HasValue()) {
        std::cerr << "Publisher failed" << std::endl;
        return 1;
    }
    
    std::cout << "Moving publisher from Result..." << std::endl;
    Publisher<Data> pub_temp = std::move(pub_result).Value();
    
    std::cout << "Creating shared_ptr..." << std::endl;
    auto publisher = std::make_shared<Publisher<Data>>(std::move(pub_temp));
    
    std::cout << "Publisher ready, trying Loan..." << std::endl;
    auto loan1 = publisher->Loan();
    std::cout << "First loan: " << (loan1.HasValue() ? "OK" : "FAIL") << std::endl;
    
    std::cout << "\nCreating first subscriber..." << std::endl;
    auto sub_result = Subscriber<Data>::Create("test", {});
    if (!sub_result.HasValue()) {
        std::cerr << "Subscriber failed" << std::endl;
        return 1;
    }
    
    std::cout << "Moving subscriber..." << std::endl;
    auto subscriber = std::make_shared<Subscriber<Data>>(std::move(sub_result).Value());
    
    std::cout << "Subscriber created, trying Loan again..." << std::endl;
    auto loan2 = publisher->Loan();
    std::cout << "Second loan: " << (loan2.HasValue() ? "OK" : "FAIL") << std::endl;
    
    return 0;
}

#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
