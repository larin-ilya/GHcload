@echo off
rem === Сборка GITHUBCLOAD_GUI.exe (неоморфный интерфейс на Win32 + GDI+) ===
rem Требуется набор C:\PORTABLE\mingw32 (i686-w64-mingw32-g++)

set MINGW=C:\PORTABLE\mingw32\bin
if not exist "%MINGW%\g++.exe" (
  echo [ОШИБКА] Компилятор не найден: %MINGW%\g++.exe
  pause & exit /b 1
)

"%MINGW%\g++.exe" GCB_GUI.cpp -o GITHUBCLOAD_GUI.exe -mwindows -O2 -std=c++17 -static -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE -lgdiplus -lgdi32 -luser32 -lcomctl32 -lole32 -luuid -lshlwapi -lshell32 -ladvapi32
if %errorlevel% neq 0 (
  echo [ОШИБКА] Сборка провалилась.
  pause & exit /b 1
)
echo [OK] Готово: GITHUBCLOAD_GUI.exe
pause
