#include "../../source/inc/ipc/Publisher.hpp"
#include "LoggingHooks.hpp"
#include <iostream>

using namespace lap::core::ipc;
using namespace lap::core;

struct Data { UInt64 value; };

int main() {
    auto hooks = std::make_shared<LoggingHooks>(true);
    
    PublisherConfig cfg;
    cfg.max_chunks = 4;
    cfg.chunk_size = sizeof(Data);
    
    auto pub_result = Publisher<Data>::Create("test", cfg);
    if (!pub_result.HasValue()) {
        std::cerr << "Failed" << std::endl;
        return 1;
    }
    
    auto pub = std::make_shared<Publisher<Data>>(std::move(pub_result).Value());
    pub->SetEventHooks(hooks);
    
    // Loan some samples
    for (int i = 0; i < 6; ++i) {  // More than max_chunks
        auto sample_result = pub->Loan();
        if (!sample_result.HasValue()) {
            std::cout << "Loan " << i << " failed (expected)" << std::endl;
        } else {
            std::cout << "Loan " << i << " succeeded" << std::endl;
        }
    }
    
    return 0;
}
