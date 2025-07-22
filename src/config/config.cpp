#include "config.h"
#include <fstream>
#include <stdexcept>

ServerConfig getServerConfig(const std::string& file_name) {

	std::ifstream ifs(file_name);

	if (!ifs.is_open())
		throw std::runtime_error("Cannot open server config file: " + file_name);

	nlohmann::json json;
	ifs >> json;

	ServerConfig server_config;

	server_config.udp_ip = json["udp_ip"];
	server_config.udp_port = json["udp_port"];
	server_config.session_timeout_sec = json["session_timeout_sec"];
	server_config.cdr_file = json["cdr_file"];
	server_config.http_port = json["http_port"];
	server_config.graceful_shutdown_rate = json["graceful_shutdown_rate"];
	server_config.log_file = json["log_file"];
	server_config.log_level = json["log_level"];

	for (const auto& bl : json["blacklist"])
		server_config.blacklist.push_back(bl.get<std::string>());

	return server_config;
}

ClientConfig getClientConfig(const std::string& file_name) {

	std::ifstream ifs(file_name);

	if (!ifs.is_open())
		throw std::runtime_error("Cannot open client config file: " + file_name);

	nlohmann::json json;
	ifs >> json;

	ClientConfig client_config;

	client_config.server_ip = json["server_ip"];
	client_config.server_port = json["server_port"];
	client_config.log_file = json["log_file"];
	client_config.log_level = json["log_level"];

	return client_config;
}