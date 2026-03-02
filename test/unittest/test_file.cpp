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
        File::Util::Remove(testFile);
        File::Util::Remove(testFileCopy);
    }

    std::string testFile;
    std::string testFileCopy;
};

TEST_F(FileTest, Exists) {
    EXPECT_TRUE(File::Util::Exists(testFile));
    EXPECT_FALSE(File::Util::Exists("non_existent_file.txt"));
}

TEST_F(FileTest, Remove) {
    EXPECT_TRUE(File::Util::Remove(testFile));
    EXPECT_FALSE(File::Util::Exists(testFile));
}

TEST_F(FileTest, Copy) {
    EXPECT_TRUE(File::Util::Copy(testFile, testFileCopy));
    EXPECT_TRUE(File::Util::Exists(testFileCopy));
}

TEST_F(FileTest, Move) {
    std::string movedFile = "moved_file.txt";
    EXPECT_TRUE(File::Util::Move(testFile, movedFile));
    EXPECT_TRUE(File::Util::Exists(movedFile));
    EXPECT_FALSE(File::Util::Exists(testFile));
    File::Util::Remove(movedFile);
}

TEST_F(FileTest, Create) {
    std::string newFile = "new_file.txt";
    EXPECT_TRUE(File::Util::Create(newFile));
    EXPECT_TRUE(File::Util::Exists(newFile));
    File::Util::Remove(newFile);
}

TEST_F(FileTest, Size) {
    EXPECT_EQ(File::Util::FileSize(testFile), 12); // "Test content" is 12 bytes
    EXPECT_EQ(File::Util::FileSize("non_existent_file.txt"), 0);
}