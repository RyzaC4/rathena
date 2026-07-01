@echo off
setlocal EnableDelayedExpansion

REM Merge Fake Player Engine into local rAthena server
REM Default: C:\Users\spd20\Desktop\server
set "SERVER_ROOT=C:\Users\spd20\Desktop\server"
if not "%~1"=="" set "SERVER_ROOT=%~1"

echo ============================================
echo  Merge Fake Player Engine -> Server
echo  Path: %SERVER_ROOT%
echo ============================================
echo.

if not exist "%SERVER_ROOT%\src\map\map-server.vcxproj" (
    echo [ERROR] Not an rAthena folder: %SERVER_ROOT%
    exit /b 1
)

cd /d "%SERVER_ROOT%"

where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Git required. Install Git for Windows first.
    exit /b 1
)

git rev-parse --is-inside-work-tree >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] %SERVER_ROOT% is not a git repository.
    echo   Clone first: git clone https://github.com/RyzaC4/rathena.git server
    exit /b 1
)

echo [1] Save local changes (stash)...
git status --porcelain | findstr /r "." >nul
if %ERRORLEVEL%==0 (
    echo   Uncommitted changes found - stashing...
    git stash push -m "before-fake-player-merge"
    set STASHED=1
) else (
    set STASHED=0
)

echo.
echo [2] Fetch from origin...
git fetch origin
if %ERRORLEVEL% neq 0 (
    echo [ERROR] git fetch failed. Check network / remote URL.
    exit /b 1
)

echo.
echo [3] Merge Fake Player (choose one method)...
echo   A = merge master (includes Fake Player after upstream merge)
echo   B = merge branch cursor/fake-player-engine-66ff only
echo   C = apply tools\fake_player.diff patch
set /p MERGE_MODE=Choose [A/B/C] (default A):

if /i "%MERGE_MODE%"=="" set MERGE_MODE=A
if /i "%MERGE_MODE%"=="B" goto MERGE_BRANCH
if /i "%MERGE_MODE%"=="C" goto APPLY_PATCH
goto MERGE_MASTER

:MERGE_MASTER
echo   Merging origin/master...
git merge origin/master --no-edit
goto MERGE_DONE

:MERGE_BRANCH
echo   Merging origin/cursor/fake-player-engine-66ff...
git merge origin/cursor/fake-player-engine-66ff --no-edit
goto MERGE_DONE

:APPLY_PATCH
if not exist "tools\fake_player.diff" (
    echo [ERROR] tools\fake_player.diff not found. Pull master first.
    exit /b 1
)
git apply --check tools\fake_player.diff
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Patch check failed - conflicts with your custom files.
    echo   Fix conflicts manually or upload server to get help.
    exit /b 1
)
git apply tools\fake_player.diff
goto MERGE_DONE

:MERGE_DONE
if %ERRORLEVEL% neq 0 (
    echo.
    echo [CONFLICT] Merge has conflicts. Common files:
    echo   src/custom/atcommand.inc
    echo   src/custom/battle_config_init.inc
    echo   src/map/pc.cpp, clif.cpp, map.cpp
    echo.
    echo Resolve conflicts, then:
    echo   git add .
    echo   git commit -m "Merge Fake Player Engine"
    if "%STASHED%"=="1" echo   git stash pop
    exit /b 1
)

echo.
echo [4] Merge OK.

if "%STASHED%"=="1" (
    echo [5] Restore stashed changes...
    git stash pop
    if %ERRORLEVEL% neq 0 (
        echo [WARN] stash pop had conflicts - resolve manually.
    )
)

echo.
echo [6] Next steps:
echo   mysql -u root -p ragnarok ^< sql-files\fake_player\main.sql
echo   Edit conf\battle\fake_player.conf - fake_player_enable: yes
echo   Rebuild map-server in Visual Studio
echo   In-game: @fakeplayerstats
echo.
pause
