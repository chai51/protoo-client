cmake_minimum_required(VERSION 3.8)

project(protoo)
cmake_policy(SET CMP0091 NEW)

# 工程选项
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd /Od /D")
else()
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")
endif()

# 工程配置
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(
  include
  deps
  ${Boost_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)
link_directories(
  ${Boost_LIBRARY_DIRS}
)

file(GLOB FILE
  src/*.cpp
)

add_library(protoo STATIC ${FILE})

target_link_libraries(
  protoo
)

install(TARGETS protoo
  LIBRARY DESTINATION lib
)
install(DIRECTORY
  ${PROJECT_SOURCE_DIR}/include DESTINATION inlcude
)