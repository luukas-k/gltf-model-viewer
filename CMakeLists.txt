cmake_minimum_required(VERSION 4.0)

project(gltf_model_viewer)

add_executable(gltf_model_viewer "src/main.cpp")
set_property(TARGET gltf_model_viewer PROPERTY CXX_STANDARD 23)

include(FetchContent)

# GLFW
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG master)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLAD
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG 73db193f853e2ee079bf3ca8a64aa2eaf6459043 GIT_PROGRESS TRUE SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(glad)

glad_add_library(glad_gl_core_46 STATIC REPRODUCIBLE LOADER API gl:core=4.6)

# STB
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master)
FetchContent_GetProperties(stb)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# GLM
FetchContent_Declare(glm GIT_REPOSITORY	https://github.com/g-truc/glm.git GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863)
FetchContent_MakeAvailable(glm)

# json
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

# Link dependencies

target_link_libraries(gltf_model_viewer PRIVATE stb glfw glad_gl_core_46 glm::glm nlohmann_json::nlohmann_json)


# vs debug working directory
set_property(TARGET gltf_model_viewer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/")
