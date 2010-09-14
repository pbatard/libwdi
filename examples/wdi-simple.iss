; This examples demonstrates how libwdi can be used in an installer script
; to automatically intall usb drviers along with your application.
;
; Requirements: Inno Setup (http://www.jrsoftware.org/isdl.php)
;
; To use this script, do the following:
; - configure libwdi (see config.h)
; - compile wdi-simple.exe
; - customize other settings (strings)
; - open this script with Inno Setup
; - modify the run section at the bottom for your device hardware id
; - compile and run

[Setup]
AppName = YourApplication
AppVerName = YourApplication 0.1.10.2
AppPublisher = YourApplication
AppPublisherURL = http://test.url.com/
AppVersion = 0.1.10.1
DefaultDirName = {pf}\TestApp
DefaultGroupName = YourApplication
Compression = lzma
SolidCompression = yes
; Win2000 or higher
MinVersion = 5,5

; This installation requires admin priveledges.  This is needed to install
; drivers on windows vista and later.  This is not strictly required with
; libwdi; it can detect and elevate privileges when needed.
PrivilegesRequired = admin

[Files]
; copy the 32bit wdi installer to the application directory.
; note: this installer also works with 64bit
Source: "wdi-simple.exe"; DestDir: "{app}"; Flags: replacesameversion promptifolder;

[Icons]
Name: "{group}\Uninstall YourApplication"; Filename: "{uninstallexe}"

[Run]
; run the 32bit wdi installer
;
; -n, --name <name>          set the device name
; -f, --inf <name>           set the inf name
; -m, --manufacturer <name>  set the manufacturer name
; -v, --vid <id>             set the vendor ID (VID)
; -p, --pid <id>             set the product ID (PID)
; -i, --iid <id>             set the interface ID (MI)
; -t, --type <driver_type>   set the driver to install
;                            (0=WinUSB, 1=libusb0.sys, 2=custom)
; -d, --dest <dir>           set the extraction directory
; -x, --extract              extract files only (don't install)
; -s, --silent               silent mode
; -b, --progressbar=[HWND]   display a progress bar during install
;                            an optional HWND can be specified
; -l, --log                  set log level (0 = debug, 4 = none)
; -h, --help                 display usage
;
Filename: "{app}\wdi-simple.exe"; Flags: "runhidden"; Parameters: " --name ""XBox Controller"" --vid 0x045e --pid 0x0289 --progressbar={wizardhwnd}"; StatusMsg: "Installing YourApplication driver (this may take a few seconds) ...";

