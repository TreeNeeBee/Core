#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // NOTE: Unit tests are executed in alphabetical order by default.
    // InitializationTest is named to run first (starts with 'I').
    // Each test manages its own initialization/deinitialization lifecycle.
    // 
    // For tests that don't test CInitialization itself, they should:
    // 1. Call Initialize() in their SetUp() or at test start
    // 2. Call Deinitialize() in their TearDown() or at test end
    //
    // This ensures proper AUTOSAR initialization lifecycle management.
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}