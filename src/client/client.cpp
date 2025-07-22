#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../config/config.h"

// Конвертация IMSI в BCD
std::vector<uint8_t> convertImsiToBcd(const std::string& imsi) {

	std::vector<uint8_t> bcd(8, 0);

	for (size_t i = 0; i < imsi.length(); ++i) {

		char c = imsi[i];

		if (!isdigit(static_cast<unsigned char>(c)))
			throw std::invalid_argument("IMSI char must be digit");

		int digit = c - '0';

		if (i % 2 == 0)
			bcd[i / 2] = static_cast<uint8_t>(digit << 4);
		else
			bcd[i / 2] |= static_cast<uint8_t>(digit);
	}

	bcd[7] |= 0x0F;

	return bcd;
}

// Клиент
int main(int argc, char* argv[]) {

	if (argc != 3) {
		std::cerr << "Usage: " << argv[0] << " <config.json> <IMSI>" << std::endl;
		return 1;
	}

	std::string config_file_name = argv[1];
	std::string imsi = argv[2];

	// Загрузка конфигурации
	ClientConfig client_config;

	try {
		client_config = getClientConfig(config_file_name);
	}
	catch (const std::exception& e) {
		std::cerr << "Error loading client config: " << e.what() << std::endl;
		return 1;
	}

	// Создание логгера
	bool is_debug_enabled = (client_config.log_level == "DEBUG");

	// Логгирование в консоль
	auto console_log = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_log->set_level(is_debug_enabled ? spdlog::level::debug : spdlog::level::info);

	// Логгирование в файл
	auto file_log = std::make_shared<spdlog::sinks::basic_file_sink_mt>(client_config.log_file, true);
	file_log->set_level(spdlog::level::debug);

	auto logger = std::make_shared<spdlog::logger>(
		"client_logger",
		spdlog::sinks_init_list{ console_log, file_log }
	);

	logger->set_level(spdlog::level::debug);
	logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(logger);

	logger->info("Client starting, IMSI={}, config={}, debug={}",
		imsi, config_file_name, is_debug_enabled);
	logger->debug("Loaded client config: server_ip={}, server_port={}, log_file={}",
		client_config.server_ip, client_config.server_port, client_config.log_file);

	// Конвертация IMSI в BCD
	std::vector<uint8_t> imsi_bcd;

	try {
		imsi_bcd = convertImsiToBcd(imsi);
	}
	catch (const std::exception& e) {
		logger->error("Cannot convert IMSI to BCD '{}': {}", imsi, e.what());
		spdlog::shutdown();
		return 1;
	}

	logger->debug("IMSI '{}' converted to BCD: [{}]", imsi,
		fmt::join(imsi_bcd, ","));

	// Создание UDP-сокета
	int sock = socket(AF_INET, SOCK_DGRAM, 0);

	if (sock < 0) {
		logger->critical("Cannot create UDP socket: {}", strerror(errno));
		spdlog::shutdown();
		return 1;
	}

	logger->debug("UDP socket created (fd={})", sock);

	sockaddr_in server_address{};
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(client_config.server_port);

	if (inet_pton(AF_INET, client_config.server_ip.c_str(), &server_address.sin_addr) <= 0) {
		logger->critical("Invalid server IP address: {}", client_config.server_ip);

		close(sock);
		spdlog::shutdown();
		return 1;
	}

	logger->debug("Set server address: {}:{}", client_config.server_ip, client_config.server_port);

	// Отправка на сервер
	ssize_t sent = sendto(sock,
		imsi_bcd.data(),
		imsi_bcd.size(),
		0,
		reinterpret_cast<struct sockaddr*>(&server_address),
		sizeof(server_address));

	if (sent < 0) {
		logger->error("Cannot sendto: {}", strerror(errno));

		close(sock);
		spdlog::shutdown();
		return 1;
	}

	logger->info("Sent {} bytes to {}:{}", sent,
		client_config.server_ip, client_config.server_port);

	// Получение ответа от сервера
	char buffer[32] = {};
	socklen_t len = sizeof(server_address);

	ssize_t n = recvfrom(sock,
		buffer,
		sizeof(buffer) - 1,
		0,
		reinterpret_cast<struct sockaddr*>(&server_address),
		&len);

	if (n < 0) {
		logger->error("Cannot recvfrom: {}", strerror(errno));

		close(sock);
		spdlog::shutdown();
		return 1;
	}

	buffer[n] = '\0';

	std::string response(buffer);

	logger->info("Received response ({} bytes): '{}'", n, response);

	std::cout << response << std::endl;
	close(sock);
	spdlog::shutdown();

	return 0;
}
