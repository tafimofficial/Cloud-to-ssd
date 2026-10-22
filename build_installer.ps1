$ErrorActionPreference = "Stop"

$msvc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207"
$sdk = "10.0.26100.0"
$sdkDir = "C:\Program Files (x86)\Windows Kits\10"

Write-Host "Configuring MSVC build environment for Installer..."
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

Set-Location "t:\Cloud-SSD"

# 1. Compile Uninstall.exe
Write-Host "Compiling 'Uninstall.exe'..."
& $clExe /nologo /O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS "installer_src\uninstaller.c" /Fe:"t:\Cloud-SSD\installer_bundle\Uninstall.exe" /link /SUBSYSTEM:WINDOWS "shell32.lib" "advapi32.lib" "user32.lib"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile Uninstall.exe"
}

# 2. Refresh payload bundle on fast local drive
Write-Host "Preparing bundle files on local storage..."
$b = "$env:TEMP\tb_$([System.IO.Path]::GetRandomFileName())"
New-Item -ItemType Directory -Path $b -Force | Out-Null
Copy-Item "Tafim Drive.exe" "$b\Tafim Drive.exe" -Force
if (Test-Path "C:\Users\T\AppData\Local\Programs\Tafim Drive\rclone.exe") {
    Copy-Item "C:\Users\T\AppData\Local\Programs\Tafim Drive\rclone.exe" "$b\rclone.exe" -Force
} else {
    Copy-Item "tools\rclone\rclone.exe" "$b\rclone.exe" -Force
}
if (Test-Path "C:\Users\T\AppData\Local\Programs\CloudPlayX\cpx.exe") {
    Copy-Item "C:\Users\T\AppData\Local\Programs\CloudPlayX\cpx.exe" "$b\cpx.exe" -Force
} elseif (Test-Path "tools\cpx.exe") {
    Copy-Item "tools\cpx.exe" "$b\cpx.exe" -Force
}
Copy-Item "rclone.conf" "$b\rclone.conf" -Force
Copy-Item "tafim_drive.ico" "$b\tafim_drive.ico" -Force
Copy-Item "tafim_drives.json" "$b\tafim_drives.json" -Force
Copy-Item "installer_bundle\Uninstall.exe" "$b\Uninstall.exe" -Force

Write-Host "Compressing payload.zip..."
tar.exe -a -cf "$env:TEMP\payload.zip" -C $b *
Copy-Item "$env:TEMP\payload.zip" "t:\Cloud-SSD\payload.zip" -Force

# 3. Compile installer resource
Write-Host "Compiling installer resource (icon + embedded payload.zip)..."
Set-Location "t:\Cloud-SSD\installer_src"
& $rcExe /fo "installer_resource.res" "installer_resource.rc"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile installer resource"
}

# 4. Compile Installer executable
Write-Host "Compiling 'Tafim Drive Setup.exe'..."
$installerLibs = @("kernel32.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "advapi32.lib", "comctl32.lib", "dwmapi.lib")
& $clExe /nologo /O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS "installer.c" "installer_resource.res" $installerLibs /Fe:"t:\Cloud-SSD\Tafim Drive Setup.exe" /link /SUBSYSTEM:WINDOWS
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile Tafim Drive Setup.exe"
}

Set-Location "t:\Cloud-SSD"
Write-Host "Installer built successfully!"
Get-Item "t:\Cloud-SSD\Tafim Drive Setup.exe" | Select-Object Name, Length, LastWriteTime
