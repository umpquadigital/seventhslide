# seventhslide_link - the sync's connections between SeventhSlides.
#
# Rules:
#   - No Qt Quick, and no dependency on src/app.
#   - Core stays network-free (see src/core/CMakeLists.txt); the connections
#     live here, over core's sync engine, member list and crypto.

add_library(seventhslide_link STATIC
    tls.cpp
    frames.cpp
    routes.cpp
    outbox.cpp
    media_in.cpp
    peer.cpp
    peer_hello.cpp
    peer_want.cpp
    peer_media.cpp
    peer_pieces.cpp
    peer_files.cpp
    peer_parts.cpp
    link.cpp
    discovery.cpp
    reach.cpp
    admin.cpp
    library_sync.cpp
    sync_node.cpp
    sync_settings.cpp
    # Listed so AUTOMOC sees Q_OBJECT in headers that do not sit beside their
    # translation unit.
    include/seventhslide/link/peer.hpp
    include/seventhslide/link/link.hpp
    include/seventhslide/link/discovery.hpp
    include/seventhslide/link/reach.hpp
    include/seventhslide/link/library_sync.hpp
    include/seventhslide/link/sync_turns.hpp
    include/seventhslide/link/sync_node.hpp
)

add_library(SeventhSlide::link ALIAS seventhslide_link)

target_include_directories(seventhslide_link
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_link
    PUBLIC
        SeventhSlide::core
        Qt6::Core
        Qt6::Network
)
# OpenSSL hands Qt the key, and reads the peer's out of its certificate.
target_link_libraries(seventhslide_link PRIVATE seventhslide_warnings OpenSSL::Crypto)
# Closing a connection no socket took (``Link``): a shared Qt does not pass this on.
if(WIN32)
    target_link_libraries(seventhslide_link PRIVATE ws2_32)
endif()

target_compile_features(seventhslide_link PUBLIC cxx_std_20)
