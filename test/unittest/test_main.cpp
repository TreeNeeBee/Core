#include <gtest/gtest.h>
#include "CMemory.hpp"

int main(int argc, char** argv)
{
    ::lap::core::MemManager::getInstance()->initialize();;

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}