#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <chrono>
#include <fstream>
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "../config/config.h"
#include <iomanip>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Функция конвертации BCD в IMSI
std::string convertBcdToImsi(const std::vector<uint8_t>& bcd) {

	std::string imsi;

	if (bcd.empty() || bcd.size() != 8)
		throw std::invalid_argument("Invalid BCD length. Must be between 1 and 8 bytes.");

	for (size_t i = 0; i < 7; ++i) {

		uint8_t high_nibble = (bcd[i] >> 4) & 0x0F;
		uint8_t low_nibble = bcd[i] & 0x0F;

		if (high_nibble > 9)
			throw std::invalid_argument("Invalid BCD digit in high nibble");

		imsi += '0' + high_nibble;

		if (low_nibble > 9)
			throw std::invalid_argument("Invalid BCD digit in low nibble");

		imsi += '0' + low_nibble;
	}

	uint8_t high_nibble_last = (bcd[7] >> 4) & 0x0F;

	if (high_nibble_last > 9)
		throw std::invalid_argument("Invalid BCD last high nibble digit");

	imsi += '0' + high_nibble_last;

	return imsi;
}

// Сервер
int main(int argc, char* argv[]) {

	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <config.json>" << std::endl;
		return 1;
	}

	std::string config_file_name = argv[1];

	// Загрузка конфигурации сервера
	ServerConfig server_config;

	try {
		server_config = getServerConfig(config_file_name);
	}
	catch (const std::exception& e) {
		std::cerr << "Error loading server config: " << e.what() << std::endl;
		return 1;
	}

	// Создание логгера
	bool is_debug_enabled = (server_config.log_level == "DEBUG");

	// Логгирование в консоль
	auto console_log = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_log->set_level(is_debug_enabled ? spdlog::level::debug : spdlog::level::info);

	// Логгирование в файл
	auto file_log = std::make_shared<spdlog::sinks::basic_file_sink_mt>(server_config.log_file, true);
	file_log->set_level(spdlog::level::debug);

	auto spd_logger = std::make_shared<spdlog::logger>(
		"server_logger",
		spdlog::sinks_init_list{ console_log, file_log }
	);

	spd_logger->set_level(spdlog::level::debug);
	spd_logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(spd_logger);

	spd_logger->info("Server starting: UDP {}:{}  HTTP port {}  CDR file {}  debug={}",
		server_config.udp_ip, server_config.udp_port, server_config.http_port, server_config.cdr_file, is_debug_enabled);

	spd_logger->debug("Config: timeout={}s, graceful_rate={} sess/sec",
		server_config.session_timeout_sec, server_config.graceful_shutdown_rate);

	std::ofstream cdr_stream(server_config.cdr_file, std::ios::app);

	if (!cdr_stream.is_open()) {
		spd_logger->critical("Cannot open CDR file: {}", server_config.cdr_file);
		return 1;
	}

	std::map<std::string, std::chrono::steady_clock::time_point> sessions;
	std::set<std::string> blacklist(server_config.blacklist.begin(), server_config.blacklist.end());
	std::mutex mutex;
	std::condition_variable cv;
	std::mutex cdr_mutex;
	bool shutting_down = false;
	bool is_shutdown_completed = false;

	// Обработка UDP
	auto handleUdp = [&]() {
		int sock = socket(AF_INET, SOCK_DGRAM, 0);
		timeval tv{ 1, 0 };

		spd_logger->debug("Start UDP");

		if (sock < 0) {
			spd_logger->critical("Cannot create UDP socket: {}", strerror(errno));
			return;
		}

		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
			spd_logger->warn("Cannot to set SO_RCVTIMEO: {}", strerror(errno));

		sockaddr_in server_address{};

		server_address.sin_family = AF_INET;
		server_address.sin_addr.s_addr = inet_addr(server_config.udp_ip.c_str());
		server_address.sin_port = htons(server_config.udp_port);

		if (bind(sock, (sockaddr*)&server_address, sizeof(server_address)) < 0) {
			spd_logger->critical("Cannot bind {}:{} – {}", server_config.udp_ip, server_config.udp_port, strerror(errno));
			close(sock);
			return;
		}

		spd_logger->info("UDP is listening on {}:{}", server_config.udp_ip, server_config.udp_port);

		while (true) {

			char buffer[8];
			sockaddr_in client_address{};
			socklen_t len = sizeof(client_address);
			int n = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&client_address, &len);
			std::string imsi;
			std::vector<uint8_t> imsi_bcd(buffer, buffer + 8);

			{
				std::lock_guard<std::mutex> lock(mutex);

				if (shutting_down) {
					spd_logger->debug("UDP thread shutted down");
					break;
				}
			}

			if (n < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;

				spd_logger->error("Recvfrom error: {}", strerror(errno));
				continue;
			}

			spd_logger->debug("Received {} bytes from {}:{}", n,
				inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

			try {
				imsi = convertBcdToImsi(imsi_bcd);
			}
			catch (const std::exception& e) {
				spd_logger->warn("Cannot decode BCD: {}", e.what());
				continue;
			}

			spd_logger->debug("Decoded IMSI {}", imsi);

			{
				std::lock_guard<std::mutex> lock(mutex);

				if (blacklist.count(imsi) || sessions.count(imsi)) {
					sendto(sock, "rejected", 8, 0, (sockaddr*)&client_address, len);
					spd_logger->info("Subscriber {} rejected", imsi);
				}
				else {
					sessions[imsi] = std::chrono::steady_clock::now();

					{
						std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
						auto now_c = std::time(nullptr);

						cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
							<< "," << imsi << ",create\n";
					}

					spd_logger->info("Session created for IMSI {}", imsi);

					sendto(sock, "created", 7, 0, (sockaddr*)&client_address, len);
				}
			}
		}

		close(sock);
		};

	// Обработка HTTP
	auto handleHttp = [&]() {

		spd_logger->debug("Start HTTP");

		httplib::Server http_server;

		http_server.Get("/check_subscriber", [&](auto& request, auto& result) {
			std::string imsi = request.get_param_value("imsi");

			spd_logger->debug("HTTP /check_subscriber imsi={}", imsi);

			std::lock_guard<std::mutex> lock(mutex);

			result.body = sessions.count(imsi) ? "active" : "not active";
			});

		http_server.Get("/stop", [&](auto&, auto& result) {
			spd_logger->info("HTTP /stop called");

			{
				std::lock_guard<std::mutex> lock(mutex);
				shutting_down = true;
			}

			http_server.stop();

			result.status = 200;

			result.body = "Shutdown initiated";

			});

		if (!http_server.listen("0.0.0.0", server_config.http_port))
			spd_logger->error("Cannot HTTP listen on port {}", server_config.http_port);
		};

	// Очистка сессий по тайм-ауту и плавное завершение работы
	auto sessionDeleter = [&]() {
		spd_logger->debug("Starting cleanup thread");

		while (true) {
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (shutting_down) break;
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
			std::vector<std::string> imsi_delete_list;
			auto now = std::chrono::steady_clock::now();

			{
				std::lock_guard<std::mutex> lock(mutex);

				for (auto& [imsi, creation_time] : sessions) {
					if (now - creation_time > std::chrono::seconds(server_config.session_timeout_sec))
						imsi_delete_list.push_back(imsi);
				}
			}

			for (auto& imsi : imsi_delete_list) {
				{
					std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
					auto now_c = std::time(nullptr);

					cdr_stream << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
						<< "," << imsi << ",deleted\n";
				}

				{
					std::lock_guard<std::mutex> lock(mutex);
					sessions.erase(imsi);
				}

				spd_logger->info("Session deleted for IMSI {}", imsi);
			}
		}

		// Плавное завершение работы
		spd_logger->info("Graceful shutdown with {} deleted sessions per sec", server_config.graceful_shutdown_rate);

		while (true) {
			std::vector<std::string> delete_list;

			{
				std::lock_guard<std::mutex> lock(mutex);

				if (sessions.empty())
					break;

				int counter = 0;

				for (auto& [imsi, creation_time] : sessions) {
					if (counter++ >= server_config.graceful_shutdown_rate)
						break;

					delete_list.push_back(imsi);
				}
			}

			if (delete_list.empty())
				spd_logger->debug("No sessions to delete with shutdown");

			{
				std::lock_guard<std::mutex> cdr_lock(cdr_mutex);
				auto now_counter = std::time(nullptr);

				for (auto& imsi : delete_list) {
					cdr_stream << std::put_time(std::localtime(&now_counter), "%Y-%m-%d %H:%M:%S")
						<< "," << imsi << ",delete with shutdown\n";

					spd_logger->info("Gracefully deleted session for IMSI {}", imsi);

					std::lock_guard<std::mutex> lock(mutex);
					sessions.erase(imsi);
				}
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			is_shutdown_completed = true;
			cv.notify_one();
		}

		spd_logger->info("Graceful shutdown completed");
		};

	// Создание потоков для запуска всех активных функций
	std::thread thr1(handleUdp), thr2(handleHttp), thr3(sessionDeleter);
	{
		std::unique_lock<std::mutex> lock(mutex);
		cv.wait(lock, [&] { return is_shutdown_completed; });
	}

	// Ожидание завершения всех потоков
	thr1.join();
	thr2.join();
	thr3.join();

	spd_logger->info("Exit from server");
	cdr_stream.close();
	spdlog::shutdown();

	return 0;
}
