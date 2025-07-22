add_executable(mini_pgw_server server.cpp)
target_link_libraries(mini_pgw_server PRIVATE config nlohmann_json::nlohmann_json httplib::httplib spdlog::spdlog)