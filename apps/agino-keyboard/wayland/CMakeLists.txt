# Qt Wayland shell factories are a private ABI; the deb pins the matching ABI.
add_library(agino-keyboard-shell MODULE keyboardshell.cpp)
target_include_directories(agino-keyboard-shell PRIVATE ../src)
target_link_libraries(agino-keyboard-shell PRIVATE Qt6::WaylandClientPrivate)
qt6_generate_wayland_protocol_client_sources(agino-keyboard-shell PRIVATE_CODE
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml)
set_target_properties(agino-keyboard-shell PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/wayland-shell-integration)
install(TARGETS agino-keyboard-shell LIBRARY DESTINATION
    lib/x86_64-linux-gnu/qt6/plugins/wayland-shell-integration)
install(FILES protocols/wlr-layer-shell-unstable-v1.xml
    DESTINATION share/doc/agino-keyboard/protocols)
# wlr get_popup references xdg_popup even though the keyboard never uses it.
# Qt's private xdg symbols are hidden: generate protocol descriptors locally.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(protocol_dir wayland-protocols pkgdatadir)
set(xdg_protocol ${protocol_dir}/stable/xdg-shell/xdg-shell.xml)
set(xdg_code ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
add_custom_command(OUTPUT ${xdg_code}
    COMMAND Wayland::Scanner private-code ${xdg_protocol} ${xdg_code}
    DEPENDS ${xdg_protocol} Wayland::Scanner)
target_sources(agino-keyboard-shell PRIVATE ${xdg_code})
target_link_options(agino-keyboard-shell PRIVATE -Wl,--no-undefined)
install(FILES ${xdg_protocol} DESTINATION share/doc/agino-keyboard/protocols)
