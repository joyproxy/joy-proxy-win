@echo off
REM JoyProxy Windows - push to GitHub via JoyProxy edge
REM Usage: deploy.bat [YOUR_GITHUB_PAT]

setlocal
set HTTP_PROXY=http://sg-xx.edge.joyproxy.com:10000
set HTTPS_PROXY=http://sg-xx.edge.joyproxy.com:10000

if not "%~1"=="" (
  echo %~1| gh auth login --hostname github.com --git-protocol https --with-token
  if errorlevel 1 exit /b 1
)

git push -u origin main
if errorlevel 1 exit /b 1

echo.
echo Done: https://github.com/joyproxy/joy-proxy-win
echo.
