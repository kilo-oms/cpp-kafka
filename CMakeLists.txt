cmake_minimum_required(VERSION 3.14)
project(MarketDepthProcessing CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost headers (header-only OK)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

# Source files
set(SRC_FILES
    src/main.cpp
    src/KafkaConsumer.cpp
    src/KafkaProducer.cpp
    src/MessageFactory.cpp
)

add_executable(order_book ${SRC_FILES})

# Include directories
target_include_directories(order_book
    PRIVATE
        include
        ${Boost_INCLUDE_DIRS}
)

# Link libraries (yaml-cpp, rdkafka, pthreads)
target_link_libraries(order_book
    PRIVATE
        Threads::Threads
        yaml-cpp
        rdkafka   # This is equivalent to -lrdkafka         
)

# Optional: Make a release target
add_custom_target(release
    DEPENDS order_book
)