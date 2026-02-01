/**
 * @file        test_controlblock_size.cpp
 * @brief       Unit test for ControlBlock layout
 */

#include <gtest/gtest.h>
#include "ipc/ControlBlock.hpp"

using namespace lap::core::ipc;

TEST(ControlBlockTest, HeaderSizeWithinLimit)
{
    EXPECT_LE(sizeof(ControlBlock::header), static_cast<size_t>(32));
}

TEST(ControlBlockTest, PoolStateWithinLimit)
{
    EXPECT_LE(sizeof(ControlBlock::pool_state), static_cast<size_t>(8));
}
