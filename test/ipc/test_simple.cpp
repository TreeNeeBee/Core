#include "../../source/inc/ipc/Publisher.hpp"
#include <iostream>

using namespace lap::core::ipc;
using namespace lap::core;

struct Data { UInt64 value; };

int main() {
    PublisherConfig cfg;
    cfg.max_chunks = 4;
    cfg.chunk_size = sizeof(Data);
    
    std::cout << "Creating publisher..." << std::endl;
    auto pub_result = Publisher<Data>::Create("test", cfg);
    
    if (!pub_result.HasValue()) {
        std::cerr << "Failed to create publisher: error code=" 
                  << pub_result.Error().Value() << std::endl;
        return 1;
    }
    
    std::cout << "Publisher created successfully" << std::endl;
    
    auto pub = std::make_shared<Publisher<Data>>(std::move(pub_result).Value());
    
    std::cout << "Attempting to loan..." << std::endl;
    auto loan_result = pub->Loan();
    if (!loan_result.HasValue()) {
        std::cerr << "Loan failed: error code=" << loan_result.Error().Value() << std::endl;
        return 1;
    }
    
    std::cout << "Loan succeeded!" << std::endl;
    return 0;
}
#include "../../source/src/ipc/Publisher.cpp"
