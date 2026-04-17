@echo off
@set PATH=%PATH%;%~dp0bin
@chcp 65001 > nul

title Win32 OpenSSL Command Prompt
echo Win32 OpenSSL Command Prompt
echo.
openssl version -a
echo.

%SystemDrive%
cd %UserProfile%

cmd.exe /K
