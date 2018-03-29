unit main;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, StdCtrls, iniFiles, ShellApi, SDL2;

type

  TDisplayMode = record
    w: Integer;
    h: Integer;
  end;

  TDisplayModeList = record
    displayMode: array of TDisplayMode;
  end;

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
    DisplayLabel: TLabel;
    DisplayComboBox: TComboBox;
    DisplayButton: TButton;
    DisplayModeCheckBox: TCheckBox;
    procedure FormCreate(Sender: TObject);
    procedure MusicScrollbarChange(Sender: TObject);
    procedure SoundScrollbarChange(Sender: TObject);
    procedure SaveBtnClick(Sender: TObject);
    procedure StartBtnClick(Sender: TObject);
    procedure CmdStartBtnClick(Sender: TObject);
    procedure EnvBtnClick(Sender: TObject);
    procedure EditorBtnClick(Sender: TObject);
    procedure DisplayComboBoxChange(Sender: TObject);
    procedure DisplayButtonClick(Sender: TObject);
  private
    displayMode: TDisplayMode;
    displayModeList: TDisplayModeList;
    standardDisplayModeList: array[0..2] of TDisplayMode;
    function isInDisplayModeList(dm: TDisplayMode): Integer; overload;
    function isInDisplayModeList(dmList: TDisplayModeList; dm: TDisplayMode): Integer; overload;
    procedure initDisplayModeList();
    function getAllDisplayMode(): TDisplayModeList;
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
  fullScreen, playerAlpha, canChangeDisplayMode: Boolean;
begin
  if (not FileExists(configFileName)) then
  begin
    ShowMessage('找不到配置文件：' + configFileName + ' !');
  end;
  ini := TIniFile.Create(configFileName);
  musicVolume := ini.ReadInteger('Game', 'MusicVolume', 100);
  soundVolume := ini.ReadInteger('Game', 'SoundVolume', 100);
  fullScreen := ini.ReadBool('Game', 'FullScreen', true);
  canChangeDisplayMode := ini.ReadBool('Game', 'CanChangeDisplayMode', false);
  playerAlpha := ini.ReadBool('Game', 'PlayerAlpha', true);
  displayMode.w := ini.ReadInteger('Game', 'WindowWidth', 1280);
  displayMode.h := ini.ReadInteger('Game', 'WindowHeight', 720);
  MusicScrollbar.Position := musicVolume;
  SoundScrollbar.Position := soundVolume;
  FullScreenCheckBox.Checked := fullScreen;
  DisplayModeCheckBox.Checked := canChangeDisplayMode;
  AlphaCheckBox.Checked := playerAlpha;
  MusicScrollbarChange(self);
  SoundScrollbarChange(self);
  ini.Free;
end;

procedure TStarterForm.saveConfig();
var
  ini: TIniFile;
  musicVolume, soundVolume: Integer;
  fullScreen, canChangeDisplayMode, playerAlpha: Boolean;
begin
  musicVolume := MusicScrollbar.Position;
  soundVolume := SoundScrollbar.Position;
  fullScreen := FullScreenCheckBox.Checked;
  canChangeDisplayMode := DisplayModeCheckBox.Checked;
  playerAlpha := AlphaCheckBox.Checked;
  ini := TIniFile.Create(configFileName);
  ini.WriteInteger('Game', 'MusicVolume', musicVolume);
  ini.WriteInteger('Game', 'SoundVolume', soundVolume);
  ini.WriteBool('Game', 'FullScreen', fullScreen);
  ini.WriteBool('Game', 'CanChangeDisplayMode', canChangeDisplayMode);
  ini.WriteBool('Game', 'PlayerAlpha', playerAlpha);
  ini.WriteInteger('Game', 'WindowWidth', displayMode.w);
  ini.WriteInteger('Game', 'WindowHeight', displayMode.h);
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
    VCRedistExeName := VCRedistExePath + VCRedistExe32;
  runExe(VCRedistExeName, VCRedistExePath, '安装程序启动失败！');
end;

procedure TStarterForm.CmdStartBtnClick(Sender: TObject);
begin
  saveConfig();
  runGame(cmdGameFileName);
end;

procedure TStarterForm.DisplayButtonClick(Sender: TObject);
var
  str: String;
  strList: TStringList;
  count, index: Integer;
  dm: TDisplayMode;
begin
  str := InputBox('自定义分辨率', '自定义分辨率', DisplayComboBox.Items[DisplayComboBox.ItemIndex]);
  str := StringReplace(str, ' ', '', [rfReplaceAll]);
  strList := TStringList.Create;
  count := ExtractStrings(['*'], [], PWideChar(str), strList);
  if count = 2 then
  begin
    try
      dm.w := StrToInt(strList[0]);
      dm.h := StrToInt(strList[1]);
      if (dm.w >= 640) and (dm.h >= 480) then
      begin
        count := isInDisplayModeList(dm);
        if count < 0 then
        begin
          index := Length(displayModeList.displayMode);
          SetLength(displayModeList.displayMode, index + 1);
          displayModeList.displayMode[index] := dm;
          DisplayComboBox.Items.Add(inttostr(dm.w) + ' * ' + inttostr(dm.h));
          DisplayComboBox.ItemIndex := index;
        end
        else
        begin
          DisplayComboBox.ItemIndex := count;
        end;
      end
      else
      begin
        ShowMessage('分辨率太小！');
      end;
    except
      ShowMessage('分辨率输入错误！');
    end;
  end
  else
    ShowMessage('分辨率输入错误！');
  strList.Free;
  DisplayComboBoxChange(Sender);
end;

procedure TStarterForm.DisplayComboBoxChange(Sender: TObject);
begin
  if DisplayComboBox.ItemIndex >= 0 then
  begin
    displayMode := displayModeList.displayMode[DisplayComboBox.ItemIndex];
  end;
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
  initDisplayModeList();
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

function TStarterForm.isInDisplayModeList(dmList: TDisplayModeList; dm: TDisplayMode): Integer;
var
  i: Integer;
begin
  result := -1;
  for i := 0 to Length(dmList.displayMode) - 1 do
    if (dm.w = dmList.displayMode[i].w) and (dm.h = dmList.displayMode[i].h) then
    begin
      result := i;
      break;
    end;
end;

function TStarterForm.isInDisplayModeList(dm: TDisplayMode): Integer;
begin
  result := isInDisplayModeList(displayModeList, dm);
end;

procedure TStarterForm.initDisplayModeList();
var
  i: Integer;
  index: Integer;
  dmList: TDisplayModeList;
begin
  standardDisplayModeList[0].w := 1280;
  standardDisplayModeList[0].h := 720;
  standardDisplayModeList[1].w := 800;
  standardDisplayModeList[1].h := 600;
  standardDisplayModeList[2].w := 640;
  standardDisplayModeList[2].h := 480;
  SetLength(displayModeList.displayMode, Length(standardDisplayModeList));
  for i := 0 to Length(standardDisplayModeList) - 1 do
  begin
    displayModeList.displayMode[i] := standardDisplayModeList[i];
  end;

  dmList := getAllDisplayMode();
  for i := 0 to Length(dmList.displayMode) - 1 do
    if isInDisplayModeList(dmList.displayMode[i]) < 0 then
    begin
      index := Length(displayModeList.displayMode);
      SetLength(displayModeList.displayMode, Length(displayModeList.displayMode) + 1);
      displayModeList.displayMode[index] := dmList.displayMode[i];
    end;
  SetLength(dmList.displayMode, 0);

  index := isInDisplayModeList(displayMode);
  if index < 0 then
  begin
    index := Length(displayModeList.displayMode);
    SetLength(displayModeList.displayMode, index + 1);
    displayModeList.displayMode[index] := displayMode;
  end;

  DisplayComboBox.Clear();
  for i := 0 to Length(displayModeList.displayMode) - 1 do
    DisplayComboBox.Items.Add(inttostr(displayModeList.displayMode[i].w) + ' * ' + inttostr(displayModeList.displayMode[i].h));
  DisplayComboBox.ItemIndex := index;
end;

function TStarterForm.getAllDisplayMode(): TDisplayModeList;
var
  count, i: Integer;
  dm: TSDL_DisplayMode;
begin
  SDL_Init(SDL_INIT_VIDEO);
  count := SDL_GetNumDisplayModes(0);
  setlength(result.displayMode, count);
  for i := 0 to count - 1 do
  begin
    SDL_GetDisplayMode(0, i, @dm);
    result.displayMode[i].w := dm.w;
    result.displayMode[i].h := dm.h;
  end;
  SDL_Quit();
end;

function TStarterForm.IsWOW64: BOOL;
var
  IsWow64Process: function(Handle: Windows.THandle; var Res: Windows.BOOL): Windows.BOOL; stdcall;
  Kernel32Handle: Cardinal;
begin
  Result := False;
  Kernel32Handle := GetModuleHandle('KERNEL32.DLL');
  if Kernel32Handle = 0 then
    Kernel32Handle := LoadLibrary('KERNEL32.DLL');
  IsWOW64Process := GetProcAddress(Kernel32Handle,'IsWow64Process');
  IsWow64Process(GetCurrentProcess, Result);
end;

end.
