# seventhslide_server - SeventhSlide as a server: `seventhslide --server`.
#
# Rules:
#   - Over core and the link, with Qt Network and Sql: never Qt Quick, never
#     src/app. Qt Gui comes only through core (its fonts and pictures).
#   - It runs under a QCoreApplication on a computer with no screen, so
#     nothing it runs may need a window system. A test runs the program's
#     server where none can be loaded (tests/test_server.cpp).
#   - The settings screens (FTXUI) stay thin over config_model, which knows no
#     FTXUI. Nothing else here uses it.

include(FetchContent)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        ${SEVENTHSLIDE_FTXUI_VERSION}
    GIT_SHALLOW    TRUE
)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

add_library(seventhslide_server STATIC
    command_line.cpp
    data_folder.cpp
    settings_edit.cpp
    server.cpp
    control.cpp
    commands.cpp
    config_model.cpp
    config_screen.cpp
    terminal.cpp
    run.cpp
    # Listed so AUTOMOC sees Q_OBJECT in headers that do not sit beside their
    # translation unit.
    include/seventhslide/server/server.hpp
    include/seventhslide/server/control.hpp
)

add_library(SeventhSlide::server ALIAS seventhslide_server)

target_include_directories(seventhslide_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_server
    PUBLIC
        SeventhSlide::core
        SeventhSlide::link
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)
target_link_libraries(seventhslide_server PRIVATE seventhslide_warnings ftxui::component)
# Its headers are not held to this project's warnings.
target_include_directories(seventhslide_server SYSTEM PRIVATE ${ftxui_SOURCE_DIR}/include)

target_compile_features(seventhslide_server PUBLIC cxx_std_20)

# Windows: the server as a console program of its own (console_main.cpp), as
# seventhslide.exe is a window program, beside it in bin/.
if(WIN32 AND SEVENTHSLIDE_BUILD_APP)
    add_executable(seventhslide_server_console console_main.cpp)
    set_target_properties(seventhslide_server_console PROPERTIES
        OUTPUT_NAME seventhslide-server
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    target_link_libraries(seventhslide_server_console PRIVATE
        SeventhSlide::server seventhslide_warnings)
    seventhslide_windows_executable(seventhslide_server_console)
endif()
