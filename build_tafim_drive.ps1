$ErrorActionPreference = "Stop"

$msvc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
$sdk = "10.0.26100.0"
$sdkDir = "C:\Program Files (x86)\Windows Kits\10"

Write-Host "Configuring MSVC build environment..."
$env:INCLUDE = @(
    "$msvc\include",
    "$sdkDir\Include\$sdk\ucrt",
    "$sdkDir\Include\$sdk\um",
    "$sdkDir\Include\$sdk\shared"
) -join ";"

$env:LIB = @(
    "$msvc\lib\x64",
    "$sdkDir\Lib\$sdk\ucrt\x64",
    "$sdkDir\Lib\$sdk\um\x64"
) -join ";"

$env:PATH = "$msvc\bin\Hostx64\x64;$sdkDir\bin\$sdk\x64;$env:PATH"

$rcExe = "$sdkDir\bin\$sdk\x64\rc.exe"
$clExe = "$msvc\bin\Hostx64\x64\cl.exe"

Set-Location "t:\Cloud-SSD\tafim_drive_src"

Write-Host "Compiling Windows resource file (icon and metadata)..."
& $rcExe /fo "resource.res" "resource.rc"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Resource compilation failed!"
}

Write-Host "Compiling C source files into 'Tafim Drive.exe'..."
$cFiles = @("main.c", "tafim_config.c", "tafim_terabox.c", "tafim_mount.c", "tafim_ui.c", "tafim_server.c", "tafim_gui.c")
$libs = @("kernel32.lib", "user32.lib", "gdi32.lib", "shell32.lib", "mpr.lib", "advapi32.lib", "winhttp.lib", "ws2_32.lib", "comctl32.lib", "dwmapi.lib", "uxtheme.lib")

& $clExe /nologo /O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS $cFiles "resource.res" $libs /Fe:"Tafim Drive.exe" /link /SUBSYSTEM:WINDOWS
if ($LASTEXITCODE -ne 0) {
    Write-Error "C compilation failed!"
}

Write-Host "Copying 'Tafim Drive.exe' to root workspace directory..."
Copy-Item "Tafim Drive.exe" "t:\Cloud-SSD\Tafim Drive.exe" -Force

Set-Location "t:\Cloud-SSD"
Write-Host "Build complete successfully!"
Get-Item "t:\Cloud-SSD\Tafim Drive.exe" | Select-Object -Property Name, Length, LastWriteTime
