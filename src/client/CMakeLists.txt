add_executable(mini_pgw_client client.cpp)
target_link_libraries(mini_pgw_client PRIVATE config nlohmann_json::nlohmann_json spdlog::spdlog)