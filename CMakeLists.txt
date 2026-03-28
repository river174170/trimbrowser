cmake_minimum_required(VERSION 3.16)
project(TrimBrowser CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build type ──────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Source files ────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/browser_core.cpp
    src/litehtml_container.cpp
)

# ── litehtml (vendored) ──────────────────────────────────────────────────────
# litehtml is a git submodule at vendor/litehtml
add_subdirectory(vendor/litehtml EXCLUDE_FROM_ALL)

# ── libcurl (vendored static build) ─────────────────────────────────────────
# We build a minimal static curl against the sysroot's openssl
find_library(CURL_LIB NAMES curl libcurl PATHS ${CMAKE_SYSROOT}/usr/lib
             ${CMAKE_FIND_ROOT_PATH}/usr/lib NO_DEFAULT_PATH)
if(NOT CURL_LIB)
  # Fall back to vendored static curl
  set(CURL_LIB "${CMAKE_CURRENT_BINARY_DIR}/vendor/curl/lib/libcurl.a")
  set(HAVE_BUNDLED_CURL TRUE)
endif()

# ── Find system libraries ────────────────────────────────────────────────────
# When cross-compiling, CMAKE_FIND_ROOT_PATH points to the TrimUI sysroot
find_package(Freetype REQUIRED)

# SDL2 — try pkg-config first, then manual
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
endif()
if(NOT SDL2_FOUND)
  find_path(SDL2_INCLUDE_DIR SDL2/SDL.h
            PATHS ${CMAKE_SYSROOT}/usr/include ${CMAKE_FIND_ROOT_PATH}/usr/include)
  find_library(SDL2_LIBRARY NAMES SDL2 libSDL2
               PATHS ${CMAKE_SYSROOT}/usr/lib ${CMAKE_FIND_ROOT_PATH}/usr/lib)
endif()

# OpenSSL 1.1
find_library(SSL_LIB   NAMES ssl   libssl   PATHS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)
find_library(CRYPTO_LIB NAMES crypto libcrypto PATHS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)

# ── Executable ──────────────────────────────────────────────────────────────
add_executable(trimbrowser ${SOURCES})

target_include_directories(trimbrowser PRIVATE
    src
    vendor/litehtml/include
    ${FREETYPE_INCLUDE_DIRS}
)

if(SDL2_FOUND AND TARGET PkgConfig::SDL2)
    target_link_libraries(trimbrowser PRIVATE PkgConfig::SDL2)
else()
    target_include_directories(trimbrowser PRIVATE ${SDL2_INCLUDE_DIR})
    target_link_libraries(trimbrowser PRIVATE ${SDL2_LIBRARY})
endif()

target_link_libraries(trimbrowser PRIVATE
    litehtml
    ${FREETYPE_LIBRARIES}
    ${CURL_LIB}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    z       # libz (zlib)
    pthread
    dl
    m
    stdc++fs
)

# Reduce binary size
target_compile_options(trimbrowser PRIVATE -Os -ffunction-sections -fdata-sections)
set_target_properties(trimbrowser PROPERTIES
    LINK_FLAGS "-Wl,--gc-sections -Wl,-rpath,'$ORIGIN'"
)

# ── Install / packaging ──────────────────────────────────────────────────────
install(TARGETS trimbrowser DESTINATION .)
