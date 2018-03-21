unit main;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, iniFiles, ShellApi;

type
  TStarterForm = class(TForm)
    MusicScrollbar: TScrollBar;
    SoundScrollbar: TScrollBar;
    FullScreenCheckBox: TCheckBox;
    AlphaCheckBox: TCheckBox;
    StartBtn: TButton;
    SaveBtn: TButton;
    MusicLabel: TLabel;
    SoundLabel: TLabel;
    CmdStartBtn: TButton;
    EnvBtn: TButton;
    EditorBtn: TButton;
    procedure FormCreate(Sender: TObject);
    procedure MusicScrollbarChange(Sender: TObject);
    procedure SoundScrollbarChange(Sender: TObject);
    procedure SaveBtnClick(Sender: TObject);
    procedure StartBtnClick(Sender: TObject);
    procedure CmdStartBtnClick(Sender: TObject);
    procedure EnvBtnClick(Sender: TObject);
    procedure EditorBtnClick(Sender: TObject);
  private
    function IsWOW64: BOOL;
  public
    function runExe(exeName, exePath: String; errorInfo: String = ''): Boolean;
    procedure setupVCRedist();
    procedure loadConfig();
    procedure saveConfig();
    procedure runGame(gName: string);
  end;

var
  StarterForm: TStarterForm;
  starterPath: String = '';
const
  configFileName = 'game\config.ini';
  gameFileName = 'shf-cpp.exe';
  cmdGameFileName = 'shf-cpp-cmd.exe';
  gamePath = 'game\';
  vcRedistPath = 'VCRedist\';
  vcRedistExe64 = 'vc_redist.x64.exe';
  vcRedistExe32 = 'vc_redist.x86.exe';
  editorPath = 'Editor\';
  editorFileName = 'shfEditor.exe';

implementation

{$R *.dfm}

function TStarterForm.runExe(exeName, exePath: String; errorInfo: String = ''): Boolean;
begin
  if (not FileExists(exeName)) then
  begin
    ShowMessage('找不到文件：' + exeName + ' !');
    Result := false;
    exit;
  end;
  Result := (ShellExecute(0, 'open', PWideChar(exeName), nil, PWideChar(exePath), SW_NORMAL) >= 32);
  if (not Result) then
    if errorInfo = '' then
      ShowMessage('程序:' + exeName + '启动失败！')
    else
      ShowMessage(errorInfo);
end;

procedure TStarterForm.loadConfig();
var
  ini: TIniFile;
  musicVolume, soundVolume: Integer;
  fullScreen, playerAlpha: Boolean;
begin
  if (not FileExists(configFileName)) then
  begin
    ShowMessage('找不到配置文件：' + configFileName + ' !');
  end;
  ini := TIniFile.Create(configFileName);
  musicVolume := ini.ReadInteger('Game', 'MusicVolume', 100);
  soundVolume := ini.ReadInteger('Game', 'SoundVolume', 100);
  fullScreen := ini.ReadBool('Game', 'FullScreen', true);
  playerAlpha := ini.ReadBool('Game', 'PlayerAlpha', true);
  MusicScrollbar.Position := musicVolume;
  SoundScrollbar.Position := soundVolume;
  FullScreenCheckBox.Checked := fullScreen;
  AlphaCheckBox.Checked := playerAlpha;
  MusicScrollbarChange(self);
  SoundScrollbarChange(self);
  ini.Free;
end;

procedure TStarterForm.saveConfig();
var
  ini: TIniFile;
  musicVolume, soundVolume: Integer;
  fullScreen, playerAlpha: Boolean;
begin
  musicVolume := MusicScrollbar.Position;
  soundVolume := SoundScrollbar.Position;
  fullScreen := FullScreenCheckBox.Checked;
  playerAlpha := AlphaCheckBox.Checked;
  ini := TIniFile.Create(configFileName);
  ini.WriteInteger('Game', 'MusicVolume', musicVolume);
  ini.WriteInteger('Game', 'SoundVolume', soundVolume);
  ini.WriteBool('Game', 'FullScreen', fullScreen);
  ini.WriteBool('Game', 'PlayerAlpha', playerAlpha);
  ini.Free;
end;

procedure TStarterForm.runGame(gName: string);
var
  gameExeName: String;
  gamePathName: String;
begin
  gamePathName := starterPath + gamePath;
  gameExeName := gamePathName + gName;
  if (runExe(gameExeName, gamePathName, '游戏启动失败！')) then
    Halt(0);
end;

procedure TStarterForm.setupVCRedist();
var
  VCRedistExePath: String;
  VCRedistExeName: String;
begin
  VCRedistExePath := starterPath + VCRedistPath;
  if isWOW64() then
    VCRedistExeName := VCRedistExePath + VCRedistExe64
  else
    VCRedistExeName := VCRedistExeName + VCRedistExe32;
  runExe(VCRedistExeName, VCRedistExePath, '安装程序启动失败！');
end;

procedure TStarterForm.CmdStartBtnClick(Sender: TObject);
begin
  saveConfig();
  runGame(cmdGameFileName);
end;

procedure TStarterForm.EditorBtnClick(Sender: TObject);
var
  EditorExePath: String;
  EditorExeName: String;
begin
  EditorExePath := starterPath + EditorPath;
  EditorExeName := EditorExePath + EditorFileName;
  runExe(EditorExeName, EditorExePath, '编辑器启动失败！');
end;

procedure TStarterForm.EnvBtnClick(Sender: TObject);
begin
  setupVCRedist();
end;

procedure TStarterForm.FormCreate(Sender: TObject);
begin
  starterPath := ExtractFilePath(ParamStr(0));
  loadConfig();
end;

procedure TStarterForm.MusicScrollbarChange(Sender: TObject);
begin
  MusicLabel.Caption := '音乐音量：' + inttostr(MusicScrollbar.Position);
end;

procedure TStarterForm.SaveBtnClick(Sender: TObject);
begin
  saveConfig();
end;

procedure TStarterForm.SoundScrollbarChange(Sender: TObject);
begin
  SoundLabel.Caption := '音效音量：' + inttostr(SoundScrollbar.Position);
end;

procedure TStarterForm.StartBtnClick(Sender: TObject);
begin
  saveConfig();
  runGame(gameFileName);
end;

function TStarterForm.IsWOW64: BOOL;
begin
  Result := False;
  if GetProcAddress(GetModuleHandle(kernel32), 'IsWow64Process') <> nil then
    IsWow64Process(GetCurrentProcess, Result);
end;

end.
