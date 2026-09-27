; ============================================================================
;  FT891Remote - Inno Setup script (bilingual English / French)
;
;  Saved as UTF-8 with a byte-order mark: without it Inno Setup may read the
;  file in the system code page, and the French accents come out garbled.
;
;  Build the two executables first, then run build_all.bat, which gathers
;  everything the installer needs into installer\dist and compiles this
;  file with Inno Setup 6 (ISCC.exe FT891Remote.iss).
;
;  The user picks which programs to install: both, the server only, or the
;  client only.
; ============================================================================

#define AppName        "FT891Remote"
; The version is passed by build_all.bat (/DAppVersion=x.y.z), which reads it
; from CMakeLists.txt. The value below only serves when this file is compiled
; by hand, and is then wrong: go through build_all.bat.
#ifndef AppVersion
  #define AppVersion   "0.0.0"
#endif
#define AppPublisher   "FT891Remote Community"
#define AppURL         "https://github.com/"
#define ServerExe      "ft891remote-server.exe"
#define ClientExe      "ft891remote.exe"

[Setup]
AppId={{7B1E6C3A-2F4D-4E8B-9A61-8913F0C2D5A4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} setup
VersionInfoCopyright=MIT licence
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#ClientExe}
LicenseFile=..\LICENSE.txt
OutputDir=output
OutputBaseFilename=FT891Remote-{#AppVersion}-setup
SetupIconFile=..\icons\ft891remote.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes
ShowLanguageDialog=yes

; --------------------------------------------------------------- languages
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french";  MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
; ---- English
english.ComponentsTitle=Which programs do you want to install?
english.CompServer=Station server (runs next to the FT-891)
english.CompClient=Remote front panel (runs where you are)
english.CompShared=Shared runtime libraries
english.TypeFull=Both programs
english.TypeServer=Station server only
english.TypeClient=Remote front panel only
english.TypeCustom=Custom
english.TaskDesktopServer=Create a desktop shortcut for the server
english.TaskDesktopClient=Create a desktop shortcut for the client
english.TaskFirewall=Allow FT891Remote through the Windows firewall (TCP 7300, UDP 7301)
english.LaunchServer=Start the station server
english.InstallingRuntime=Installing the Microsoft Visual C++ runtime...
english.LaunchClient=Start the remote front panel
english.NeedComponent=Please select at least one program to install.

; ---- Français
french.ComponentsTitle=Quels programmes voulez-vous installer ?
french.CompServer=Serveur de station (tourne à côté du FT-891)
french.CompClient=Façade distante (tourne là où vous êtes)
french.CompShared=Bibliothèques d'exécution communes
french.TypeFull=Les deux programmes
french.TypeServer=Serveur de station seul
french.TypeClient=Façade distante seule
french.TypeCustom=Personnalisé
french.TaskDesktopServer=Créer un raccourci du serveur sur le Bureau
french.TaskDesktopClient=Créer un raccourci du client sur le Bureau
french.TaskFirewall=Autoriser FT891Remote dans le pare-feu Windows (TCP 7300, UDP 7301)
french.LaunchServer=Démarrer le serveur de station
french.InstallingRuntime=Installation du runtime Microsoft Visual C++...
french.LaunchClient=Démarrer la façade distante
french.NeedComponent=Veuillez sélectionner au moins un programme à installer.

; -------------------------------------------------------------- components
[Types]
Name: "full";   Description: "{cm:TypeFull}"
Name: "server"; Description: "{cm:TypeServer}"
Name: "client"; Description: "{cm:TypeClient}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "server"; Description: "{cm:CompServer}"; Types: full server
Name: "client"; Description: "{cm:CompClient}"; Types: full client

[Tasks]
Name: "desktopserver"; Description: "{cm:TaskDesktopServer}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Components: server
Name: "desktopclient"; Description: "{cm:TaskDesktopClient}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Components: client
Name: "firewall";      Description: "{cm:TaskFirewall}"; Components: server

; ------------------------------------------------------------------- files
[Files]
; The two programs, each tied to its component.
Source: "dist\{#ServerExe}"; DestDir: "{app}"; Components: server; Flags: ignoreversion
Source: "dist\{#ClientExe}"; DestDir: "{app}"; Components: client; Flags: ignoreversion

; Everything else (Qt, PortAudio, Opus, plugin folders) is shared by both
; programs and always installed.
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "{#ServerExe},{#ClientExe},vc_redist.x64.exe"

; The Visual C++ runtime, when build_all.bat found it: a fresh Windows does not
; have it, and both programs would stop at start-up on a missing MSVCP140.dll.
; Unpacked to a temporary folder, run, then deleted. The test is made against
; SourcePath: ISCC runs from the project root, not from this folder.
#ifexist SourcePath + "dist\vc_redist.x64.exe"
Source: "dist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
#endif

Source: "..\README.md";    DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt";  DestDir: "{app}"; Flags: ignoreversion

; ------------------------------------------------------------------- icons
[Icons]
Name: "{group}\FT891Remote Server"; Filename: "{app}\{#ServerExe}"; Components: server
Name: "{group}\FT891Remote"; Filename: "{app}\{#ClientExe}"; Components: client
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\FT891Remote Server"; Filename: "{app}\{#ServerExe}"; \
    Components: server; Tasks: desktopserver
Name: "{autodesktop}\FT891Remote"; Filename: "{app}\{#ClientExe}"; \
    Components: client; Tasks: desktopclient

; --------------------------------------------------------------------- run
[Run]
; A runtime already present, of the same or a newer version, makes it return
; at once with a non-zero code, which Inno Setup ignores.
#ifexist SourcePath + "dist\vc_redist.x64.exe"
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "{cm:InstallingRuntime}"; Flags: waituntilterminated
#endif
Filename: "{sys}\netsh.exe"; Tasks: firewall; Flags: runhidden; \
    Parameters: "advfirewall firewall add rule name=""FT891Remote control (TCP 7300)"" dir=in action=allow protocol=TCP localport=7300"
Filename: "{sys}\netsh.exe"; Tasks: firewall; Flags: runhidden; \
    Parameters: "advfirewall firewall add rule name=""FT891Remote audio (UDP 7301)"" dir=in action=allow protocol=UDP localport=7301"

Filename: "{app}\{#ServerExe}"; Description: "{cm:LaunchServer}"; \
    Components: server; Flags: nowait postinstall skipifsilent unchecked
Filename: "{app}\{#ClientExe}"; Description: "{cm:LaunchClient}"; \
    Components: client; Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
Filename: "{sys}\netsh.exe"; RunOnceId: "DelFwTcp"; Flags: runhidden; \
    Parameters: "advfirewall firewall delete rule name=""FT891Remote control (TCP 7300)"""
Filename: "{sys}\netsh.exe"; RunOnceId: "DelFwUdp"; Flags: runhidden; \
    Parameters: "advfirewall firewall delete rule name=""FT891Remote audio (UDP 7301)"""

; ------------------------------------------------------------------- code
[Code]
// Unticking both components would install runtime libraries and nothing else.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectComponents then
    if not (IsComponentSelected('server') or IsComponentSelected('client')) then
    begin
      MsgBox(ExpandConstant('{cm:NeedComponent}'), mbError, MB_OK);
      Result := False;
    end;
end;
