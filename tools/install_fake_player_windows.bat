@echo off
setlocal EnableDelayedExpansion

REM Fake Player Engine - Windows install helper
REM Default server path (edit if your rAthena root is elsewhere)
set "SERVER_ROOT=C:\Users\spd20\Desktop\server"

if not "%~1"=="" set "SERVER_ROOT=%~1"

echo ============================================
echo  Fake Player Engine - Install Helper
echo  Server root: %SERVER_ROOT%
echo ============================================
echo.

if not exist "%SERVER_ROOT%\src\map\map-server.vcxproj" (
    echo [ERROR] rAthena map-server not found at:
    echo   %SERVER_ROOT%
    echo.
    echo Usage: install_fake_player_windows.bat [path_to_rathena]
    exit /b 1
)

cd /d "%SERVER_ROOT%"

echo [1/4] Checking git branch...
where git >nul 2>&1
if %ERRORLEVEL%==0 (
    git rev-parse --is-inside-work-tree >nul 2>&1
    if !ERRORLEVEL!==0 (
        echo   Git repo detected. Recommended:
        echo     git fetch origin
        echo     git pull origin master
        echo   Or run: tools\merge_fake_player_server.bat
    )
) else (
    echo   Git not found - copy files from PR manually or apply patch:
    echo   tools\fake_player_engine.patch
)

echo.
echo [2/4] Verifying required files...
set MISSING=0
for %%F in (
    "conf\battle\fake_player.conf"
    "sql-files\fake_player\main.sql"
    "db\fake_player\fake_map_db.txt"
    "db\fake_player\fake_skill_db.txt"
    "db\fake_player\fake_equip_db.txt"
    "src\map\fake_player.hpp"
    "src\map\fake_player\fake_player_core.cpp"
    "src\map\fake_player\fake_player_ai.cpp"
    "src\map\fake_player\fake_player_db.cpp"
) do (
    if not exist "%SERVER_ROOT%\%%~F" (
        echo   [MISSING] %%~F
        set MISSING=1
    )
)
if "%MISSING%"=="1" (
    echo.
    echo [ERROR] Some files are missing. Pull branch cursor/fake-player-engine-66ff first.
    exit /b 1
)
echo   All core files present.

echo.
echo [3/4] SQL database...
echo   Run in HeidiSQL / MySQL client:
echo     USE ragnarok;
echo     SOURCE %SERVER_ROOT:\=/%/sql-files/fake_player/main.sql;
echo.
echo   Or command line:
echo     mysql -u root -p ragnarok ^< sql-files\fake_player\main.sql

echo.
echo [4/4] Build map-server...
echo   Option A - Visual Studio:
echo     Open rAthena.sln ^> Build map-server (Release)
echo   Option B - MSYS2/MinGW:
echo     ./configure --enable-packetver=YOUR_PACKETVER
echo     make map
echo.

echo Config: edit conf\battle\fake_player.conf
echo   fake_player_enable: yes
echo   fake_max_population: 50   ^(start low, increase later^)
echo.
echo After start map-server, in-game GM:
echo   @fakeplayerstats
echo   @fakeplayer
echo   @reloadfakeplayerdb
echo.
echo PR: https://github.com/RyzaC4/rathena/pull/2
echo Done.
pause
