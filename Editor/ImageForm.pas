unit ImageForm;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, ExtCtrls, StdCtrls, PNGImage, PicFile, Head;

type
  TImageEditor = class(TForm)
    Panel1: TPanel;
    Panel2: TPanel;
    ScrollBar: TScrollBar;
    Image: TImage;
    FileNameEdit: TEdit;
    FileNameLabel: TLabel;
    OpenBtn: TButton;
    OpenDialog: TOpenDialog;
    procedure FormResize(Sender: TObject);
    procedure FormCreate(Sender: TObject);
    procedure FormClose(Sender: TObject; var Action: TCloseAction);
    procedure OpenBtnClick(Sender: TObject);
    procedure ScrollBarChange(Sender: TObject);
    procedure ImageMouseMove(Sender: TObject; Shift: TShiftState; X,
      Y: Integer);
  private
    { Private declarations }
    FBuffer: TBitmap;
    FNowFirstPicNum: Integer;
    FIMPImageFile: TIMPImageFile;
    FLineNum: Integer;
    FLineWidth: Integer;
    FSelectedIndex: Integer;
    procedure DrawBuffer();
    procedure BufferPresent();
  const
    FLinePicNum: Integer = 10;
    FLineHeight: Integer = 150;
  public
    { Public declarations }
    function GetNowFirstPicNum(): Integer;
    property nowFirstPicNum: Integer read GetNowFirstPicNum;
    function GetPicCount(): Integer;
    property picCount: Integer read GetPicCount;

  end;

implementation

{$R *.dfm}

procedure TImageEditor.FormClose(Sender: TObject; var Action: TCloseAction);
begin
  FBuffer.Free;
  FIMPImageFile.Free;
  Action := caFree;
  CImageEditor := false;
end;

procedure TImageEditor.FormCreate(Sender: TObject);
begin
  FBuffer := TBitmap.Create;
  FIMPImageFile := TIMPImageFile.Create;
end;

procedure TImageEditor.FormResize(Sender: TObject);
begin
  Image.Picture.Bitmap.Width := Image.Width;
  Image.Picture.Bitmap.Height := Image.Height;
  FBuffer.Width := Image.Width;
  FBuffer.Height := Image.Height;
  FLineNum := Image.Height div FLineHeight + 1;
  FLineWidth := Image.Width div FLinePicNum;
  DrawBuffer();
  BufferPresent();
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
  FBuffer.Canvas.Brush.Color := clWhite;
  FBuffer.Canvas.FillRect(FBuffer.Canvas.ClipRect);
  FBuffer.Canvas.Brush.Style := bsClear;
  FBuffer.Canvas.Font.Color := clred;
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

procedure TImageEditor.BufferPresent();
begin
  Image.Canvas.CopyRect(Image.Canvas.ClipRect, FBuffer.Canvas, FBuffer.Canvas.ClipRect);
end;

function TImageEditor.GetNowFirstPicNum(): Integer;
begin
  result := FNowFirstPicNum;
end;

function TImageEditor.GetPicCount(): Integer;
begin
  result := FIMPImageFile.imageCount;
end;

procedure TImageEditor.ImageMouseMove(Sender: TObject; Shift: TShiftState; X,
  Y: Integer);
var
  temp: Integer;
begin
  //FSelectedIndex
end;

procedure TImageEditor.OpenBtnClick(Sender: TObject);
begin
  if OpenDialog.Execute() then
  begin
    if FIMPImageFile.Load(OpenDialog.FileName) then
    begin
      FileNameEdit.Text := OpenDialog.FileName;
      ScrollBar.Max := FIMPImageFile.imageCount div FLinePicNum;
      ScrollBar.Position := 0;
      FNowFirstPicNum := 0;
      DrawBuffer();
      BufferPresent();
    end;
  end;
end;

procedure TImageEditor.ScrollBarChange(Sender: TObject);
begin
  FNowFirstPicNum := ScrollBar.Position * FLinePicNum;
  DrawBuffer();
  BufferPresent();
end;

end.
