#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <filesystem>

// C++17 features demonstration
namespace fs = std::filesystem;

// Using std::optional (C++17)
std::optional<std::string> findUser(int id) {
    if (id == 1) {
        return "Alice";
    }
    return std::nullopt;
}

// Using std::variant (C++17)
using Value = std::variant<int, double, std::string>;

void printValue(const Value& v) {
    std::visit([](const auto& val) {
        std::cout << "Value: " << val << std::endl;
    }, v);
}

int main() {
    std::cout << "C++17 Core Project" << std::endl;
    std::cout << "==================" << std::endl;
    
    // Structured bindings (C++17)
    auto [x, y] = std::make_pair(10, 20);
    std::cout << "Structured binding: x=" << x << ", y=" << y << std::endl;
    
    // std::optional
    auto user = findUser(1);
    if (user) {
        std::cout << "Found user: " << *user << std::endl;
    }
    
    // std::variant
    Value val1 = 42;
    Value val2 = 3.14;
    Value val3 = std::string("Hello");
    
    printValue(val1);
    printValue(val2);
    printValue(val3);
    
    // if-init statement (C++17)
    if (auto result = findUser(2); result) {
        std::cout << "User found: " << *result << std::endl;
    } else {
        std::cout << "User not found" << std::endl;
    }
    
    std::cout << "\nAll C++17 features working correctly!" << std::endl;
    
    return 0;
}
