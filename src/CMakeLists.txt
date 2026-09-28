cmake_minimum_required(VERSION 3.13)
project(nfs3ds_src C)

file(GLOB SRC_FILES "*.c" "*/**.c")
add_executable(nfs3ds_demo ${SRC_FILES})

# Include headers
target_include_directories(nfs3ds_demo PRIVATE ${CMAKE_SOURCE_DIR}/../include)

# Link libraries (devkitPro / citro3d / libctru) - user to configure toolchain
# target_link_libraries(nfs3ds_demo citro3d ctrulib)
