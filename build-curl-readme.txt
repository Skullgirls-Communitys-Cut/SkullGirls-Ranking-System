cmake -B _build_temp -G "Visual Studio 17 2022" -A Win32 -C build_curl_x86.cmake .
cmake --build _build_temp --config Release --target install