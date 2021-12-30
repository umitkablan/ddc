add_executable(
        qlistener qlistener.cpp)

target_link_libraries(qlistener amqpcpp redis++)

conan_target_link_libraries(qlistener)

install(TARGETS qlistener)
