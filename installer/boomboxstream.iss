; Instalador do BoomBox Stream para Windows (Inno Setup 6).
; Compilado pelo CI:
;   iscc /DMyAppVersion=0.2.0 /DSourceDir=..\dist /DOutputDir=..\release installer\boomboxstream.iss
; SourceDir deve ter o layout do "cmake --install":
;   obs-plugins\64bit\boomboxstream.dll
;   data\obs-plugins\boomboxstream\...

#define MyAppName "BoomBox Stream"
#define MyAppPublisher "xFreDxx"
#define MyAppURL "https://github.com/xFreDxx/boomboxstream"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif

[Setup]
; Identificador fixo: não mude, senão atualizações viram instalações paralelas.
AppId={{FF487FD2-B4E0-4B4D-B759-1562AA560E57}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName} Setup
DefaultDirName={code:GetOBSDir}
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
UninstallDisplayName={#MyAppName} (plugin do OBS)
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=boomboxstream-{#MyAppVersion}-windows-x64-installer
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ptbr.SelectOBS=Escolha a pasta onde o OBS Studio está instalado.
english.SelectOBS=Choose the folder where OBS Studio is installed.
ptbr.NoOBS=O OBS Studio (bin\64bit\obs64.exe) não foi encontrado nessa pasta. Escolha a pasta correta do OBS.
english.NoOBS=OBS Studio (bin\64bit\obs64.exe) was not found in that folder. Please choose the correct OBS folder.

[Files]
Source: "{#SourceDir}\obs-plugins\64bit\boomboxstream.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceDir}\data\obs-plugins\boomboxstream\*"; DestDir: "{app}\data\obs-plugins\boomboxstream"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: dirifempty; Name: "{app}\data\obs-plugins\boomboxstream"

[Code]
function GetOBSDir(Value: string): string;
var
  Path: string;
begin
  Result := ExpandConstant('{commonpf64}\obs-studio');
  if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Path) and (Path <> '') then
    Result := Path;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Path) and (Path <> '') then
    Result := Path;
end;

procedure InitializeWizard();
begin
  WizardForm.SelectDirLabel.Caption := CustomMessage('SelectOBS');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and
     not FileExists(AddBackslash(WizardDirValue) + 'bin\64bit\obs64.exe') then
  begin
    MsgBox(CustomMessage('NoOBS'), mbError, MB_OK);
    Result := False;
  end;
end;
