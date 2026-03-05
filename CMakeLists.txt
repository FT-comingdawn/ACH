cmake_minimum_required(VERSION 3.10.0)
project(ACH VERSION 0.1.0 LANGUAGES C CXX)

find_package(OpenSSL REQUIRED)

add_library(ACH
    src/WebServer.cpp
    src/HttpRequest.cpp
    src/HttpResponse.cpp
    src/WsConnection.cpp
)

target_include_directories(ACH
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ACH
    OpenSSL::SSL
    OpenSSL::Crypto
)
