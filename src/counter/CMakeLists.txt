
add_executable(
  countersrv countersrv.cpp)

target_include_directories(countersrv PRIVATE ${crow_all_BINARY_DIR})
target_link_libraries(countersrv redis++)

conan_target_link_libraries(countersrv)

install(TARGETS countersrv)
