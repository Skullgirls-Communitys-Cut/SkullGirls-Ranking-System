cmake_minimum_required(VERSION 3.15)
project(BuildCurl)

# mbedTLS
add_subdirectory(external/mbedtls)

# Curl
add_subdirectory(external/curl)
