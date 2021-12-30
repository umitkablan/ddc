add_executable(
    filefakedev fakefile.cpp)

conan_target_link_libraries(filefakedev)

target_link_libraries(filefakedev amqpcpp)

install(TARGETS filefakedev)
