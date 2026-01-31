# ImSMB

An app for linux, for browsing and managing files on network drives. Easily upload, download, and organize files from SMB shares with a simple, clean interface.



## Features

- **Browse Network Shares** - Navigate through files and folders on your SMB network drives just like a regular file explorer
- **Upload Files** - Drag and drop files into the app to upload them to the network, or browse and select files manually
- **Download Files** - Save files from the network to your computer with real-time progress tracking
- **Manage Files** - Delete, rename, and organize files directly on the network share
- **Custom Themes** - Change the look and feel of the app with a built-in theme editor
- **See Your Progress** - Watch upload and download progress with live progress bars


#### Styling 


- **Theme Persistence**: Save and load ImGui theme configurations
- **Color Customization**: Full ImGui color palette editing
- **Rounding Controls**: Adjust frame, window, and scrollbar rounding parameters

## Building & Dependencies

### Build Requirements

- **OpenGL 3.3+**
- **GLFW 3.x** - Window and input management
- **GLEW** - OpenGL extension loader
- **Samba 4.0+** - `libsmbclient` library

### Build
```bash
./compile 
```

### Dependencies Installation
```bash
# Ubuntu/Debian
sudo apt-get install libglfw3-dev libglew-dev libsmbclient-dev

# Fedora/RHEL
sudo dnf install glfw-devel glew-devel samba-devel

```

## Usage

### Connecting to an SMB Share
1. Launch the application: `./SMBViewer` (you can edit executable name in ` compile `)
2. Browse and navigate directories in the SMB Browser tab

### File Operations
- **Upload**: Drag files from your file explorer into the window
- **Download**: Right-click files and select download to save locally
- **Delete**: Right-click files/folders and confirm deletion
- **Rename**: Right-click and rename files on the share

### Customizing the Theme
1. Click the **Settings** tab
2. Expand **Theme** → **Colors** or **Rounding**
3. Modify colors using color pickers or adjust rounding sliders
4. Click **Save Theme** to persist changes


### Progress Tracking
Each file operation (upload/download) generates:
- Unique task ID (auto-incrementing)
- Status map entry with transferred bytes and total size
- Progress percentage calculation for UI display
- Completion flag and success status

### SMB Authentication
- Supports both authenticated and guest access


### Libraries Used
- **ImGui** - Immediate mode GUI library
- **GLFW** - Window and input management
- **GLEW** - OpenGL extensions
- **libsmbclient** - Samba client library
- **stb_image** - Image decoding


### Screenshots
<img width="1287" height="828" alt="Screenshot_20260131_165207" src="https://github.com/user-attachments/assets/e02c4d38-1ec3-41bc-bae8-5c93edf01d61" />
<break></break>
<img width="1286" height="827" alt="Screenshot_20260131_165602" src="https://github.com/user-attachments/assets/1accb3ba-940a-4b94-ae1f-e2c69f3ffacf" />



