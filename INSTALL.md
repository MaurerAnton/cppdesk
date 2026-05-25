# Installation Guide

## Linux
### Ubuntu/Debian
```bash
sudo dpkg -i cppdesk_1.3.0_amd64.deb
sudo apt-get install -f  # fix dependencies
```

### Fedora/RHEL
```bash
sudo rpm -i cppdesk-1.3.0.x86_64.rpm
```

### Arch Linux
```bash
yay -S cppdesk
```

### Flatpak
```bash
flatpak install flathub com.cppdesk.CppDesk
```

### Snap
```bash
sudo snap install cppdesk
```

### AppImage
```bash
chmod +x cppdesk-1.3.0-x86_64.AppImage
./cppdesk-1.3.0-x86_64.AppImage
```

## Windows
1. Download `cppdesk-1.3.0-win64.exe`
2. Run the installer
3. Or use portable ZIP: extract and run `cppdesk.exe`

## macOS
1. Download `cppdesk-1.3.0.dmg`
2. Open the DMG
3. Drag cppdesk to Applications
4. Grant Accessibility and Screen Recording permissions when prompted

## Android
1. Download APK from GitHub Releases
2. Enable 'Install from Unknown Sources'
3. Install the APK

## iOS
1. Available via TestFlight
2. Or sideload via AltStore

## Docker
```bash
docker pull maureranton/cppdesk:latest
docker run -p 21116:21116 -p 21117:21117 maureranton/cppdesk:latest
```

## Build from Source
See [BUILDING.md](docs/BUILDING.md)