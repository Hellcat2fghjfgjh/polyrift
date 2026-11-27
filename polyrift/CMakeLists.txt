cmake_minimum_required(VERSION 3.15)
project(polyrift CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Link the C++ runtime into the program so the downloadable builds run on machines
# that have no compiler or redistributable installed.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(polyrift ${SOURCES})

if(WIN32)
  target_link_libraries(polyrift PRIVATE ws2_32)
endif()
if(MSVC)
  target_compile_options(polyrift PRIVATE /W3)
else()
  target_compile_options(polyrift PRIVATE -Wall -Wextra)
  if(MINGW)
    target_link_options(polyrift PRIVATE -static)
  elseif(NOT APPLE)
    target_link_options(polyrift PRIVATE -static-libstdc++ -static-libgcc)
  endif()
endif()

# Keep the web client next to the executable so "./polyrift" just works from the build folder.
add_custom_command(TARGET polyrift POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/public $<TARGET_FILE_DIR:polyrift>/public)
