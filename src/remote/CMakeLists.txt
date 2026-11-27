# seventhslide_remote - LAN web remote transport.
#
# Rules:
#   - No Qt Quick, and no dependency on src/app. The only operator surface this
#     library may touch is IRemoteBridge, which src/app implements.
#   - Core stays HTTP-free (see src/core/CMakeLists.txt); all serving lives here.

add_library(seventhslide_remote STATIC
    remote_bridge.cpp
    remote_protocol.cpp
    remote_auth.cpp
    remote_thumbs.cpp
    remote_http_util.cpp
    remote_server.cpp
    remote_routes.cpp
    # Listed so AUTOMOC sees Q_OBJECT in headers that do not sit beside their
    # translation unit (RemoteServer lives under include/seventhslide/remote).
    include/seventhslide/remote/remote_server.hpp
)

add_library(SeventhSlide::remote ALIAS seventhslide_remote)

target_include_directories(seventhslide_remote
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_remote
    PUBLIC
        SeventhSlide::core
        Qt6::Core
        Qt6::Network
        Qt6::HttpServer
        Qt6::WebSockets
)
target_link_libraries(seventhslide_remote PRIVATE seventhslide_warnings)

target_compile_features(seventhslide_remote PUBLIC cxx_std_20)

# The web app is embedded rather than installed alongside the binary, so a
# package cannot lose it.
qt_add_resources(seventhslide_remote "remote_web"
    PREFIX "/remote"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/web"
    FILES
        web/index.html
        # The client's stylesheets and modules; the server serves every file
        # in css/ and js/.
        web/css/tokens.css
        web/css/base.css
        web/css/controls.css
        web/css/layout.css
        web/css/lists.css
        web/css/controller.css
        web/js/main.js
        web/js/dom.js
        web/js/state.js
        web/js/connection.js
        web/js/gestures.js
        web/js/panels.js
        web/js/controller.js
        web/js/well.js
        web/js/preview.js
        web/js/stacks.js
        web/js/service.js
        web/js/library.js
        web/js/bible.js
        web/js/menus.js
        web/manifest.webmanifest
        # Browser/PWA icons.
        web/icons/favicon.png
        web/icons/favicon.ico
        web/icons/apple-touch-icon.png
        web/icons/icon-192.png
        web/icons/icon-512.png
)
