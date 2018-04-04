unit ImageForm;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, ExtCtrls, StdCtrls, PNGImage, PicFile, Head, Menus;

type
  TImageEditor = class(TForm)
    Panel1: TPanel;
    Panel2: TPanel;
    ScrollBar: TScrollBar;
    Image: TImage;
    OpenDialog: TOpenDialog;
    Panel3: TPanel;
    FileNameLabel: TLabel;
    FileNameEdit: TEdit;
    OpenBtn: TButton;
    Panel4: TPanel;
    PopupMenu: TPopupMenu;
    DirectionEdit: TEdit;
    DirectionLabel: TLabel;
    IntervalLabel: TLabel;
    IntervalEdit: TEdit;
    SaveBtn: TButton;
    InsertItem: TMenuItem;
    EditItem: TMenuItem;
    DeleteItem: TMenuItem;
    AddItem: TMenuItem;
    procedure FormResize(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormClose(Sender: TObject; var Action: TCloseAction);
    procedure OpenBtnClick(Sender: TObject);
    procedure ScrollBarChange(Sender: TObject);
    procedure ImageMouseMove(Sender: TObject; Shift: TShiftState; X,
      Y: Integer);
    procedure SaveBtnClick(Sender: TObject);
    procedure ImageMouseUp(Sender: TObject; Button: TMouseButton;
      Shift: TShiftState; X, Y: Integer);
    procedure DeleteItemClick(Sender: TObject);
    procedure InsertItemClick(Sender: TObject);
    procedure AddItemClick(Sender: TObject);
    procedure EditItemClick(Sender: TObject);
  private
    { Private declarations }
    FBuffer: TBitmap;
    FNowFirstPicNum: Integer;
    FIMPImageFile: TIMPImageFile;
    FLineNum: Integer;
    FLineWidth: Integer;
    FSelectedIndex: Integer;
    PopupSelectedIndex: Integer;
    procedure DrawBuffer();
    procedure BufferPresent();
    procedure DrawSquare(x, y: Integer);
    procedure ReDraw();
    function InsertFrame(idx: Integer): Boolean;
    function EditFrame(idx: Integer): Boolean;
  const
    FLinePicNum: Integer = 10;
    FLineHeight: Integer = 150;
  public
    { Public declarations }
    Col: Cardinal;
    BGCol: Cardinal;
    function GetNowFirstPicNum(): Integer;
    property nowFirstPicNum: Integer read GetNowFirstPicNum;
    function GetPicCount(): Integer;
    property picCount: Integer read GetPicCount;

  end;

implementation

uses
  ImageEdit;

{$R *.dfm}

procedure TImageEditor.FormClose(Sender: TObject; var Action: TCloseAction);
begin
  FBuffer.Free();
  FBuffer := nil;
  Action := caFree;
  CImageEditor := False;
  FIMPImageFile.Free();
  FIMPImageFile := nil;
end;

procedure TImageEditor.FormCreate(Sender: TObject);
begin
  FBuffer := TBitmap.Create;
  FIMPImageFile := TIMPImageFile.Create;
  Col := clRed;
  BGCol := clWhite;
end;

procedure TImageEditor.FormResize(Sender: TObject);
begin
  if FBuffer = nil then
    exit;
  Image.Picture.Bitmap.Width := Image.Width;
  Image.Picture.Bitmap.Height := Image.Height;
  FBuffer.Width := Image.Width;
  FBuffer.Height := Image.Height;
  FLineNum := Image.Height div FLineHeight + 1;
  FLineWidth := Image.Width div FLinePicNum;
  ReDraw();
end;

procedure TImageEditor.DeleteItemClick(Sender: TObject);
begin
  FIMPImageFile.DeleteFrame(PopupSelectedIndex);
  ReDraw();
end;

procedure TImageEditor.DrawBuffer();
var
  I, ix, iy, totalNum: Integer;
  rect: TRect;
const
  titleHeight = 15;
begin
  ix := 0;
  iy := 0;
  totalNum := FLinePicNum * FLineNum + FNowFirstPicNum;
  if picCount < totalNum then
  begin
    totalNum := picCount;
  end;
  if FBuffer = nil then
    exit;
  FBuffer.Canvas.Brush.Color := BGCol;
  FBuffer.Canvas.FillRect(FBuffer.Canvas.ClipRect);
  FBuffer.Canvas.Brush.Style := bsClear;
  FBuffer.Canvas.Font.Color := Col;
  FBuffer.Canvas.Font.Size := 10;
  for I := FNowFirstPicNum to totalNum - 1 do
  begin
    rect.Left := ix * FLineWidth;
    rect.Top := iy * FLineHeight + titleHeight;
    rect.Right := FLineWidth + rect.Left;
    rect.Bottom := FLineHeight + rect.Top - titleHeight;
    FBuffer.Canvas.TextOut(rect.Left, rect.Top - titleHeight, inttostr(I + 1));
    FIMPImageFile.StretchDrawFrame(@FBuffer, I, rect);
    inc(ix);
    if ix = FLinePicNum then
    begin
      ix := 0;
      inc(iy);
    end;
  end;
end;

procedure TImageEditor.AddItemClick(Sender: TObject);
begin
  if InsertFrame(picCount) then
    ReDraw();
end;

procedure TImageEditor.BufferPresent();
begin
  if FBuffer = nil then
    exit;
  Image.Canvas.CopyRect(Image.Canvas.ClipRect, FBuffer.Canvas, FBuffer.Canvas.ClipRect);
end;

procedure TImageEditor.DrawSquare(x, y: Integer);
var
  i: Integer;
begin
  for i := 0 to FLineWidth - 1 do
  begin
    Image.Canvas.Pixels[x + i, y] := clRed;
    Image.Canvas.Pixels[x + i, y + FLineHeight] := Col;
  end;
  for i := 1 to FLineHeight - 2 do
  begin
    Image.Canvas.Pixels[x, y + i] := clRed;
    Image.Canvas.Pixels[x + FLineWidth, y + i] := Col;
  end;
end;

procedure TImageEditor.EditItemClick(Sender: TObject);
begin
  if EditFrame(PopupSelectedIndex) then
    ReDraw();
end;

procedure TImageEditor.ReDraw;
begin
  DrawBuffer();
  BufferPresent();
  FSelectedIndex := -1;
end;

function TImageEditor.InsertFrame(idx: Integer): Boolean;
var
  data: array of Byte;
  len, count, i: Integer;
  FH: Cardinal;
begin
  Result := false;
  OpenDialog.Filter := 'PNG files (*.png)|*.png|All files (*.*)|*.*';
  if OpenDialog.Execute() then
  begin
    count := 0;
    for i := 0 to OpenDialog.Files.Count - 1 do
    begin
      if FileExists(OpenDialog.Files[i]) then
      begin
        FH := FileOpen(OpenDialog.Files[i], fmOpenRead);
        len := FileSeek(FH, 0, 2);
        FileSeek(FH, 0, 0);
        if len <= 0 then
        begin
          FIMPImageFile.InsertFrame(idx + count, nil, 0, 0, 0);
        end
        else
        begin
          SetLength(data, len);
          FileRead(FH, data[0], len);
          FIMPImageFile.InsertFrame(idx + count, @data[0], len, 0, 0);
        end;
        SetLength(data, 0);
        FileClose(FH);
        Inc(count);
        Result := true;
      end;
    end;
  end;
end;

function TImageEditor.EditFrame(idx: Integer): Boolean;
var
  ImageEditForm: TImageEditForm;
begin
  ImageEditForm := TImageEditForm.Create(Self);
  Result := ImageEditForm.doEdit(idx, @FIMPImageFile);
  ImageEditForm.Free();
end;

function TImageEditor.GetNowFirstPicNum(): Integer;
begin
  result := FNowFirstPicNum;
end;

function TImageEditor.GetPicCount(): Integer;
begin
  if FIMPImageFile <> nil then
    result := FIMPImageFile.imageCount
  else
    result := 0;
end;

procedure TImageEditor.ImageMouseMove(Sender: TObject; Shift: TShiftState; X,
  Y: Integer);
var
  temp: Integer;
begin
  if x >= FLinePicNum * FLineWidth then
  begin
    temp := -1;
  end
  else
  begin
    temp := x div FLineWidth + (y div FLineHeight) * FLinePicNum;
    if (FIMPImageFile <> nil) and (temp + FNowFirstPicNum >= FIMPImageFile.imageCount) then
      temp := -1;
  end;

  if temp <> FSelectedIndex then
  begin
    FSelectedIndex := temp;
    BufferPresent();
    if FSelectedIndex >= 0 then
      DrawSquare(FSelectedIndex mod FLinePicNum * FLineWidth, FSelectedIndex div FLinePicNum * FLineHeight);
  end;
end;

procedure TImageEditor.ImageMouseUp(Sender: TObject; Button: TMouseButton;
  Shift: TShiftState; X, Y: Integer);
var
  pt: TPoint;
begin
  GetCursorPos(pt);
  if (FSelectedIndex >= 0) and (Button = mbRight) then
  begin
    PopupSelectedIndex := FSelectedIndex + FNowFirstPicNum;
    PopupMenu.Popup(pt.X, pt.Y);
  end;
end;

procedure TImageEditor.InsertItemClick(Sender: TObject);
begin
  if InsertFrame(PopupSelectedIndex) then
    ReDraw();
end;

procedure TImageEditor.OpenBtnClick(Sender: TObject);
begin
  if OpenDialog.Execute() then
  begin
    OpenDialog.Filter :=  'All files (*.*)|*.*';
    if FIMPImageFile.Load(OpenDialog.FileName) then
    begin
      FileNameEdit.Text := OpenDialog.FileName;
      ScrollBar.Max := FIMPImageFile.imageCount div FLinePicNum;
      ScrollBar.Position := 0;
      FNowFirstPicNum := 0;
      DirectionEdit.Text := IntToStr(FIMPImageFile.directions);
      IntervalEdit.Text := IntToStr(FIMPImageFile.interval);
      ReDraw();
    end;
  end;
end;

procedure TImageEditor.SaveBtnClick(Sender: TObject);
begin
  FIMPImageFile.directions := StrToInt(DirectionEdit.Text);
  FIMPImageFile.interval := StrToInt(IntervalEdit.Text);
  FIMPImageFile.Save(FileNameEdit.Text);
end;

procedure TImageEditor.ScrollBarChange(Sender: TObject);
begin
  FNowFirstPicNum := ScrollBar.Position * FLinePicNum;
  ReDraw();
end;

end.
