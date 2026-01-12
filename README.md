# Windows Port Killer

A simple GUI application to view, search, and kill processes using network ports on Windows.

![Windows](https://img.shields.io/badge/platform-Windows-blue)
![Python](https://img.shields.io/badge/python-3.8%2B-green)
![License](https://img.shields.io/badge/license-MIT-orange)

## Features

- **View All Connections** - See all active network connections with details
- **Search by Port** - Filter connections by port number
- **Search by Process** - Filter by process name
- **Protocol Filter** - Filter by TCP/UDP
- **Kill Processes** - Terminate processes that are using specific ports
- **Sortable Columns** - Click headers to sort by any column
- **Right-Click Menu** - Quick actions (kill, copy PID, copy port)
- **Dark Theme** - Easy on the eyes

## Screenshot

```
+------------------------------------------------------------------+
|  Port Killer - Windows Port Manager                              |
+------------------------------------------------------------------+
|  Search Port: [____]  Search Process: [____]  Protocol: [ALL v]  |
|                                          [Refresh] [Kill Selected]|
+------------------------------------------------------------------+
|  Port | Protocol | Local Address | Foreign Address | State | PID |
|-------|----------|---------------|-----------------|-------|-----|
|  80   | TCP      | 0.0.0.0:80    | 0.0.0.0:0      | LISTEN| 4   |
|  443  | TCP      | 0.0.0.0:443   | 0.0.0.0:0      | LISTEN| 1234|
|  3000 | TCP      | 127.0.0.1:3000| 0.0.0.0:0      | LISTEN| 5678|
+------------------------------------------------------------------+
```

## Installation

### Option 1: Download Executable (Recommended)

1. Go to [Releases](https://github.com/SlasshyOverhere/windows-port-killer/releases)
2. Download `PortKiller.exe`
3. Run as Administrator for full functionality

### Option 2: Run from Source

```bash
# Clone the repository
git clone https://github.com/SlasshyOverhere/windows-port-killer.git
cd windows-port-killer

# Run the application
python port_killer.py
```

## Usage

1. **Launch the app** - Double-click `PortKiller.exe` or run `python port_killer.py`
2. **Run as Administrator** - Required to kill system processes
3. **Search** - Use the search boxes to filter by port or process name
4. **Kill a process** - Select a row and click "Kill Selected" or double-click

### Keyboard Shortcuts

- **Double-click** - Kill selected process
- **Right-click** - Context menu with options

## Requirements

- Windows 10/11
- Python 3.8+ (only if running from source)
- Administrator privileges (for killing processes)

## Building from Source

```bash
# Install PyInstaller
pip install pyinstaller

# Build the executable
pyinstaller --onefile --windowed --name "PortKiller" port_killer.py
```

The executable will be created in the `dist` folder.

## How It Works

The application uses Windows built-in commands:
- `netstat -ano` - Lists all network connections with PIDs
- `tasklist` - Gets process names from PIDs
- `taskkill /F /PID` - Terminates processes

## License

MIT License - feel free to use and modify as needed.

## Contributing

Pull requests are welcome! For major changes, please open an issue first.
