cmake_minimum_required(VERSION 3.15)
project(Fs)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" ${CMAKE_MODULE_PATH})
include(FetchContent)

FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG        v1.28.0
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(gRPC)


find_package(FUSE REQUIRED)
include_directories(${FUSE_INCLUDE_DIR})
add_definitions("-D_FILE_OFFSET_BITS=64")

add_executable(server server.cpp includes/hello.pb.cc includes/hello.grpc.pb.cc)
target_link_libraries(server grpc++)
target_link_libraries(server ${FUSE_LIBRARIES})


find_package(OpenSSL REQUIRED)
add_executable(client client.cpp includes/hello.pb.cc includes/hello.grpc.pb.cc)
target_link_libraries(client grpc++)
target_link_libraries(client OpenSSL::SSL)
target_link_libraries(client ${FUSE_LIBRARIES})
