cmake_minimum_required(VERSION 3.16)
project(vlm_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(HailoRT REQUIRED)
find_package(OpenCV REQUIRED)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(vlm_app main.cpp backend.cpp)

target_include_directories(vlm_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

if(WIN32)
    target_compile_definitions(vlm_app PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

target_link_libraries(vlm_app PRIVATE
    HailoRT::libhailort
    ${OpenCV_LIBS}
    nlohmann_json::nlohmann_json
)

install(TARGETS vlm_app RUNTIME DESTINATION bin)
