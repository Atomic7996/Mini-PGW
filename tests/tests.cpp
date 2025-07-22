#include <gtest/gtest.h>
#include "../src/config/config.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <sys/socket.h>
#include <thread>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Функция для создания тестового конфигурационного файла
static void createTestConfigFile(const std::string& fileName, const std::string& text) {
    std::ofstream ofs(fileName);
    ofs << text;
    ofs.close();
}

TEST(ConfigTest, LoadServerConfigValid) {
    const std::string fileName = "server_test_config.json";

    createTestConfigFile(fileName, R"({
        "udp_ip":"127.0.0.1",
        "udp_port":5050,
        "session_timeout_sec":30,
        "cdr_file":"cdr.log",
        "http_port":8080,
        "graceful_shutdown_rate":5,
        "log_file":"server.log",
        "log_level":"INFO",
        "blacklist":["000000000000001","123456789123456"]
    })");

    ServerConfig cfg = getServerConfig(fileName);

    EXPECT_EQ(cfg.udp_ip, "127.0.0.1");
    EXPECT_EQ(cfg.udp_port, 5050);
    EXPECT_EQ(cfg.session_timeout_sec, 30);
    EXPECT_EQ(cfg.cdr_file, "cdr.log");
    EXPECT_EQ(cfg.http_port, 8080);
    EXPECT_EQ(cfg.graceful_shutdown_rate, 5);
    EXPECT_EQ(cfg.log_file, "server.log");
    EXPECT_EQ(cfg.log_level, "INFO");
    EXPECT_EQ(cfg.blacklist.size(), 2u);
    EXPECT_EQ(cfg.blacklist[0], "000000000000001");
    EXPECT_EQ(cfg.blacklist[1], "123456789123456");

    std::remove(fileName.c_str());
}

TEST(ConfigTest, LoadServerConfigNoFile) {
    EXPECT_THROW(getServerConfig("not_exist_server.json"), std::runtime_error);
}

TEST(ConfigTest, LoadClientConfigValid) {
    const std::string fileName = "client_test_config.json";

    createTestConfigFile(fileName, R"({
        "server_ip":"127.0.0.1",
        "server_port":5050,
        "log_file":"client.log",
        "log_level":"INFO"
    })");

    ClientConfig cfg = getClientConfig(fileName);

    EXPECT_EQ(cfg.server_ip, "127.0.0.1");
    EXPECT_EQ(cfg.server_port, 5050);
    EXPECT_EQ(cfg.log_file, "client.log");
    EXPECT_EQ(cfg.log_level, "INFO");

    std::remove(fileName.c_str());
}

TEST(ConfigTest, LoadClientConfigNoFile) {
    EXPECT_THROW(getClientConfig("not_exist_client.json"), std::runtime_error);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
