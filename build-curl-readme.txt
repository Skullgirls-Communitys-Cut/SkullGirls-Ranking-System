cmake -B _curl_build_temp -A Win32 -C build_curl_x86.cmake .
cmake --build _curl_build_temp --config Release --target install
