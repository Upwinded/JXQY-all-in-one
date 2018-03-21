unit RePack;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, ExtCtrls, StdCtrls, CheckLst, head, pakFile, Grids, ValEdit, picFile, PNGImage, math;

type
  TRepackForm = class(TForm)
    Panel1: TPanel;
    Panel3: TPanel;
    Panel4: TPanel;
    OpenDialog1: TOpenDialog;
    Button1: TButton;
    Button2: TButton;
    Button3: TButton;
    Button4: TButton;
    ValueListEditor1: TValueListEditor;
    Button5: TButton;
    Button6: TButton;
    SaveDialog1: TSaveDialog;
    Button7: TButton;
    Edit1: TEdit;
    Edit2: TEdit;
    Button8: TButton;
    Button9: TButton;
    Button10: TButton;
    Button11: TButton;
    Button12: TButton;
    Button13: TButton;
    Button14: TButton;
    procedure FormClose(Sender: TObject; var Action: TCloseAction);
    procedure Button1Click(Sender: TObject);
    procedure Button2Click(Sender: TObject);
    procedure addFileList(fileName, dir: string);
    procedure Button3Click(Sender: TObject);
    procedure Button4Click(Sender: TObject);
    procedure Button5Click(Sender: TObject);
    procedure Button6Click(Sender: TObject);
    procedure Button7Click(Sender: TObject);
    procedure Button8Click(Sender: TObject);
    procedure Button9Click(Sender: TObject);
    procedure Button10Click(Sender: TObject);
    procedure Button11Click(Sender: TObject);
    procedure Button12Click(Sender: TObject);
    procedure Button13Click(Sender: TObject);
    procedure Button14Click(Sender: TObject);

  private
    { Private declarations }
  public
    { Public declarations }
    procedure CountAllID;
    procedure RearrangetAllID;
  end;

type
  PStringList = ^TStringList;

function unpakFile(fileName, dir: string): boolean;
function transFileFormat(filename: string; alpha: boolean): boolean;
function transAllImage(MPCFile: PMPCFile; alpha: boolean): boolean;
function transImage(MPCFile: PMPCFile; index: integer; alpha: boolean): boolean;

function transscriptfile(fileName: string): boolean;

function replacewav(fileName: string): boolean;

function cutFile(filename: string): boolean;
function cutImage(img: PImpImage; index: integer): boolean;
function cutPNG(PNG: TPNGImage): cardinal;
function ExtractStrings2(Separators, WhiteSpace: TSysCharSet; Content: PChar; Strings: TStrings): Integer;

implementation

{$R *.dfm}


function ExtractStrings2(Separators, WhiteSpace: TSysCharSet; Content: PChar;
  Strings: TStrings): Integer;
var
  Head, Tail: PChar;
  EOS, InQuote: Boolean;
  QuoteChar: Char;
  Item: string;
begin
  Result := 0;
  if (Content = nil) or (Content^=#0) or (Strings = nil) then Exit;
  Tail := Content;
  InQuote := False;
  QuoteChar := #0;
  Strings.BeginUpdate;
  try
    repeat
      while (Tail^ in WhiteSpace + [#13, #10]) do
        Tail := StrNextChar(Tail);
      Head := Tail;
      while not (Tail^ in Separators + [#0, #13, #10]) do
      begin
        Tail := StrNextChar(Tail);
      end;
      EOS := Tail^ = #0;
      if (Head <> Tail) and (Head^ <> #0) then
      begin
        if Strings <> nil then
        begin
          SetString(Item, Head, Tail - Head);
          Strings.Add(Item);
        end;
        Inc(Result);
      end;
      Tail := StrNextChar(Tail);
    until EOS;
  finally
    Strings.EndUpdate;
  end;
end;

function cutPNG(PNG: TPNGImage): cardinal;
var
  I, I2, I3: integer;
  ix, iy: cardinal;
  alpha: array of array of byte;
  data: array of array of byte;
  top, bottom, left, right: integer;
  minalpha: integer;
begin
  //

  try

  result := 0;
  top := 0;
  bottom := 0;
  left := 0;
  right := 0;
  setlength(alpha, png.Height, png.Width);
  setlength(data, png.Height, png.Width * 3);

  for I := 0 to png.Height- 1 do
  begin
    copymemory(@alpha[I][0], png.AlphaScanline[I], png.Width);
    copymemory(@data[I][0], png.Scanline[I], png.Width * 3);
  end;

  minalpha := png.Width;
  for I := 0 to png.height - 1 do
  begin
    for I2:= 0 to png.Width - 1 do
    begin
      if alpha[I][I2] <> 0 then
      begin
        minalpha := min(minalpha, I2);
        break;
      end;
    end;
  end;
  left := minalpha;

  minalpha := png.Width;
  for I := 0 to png.height - 1 do
  begin
    for I2:= 0 to png.Width - 1 do
    begin
      if alpha[I][png.Width - 1 - I2] <> 0 then
      begin
        minalpha := min(minalpha, I2);
        break;
      end;
    end;
  end;
  right := minalpha;

  minalpha := png.height;
  for I := 0 to png.width - 1 do
  begin
    for I2:= 0 to png.height - 1 do
    begin
      if alpha[I2][I] <> 0 then
      begin
        minalpha := min(minalpha, I2);
        break;
      end;
    end;
  end;
  top := minalpha;

  minalpha := png.height;
  for I := 0 to png.width - 1 do
  begin
    for I2:= 0 to png.height - 1 do
    begin
      if alpha[png.height - 1 - I2][I] <> 0 then
      begin
        minalpha := min(minalpha, I2);
        break;
      end;
    end;
  end;
  bottom := minalpha;

  //showmessage(inttostr(left) + ',' + inttostr(right) + ',' +inttostr(top) + ',' +inttostr(bottom)+ ',' +inttostr(png.Width) + ',' +inttostr(png.Height));
  if left + right + top + bottom = 0 then
    exit;
  if (right + left < png.Width) and (top + bottom < png.Height) then
  begin
    png.Resize(png.Width - left - right, png.Height - top - bottom);
    for I := 0 to png.height - 1 do
      for I2 := 0 to png.Width - 1 do
      begin
        copymemory(png.Scanline[I], @data[I + top][left * 3], png.Width * 3);
        copymemory(png.AlphaScanline[I], @alpha[I + top][left], png.Width);
      end;
    iy := top;
    ix := left;
    result := iy shl 16 + ix;
  end;
  except

  end;
end;

function cutImage(img: PImpImage; index: integer): boolean;
var
  I, I2, I3: integer;
  ix, iy: integer;
  rs: TmemoryStream;
  PNG: TPNGImage;
  offset: cardinal;
begin
  //
  try
  result := false;
  if img = nil then
    exit;
  if (index < 0) or (index >= img.frameCount) then
    exit;
  rs := TMemorystream.Create;
  rs.SetSize(img.frame[index].dataLen);
  rs.Seek(0, 0);
  rs.Write(img.frame[index].data[0], img.frame[index].dataLen);
  rs.Seek(0, 0);
  PNG := TPNGImage.Create;
  PNG.LoadFromStream(rs);
  except
    exit;
  end;
  //png.SaveToFile(inttostr(index) + 'oripng' + '.png');
  offset := cutPNG(PNG);
  //png.SaveToFile(inttostr(index) + 'outpng' + '.png');
  try
  rs.Clear;
  png.SaveToStream(rs);
  png.Free;
  img.frame[index].dataLen := rs.seek(0, 2);
  rs.Seek(0, 0);
  ix := offset and $FFFF;
  iy := (offset and $FFFF0000) shr 16;
  img.frame[index].xOffset := img.frame[index].xOffset - ix;
  img.frame[index].yOffset := img.frame[index].yOffset - iy;
  setlength(img.frame[index].data, img.frame[index].dataLen);
  rs.Read(img.frame[index].data[0], img.frame[index].dataLen);
  rs.Free;
  result := true;
  except
    exit;
  end;
end;

function cutFile(filename: string): boolean;
var
  IMG: TIMPImage;
  PicFile: TPicFIle;
  I: integer;
begin
  //
  result := false;
  picFile := TPicFile.Create;
  if picFile.LoadIMPImage(@img, fileName) then
  begin
    result := true;
  end;
  if result then
  begin
    for I := 0 to img.frameCount - 1 do
    begin
      if not cutImage(@img, i) then
      begin
        result := false;
        break;
      end;
    end;
  end;
  if result then
    PicFile.SaveIMPImage(@img, fileName);
  picFile.Free;
end;

function replacewav(fileName: string): boolean;
var
  str: TStringList;
  I, spos: integer;
begin
  str := TStringList.Create;
  str.LoadFromFile(fileName);
  for I := 0 to str.Count - 1 do
  begin
    spos := pos('PlayMusic', str.Strings[I]);
    if spos > 0 then
    begin
      spos := max(max(pos('"ks', str.Strings[I]), pos('"KS', str.Strings[I])), max(pos('"Ks', str.Strings[I]) , pos('"kS', str.Strings[I])));
      if spos > 0 then
      begin
        str.Strings[I] := stringReplace(str.Strings[I], '.wav', '.mp3', [rfReplaceAll, rfIgnoreCase]);
      end;
    end;
  end;
  str.SaveToFile(fileName);
  str.Free;
end;

function transscriptfile(fileName: string): boolean;
const
  getvar: String = 'GetVar';
var
  str: TStringList;
  I, I2, I3, I4: integer;
  ifCount, spos, ifc: integer;
  ifLine: array of integer;
  labelLine: array of integer;
  labelName: array of string;
  strCount: integer;
  strList: TStringList;
  ifList: array of string;
  sp, oplua: String;
  op : AnsiChar;
  newif: array of TstringList;
  newStr: TStringList;
  same: boolean;
  haveIf: Boolean;
begin
  //
  str := TStringList.Create;
  str.LoadFromFile(fileName);
  for I := 0 to str.Count - 1 do
  begin
    str.Strings[I] := stringReplace(str.Strings[I], 'Return;', 'return;', [rfReplaceAll, rfIgnoreCase]);
    str.Strings[I] := stringReplace(str.Strings[I], ' ', '', [rfReplaceAll]);
    str.Strings[I] := stringReplace(str.Strings[I], #9, '', [rfReplaceAll]);
    //str.Strings[I] := stringReplace(str.Strings[I], '"', '''', [rfReplaceAll]);
    str.Strings[I] := stringReplace(str.Strings[I], '//', '--', [rfReplaceAll]);
    if (length(str.Strings[I]) >= 10) and (lowerCase(str.Strings[I])[1] = 'a')
      and (lowerCase(str.Strings[I])[2] = 's') and (lowerCase(str.Strings[I])[3] = 's')
      and (lowerCase(str.Strings[I])[4] = 'i') and (lowerCase(str.Strings[I])[5] = 'g')
      and (lowerCase(str.Strings[I])[6] = 'n') then
    begin
      strList := TStringList.Create;
      strList.Clear;
      strCount := ExtractStrings2(['$', ',', '"'], [], PWideChar(str.Strings[I]), strList);
      if strCount = 3 then
      begin
        strList.Strings[1] := '"' + strList.Strings[1] + '",';
        str.Strings[I] :=  strList.Strings[0] + strList.Strings[1] + strList.Strings[2];
      end;
      strList.Free;
    end
    else if (length(str.Strings[I]) >= 10) and (lowerCase(str.Strings[I])[1] = 'a')
      and (lowerCase(str.Strings[I])[2] = 'd') and (lowerCase(str.Strings[I])[3] = 'd') and (str.Strings[I][4] = '(') then
    begin
      strList := TStringList.Create;
      strList.Clear;
      strCount := ExtractStrings2(['$', ',', '"'], [], PWideChar(str.Strings[I]), strList);
      if strCount = 3 then
      begin
        strList.Strings[1] := '"' + strList.Strings[1] + '",';
        str.Strings[I] :=  strList.Strings[0] + strList.Strings[1] + strList.Strings[2];
      end;
      strList.Free;
    end
    else if (length(str.Strings[I]) >= 5) and (lowerCase(str.Strings[I])[1] = '$') then
    begin
      strList := TStringList.Create;
      strList.Clear;
      strCount := ExtractStrings2(['$', '=', ';'], [], PWideChar(str.Strings[I]), strList);
      if strCount = 2 then
      begin
        str.Strings[I] := 'Assign("' + strList.Strings[0] + '",' + strList.Strings[1] + ');';
      end;
      strList.Free;
    end;
  end;
  ifCount := 0;
  setlength(ifLine, 0);
  for I := 0 to str.Count - 1 do
  begin
    if length(str.Strings[I]) >= 9 then
    begin
      if (lowerCase(str.Strings[I])[1] = 'i') and (lowerCase(str.Strings[I])[2] = 'f') and (str.Strings[I][3] = '(') then
      begin
        inc(ifCount);
        setlength(ifLine, ifCount);
        setlength(newif, ifCount);
        newif[ifCount - 1] := TSTringList.Create;
        setlength(labelName, ifCount);
        setlength(labelLine, ifCount);
        setlength(ifList, ifCount);
        ifLine[ifCount - 1] := i;
        labelLine[ifCount - 1] := -1;
        //
        op := '=';
        oplua := '';
        spos := 0;
        spos := pos('<>', str.Strings[I]);
        if spos <= 0 then
        begin
        spos := pos('==', str.Strings[I]) + pos(',', str.Strings[i]);
        if spos <= 0 then
        begin
          spos := pos('<=', str.Strings[I]);
          if spos <= 0 then
          begin
            spos := pos('>=', str.Strings[I]);
            if spos <= 0 then
            begin
              spos := pos('!=', str.Strings[I]);
              if spos <= 0 then
              begin
                spos := pos('>', str.Strings[I]);
                if spos <= 0 then
                begin
                  spos := pos('<', str.Strings[I]);
                  if spos > 0 then
                  begin
                    op := '<';
                    oplua := '<';
                  end;
                end
                else
                begin
                  op := '>';
                  oplua := '>';
                end;
              end
              else
              begin
                op := '!';
                oplua := '~=';
              end;
            end
            else
            begin
              op := '>';
              oplua := '>=';
            end;
          end
          else
          begin
            op := '<';
            oplua := '<=';
          end;
        end
        else
        begin
          op := '=';
          oplua := '==';
        end;
        end
        else
        begin
          op := '<';
          oplua := '~=';
        end;

        if spos > 0 then
        begin
          strList := TStringList.Create;
          strList.Clear;
          strCount := ExtractStrings(['$', '<', '>', '=', ')', '@', ';', ','], [], PWideChar(str.Strings[I]), strList);
          if strCount = 4 then
          begin
            str.Strings[I] := 'if ' + getvar + '("' + strList.Strings[1] + '") ' + oplua + ' ' +  strList.Strings[2] + ' then';
            labelName[ifCount - 1] := strList.strings[3];
          end
          else if strCount = 5 then
          begin
            str.Strings[I] := 'if ' + getvar + '("' + strList.Strings[1] + '") ' + oplua + ' ' +  strList.Strings[2] + ' then';
            labelName[ifCount - 1] := strList.strings[4];
          end;
          ifList[ifCount - 1] := str.Strings[I];
          str.Strings[I] := '';
          strList.Free;
        end;
      end;
    end;
  end;
  if ifCount > 0 then
  begin
    for I := 0 to str.Count - 1 do
    begin
      if (length(str.Strings[I]) > 1) and (str.Strings[I][1] = '@') then
      begin
        for I2 := 0 to ifCount - 1 do
        begin
          if (labelName[I2] <> '') and (length(labelName[I2]) + 2 = length(str.Strings[I])) then
          begin
            same := true;
            for i3 := 1 to length(labelName[I2]) do
            begin
              if labelName[I2][i3] <> str.Strings[I][i3 + 1] then
              begin
                same := false;
                break;
              end;
            end;
            if same then
              labelLine[i2] := I;
          end;
        end;
      end;
    end;
    //清除label
    for I := 0 to str.Count - 1 do
    begin
      if (length(str.Strings[I]) > 1) and (str.Strings[I][1] = '@') then
      begin
        str.Strings[I] := '';
      end;
    end;
    for I := ifCount - 1 downto 0 do
    begin
      newif[i] := TStringList.Create;
      newif[i].Clear;
      newif[i].Append(ifList[I]);
      if labelLine[I] > 0 then
      begin
        if labelLine[I] <= ifLine[I] then
          showmessage('存在向回跳转！' + fileName);
        for I2 := labelLine[I] to str.Count - 1 do
        begin
          haveIf := false;
          for I3 := I + 1 to ifCount - 1 do
          begin
            if (ifLine[i3] = i2) then
            begin
              haveIf := true;
              for I4 := 0 to newIf[i3].Count - 1 do
              begin
                newif[i].Append('  ' + newIf[i3][I4]);
              end;
              break;
            end;
          end;
          if (not haveIf) then
          begin
            newif[i].Append('  ' + str.Strings[I2]);
            if str.Strings[I2] = 'return;' then
              break;
          end;

        end;
      end;

      newif[i].Append('end');
    end;
    newStr := TStringList.Create;
    newStr.Clear;
    for I := 0 to str.Count - 1 do
    begin
      same := false;
      for I2 := 0 to ifCount - 1 do
      begin
        if I = ifLine[I2] then
        begin
          same := true;
          ifc := I2;
          break;
        end;
      end;
      if same then
      begin
        for I2 := 0 to newif[ifc].Count - 1 do
        begin
          newStr.Append(newif[ifc].Strings[I2]);
        end;
      end
      else
      begin
        newStr.Append(str.Strings[I]);
        if str.Strings[I] = 'return;' then
        begin
          break;
        end;
      end;
    end;
    for I := 0 to newStr.Count - 1 do
    begin
      newStr.Strings[I] := '  ' + newStr.Strings[I];
    end;
    //newstr.Insert(0, 'function Run()');
    //newstr.Append('end');
    newStr.SaveToFile(fileName);
    newStr.Free;
    for I := 0 to ifCount - 1 do
    begin
      newif[I].Free;
    end;
  end
  else
  begin
    newStr := TStringList.Create;
    newStr.Clear;
    for I := 0 to Str.Count - 1 do
    begin
      newStr.Append('  ' + Str.Strings[I]);
      if str.Strings[I] = 'return;' then
      begin
        break;
      end;
    end;
    //str.Insert(0, 'function Run()');
    //str.Append('end');
    newStr.SaveToFile(fileName);
    newStr.Free;
  end;
  str.Free;
end;

procedure TRepackForm.Button10Click(Sender: TObject);
var
  str: TStringList;
  I, i2, i3: integer;
  ini: boolean;
begin
  //
  str := TStringList.Create;
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    str.Clear;
    str.LoadFromFile(ValueListEditor1.Keys[I]);
    ini := false;
    if str.Count > 0 then
    begin
      for I2 := 0 to str.Count - 1 do
      begin
        if length(str.Strings[I2]) > 0 then
        begin
          if (str.Strings[I2])[1] = wideChar('[') then
          begin
            ini := true;
            break;
          end;
        end;
      end;
    end;
    if not ini then
    begin
      str.SaveToFile(ValueListEditor1.Keys[I] + '.bak');
      transScriptFile(ValueListEditor1.Keys[I]);
    end;
  end;
  str.Free;
  showmessage('转换格式完成！');
end;

procedure TRepackForm.Button11Click(Sender: TObject);
var
  f, fileID: cardinal;
  I: integer;
  dir, tempname, relativepath: string;
  fileList: TStringList;
begin
  if SelectFolderDialog(self.handle, '选择要添加的目录', '', dir) then
  begin
    if (dir[length(dir)] <> '\') and (dir[length(dir)] <> '/') then
        dir := dir + '\';
    if not DirectoryExists(dir) then
      exit;
    fileList := GetAllFile(dir, '.TXT');
    if SelectFolderDialog(self.handle, '选择相对路径', dir, dir) then
    begin
      if (dir[length(dir)] <> '\') and (dir[length(dir)] <> '/') then
        dir := dir + '\';
      if not DirectoryExists(dir) then
      begin
        if not ForceDirectories(dir) then
        begin
          showmessage('相对路径错误！');
          fileList.Free;
          exit;
        end;
      end;
      for I := 0 to fileList.Count - 1 do
      begin
        if fileexists(fileList.Strings[i]) and (lowerCase(ExtractFileName(fileList.Strings[i])) <> 'talk.txt') then
        begin
          relativepath := ExtractRelativePath(dir, ExtractFilePath(fileList.Strings[i]));
          tempname := ExtractFileName(fileList.Strings[i]);
          if tempname <> '' then
          begin
            if tempname[1] = '$' then
            begin
              try
                fileID := strtoint(tempname);
                valueListEditor1.InsertRow(fileList.Strings[i], '$' + format('%x', [fileID]), true);
              except
                valueListEditor1.InsertRow(fileList.Strings[i], relativepath + tempname, true);
              end;
            end
            else
            begin
              valueListEditor1.InsertRow(fileList.Strings[i], relativepath + tempname, true);
            end;
          end;
        end;
      end;
    end;
    fileList.Free;
  end;
end;

procedure TRepackForm.Button12Click(Sender: TObject);
var
  I: integer;
  path, name, newName: string;
begin
  for I := 0 to valueListEditor1.RowCount - 1 do
  begin
    name := valueListEditor1.Keys[I];
    path := ExtractFilePath(name);
    newname := ExtractFileName(name);
    if length(newname) > 4 then
    begin
      setlength(newname, length(newname) - 4);
      newname := '$' + newname;
      renameFile(name, path + newName);
      valueListEditor1.Keys[I] := path + newName;
      valueListEditor1.Values[valueListEditor1.Keys[I]] := newName;
    end;
  end;
end;

procedure TRepackForm.Button13Click(Sender: TObject);
var
  str: TStringList;
  I, i2, i3: integer;
  ini: boolean;
begin
  //
  str := TStringList.Create;
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    str.Clear;
    str.LoadFromFile(ValueListEditor1.Keys[I]);
    ini := false;
    if str.Count > 0 then
    begin
      for I2 := 0 to str.Count - 1 do
      begin
        if length(str.Strings[I2]) > 0 then
        begin
          if (str.Strings[I2])[1] = wideChar('[') then
          begin
            ini := true;
            break;
          end;
        end;
      end;
    end;
    if not ini then
    begin
      //str.SaveToFile(ValueListEditor1.Keys[I] + '.bak');
      replacewav(ValueListEditor1.Keys[I]);
    end;
  end;
  str.Free;
  showmessage('转换格式完成！');
end;

procedure TRepackForm.Button14Click(Sender: TObject);
var
  I: integer;
begin
    //剪裁图片
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    cutFile(ValueListEditor1.Keys[I]);
  end;
  showmessage('转换格式完成！');
end;

procedure TRepackForm.Button1Click(Sender: TObject);
var
  dir: String;
begin
  OpenDialog1.Filter := 'Pack file(*.Pak,*.dat)|*.Pak;*.dat|All files (*.*)|*.*';
  if OpenDialog1.Execute then
  begin
    if fileExists(OpenDialog1.fileName) then
    begin
      if SelectFolderDialog(self.handle, '选择保存文件夹', '', dir) then
      begin
        if dir[length(dir)] <> '\' then
          dir := dir + '\';
        if not DirectoryExists(dir) then
        begin
          if not ForceDirectories(dir) then
          begin
            showmessage('保存路径错误！');
            exit;
          end;
        end;
        if unpakFile(OpenDialog1.fileName, dir) then
        begin
          showmessage('解包文件：' + OpenDialog1.fileName + ' 成功！');
          addFileList(OpenDialog1.fileName, dir);
        end
        else
        begin
          showmessage('解包文件：' + OpenDialog1.fileName + ' 出错！');
        end;
      end;
    end
    else
    begin
      showmessage('文件：' + OpenDialog1.fileName + ' 不存在！');
    end;
  end;
end;

procedure TRepackForm.addFileList(fileName, dir: string);
var
  pak: TPakFile;
  pt: TPakType;
  filenum, fh, fsize, i: integer;
  fileID: array of cardinal;
begin
  //
  if not fileExists(fileName) then
  begin
    exit;
  end;
  if not DirectoryExists(dir) then
  begin
    if not ForceDirectories(dir) then
    begin
      exit;
    end;
  end;

  filenum := 0;
  pak := TPakFile.create;
  pt := pak.CalPakType(fileName);
  pak.Destroy;
  case (pt) of
    ptPAK:
      begin
        fh := fileOpen(fileName, fmopenread);
        fsize := fileseek(fh, 0, 2);
        if fsize >= 8 then
        begin
          fileseek(fh, 4, 0);
          fileread(fh, filenum, 4);
          if (filenum > 0) and (fsize >= filenum * 12 + 8) then
          begin
            fileseek(fh, 8, 0);
            setlength(fileID, filenum);
            for i := 0 to filenum - 1 do
            begin
              fileread(fh, fileID[i], 4);
              fileseek(fh, 8, 1);
            end;
            fileclose(fh);
          end
          else
          begin
            filenum := 0;
            fileclose(fh);
          end;
        end
        else
        begin
          filenum := 0;
          fileclose(fh);
        end;
      end;
    ptPACKAGE:
      begin
        fh := fileOpen(fileName, fmopenread);
        fsize := fileseek(fh, 0, 2);
        if fsize >= 8 then
        begin
          fileseek(fh, 8, 0);
          fileread(fh, filenum, 4);
          if (filenum > 0) and (fsize >= filenum * 12 + 16) then
          begin
            fileseek(fh, 16, 0);
            setlength(fileID, filenum);
            for i := 0 to filenum - 1 do
            begin
              fileread(fh, fileID[i], 4);
              fileseek(fh, 8, 1);
            end;
            fileclose(fh);
          end
          else
          begin
            filenum := 0;
            fileclose(fh);
          end;
        end
        else
        begin
          filenum := 0;
          fileclose(fh);
        end;
      end;
  end;

  valueListEditor1.Strings.Clear;

  if filenum > 0 then
  begin
    for i := 0 to filenum - 1 do
    begin
      valueListEditor1.InsertRow(dir + '$' + format('%x', [fileID[i]]), '$' + format('%x', [fileID[i]]), true);
    end;
  end;

end;

function transImage(MPCFile: PMPCFile; index: integer; alpha: boolean): boolean;
var
  ix, iy, i, i2: integer;
  state, tempint: integer;
  pa, pc, pdata: PByte;
  A, R, G, B: byte;
  tempcol: cardinal;
  tdata: array of byte;
  png: TPNGImage;
  bmp: TBitmap;
  rs: TMemoryStream;
begin
  result := false;
  if MPCFile = Nil then
  begin
    exit;
  end;

  case MPCFile.Pictype of
    MPC, SHD:
      begin
        if (index < 0) or (index >= MPCFile.MPCFileHead.PicCount) then
        begin
          exit;
        end;
      end;
    ASF100, ASF101:
      begin
        if (index < 0) or (index >= MPCFile.ASFFileHead.PicCount) then
        begin
          exit;
        end;
      end;
    else
      begin
        exit;
      end;
  end;

  png := TPNGimage.Create;
  bmp := Tbitmap.Create;
  bmp.Width := MPCFile.MPCpic[index].MPCpichead.Width;
  bmp.Height := MPCFile.MPCpic[index].MPCpichead.height;
  bmp.PixelFormat := pf24bit;
  png.Assign(bmp);
  png.CreateAlpha;

  for I := 0 to png.Height - 1 do
  begin
    if png.Width > 0 then
    begin
      zeroMemory(png.AlphaScanline[i], png.Width);
      zeroMemory(png.Scanline[i], png.Width * 3);
    end;
  end;

  case MPCFile.Pictype of
    MPC:
      begin
        result := true;
        ix := 0;
        iy := 0;
        state := 0;
        for I := 0 to MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen - 1 do
        begin
          pdata := @MPCFile.MPCpic[index].data[I];
          if state = 0 then
          begin
            if Pdata^ <= 128 then
            begin
              state := Pdata^;
            end
            else
            begin
              inc(ix, Pdata^ - 128);
              if ix >= MPCFile.MPCpic[index].MPCpichead.Width then
              begin
                inc(iy);
                ix := 0;
              end;
            end;
          end
          else
          begin
            if (ix >= 0) and (iy >= 0) and (ix < MPCFile.MPCpic[index].MPCpichead.Width) and (iy < MPCFile.MPCpic[index].MPCpichead.Height) then
            begin
              if alpha then
              begin
                A := MPCFile.MPCpalette.col[Pdata^].A;
              end
              else
              begin
                A := 255;
              end;
              R := MPCFile.MPCpalette.col[Pdata^].R;
              G := MPCFile.MPCpalette.col[Pdata^].G;
              B := MPCFile.MPCpalette.col[Pdata^].B;
              pa := PByte(png.AlphaScanline[iy]);
              copymemory(pa + ix, @A, 1);
              pc := PByte(png.Scanline[iy]);
              inc(pc, ix * 3);
              copymemory(pc, @B, 1);
              copymemory(pc + 1, @G, 1);
              copymemory(pc + 2, @R, 1);
            end;
            inc(ix);
            if ix >= MPCFile.MPCpic[index].MPCpichead.Width then
            begin
              inc(iy);
              ix := 0;
            end;
            dec(State);
          end;
        end;
      end;
    SHD:
      begin
        ix := 0;
        iy := 0;
        result := true;
        for I := 0 to MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen - 1 do
        begin
          pdata := @(MPCFile.MPCpic[index].data[I]);
          if Pdata^ <= 128 then
          begin
            for state := 0 to Pdata^- 1 do
            begin
              if (ix >= 0) and (iy >= 0) and (ix < MPCFile.MPCpic[index].MPCpichead.Width) and (iy < MPCFile.MPCpic[index].MPCpichead.Height) then
              begin
                A := 128;
                pa := PByte(png.AlphaScanline[iy]);
                copymemory(pa + ix, @A, 1);
                pc := PByte(png.Scanline[iy]);
                inc(pc, ix * 3);
                //zeromemory(pc, 3);
              end;
              inc(ix);
            end;
            if ix >= MPCFile.MPCpic[index].MPCpichead.Width then
            begin
              inc(iy);
              ix := 0;
            end;
          end
          else
          begin
            inc(ix, Pdata^ - 128);
            if ix >= MPCFile.MPCpic[index].MPCpichead.Width then
            begin
              inc(iy);
              ix := 0;
            end;
          end;
        end;
      end;
    ASF100, ASF101:
      begin
        result := true;
        ix := 0;
        iy := 0;
        state := 0;
        I := 0;
        if MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen > 0 then
          pdata := @(MPCFile.MPCpic[index].data[0]);
        while (I < MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen - 1) do
        begin
          tempint := Pdata^;
          inc(Pdata);
          state := Pdata^;
          inc(Pdata);
          Inc(I, 2);
          if state = 0 then
          begin
            inc(ix, tempint);
          end
          else
          begin
            for I2 := 0 to tempint - 1 do
            begin
              if (ix >= 0) and (iy >= 0) and (ix < MPCFile.MPCpic[index].MPCpichead.Width) and (iy < MPCFile.MPCpic[index].MPCpichead.Height) then
              begin
                A := state;
                R := MPCFile.MPCpalette.col[Pdata^].R;
                G := MPCFile.MPCpalette.col[Pdata^].G;
                B := MPCFile.MPCpalette.col[Pdata^].B;
                pa := PByte(png.AlphaScanline[iy]);
                copymemory(pa + ix, @A, 1);
                pc := PByte(png.Scanline[iy]);
                inc(pc, ix * 3);
                copymemory(pc, @B, 1);
                copymemory(pc + 1, @G, 1);
                copymemory(pc + 2, @R, 1);
              end;
              inc(ix);
              inc(Pdata);
              Inc(I);
            end;
          end;
          if ix >= MPCFile.MPCpic[index].MPCpichead.Width then
          begin
            inc(iy);
            ix := 0;
          end;
        end;
      end;
  end;

  rs := TmemoryStream.Create;
  rs.Seek(0, 0);
  png.SaveToStream(rs);
  png.Free;
  bmp.Free;
  MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen := rs.Seek(0, 2);
  rs.Seek(0, 0);
  if MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen > 0 then
  begin
    setlength(MPCFile.MPCpic[index].data, MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen);
    copymemory(@MPCFile.MPCpic[index].data[0], rs.Memory, MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen);
  end
  else
  begin
    MPCFile.MPCpic[index].MPCpichead.MPCpicDataLen := 0;
    setlength( MPCFile.MPCpic[index].data, 0);
  end;
  rs.Free;

end;

function transAllImage(MPCFile: PMPCFile; alpha: boolean): boolean;
var
  I: integer;
begin
  //
  result := false;
  if MPCFile = Nil then
  begin
    exit;
  end;

  case MPCFile.Pictype of
    MPC, SHD:
      begin
        result := true;
        for I := 0 to MPCFile.MPCFileHead.PicCount - 1 do
        begin
          if not transImage(MPCFile, I, alpha) then
            result := false;
        end;
      end;
    ASF100, ASF101:
      begin
        result := true;
        for I := 0 to MPCFile.ASFFileHead.PicCount - 1 do
        begin
          if not transImage(MPCFile, I, alpha) then
            result := false;
        end;
      end;
  end;
end;

function transFileFormat(filename: string; alpha: boolean): boolean;
var
  picF: TPicFile;
  MPCFile: TMPCFile;
  IMGFile: TIMPImage;
  I: integer;
begin
  //
  result := false;
  picF := TPicfile.Create;
  picF.ClearMPCFile(@MPCFile);
  if (picF.loadPicFromFile(@MPCFile, filename)) and (transAllImage(@MPCFile, alpha))  then
  begin
    case MPCFile.Pictype of
      MPC, SHD:
        begin
          IMGFile.frameCount := max(MPCFile.MPCFileHead.PicCount, 0);
          IMGFile.directions := MPCFile.MPCFileHead.directions;
          IMGFile.interval := MPCFile.MPCFileHead.interval;
          for I := 0 to 4 do
            IMGFile.NULL[I] := 0;
          setlength(IMGFile.frame, IMGFile.frameCount);
          for I := 0 to IMGFile.frameCount - 1 do
          begin
            IMGFile.frame[I].dataLen := max(MPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, 0);
            IMGFile.frame[I].xOffset := MPCFile.MPCpic[I].MPCpichead.Width div 2;
            IMGFile.frame[I].yOffset := MPCFile.MPCpic[I].MPCpichead.Height - MPCFile.MPCFileHead.Ymove;
            IMGFile.frame[I].Null := 0;
            setlength(IMGFile.frame[I].data, IMGFile.frame[I].dataLen);
            if IMGFile.frame[I].dataLen > 0 then
            begin
              copymemory(@IMGFile.frame[I].data[0], @MPCFile.MPCpic[I].data[0], IMGFile.frame[I].dataLen);
            end;
          end;
          picF.SaveIMPImage(@IMGFile, filename);
        end;
      ASF100, ASF101:
        begin
          IMGFile.frameCount := max(MPCFile.ASFFileHead.PicCount, 0);
          IMGFile.directions := MPCFile.ASFFileHead.directions;
          IMGFile.interval := MPCFile.ASFFileHead.interval;
          for I := 0 to 4 do
            IMGFile.NULL[I] := 0;
          setlength(IMGFile.frame, IMGFile.frameCount);
          for I := 0 to IMGFile.frameCount - 1 do
          begin
            IMGFile.frame[I].dataLen := max(MPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, 0);
            IMGFile.frame[I].xOffset := MPCFile.ASFFileHead.Xmove;
            IMGFile.frame[I].yOffset := MPCFile.ASFFileHead.Ymove + TILE_HEIGHT div 2;
            IMGFile.frame[I].Null := 0;
            setlength(IMGFile.frame[I].data, IMGFile.frame[I].dataLen);
            if IMGFile.frame[I].dataLen > 0 then
            begin
              copymemory(@IMGFile.frame[I].data[0], @MPCFile.MPCpic[I].data[0], IMGFile.frame[I].dataLen);
            end;
          end;
          picF.SaveIMPImage(@IMGFile, filename);
        end;
    end;
  end;
  picF.destroy;
  result := true;
end;

function unpakFile(fileName, dir: string): boolean;
var
  filenum, fh, fsize, i: integer;
  fileID: array of cardinal;
  pak: TPakFile;
  pt: TPakType;
  ret: integer;
begin
  //
  result := false;
  if not fileExists(fileName) then
  begin
    exit;
  end;

  if not DirectoryExists(dir) then
  begin
    if not ForceDirectories(dir) then
    begin
      exit;
    end;
  end;

  filenum := 0;
  pak := TPakFile.create;
  pt := pak.CalPakType(fileName);
  pak.Destroy;
  case (pt) of
    ptPAK:
      begin
        fh := fileOpen(fileName, fmopenread);
        fsize := fileseek(fh, 0, 2);
        if fsize >= 8 then
        begin
          fileseek(fh, 4, 0);
          fileread(fh, filenum, 4);
          if (filenum > 0) and (fsize >= filenum * 12 + 8) then
          begin
            fileseek(fh, 8, 0);
            setlength(fileID, filenum);
            for i := 0 to filenum - 1 do
            begin
              fileread(fh, fileID[i], 4);
              fileseek(fh, 8, 1);
            end;
            fileclose(fh);
          end
          else
          begin
            filenum := 0;
            fileclose(fh);
          end;
        end
        else
        begin
          filenum := 0;
          fileclose(fh);
        end;
      end;
    ptPACKAGE:
      begin
        fh := fileOpen(fileName, fmopenread);
        fsize := fileseek(fh, 0, 2);
        if fsize >= 8 then
        begin
          fileseek(fh, 8, 0);
          fileread(fh, filenum, 4);
          if (filenum > 0) and (fsize >= filenum * 12 + 16) then
          begin
            fileseek(fh, 16, 0);
            setlength(fileID, filenum);
            for i := 0 to filenum - 1 do
            begin
              fileread(fh, fileID[i], 4);
              fileseek(fh, 8, 1);
            end;
            fileclose(fh);
          end
          else
          begin
            filenum := 0;
            fileclose(fh);
          end;
        end
        else
        begin
          filenum := 0;
          fileclose(fh);
        end;
      end;
  end;

  pak := TPakFile.create;
  if filenum > 0 then
  begin
    for i := 0 to filenum - 1 do
    begin
      ret := pak.ReadFromFileToBuffer(fileName, fileID[i]);
      if (ret >= 0) then
      begin
        if pak.bufferlen > 0 then
        begin
          fh := filecreate(dir + '$' + format('%x', [fileID[i]]));
          filewrite(fh, pak.databuffer[0], pak.bufferlen);
          fileclose(fh);
        end;
      end;
    end;
  end;
  pak.Destroy;
  result := true;
end;

procedure TRepackForm.Button2Click(Sender: TObject);
var
  f, fileID: cardinal;
  I: integer;
  dir, tempname, relativepath: string;
begin
  OpenDialog1.Filter := 'All files (*.*)|*.*';
  OpenDialog1.Options := OpenDialog1.Options + [ofAllowMultiSelect];
  if OpenDialog1.Execute then
  begin
    if SelectFolderDialog(self.handle, '选择相对路径', ExtractFilePath(OpenDialog1.FileName), dir) then
    begin
      if (dir[length(dir)] <> '\') and (dir[length(dir)] <> '/') then
        dir := dir + '\';
      if not DirectoryExists(dir) then
      begin
        if not ForceDirectories(dir) then
        begin
          showmessage('相对路径错误！');
          exit;
        end;
      end;
      for I := 0 to OpenDialog1.Files.Count - 1 do
      begin

        if fileexists(OpenDialog1.Files.Strings[i]) then
        begin
          relativepath := ExtractRelativePath(dir, ExtractFilePath(OpenDialog1.Files.Strings[i]));
          tempname := ExtractFileName(OpenDialog1.Files.Strings[i]);
          if tempname <> '' then
          begin
            if tempname[1] = '$' then
            begin
              try
                fileID := strtoint(tempname);
                valueListEditor1.InsertRow(OpenDialog1.Files.Strings[i], '$' + format('%x', [fileID]), true);
              except
                 valueListEditor1.InsertRow(OpenDialog1.Files.Strings[i], relativepath + tempname, true);
              end;
            end
            else
            begin
              valueListEditor1.InsertRow(OpenDialog1.Files.Strings[i], relativepath + tempname, true);
            end;
          end;
        end;
      end;
    end;
  end;
  OpenDialog1.Options := OpenDialog1.Options - [ofAllowMultiSelect];
end;

procedure TRepackForm.Button3Click(Sender: TObject);
begin
  if ValueListEditor1.Row >= 0 then
  begin
    ValueListEditor1.DeleteRow(ValueListEditor1.Row);
  end;
end;

procedure TRepackForm.Button4Click(Sender: TObject);
begin
  ValueListEditor1.Strings.clear;
end;

procedure TRepackForm.Button5Click(Sender: TObject);
var
  I: integer;
begin
  //
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    transFileFormat(ValueListEditor1.Keys[I], false);
  end;
  showmessage('转换格式完成！');
end;

procedure TRepackForm.Button6Click(Sender: TObject);
var
  data, dataout: array of byte;
  outlen: integer;
  I, filenum, fh, fph, buffer, temp: integer;
  PakdataHead: array of TPakDataHead;
  pak: TPakFile;
begin
  //

  CountAllID;
  RearrangetAllID;

  filenum := ValueListEditor1.RowCount - 1;
  setlength(pakDataHead, filenum);

  try
    for I := 0 to filenum - 1 do
    begin
      pakDataHead[i].fileID := strtoint(ValueListEditor1.Values[ValueListEditor1.Keys[I + 1]]);
      if fileexists(ValueListEditor1.Keys[I + 1]) then
      begin
        fh := fileopen(ValueListEditor1.Keys[I + 1], fmopenread);
        pakDataHead[i].filelen := fileseek(fh, 0, 2);
        fileclose(fh);
      end
      else
      begin
        pakDataHead[i].filelen := 0;
      end;
    end;
  except
    showmessage('文件ID错误，编号：' + inttostr(I));
    setlength(pakDataHead, 0);
    exit;
  end;
  savedialog1.Filter := 'All files (*.*)|*.*';
  if savedialog1.Execute then
  begin
    if fileexists(savedialog1.FileName) then
    begin
      deleteFile(savedialog1.FileName);
    end;
    fph := filecreate(savedialog1.FileName);
    Pak := TPakFile.Create;
    fileseek(fph, 0, 0);
    filewrite(fph, Pak.PACKAGEFILE_HEAD[1], length(Pak.PACKAGEFILE_HEAD));
    filewrite(fph, filenum, 4);
    buffer := 2;
    filewrite(fph, buffer, 4);
    if filenum > 0 then
    begin
      filewrite(fph, PakDataHead[0], Filenum * sizeof(TPakDataHead));
      temp := length(Pak.PACKAGEFILE_HEAD) + 8 + Filenum * sizeof(TPakDataHead);
      for I := 0 to filenum - 1 do
      begin
        PakDataHead[I].Offset := temp;
        if PakDataHead[I].filelen > 0 then
        begin
          setlength(data, PakDataHead[I].filelen);
          fh := fileopen(ValueListEditor1.Keys[I + 1], fmopenread);
          fileread(fh, data[0], PakDataHead[I].filelen);
          fileclose(fh);
          setlength(dataout, PakDataHead[I].filelen * 2 + 64);
          outlen := pak.pak_Package(@data[0], PakDataHead[I].filelen, @dataout[0], 2);
          if outlen > 0 then
          begin
            filewrite(fph, dataout[0], outlen);
            temp := temp + outlen;
          end;
        end;
      end;
    end;
    fileseek(fph, length(Pak.PACKAGEFILE_HEAD) + 8, 0);
    filewrite(fph, PakDataHead[0], Filenum * sizeof(TPakDataHead));
    setlength(data, 0);
    setlength(dataout, 0);
    setlength(pakDataHead, 0);
    fileclose(fph);
    Pak.Destroy;
    showmessage('打包完成！');
  end;
end;

procedure TRepackForm.Button7Click(Sender: TObject);
var
  pak: TPakFile;
  ID: cardinal;
begin
  Pak := TPakFile.Create;
  ID := pak.HashFileName(PansiChar(widestrToAnsi(edit1.Text)));
  edit2.Text := format('$%x', [ID]);
end;

procedure TRepackForm.Button8Click(Sender: TObject);
var
  I: integer;
begin
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    transFileFormat(ValueListEditor1.Keys[I], true);
  end;
  showmessage('转换格式完成！');
end;

procedure TRepackForm.Button9Click(Sender: TObject);
var
  f, fileID: cardinal;
  I: integer;
  dir, tempname, relativepath: string;
  fileList: TStringList;
begin
  if SelectFolderDialog(self.handle, '选择要添加的目录', '', dir) then
  begin
    if (dir[length(dir)] <> '\') and (dir[length(dir)] <> '/') then
        dir := dir + '\';
    if not DirectoryExists(dir) then
      exit;
    fileList := GetAllFile(dir);
    if SelectFolderDialog(self.handle, '选择相对路径', dir, dir) then
    begin
      if (dir[length(dir)] <> '\') and (dir[length(dir)] <> '/') then
        dir := dir + '\';
      if not DirectoryExists(dir) then
      begin
        if not ForceDirectories(dir) then
        begin
          showmessage('相对路径错误！');
          fileList.Free;
          exit;
        end;
      end;
      for I := 0 to fileList.Count - 1 do
      begin
        if fileexists(fileList.Strings[i]) then
        begin
          relativepath := ExtractRelativePath(dir, ExtractFilePath(fileList.Strings[i]));
          tempname := ExtractFileName(fileList.Strings[i]);
          if tempname <> '' then
          begin
            if tempname[1] = '$' then
            begin
              try
                fileID := strtoint(tempname);
                valueListEditor1.InsertRow(fileList.Strings[i], '$' + format('%x', [fileID]), true);
              except
                valueListEditor1.InsertRow(fileList.Strings[i], relativepath + tempname, true);
              end;
            end
            else
            begin
              valueListEditor1.InsertRow(fileList.Strings[i], relativepath + tempname, true);
            end;
          end;
        end;
      end;
    end;
    fileList.Free;
  end;
end;

procedure TRepackForm.RearrangetAllID;
var
  I, I2: integer;
  k1, k2: cardinal;
  tk, tv: string;
  keys, values: array of string;
  rowCount: integer;
begin
  //
  rowCount := ValueListEditor1.RowCount - 1;
  setlength(keys, rowCount);
  setlength(values, rowCount);
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    keys[I - 1] := ValueListEditor1.Keys[I];
    values[I - 1] :=  ValueListEditor1.Values[ValueListEditor1.Keys[I]];
  end;

  for I := 0 to rowCount - 2 do
    for I2 := 0 to rowCount - 2 - I do
    begin
      try
        k1 := strtoint(Values[I2]);
      except
        k1 := 0;
      end;
      try
        k2 := strtoint(Values[I2 + 1]);
      except
        k2 := 0;
      end;
      if k1 > k2 then
      begin
        tk := Keys[I2];
        tv := Values[I2];
        Keys[I2] := Keys[I2 + 1];
        Values[I2] := Values[I2 + 1];
        Keys[I2 + 1] := tk;
        Values[I2 + 1] := tv;
      end;
    end;

  ValueListEditor1.Strings.Clear;
  for I := 0 to rowCount - 1 do
  begin
    ValueListEditor1.InsertRow(keys[I], values[I], true);
  end;
end;

procedure TRepackForm.CountAllID;
var
  I: integer;
  ID: cardinal;
  s: string;
  pak: TPakFile;
begin
  //
  pak := TPakFile.Create;
  for I := 1 to ValueListEditor1.RowCount - 1 do
  begin
    if (ValueListEditor1.Values[ValueListEditor1.Keys[I]] <> '') then
    begin
      s := ValueListEditor1.Values[ValueListEditor1.Keys[I]];
      if s[1] <> '$' then
      begin
        ValueListEditor1.Values[ValueListEditor1.Keys[I]] := '$' + Format('%x', [Pak.HashFileName(PAnsiChar(WideStrToAnsi(s)))]);
      end;
    end;
  end;
  Pak.Destroy;
end;

procedure TRepackForm.FormClose(Sender: TObject; var Action: TCloseAction);
begin
  Action := cafree;
  CRePack := false;
end;

end.
