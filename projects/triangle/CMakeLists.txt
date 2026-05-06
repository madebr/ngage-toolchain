cmake_minimum_required(VERSION 3.10)

project(triangle C)

find_package(SDL3 REQUIRED)

add_executable(triangle
  src/main.c
)
target_link_libraries(triangle PRIVATE SDL3::SDL3)

set_property(TARGET triangle PROPERTY C_STANDARD 99)

if(NGAGESDK)
  target_link_options(triangle PRIVATE "SHELL:-s UID1=0x1000007a") # KExecutableImageUidValue, e32uid.h
  target_link_options(triangle PRIVATE "SHELL:-s UID2=0x100039ce") # KAppUidValue16, apadef.h
  target_link_options(triangle PRIVATE "SHELL:-s UID3=0x1000c37e") # triangle.exe UID
endif()
