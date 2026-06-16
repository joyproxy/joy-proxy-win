#Requires -Version 5.1
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

Write-Host "==> Building Rust relay..."
if (Get-Command cargo -ErrorAction SilentlyContinue) {
    cargo build --release -p joyproxy_relay
    if ($LASTEXITCODE -ne 0) { throw "cargo build failed" }
    New-Item -ItemType Directory -Force -Path "$Root\artifacts\$Configuration" | Out-Null
    Copy-Item "$Root\target\release\joyproxy_relay.dll" "$Root\artifacts\$Configuration\" -Force
} else {
    Write-Warning "cargo not found, skip relay build"
}

Write-Host "==> Building native (CMake)..."
$NativeBuild = "$Root\build\native"
New-Item -ItemType Directory -Force -Path $NativeBuild | Out-Null
cmake -S "$Root\src\native" -B $NativeBuild -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $NativeBuild --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Copy-Item "$NativeBuild\$Configuration\JoyProxyHook.dll" "$Root\artifacts\$Configuration\" -Force
Copy-Item "$NativeBuild\$Configuration\JoyProxyService.exe" "$Root\artifacts\$Configuration\" -Force

Write-Host "==> Building UI..."
$UiDir = "$Root\src\ui\JoyProxy"
if (Get-Command dotnet -ErrorAction SilentlyContinue) {
    dotnet publish $UiDir -c $Configuration -r win-x64 --self-contained false -o "$Root\artifacts\$Configuration"
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
} else {
    Write-Warning "dotnet not found, skip UI build"
}

Write-Host "Done: $Root\artifacts\$Configuration"
