unit ImageEdit;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, PicFile, ExtCtrls, StdCtrls;

type
  TImageEditForm = class(TForm)
    LeftPanel: TPanel;
    ClientPanel: TPanel;
    XEdit: TEdit;
    XLabel: TLabel;
    YLabel: TLabel;
    YEdit: TEdit;
    OkBtn: TButton;
    CancelBtn: TButton;
    ReplaceBtn: TButton;
    OpenDialog: TOpenDialog;
    OffsetBtn: TButton;
    ScrollBox: TScrollBox;
    Image: TImage;
    procedure FormCreate(Sender: TObject);
    procedure FormClose(Sender: TObject; var Action: TCloseAction);
    procedure ReplaceBtnClick(Sender: TObject);
    procedure OkBtnClick(Sender: TObject);
    procedure OffsetBtnClick(Sender: TObject);
    procedure ImageMouseDown(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure XEditKeyPress(Sender: TObject; var Key: Char);
    procedure YEditKeyPress(Sender: TObject; var Key: Char);
  private
    { Private declarations }
    saveIdx: Integer;
    SaveIMPImageFile: PIMPImageFile;
    FIMPImageFile: TIMPImageFile;
    procedure drawOffset();
    procedure reDraw();
  public
    { Public declarations }
    Col: Cardinal;
    BGCol: Cardinal;
    function doEdit(idx: Integer; IMPImageFile: PIMPImageFile): Boolean;
  end;

implementation

{$R *.dfm}

procedure TImageEditForm.drawOffset();
var
  i, x, y: Integer;
begin
  FIMPImageFile.GetFrameOffset(0, @x, @y);
  for i := 0 to Image.Width do
    Image.Canvas.Pixels[i, y] := Col;
  for i := 0 to Image.Height - 1 do
    Image.Canvas.Pixels[x, i] := Col;
end;

procedure TImageEditForm.reDraw();
var
  bmp: TBitmap;
begin
  bmp := TBitmap.Create();
  bmp.Width := Image.Width;
  bmp.Height := Image.Height;
  bmp.Canvas.Brush.Color := BGCol;
  bmp.Canvas.Brush.Style := bsSolid;
  bmp.Canvas.FillRect(bmp.Canvas.ClipRect);
  FIMPImageFile.DrawFrame(@bmp, 0, 0, 0);
  Image.Canvas.CopyRect(Image.Canvas.ClipRect, bmp.Canvas, bmp.Canvas.ClipRect);
  bmp.Free();
  drawOffset();
end;

function TImageEditForm.doEdit(idx: Integer; IMPImageFile: PIMPImageFile): Boolean;
var
  data: PByte;
  len: Integer;
  x, y: Integer;
begin
  Result := False;
  if IMPImageFile.GetFrameData(idx, @data, @len) then
  begin
    IMPImageFile.GetFrameOffset(idx, @x, @y);
    FIMPImageFile.Clear();
    FIMPImageFile.AddFrame(data, len, x, y);
    saveIdx := idx;
    SaveIMPImageFile := IMPImageFile;
    XEdit.Text := IntToStr(x);
    YEdit.Text := IntToStr(y);
    reDraw();
    if ShowModal() = mrOK then
    begin
      Result := True;
    end;
  end;
end;

procedure TImageEditForm.FormClose(Sender: TObject; var Action: TCloseAction);
begin
  FIMPImageFile.Free();
  FIMPImageFile := nil;
end;

procedure TImageEditForm.FormCreate(Sender: TObject);
begin
  FIMPImageFile := TIMPImageFile.Create();
  Col := clRed;
  BGCol := clWhite;
end;

procedure TImageEditForm.ImageMouseDown(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
begin
  XEdit.Text := IntToStr(X);
  YEdit.Text := IntToStr(Y);
  FIMPImageFile.SetFrameOffset(0, X, Y);
  reDraw();
end;

procedure TImageEditForm.OffsetBtnClick(Sender: TObject);
var
  x, y: Integer;
begin
  FIMPImageFile.GetFrameOffset(0, @x, @y);
  try
    x := StrToInt(XEdit.Text);
  except
  end;
  try
    y := StrToInt(YEdit.Text);
  except
  end;
  FIMPImageFile.SetFrameOffset(0, x, y);
  reDraw();
end;

procedure TImageEditForm.OkBtnClick(Sender: TObject);
var
  data: PByte;
  len, x, y: Integer;
begin
  OffsetBtnClick(Sender);
  if FIMPImageFile.GetFrameData(saveIdx, @data, @len) then
  begin
    FIMPImageFile.GetFrameOffset(saveIdx, @x, @y);
    SaveIMPImageFile.SetFrame(saveIdx, data, len, x, y);
    FIMPImageFile.Clear();
  end;
end;

procedure TImageEditForm.ReplaceBtnClick(Sender: TObject);
var
  data: array of Byte;
  len: Integer;
  FH: Cardinal;
begin
  OpenDialog.Filter := 'PNG files (*.png)|*.png|All files (*.*)|*.*';
  if OpenDialog.Execute() then
  begin
    if FileExists(OpenDialog.FileName) then
    begin
      FH := FileOpen(OpenDialog.FileName, fmOpenRead);
      len := FileSeek(FH, 0, 2);
      fileSeek(FH, 0, 0);
      if len > 0 then
      begin
        SetLength(data, len);
        FileRead(FH, data[0], len);
        FIMPImageFile.SetFrameData(0, @data[0], len);
        SetLength(data, 0);
      end
      else
        FIMPImageFile.SetFrameData(0, nil, 0);
      reDraw();
      FileClose(FH);
    end;
  end;
end;

procedure TImageEditForm.XEditKeyPress(Sender: TObject; var Key: Char);
begin
  if Key = #13 then
    OffsetBtnClick(Sender);
end;

procedure TImageEditForm.YEditKeyPress(Sender: TObject; var Key: Char);
begin
  if Key = #13 then
    OffsetBtnClick(Sender);
end;

end.
