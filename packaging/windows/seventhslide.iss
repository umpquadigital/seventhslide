; The Windows installer: SeventhSlide-Windows-x64-Setup.exe, from the folder
; packaging/windows/deploy.sh gathers. Inno Setup 6.3 or later:
;
;   iscc /DAppVersion=1.3.0-beta.2 /DAppNumbers=1.3.0 /DSourceDir=<deploy.sh's folder>
;        /DOutputDir=<where the installer goes> packaging\windows\seventhslide.iss
;
; AppVersion is what the app calls itself (seventhslide::version_string),
; AppNumbers the project version alone, as the file's version must be.
;
; Installing a newer one over an older one replaces it in place: the AppId
; below is the same in every release, and must never change. A SeventhSlide
; that is running is asked to close first. Installed for everyone (Program
; Files), or for this person alone where they cannot be an administrator.
; Needs Windows 10 1903 or later, for the UTF-8 code page the programs ask
; for (seventhslide.manifest).

#ifndef AppVersion
  #error Give the version: /DAppVersion=1.3.0
#endif
#ifndef AppNumbers
  #define AppNumbers AppVersion
#endif
#ifndef SourceDir
  #error Give deploy.sh's folder: /DSourceDir=...
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{8A83FB6A-1413-47EC-9DB7-45DF32C5805A}
AppName=SeventhSlide
AppVersion={#AppVersion}
AppVerName=SeventhSlide {#AppVersion}
AppPublisher=SeventhSlide
AppPublisherURL=https://seventhslide.org
AppSupportURL=https://seventhslide.org
VersionInfoVersion={#AppNumbers}.0
VersionInfoProductVersion={#AppNumbers}.0
DefaultDirName={autopf}\SeventhSlide
DefaultGroupName=SeventhSlide
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
CloseApplications=yes
RestartApplications=no
UninstallDisplayIcon={app}\seventhslide.exe
UninstallDisplayName=SeventhSlide
SetupIconFile=..\icons\seventhslide.ico
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=SeventhSlide-Windows-x64-Setup

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An older release's DLLs and QML modules, which a newer Qt's would not
; match.
Type: files; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\SeventhSlide"; Filename: "{app}\seventhslide.exe"
Name: "{autodesktop}\SeventhSlide"; Filename: "{app}\seventhslide.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\seventhslide.exe"; Description: "{cm:LaunchProgram,SeventhSlide}"; Flags: nowait postinstall skipifsilent
