unit Head;

interface

uses
  windows,Graphics,sysutils, inifiles,classes,IdHashMessageDigest,SyncObjs,ShlObj, math,
  PNGimage, dialogs,
  JPEG;


const
  Appname: String = 'SHFEditor';
  VersionString : String = '1.0';
  fmcursor: integer = 1;
var
  titlestr: string;
  configFile: string = 'config.ini';

var
  CRePack: boolean = false;
  CRePackH: integer = 0;
  CImageEditor: Boolean = false;
  CImageEditorH: Integer = 0;
  TILE_WIDTH: integer = 64;
  TILE_HEIGHT: integer = 32;

type
  TIMGType = (IMG_NONE, IMG_JPG, IMG_PNG, IMG_BMP);

  TIMG = record
    Pic: Pointer;
    IMGType: TIMGType;
  end;

  PIMG = ^TIMG;

  PPNGImage = ^TPNGImage;
  PJPEGImage = ^TJPEGImage;
  PBitmap = ^TBitmap;

const

  RW_OK = 0;

  IMG_PNGHead: array[0..3] of Ansichar = (#$89, 'P' , 'N' , 'G');
  IMG_JPGHead: array[0..9] of AnsiChar = (#$FF , #$D8 , #$FF , #$E0 , #$00 ,#$10 , #$4A , #$46 , #$49 , #$46);
  IMG_BMPHead: array[0..1] of Ansichar = ('B', 'M');

function CampareFileHead(Pdata: Pbyte; Headstr: Ansistring): Boolean;
function SelectFolderDialog(const Handle:integer;const Caption:string;const InitFolder:string;var SelectedFolder:string):boolean;

function WideSTRtoAnsi(str: widestring): Ansistring;
function AnsitoWideStr(str: Ansistring): WideString;

function CalIMGType(PData: Pbyte): TIMGType; overload;
function CalIMGType(Filename: String): TIMGType;  overload;

function GetAllFile(const Path: string;FileEx:string = '.*'): TStringList;

implementation

function GetAllFile(const Path: string; FileEx:string = '.*'): TStringList;
var
  f: TSearchRec;
  Ret: Integer;
  strL: TStringList;
  I: integer;
begin
  FileEx := UpperCase(FileEx);
  Result := TStringList.Create;
  Ret := FindFirst(Path + '*.*', faAnyFile, f);
  while (Ret = 0) do
  begin
    if f.Attr = faDirectory then
    begin
      if (f.Name <>'.') and (f.Name <> '..') then
      begin
        strL := GetAllFile(Path + f.Name + '\', FileEx);
        for I := 0 to strL.Count - 1 do
          Result.Append(strL.Strings[I]);
        strL.Free;
      end;
    end
    else
    begin
      if (FileEx = '.*') or (UpperCase(ExtractFileExt(f.Name)) = UpperCase(FileEx)) then
        Result.Append(Path + f.Name);

    end;
    Ret := FindNext(f)
  end;
  FindClose(f)
end;


function CalIMGType(Filename: String): TIMGType;
var
  pichead: array[0..15] of Ansichar;
  picfile, len: integer;
begin
  result := IMG_NONE;
  if not fileexists(filename) then
    exit;

  picfile := fileopen(filename, fmopenread);
  fileseek(picfile, 0, 0);
  len := fileseek(picfile, 0, 2);
  fileseek(picfile, 0, 0);
  if len >= 16 then
    fileread(picfile, pichead[0], 16)
  else
  begin
    result := IMG_NONE;
    fileclose(picfile);
    exit;
  end;
  fileclose(picfile);
  result := CalIMGType(@pichead[0]);
end;

function CalIMGType(PData: Pbyte): TIMGType;
begin
  result := IMG_NONE;
  if CampareFileHead(Pdata, IMG_PNGhead) then
    result := IMG_PNG
  else if CampareFileHead(Pdata, IMG_JPGhead) then
    result := IMG_JPG
  else if CampareFileHead(Pdata, IMG_BMPhead) then
    result := IMG_BMP
  else
    result := IMG_NONE;
end;

function WideStrtoAnsi(str: WideString): Ansistring;
var
  len: integer;
  codepage: cardinal;
begin
  codepage := 936;
  len := WideCharToMultiByte(codepage, 0, PWideChar(str), -1, nil, 0, nil, nil);
  if len > 1 then
  begin
    setlength(Result, len - 1);
    WideCharToMultiByte(codepage, 0, PWideChar(str), -1, PansiChar(Result), len - 1, nil, nil);
  end
  else
    result := '';
end;

function AnsitoWideStr(str: Ansistring): WideString;
var
  len: integer;
  codepage: cardinal;
begin
  codepage := 936;
  len := MultiByteToWideChar(codepage, 0, PansiChar(str), -1, nil, 0);

  if len > 1 then
  begin
    setlength(Result, len - 1);
    MultiByteToWideChar(codepage, 0, PansiChar(str), length(str), PWideChar(Result), len - 1);
    //Result[len] := #0;
    //setlength(Result, len - 1);
  end
  else
  begin
    //setlength(Result, 1);
    //Result[1] := #0;
    //setlength(Result, 0);
    Result := '';
  end;
  //Result[len] := #0;
end;


function CampareFileHead(Pdata: Pbyte; Headstr: Ansistring): Boolean;
var
  I: integer;
  Pd: Pbyte;
begin
  result := true;
  Pd := Pdata;
  for I := 1 to length(Headstr) do
  begin
    if Pd^ <> Byte(HeadStr[I]) then
    begin
      result := false;
      break;
    end;
    inc(Pd);
  end;
end;

function SelectFolderDialog(const Handle:integer;const Caption:string;const InitFolder:string;var SelectedFolder:string):boolean;
var
  BInfo: _browseinfoW;
  Buffer: array[0..MAX_PATH] of Char;
  ID: IShellFolder;
  Eaten, Attribute: Cardinal;
  ItemID: PItemidlist;
begin
  with BInfo do
  begin
    HwndOwner := Handle;
    lpfn := nil;
    lpszTitle := Pchar(Caption);
    ulFlags := BIF_RETURNONLYFSDIRS+BIF_NEWDIALOGSTYLE;
    SHGetDesktopFolder(ID);
    ID.ParseDisplayName(0,nil,'\',Eaten,ItemID,Attribute);
    pidlRoot := ItemID;
    GetMem(pszDisplayName, MAX_PATH);
  end;

  FreeMem(Binfo.pszDisplayName);
  if SHGetPathFromIDList(SHBrowseForFolder(BInfo),  Buffer) then
  begin
    SelectedFolder := Buffer;
    if Length(SelectedFolder)<>3 then
      SelectedFolder := SelectedFolder;
    result := True;
  end
  else begin
    SelectedFolder := '';
    result := False;
  end;

end;

end.
