#works in mac m1

cmake_minimum_required(VERSION 3.10)
project(STDISCM_FINAL)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required packages
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(Threads REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Generated protobuf files
set(PROTO_SRC
    ocr.pb.cc
    ocr.grpc.pb.cc
)

set(PROTO_HDR
    ocr.pb.h
    ocr.grpc.pb.h
)

# Server
add_executable(ocr_server
    server.cpp
    ${PROTO_SRC}
    ${PROTO_HDR}
)

# Tesseract + Leptonica paths (Homebrew)
set(TESSERACT_LIB /opt/homebrew/opt/tesseract/lib/libtesseract.dylib)
set(LEPTONICA_LIB /opt/homebrew/opt/leptonica/lib/liblept.dylib)
set(TESSERACT_INCLUDE /opt/homebrew/opt/tesseract/include)
set(LEPTONICA_INCLUDE /opt/homebrew/opt/leptonica/include)

include_directories(${TESSERACT_INCLUDE} ${LEPTONICA_INCLUDE})

target_link_libraries(ocr_server
    gRPC::grpc++
    protobuf::libprotobuf
    ${TESSERACT_LIB}
    ${LEPTONICA_LIB}
    Threads::Threads
)

# Client (Qt)
add_executable(ocr_client
    client.cpp
    ${PROTO_SRC}
    ${PROTO_HDR}
)

target_link_libraries(ocr_client
    Qt6::Widgets
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)
