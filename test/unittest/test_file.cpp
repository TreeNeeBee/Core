#include <gtest/gtest.h>
#include "CFile.hpp"

using namespace lap::core;

class FileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary files for testing
        testFile = "test_file.txt";
        testFileCopy = "test_file_copy.txt";
        std::ofstream ofs(testFile);
        ofs << "Test content";
    }

    void TearDown() override {
        // Clean up temporary files
        File::remove(testFile);
        File::remove(testFileCopy);
    }

    std::string testFile;
    std::string testFileCopy;
};

TEST_F(FileTest, Exists) {
    EXPECT_TRUE(File::exists(testFile));
    EXPECT_FALSE(File::exists("non_existent_file.txt"));
}

TEST_F(FileTest, Remove) {
    EXPECT_TRUE(File::remove(testFile));
    EXPECT_FALSE(File::exists(testFile));
}

TEST_F(FileTest, Copy) {
    EXPECT_TRUE(File::copy(testFile, testFileCopy));
    EXPECT_TRUE(File::exists(testFileCopy));
}

TEST_F(FileTest, Move) {
    std::string movedFile = "moved_file.txt";
    EXPECT_TRUE(File::move(testFile, movedFile));
    EXPECT_TRUE(File::exists(movedFile));
    EXPECT_FALSE(File::exists(testFile));
    File::remove(movedFile);
}

TEST_F(FileTest, Create) {
    std::string newFile = "new_file.txt";
    EXPECT_TRUE(File::create(newFile));
    EXPECT_TRUE(File::exists(newFile));
    File::remove(newFile);
}

TEST_F(FileTest, Size) {
    EXPECT_EQ(File::size(testFile), 12); // "Test content" is 12 bytes
    EXPECT_EQ(File::size("non_existent_file.txt"), 0);
}