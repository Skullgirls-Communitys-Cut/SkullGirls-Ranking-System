cmake_minimum_required(VERSION 3.15)

# Проверяем наличие исходников
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/curl/CMakeLists.txt")
    message(FATAL_ERROR "Curl sources not found at external/curl")
endif()

# Сбрасываем кэш (хорошая практика)
unset(BUILD_SHARED_LIBS CACHE)
unset(HTTP_ONLY CACHE)
unset(CURL_USE_SCHANNEL CACHE)
unset(BUILD_CURL_EXE CACHE)
unset(BUILD_TESTING CACHE)
unset(CMAKE_INSTALL_PREFIX CACHE)

# Настройки статической сборки
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static library" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build curl.exe" FORCE)
set(HTTP_ONLY ON CACHE BOOL "Enable HTTP only" FORCE)
# Для использования OpenSSL
set(CURL_USE_SCHANNEL OFF CACHE BOOL "Use Windows SSL/TLS" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "Use OpenSSL" FORCE)
set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32" CACHE PATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
# OpenSSL paths 
set(OPENSSL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32/include" CACHE PATH "" FORCE)
#set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32/lib/libcrypto.lib" CACHE FILEPATH "" FORCE)
#set(OPENSSL_SSL_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32/lib/libssl.lib" CACHE FILEPATH "" FORCE)
set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32" CACHE PATH "" FORCE)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32/include")
set(OPENSSL_USE_STATIC_LIBS ON)
set(CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenSSL-Win32/include" CACHE PATH "" FORCE)

# Отключаем лишнее
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(ENABLE_UNICODE ON CACHE BOOL "" FORCE)

# Тесты и примеры не нужны
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Статический CRT (/MT) — только Release
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)

# Куда складывать готовые файлы
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/external/curl/curl_output_x86" CACHE PATH "" FORCE)