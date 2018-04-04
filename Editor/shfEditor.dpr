program shfEditor;

{$R *.RES}

uses
  Forms,
  Windows,
  SysUtils,
  classes,
  messages,
  Main in 'Main.pas' {SHFEditorMainForm},
  RePack in 'RePack.pas' {RepackForm},
  opendisplay in 'opendisplay.pas' {FlashForm},
  Head in 'Head.pas',
  mouse in 'mouse.pas',
  about in 'about.pas' {AboutBox},
  lazlzo in 'lazlzo.pas',
  PakFile in 'PakFile.pas',
  lzoobj in 'lzoobj.pas',
  PicFile in 'PicFile.pas',
  ImageForm in 'ImageForm.pas' {ImageEditor},
  ImageEdit in 'ImageEdit.pas' {ImageEditForm};

{$R App.RES}

var
  FlashForm: TFlashForm;

procedure openDisplay;
begin
	FlashForm := TFlashForm.Create(Application);
  FlashForm.Show;
end;

procedure processon(Tfm:TComponentClass; var fm);
begin
  Application.CreateForm(Tfm, fm);
end;

var
  appI: integer;
  MutexHandle, FileMutexHandle: THandle;
  pramstr: widestring;
  paramcountI: integer;
  Appbuf: TCopyDataStruct;
  Appstr: Ansistring;
  Appchar: PAnsichar;
  Apph, formh: HWND;

begin
  titleStr := Appname + VersionString;
  Application.Title := Appname + 'Opening';

  FileMutexHandle := CreateMutex(nil, TRUE, Pwidechar('SHFEditorOpenFileMutex'));
  if (FileMutexHandle<>0) and (GetLastError=ERROR_ALREADY_EXISTS) then
  begin
    if (paramcount > 0) and fileexists(paramstr(paramcount)) then
    begin
      Formh := findwindow('TSHFEditorMainForm', nil);
      if formh <> 0 then
      begin
        Apph := findwindow('TApplication', Pwidechar(Appname));

        Appstr := ansistring(paramstr(paramcount));
        getmem(Appchar, 200);

        Appbuf.dwData := 200;
        Appbuf.cbData := 200;
        Appbuf.lpData := Appchar;
        strpcopy(Appchar, Appstr);
        if apph <> 0 then
          if IsIconic(apph) then
            OpenIcon(apph);
        SetForegroundWindow(Apph);
        sendmessage(Formh, WM_COPYDATA, 0, integer(@Appbuf));

        CloseHandle(FileMutexHandle);
        halt;
      end;
    end;

  end;

  pramstr := 'SHFEditor';
  pramstr := ExtractFilePath(Paramstr(0)) + pramstr;
  pramstr := StringReplace(pramstr, '\', '-', [rfReplaceAll]);
  pramstr := StringReplace(pramstr, ':', '', [rfReplaceAll]);
  MutexHandle := CreateMutex(nil, TRUE, Pwidechar(pramstr));
  if (MutexHandle<>0) and (GetLastError=ERROR_ALREADY_EXISTS) then
  begin
    if MessageBox(application.Handle, PWideChar('检测到已经运行了同一路径下的' + AppName + '编辑器程序，是否要运行多个？'),  PWideChar(AppName + '提示'), MB_OKCANCEL) <> 1 then
    begin
      CloseHandle(MutexHandle);
      Halt;
    end;
  end;

  Application.Initialize;
  Application.Title := Appname;

  screen.Cursors[fmcursor] := GetResourceAsAniCursor('RESOURCE_1');
  screen.Cursor := fmcursor;

  openDisplay();
  for appI := 0 to 255 div 5 do
  begin
    FlashForm.SetFormAlpha(appI * 5);
    FlashForm.Refresh;
    sleep(8);
  end;

  processon(TSHFEditorMainForm, SHFEditorMainForm);
  processon(TAboutBox, AboutBox);

  for appI := 0 to 255 div 5 do
  begin
    FlashForm.SetFormAlpha(255 - appI * 5);
    FlashForm.Refresh;
    sleep(8);
  end;

  FlashForm.Close;
  SHFEditorMainForm.Show;
  Application.Run;

end.
