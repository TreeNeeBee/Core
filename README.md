# Core

A C++ project using C++17 standard features.

## Features

This project demonstrates various C++17 features including:
- Structured bindings
- `std::optional`
- `std::variant`
- `std::filesystem`
- if-init statements
- `std::visit`

## Building

### Prerequisites
- CMake 3.10 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)

### Build Instructions

```bash
mkdir build
cd build
cmake ..
make
```

## Running

After building, run the executable:

```bash
./build/core
```

## C++17 Standard

This project is configured to use the C++17 standard through CMake:
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```