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
        File::Util::remove(testFile);
        File::Util::remove(testFileCopy);
    }

    std::string testFile;
    std::string testFileCopy;
};

TEST_F(FileTest, Exists) {
    EXPECT_TRUE(File::Util::exists(testFile));
    EXPECT_FALSE(File::Util::exists("non_existent_file.txt"));
}

TEST_F(FileTest, Remove) {
    EXPECT_TRUE(File::Util::remove(testFile));
    EXPECT_FALSE(File::Util::exists(testFile));
}

TEST_F(FileTest, Copy) {
    EXPECT_TRUE(File::Util::copy(testFile, testFileCopy));
    EXPECT_TRUE(File::Util::exists(testFileCopy));
}

TEST_F(FileTest, Move) {
    std::string movedFile = "moved_file.txt";
    EXPECT_TRUE(File::Util::move(testFile, movedFile));
    EXPECT_TRUE(File::Util::exists(movedFile));
    EXPECT_FALSE(File::Util::exists(testFile));
    File::Util::remove(movedFile);
}

TEST_F(FileTest, Create) {
    std::string newFile = "new_file.txt";
    EXPECT_TRUE(File::Util::create(newFile));
    EXPECT_TRUE(File::Util::exists(newFile));
    File::Util::remove(newFile);
}

TEST_F(FileTest, Size) {
    EXPECT_EQ(File::Util::size(testFile), 12); // "Test content" is 12 bytes
    EXPECT_EQ(File::Util::size("non_existent_file.txt"), 0);
}