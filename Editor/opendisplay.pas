unit opendisplay;

interface

uses
  Windows, Messages, SysUtils, Variants, Classes, Graphics, Controls, Forms,
  Dialogs, ExtCtrls, StdCtrls, {jpeg,} pngimage, math;

type
  TFlashForm = class(TForm)
    Timer1: TTimer;
    procedure FormCreate(Sender: TObject);
    procedure FormClose(Sender: TObject; var Action: TCloseAction);
    procedure PNGForm_FromGraphic(AGraphic : TGraphic);
    procedure SetFormAlpha(alpha: byte);
    procedure FormClick(Sender: TObject);
    function ShowModal: integer;
    procedure Timer1Timer(Sender: TObject);
  private
    { Private declarations }
  public
    { Public declarations }
  var
    BlendFunction: TBlendFunction;
    ptDst, ptSrc: TPoint;
    Size: TSize;
    isShowModal: boolean;
    Flashbmp : TBitmap;
    Falpha: byte;
  end;

var
  //opendisbmp: Tbitmap;

  alreadyopen: Boolean = false;

implementation

{$R *.dfm}

procedure TFlashForm.FormClick(Sender: TObject);
begin
  if alreadyopen then
    close;
end;

procedure TFlashForm.FormClose(Sender: TObject; var Action: TCloseAction);
begin
  //opendisbmp.Free;
  alreadyopen := true;
  Flashbmp.Free();
  action := cafree;
end;

procedure TFlashForm.FormCreate(Sender: TObject);
var
  {JPG: TJPeGimage;
  rs: Tresourcestream;
  tempdisbmp: Tbitmap; }
  wic : TWICImage;
  r : TResourceStream;
begin
  if alreadyopen then
    self.Position := poMainFormCenter;
  isShowModal := false;
  r := TResourceStream.Create(HInstance, 'PngImage_1', RT_RCDATA);
  wic := TWICImage.Create;
  wic.LoadFromStream(r);

  PNGForm_FromGraphic(wic);
  wic.Free;
  r.Free;

  {JPG := TJPeGimage.create;
  rs := TResourceStream.Create(HInstance, 'JPGimage_1', RT_RCDATA);
  rs.Position := 0;
  JPG.LoadFromStream(rs);
  opendisbmp := Tbitmap.Create;
  tempdisbmp := Tbitmap.Create;
  tempdisbmp.Assign(JPG);
  //opendisbmp.Canvas.s
  self.Canvas.Brush.Style := bsclear;
  self.Canvas.Font.Color := clblack;
  self.Canvas.Font.Name := '宋体';
  self.Canvas.Font.Size := 16;
  processint:= 0;
  opendisbmp.Width := self.Width;
  opendisbmp.Height := self.Height;
  //opendisbmp.Canvas.StretchDraw(opendisbmp.Canvas.ClipRect, tempdisbmp);
  //image1.Canvas.StretchDraw(image1.Canvas.ClipRect, opendisbmp);
  //self.Canvas.CopyRect(self.Canvas.ClipRect,opendisbmp.Canvas,opendisbmp.Canvas.ClipRect);
  //image1.Canvas.Font.Color := Random($FFFFFF);
  self.Canvas.TextOut(0, 0, '程序载入中...');
  self.Refresh;
  JPG.Free;
  tempdisbmp.Free;}
end;

procedure TFlashForm.PNGForm_FromGraphic(AGraphic : TGraphic);
begin
  Flashbmp := TBitmap.Create;
  Flashbmp.Assign(AGraphic);
  ptDst := Point(Self.Left, Self.Top);
  ptSrc := Point(0, 0);
  Size.cx := AGraphic.Width;
  Size.cy := AGraphic.Height;

  BlendFunction.BlendOp := AC_SRC_OVER;
  BlendFunction.BlendFlags := 0;
  BlendFunction.SourceConstantAlpha := 1; // 透明度
  BlendFunction.AlphaFormat := AC_SRC_ALPHA;

  SetWindowLong(Self.Handle, GWL_EXSTYLE, GetWindowLong(Self.Handle,
      GWL_EXSTYLE) or WS_EX_LAYERED);
  UpdateLayeredWindow(Self.Handle,
     Self.Canvas.Handle,
     @ptDst,
     @Size,
     Flashbmp.Canvas.Handle,
     @ptSrc,
     0,
     @BlendFunction,
     ULW_ALPHA);

end;

procedure TFlashForm.SetFormAlpha(alpha: byte);
begin
  //
  ptDst := Point(Self.Left, Self.Top);
  ptSrc := Point(0, 0);
  BlendFunction.SourceConstantAlpha := alpha; // 透明度
  UpdateLayeredWindow(Self.Handle,
     Self.Canvas.Handle,
     @ptDst,
     @Size,
     Flashbmp.Canvas.Handle,
     @ptSrc,
     0,
     @BlendFunction,
     ULW_ALPHA);
end;

function TFlashForm.ShowModal: integer;
begin
  Falpha := 1;
  isShowModal := true;
  timer1.Enabled := true;
  result := inherited ShowModal;
end;

procedure TFlashForm.Timer1Timer(Sender: TObject);
begin
  Falpha := min(Falpha + 3, 255);
  SetFormAlpha(Falpha);
  if Falpha = 255 then
  begin
    timer1.Enabled := false;
  end;
end;

end.
