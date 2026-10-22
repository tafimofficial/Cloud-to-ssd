# 🚀 Tafim Drive

**Tafim Drive** is a high-performance Windows desktop application and virtual cloud drive manager written in native **C**. It seamlessly connects multiple cloud storage accounts—including **TeraBox (1024 GB free tier)**, **Google Drive**, **Microsoft OneDrive**, **Dropbox**, **Mega.nz**, and **WebDAV/NAS**—and mounts them directly as native Windows drive letters (such as `T:`, `U:`, `G:`, `O:`, `D:`, `M:`, `W:`) in Windows File Explorer.

---

## ✨ Features

- **Multi-Cloud & Multi-Account:** Connect multiple accounts simultaneously. Mount multiple TeraBox accounts (1024 GB each), Google Drive, OneDrive, Mega, Dropbox, or custom WebDAV servers under independent drive letters.
- **Dynamic Windows Explorer Storage Bar:** Live used vs. total storage progress bar directly in Windows File Explorer (e.g. `1023 GB free of 1024 GB`), exactly matching native local disks with no question marks (`?`).
- **Browser-Based Authentication:** Clean loopback HTTP OAuth/login integration. Clicking any cloud button opens your default browser for easy authentication and automatic drive mounting.
- **Offline VFS Caching (`--vfs-cache-mode full`):**
  - Files opened or created are saved to your local SSD cache for instant offline access.
  - Automatically re-syncs new and modified files to the cloud when internet connection is restored.
  - Customize the offline cache folder location (any SSD or hard drive) and set custom cache size limits (GB) directly in the GUI.
- **Persistent Auto-Mount on Boot:**
  - Automatically mounts all configured cloud drives on Windows startup or reboot.
  - Runs silently in the system tray.
- **Native Windows Installer & Settings Integration:**
  - Includes `Tafim Drive Setup.exe` with a dark-mode installer wizard.
  - Cleanly registered in Windows Settings (`Installed apps` / `Apps & features`) with full uninstall support.

---

## 📁 Repository Structure

```text
├── tafim_drive_src/           # Core Application Source Code
│   ├── main.c                 # Application entry point, CLI arguments & boot handler
│   ├── tafim_gui.c            # Win32 dark GUI, list view, buttons & cache settings
│   ├── tafim_mount.c          # WinFsp & rclone mount engine, dynamic storage calculation
│   ├── tafim_server.c         # Embedded loopback HTTP authentication server & web views
│   ├── tafim_config.c         # JSON configuration manager (load, parse, save)
│   ├── tafim_terabox.c        # TeraBox protocol & account connectivity
│   ├── tafim_ui.c             # Interactive CLI dashboard and table renderer
│   ├── tafim_drive.h          # Core header definitions and data models
│   └── resource.rc            # Application icon and version metadata definition
│
├── installer_src/             # Setup & Uninstaller Source Code
│   ├── installer.c            # Native Win32 installer wizard with dark UI
│   ├── uninstaller.c          # Native uninstaller for Windows Settings integration
│   └── installer_resource.rc  # Installer resource definition with embedded payload
│
├── release/                   # Pre-compiled Executables
│   ├── Tafim Drive Setup.exe  # All-in-one native Windows installer
│   └── Tafim Drive.exe        # Standalone portable executable
│
├── build_tafim_drive.ps1      # PowerShell script to compile Tafim Drive.exe
├── build_installer.ps1        # PowerShell script to compile Tafim Drive Setup.exe
├── tafim_drive.ico            # Multi-resolution application icon (16x16 to 256x256)
├── tafim_drives.json          # Template configuration file
├── rclone.conf                # Template rclone backend configuration
├── .gitignore                 # Standard build and cache exclusions
└── LICENSE                    # MIT License
```

---

## 🛠️ Building from Source

### Prerequisites
1. **Windows 10 / 11 (64-bit)**
2. **Microsoft Visual C++ (MSVC)** via Visual Studio 2022 Community or Build Tools
3. **Windows 10/11 SDK** (`rc.exe`)
4. **WinFsp** (Windows File System Proxy) installed on the system

### 1. Build `Tafim Drive.exe`
Open PowerShell in the project directory and run:
```powershell
.\build_tafim_drive.ps1
```

### 2. Build `Tafim Drive Setup.exe` (All-in-One Installer)
```powershell
.\build_installer.ps1
```

---

## 🚀 Installation & Usage

1. Run **`Tafim Drive Setup.exe`** from the `release/` directory.
2. Select your desired install location (default: `%LOCALAPPDATA%\Programs\Tafim Drive`).
3. Click **Install Now**.
4. Open **Tafim Drive** from the Desktop or Start Menu.
5. Click **+ Connect TeraBox**, **+ Google Drive**, **+ OneDrive**, **+ Dropbox**, **+ Mega**, or **+ WebDAV** to mount your cloud drives.
6. Access your files instantly in Windows File Explorer!

---

## 📄 License
This project is licensed under the MIT License.
