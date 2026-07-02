# qemu_esp32c3 target: tier-1 prototype that boots via a QEMU child process
# instead of running natively (see backends/qemu/qemu_backend.cpp). board.cpp
# only declares board/machine metadata - no sketch source, since it has no
# setup()/loop() to run in this binary.
add_sim_target(qemu_esp32c3
    ${CMAKE_CURRENT_LIST_DIR}/board.cpp
)
