@echo off
rem === Сборка ОДНОГО файла GITHUBCLOAD_GUI.exe (GUI + движок с Python и библиотеками) ===
rem Нужно: C:\PORTABLE\mingw32 (i686-w64-mingw32-g++ и windres) + python с pyinstaller
rem Шаги: 1) pyinstaller собирает ядро; 2) windres вшивает его как ресурс; 3) g++ линкует GUI.

set MINGW=C:\PORTABLE\mingw32\bin
set PY=python

echo [1/3] PyInstaller: собираю ядро GITHUBCLOAD_core.exe ...
%PY% -m PyInstaller --noconfirm --onefile --console --name GITHUBCLOAD_core --distpath dist --workpath build GITHUBCLOAD.py
if errorlevel 1 ( echo [ОШИБКА] PyInstaller && pause & exit /b 1 )

echo [2/3] windres: вшиваю ядро в ресурс ...
"%MINGW%\windres.exe" core_res.rc -O coff -o core_res.o
if errorlevel 1 ( echo [ОШИБКА] windres && pause & exit /b 1 )

echo [3/3] g++: собираю единый GITHUBCLOAD_GUI.exe ...
"%MINGW%\g++.exe" GCB_GUI.cpp core_res.o -o GITHUBCLOAD_GUI.exe -mwindows -O2 -std=c++17 -static -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE -lgdiplus -lgdi32 -luser32 -lcomctl32 -lole32 -luuid -lshlwapi -lshell32 -ladvapi32
if errorlevel 1 ( echo [ОШИБКА] g++ && pause & exit /b 1 )

del /q dist build core_res.o GITHUBCLOAD_core.spec 2>nul
rd /s /q dist build 2>nul
echo [OK] Готов единый файл: GITHUBCLOAD_GUI.exe
pause
