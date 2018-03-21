unit mouse;

interface

uses
  Windows, SysUtils, Classes, Graphics, forms, ExtCtrls;

procedure   SaveResourceAsFile(const   ResName:   string;   ResType:   pchar; const   FileName:   string);
function   SaveResourceAsTempFile(const   ResName:   string;   ResType:   pchar):   string;
function   GetResourceAsAniCursor(const   ResName:   string):   HCursor;
function   CreateTempFile: String;

implementation


function CreateTempFile: String;
var
  TempFile,TempDir : array[1..256] of Char;
  Files,Dirs:PChar;
begin
  Files:=@TempFile;
  Dirs:=@TempDir;
  GetTEmpPath(256,dirs);
  GetTempFileName(dirs,'~Tmp',0,Files);
  Result:=Copy(Files,1,Length(Files));
end;

procedure   SaveResourceAsFile(const   ResName:   string;   ResType:   pchar;
    const   FileName:   string);
begin
    with   TResourceStream.Create(hInstance,   ResName,   ResType)   do
        try
            SaveToFile(FileName);
        finally
            Free;
        end;
end;

function  SaveResourceAsTempFile(const   ResName:   string;   ResType:   pchar):   string;
begin
    Result   :=   CreateTempFile;
    SaveResourceAsFile(ResName,   ResType,   Result);
end;

function   GetResourceAsAniCursor(const   ResName:   string):   HCursor;
var
    CursorFile:   string;
begin
    CursorFile   :=   SaveResourceAsTempFile(ResName,   RT_RCDATA);
    //Result   :=   LoadImage(0,   PChar(CursorFile),   IMAGE_CURSOR,   0, 0,   LR_DEFAULTSIZE   or   LR_LOADFROMFILE);
    Result := loadcursorfromfile(Pwidechar(CursorFile));
    DeleteFile(CursorFile);
    if   Result   =   0   then
        raise   Exception.Create(SysErrorMessage(GetLastError));
end;

end.
