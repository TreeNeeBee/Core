#include <gtest/gtest.h>
#include <string>
#include <cstdlib>
#include "CPath.hpp"

using namespace lap::core;

static std::string rm_rf_cmd(const std::string &p) {
    return std::string("rm -rf ") + p;
}

TEST(PathTest, BasicOps) {
    auto app = Path::getApplicationFolder();
    EXPECT_FALSE(app.empty());

    StringView base = Path::getBaseName("/usr/bin/test");
    EXPECT_EQ(base, "test");

    StringView folder = Path::getFolder("/usr/bin/test");
    EXPECT_EQ(folder, "/usr/bin");

    auto appended = Path::append("/tmp", "myfile.txt");
    EXPECT_NE(std::string(appended.data()), "");

    const char* tmpdir_env = std::getenv("TMPDIR");
    std::string tmpdir = tmpdir_env ? tmpdir_env : "/tmp";
    std::string tmp = tmpdir + "/lap_core_test_dir";
    std::system(rm_rf_cmd(tmp).c_str());

    EXPECT_FALSE(Path::exist(tmp));
    EXPECT_TRUE(Path::createDirectory(tmp));
    EXPECT_TRUE(Path::exist(tmp));
    EXPECT_TRUE(Path::isDirectory(tmp));

    auto filePath = tmp + "/testfile.txt";
    EXPECT_FALSE(Path::exist(filePath));
    EXPECT_TRUE(Path::createFile(filePath));
    EXPECT_TRUE(Path::exist(filePath));
    EXPECT_TRUE(Path::isFile(filePath));

    std::system(rm_rf_cmd(tmp).c_str());
}

