#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>
#include <cstdlib>
#include <thread>
#include <chrono>

TEST_CASE( "1: All test cases reside in other .cpp files (empty)", "[multi-file:1]" ) {
    std::system("pkill -9 -f medialib-webserver");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}