cmake_minimum_required(VERSION 3.15)
project(Fs)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG        v1.28.0
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(gRPC)


add_executable(server server.cpp hello.pb.cc hello.grpc.pb.cc)
target_link_libraries(server grpc++)
add_executable(client client.cpp hello.pb.cc hello.grpc.pb.cc)
target_link_libraries(client grpc++)
