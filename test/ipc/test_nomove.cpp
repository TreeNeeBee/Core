#include "../../source/inc/ipc/Publisher.hpp"
#include <iostream>

using namespace lap::core::ipc;
using namespace lap::core;

struct Data { UInt64 value; };

int main() {
    PublisherConfig cfg;
    cfg.max_chunks = 4;
    
    std::cout << "Creating publisher..." << std::endl;
    auto pub_result = Publisher<Data>::Create("test", cfg);
    if (!pub_result.HasValue()) {
        std::cerr << "Failed" << std::endl;
        return 1;
    }
    
    std::cout << "Publisher created, getting pointer..." << std::endl;
    Publisher<Data>* pub_ptr = &pub_result.Value();
    
    std::cout << "Loaning..." << std::endl;
    auto loan = pub_ptr->Loan();
    std::cout << "Loan: " << (loan.HasValue() ? "OK" : "FAIL") << std::endl;
    
    return 0;
}

#include "../../source/src/ipc/Publisher.cpp"
