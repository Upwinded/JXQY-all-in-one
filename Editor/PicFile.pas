unit PicFile;

interface

uses
  Sysutils,
  Windows,
  dialogs,
  PakFile,
  PNGImage,
  JPEG,
  Graphics,
  Classes,
  Head,
  Math;

const
  Pic_Interval_Default = 30;

Type

  TIMPFrame = record
    dataLen: integer;
	  xOffset: integer;
	  yOffset: integer;
	  Null: integer;
    data: array of byte;
  end;

  TIMPImage = record
    Headstr: array[0..15] of ansiChar;
    frameCount: integer;
    directions: integer;
    interval: integer;
    NULL: array[0..4] of integer;
    frame: array of TIMPFrame;
  end;

  PIMPImage = ^TIMPImage;

  TPICTYPE = (NONE, MPC, SHD, PIC, ASF100, ASF101, RLE8, IMG); //none不是图片，mpc图片，shadow图片，pic图片(图片叠加)，IMG为单个文件

  PPICTYPE = ^TPICTYPE;

  Tcol = record
    B: byte;
    G: byte;
    R: byte;
    A: byte;
  end;

  TASFFileHead = record
    Headstr: array[0..15] of ansiChar;
    Width: integer;          //最大宽度
    Height: integer;
    PicCount: integer;       //文件数
    directions: integer;    //方向
    PaletteLen: integer;     //调色板长度
    //datalen: integer;        //数据长度，不包括头结构，调色板和文件偏移
    interval: integer;
    Xmove: integer;          //x方向偏移？
    Ymove: integer;
    MPCnil2: array[0..3] of integer;
  end;

  TMPCFileHead = record
    Headstr: array[0..15] of ansiChar;
    MPCnil1: array[0..11] of integer;
    datalen: integer;        //数据长度，不包括头结构，调色板和文件偏移
    MaxWidth: integer;        //最大宽度
    MaxHeight: integer;
    PicCount: integer;       //文件数
    directions: integer;    //数据位数？
    PaletteLen: integer;     //调色板长度
    interval: integer;          //时间间隔
    Ymove: integer;
    MPCnil2: array[0..7] of integer;
  end;

  TMPCpichead = record
    MPCpicDataLen: integer;      //含头长度
    Width: integer;
    Height: integer;
    MPCpicNil: array[0..1] of integer;
  end;

  TMPCpic = record
    MPCpichead : TMPCpichead;
    data: array of byte;
    PIC: TIMG;
  end;

  PMPCpic = ^TMPCpic;

  TMPCpalette = record
    len: integer;
    col: array of Tcol;
  end;

  PMPCpalette = ^TMPCpalette;

  TMPCFile = record
    Filename: AnsiString;
    FileID: Cardinal;
    Pictype : TPICTYPE;
    MPCFileHead: TMPCFileHead;
    ASFFileHead: TASFFileHead;
    MPCpalette: TMPCpalette;
    MPCpic: array of TMPCpic;
  end;

  PMPCFile = ^TMPCFile;

  TpicFile = class
    PICOBJ: TObject;
    Filenum: integer;
    MPCFile: array of TMPCFile;
    Pak: TPakFile;
    Constructor Create; overload;
    procedure Clear;
    procedure ClearMPCFile(picMPCFile: PMPCFile);
    procedure Release;
    destructor Destroy;
    function CalPicTYPE(filename: String): TPICTYPE; overload;
    function CalPicTYPE(Pdata: Pbyte): TPICTYPE; overload;
    function ARGB32(A, R, G, B: byte): Cardinal;
    function RGB32(R, G, B: byte): Cardinal;

    function LoadPicFromFile(picMPCFile: PMPCFile; filename: String): boolean;
    function LoadPicFromBuffer(picMPCFile: PMPCFile; Fdata: Pbyte; Bufferlen: integer): boolean;
    //function LoadPicFromPak(picMPCFile: PMPCFile; filename, PakFile: String): boolean;
    //function AddNewFile(filename: ANsistring): boolean; overload;
    //function AddNewFile(filename, PakFile: Ansistring): boolean; overload;
    function RemoveFile(filename: AnsiString): boolean; overload;
    function RemoveFile(fileID: Cardinal): boolean; overload;
    function RemoveFile2(num: integer): boolean;
    function CopyMPCFile(PSource, PDest: PMPCFile): boolean;
    function CopyMPCPic(PSource, PDest: PMPCPic): boolean;
    function CopyPalette(PSource, PDest: PMPCPalette): boolean;
    procedure SaveIMPImage(IMPImage: PIMPImage; fileName: string);
    function LoadIMPImage(IMPImage: PIMPImage; fileName: string): boolean;
    //function CreateSurfaceFromPic();
  const
    IMGHead: AnsiString = 'IMG File Ver1.0' + #0;
    MPChead: Ansistring = 'MPC File Ver2.0' + #0;
    PIChead: Ansistring = 'PIC File Ver1.0' + #0;
    SHDhead: Ansistring = 'SHD File Ver2.0' + #0;
    ASF100head: Ansistring = 'ASF 1.00' + #0 + #0 + #0 + #0 + #0 + #0 + #0 + #0;
    ASF101head: Ansistring = 'ASF 1.01' + #0 + #0 + #0 + #0 + #0 + #0 + #0 + #0;
    PNGHead: array[0..3] of Ansichar = (#$89, 'P' , 'N' , 'G');
    JPGHead: array[0..9] of AnsiChar = (#$FF , #$D8 , #$FF , #$E0 , #$00 ,#$10 , #$4A , #$46 , #$49 , #$46);
    BMPHead: array[0..1] of Ansichar = ('B', 'M');
    PicPakFileName: AnsiString = 'Pic.dat';
    TilePakFileName: AnsiString = 'Tile.dat';
  end;

  PpicFile = ^TpicFile;

  PBitmap = ^TBitmap;

  PPByte = ^PByte;

  TIMPImageFile = class
    Destructor Destroy;
  private
    FIMPImage: TIMPImage;
    function GetImageCount(): Integer;
    function GetInterval(): Integer;
    procedure SetInterval(itv: Integer);
    function GetDirection(): Integer;
    procedure SetDirection(dir: Integer);
  public
    procedure Clear();
    procedure Save(fileName: String);
    function Load(fileName: string): boolean;
    procedure DrawFrame(dest: PBitmap; index, x, y: Integer);
    procedure DrawFrameWithOffset(dest: PBitmap; index, x, y: Integer);
    procedure StretchDrawFrame(dest: PBitmap; index: Integer; rect: TRect);
    function GetFrameData(index: Integer; data: PPByte; len: PInteger): Boolean;
    procedure GetFrameOffset(index: Integer; x, y: PInteger);
    property directions: Integer read GetDirection write SetDirection;
    property interval: Integer read GetInterval write SetInterval;
    property imageCount: Integer read GetImageCount;
    procedure SetFrame(index: Integer; data: PByte; len, xOffset, yOffset: Integer);
    procedure SetFrameData(index: Integer; data: PByte; len: Integer);
    procedure SetFrameOffset(index: Integer; xOffset, yOffset: Integer);
    procedure CopyFrame(dest, source: Integer);
    procedure InsertFrame(index: Integer; data: PByte; len, xOffset, yOffset: Integer);
    procedure AddFrame(data: PByte; len, xOffset, yOffset: Integer);
    procedure DeleteFrame(index: Integer);
  const
    IMGHead: AnsiString = 'IMG File Ver1.0' + #0;
  end;

  PIMPImageFile = ^TIMPImageFile;

implementation

Destructor TIMPImageFile.Destroy;
begin
  Clear();
end;

procedure TIMPImageFile.Clear();
var
  I: Integer;
begin
  for I := 0 to FIMPImage.frameCount - 1 do
  begin
    setlength(FIMPImage.frame[I].data, 0);
  end;
  FIMPImage.frameCount := 0;
  setlength(FIMPImage.frame, 0);
  FIMPImage.directions := 0;
  FIMPImage.interval := 0;
end;

procedure TIMPImageFile.Save(fileName: String);
var
  fh: integer;
  I: integer;
  offset, len: integer;
begin
  //
  DeleteFile(@fileName[1]);
  fh := FileCreate(fileName);
  CopyMemory(@FIMPImage.Headstr[0], @IMGHead[1], 16);
  FileWrite(fh, FIMPImage.Headstr[0], 16);
  FileWrite(fh, FIMPImage.frameCount, 4);
  FileWrite(fh, FIMPImage.directions, 4);
  FileWrite(fh, FIMPImage.interval, 4);
  FileWrite(fh, FIMPImage.NULL[0], 4 * 5);
  for I := 0 to FIMPImage.frameCount - 1 do
  begin
    FileWrite(fh, FIMPImage.frame[I].dataLen, 4);
    FileWrite(fh, FIMPImage.frame[I].xOffset, 4);
    FileWrite(fh, FIMPImage.frame[I].yOffset, 4);
    FileWrite(fh, FIMPImage.frame[I].Null, 4);
    FileWrite(fh, FIMPImage.frame[I].data[0], FIMPImage.frame[I].dataLen);
  end;
  FileClose(fh);

end;

function TIMPImageFile.Load(fileName: string): boolean;
var
  fh: integer;
  I: integer;
  offset, len: integer;
  find: boolean;
begin
  //
  Clear();
  try
  result := false;
  fh := fileopen(fileName, fmopenread);
  len := fileseek(fh, 0, 2);
  fileseek(fh, 0, 0);
  if len < 48 then
  begin
    fileclose(fh);
    exit;
  end;

  fileread(fh, FIMPImage.Headstr[0], 16);
  find := false;
  for I := 0 to 16 - 1 do
  begin
    if (FIMPImage.Headstr[i] <> IMGHead[i + 1]) then
    begin
      find := true;
      break;
    end;
  end;
  if find then
  begin
    fileclose(fh);
    exit;
  end;

  fileread(fh, FIMPImage.frameCount, 4);
  fileread(fh, FIMPImage.directions, 4);
  fileread(fh, FIMPImage.interval, 4);
  fileread(fh, FIMPImage.NULL[0], 4 * 5);

  if FIMPImage.frameCount > 0 then
  begin

     setlength(FIMPImage.frame, FIMPImage.frameCount);

     for I := 0 to FIMPImage.frameCount - 1 do
     begin

       fileread(fh, FIMPImage.frame[i].dataLen, 4);
       fileread(fh, FIMPImage.frame[i].xOffset, 4);
       fileread(fh, FIMPImage.frame[i].yOffset, 4);
       fileread(fh, FIMPImage.frame[i].Null, 4);
       if FIMPImage.frame[i].datalen > 0 then
       begin
         setlength(FIMPImage.frame[i].data, FIMPImage.frame[i].dataLen);
         fileread(fh, FIMPImage.frame[i].data[0], FIMPImage.frame[i].dataLen);
       end
       else
       begin
         FIMPImage.frame[i].datalen := 0;
         setlength(FIMPImage.frame[i].data, FIMPImage.frame[i].dataLen);
       end;
     end;
     result := true;
  end;
  fileclose(fh);
  exit;
  except
    result := false;
  end;
end;

procedure TIMPImageFile.DrawFrame(dest: PBitmap; index, x, y: Integer);
var
  png: TPNGImage;
  rs: TMemoryStream;
begin
  if (index < 0) or (index >= FIMPImage.frameCount) or (dest = nil) then
    exit;
  rs := TMemoryStream.Create;
  rs.SetSize(FIMPImage.frame[index].dataLen);
  rs.Position := 0;
  if FIMPImage.frame[index].dataLen > 0 then
  begin
    rs.Write(FIMPImage.frame[index].data[0], FIMPImage.frame[index].dataLen);
  end;
  rs.Position := 0;
  png := TPNGImage.Create;
  png.LoadFromStream(rs);
  dest.Canvas.Draw(x, y, png);
  rs.Free;
  png.Free;
end;

function TIMPImageFile.GetFrameData(index: Integer; data: PPByte; len: PInteger): Boolean;
begin
  Result := False;
  if (data = nil) or (len = nil) then
    Exit();
  if (index >= 0) and (index < imageCount) then
  begin
    len^ := FIMPImage.frame[index].dataLen;
    if len^ > 0 then
      data^ := @FIMPImage.frame[index].data[0]
    else
      data^ := nil;
    Result := True;
  end;
end;

procedure TIMPImageFile.GetFrameOffset(index: Integer; x, y: PInteger);
begin
  if (index >= 0) and (index < imageCount) then
  begin
    if x <> nil then
      x^ := FIMPImage.frame[index].xOffset;
    if y <> nil then
      y^ := FIMPImage.frame[index].yOffset;
  end;
end;

procedure TIMPImageFile.DrawFrameWithOffset(dest: PBitmap; index, x, y: Integer);
begin
  //
  if (index < 0) or (index >= FIMPImage.frameCount) then
    exit;
  dec(x, FIMPImage.frame[index].xOffset);
  dec(y, FIMPImage.frame[index].yOffset);
  DrawFrame(dest, index, x, y);
end;



procedure TIMPImageFile.StretchDrawFrame(dest: PBitmap; index: Integer; rect: TRect);
var
  png: TPNGImage;
  rs: TMemoryStream;
begin
  if (index < 0) or (index >= FIMPImage.frameCount) or (dest = nil) then
    exit;
  rs := TMemoryStream.Create;
  rs.SetSize(FIMPImage.frame[index].dataLen);
  rs.Position := 0;
  if FIMPImage.frame[index].dataLen > 0 then
  begin
    rs.Write(FIMPImage.frame[index].data[0], FIMPImage.frame[index].dataLen);
  end;
  rs.Position := 0;
  png := TPNGIMage.Create;
  png.LoadFromStream(rs);
  rs.Free;
  if (png.Width > rect.Right - rect.Left) or (png.Height > rect.Bottom - rect.Top) then
  begin
    if ((rect.Right - rect.Left) / png.Width < (rect.Bottom - rect.Top) / png.Height) then
    begin
      rect.Bottom := Round((rect.Right - rect.Left) / png.Width * png.Height) + rect.Top;
    end
    else
    begin
      rect.Right := Round((rect.Bottom - rect.Top) / png.Height * png.Width) + rect.Left;
    end;
    dest.Canvas.StretchDraw(rect, png);
  end
  else
  begin
    dest.Canvas.Draw(rect.Left, rect.Top, png);
  end;
  png.Free;
end;
function TIMPImageFile.GetInterval(): Integer;
begin
  Result := FIMPImage.interval;
end;

procedure TIMPImageFile.SetInterval(itv: Integer);
begin
  FIMPImage.interval := itv;
end;

function TIMPImageFile.GetDirection(): Integer;
begin
  Result := FIMPImage.directions;
end;

procedure TIMPImageFile.SetDirection(dir: Integer);
begin
  FIMPImage.directions := dir;
end;

function TIMPImageFile.GetImageCount(): Integer;
begin
  result := FIMPImage.frameCount;
end;

procedure TIMPImageFile.CopyFrame(dest, source: Integer);
begin
  if (dest = source) or (dest < 0) or (dest >= FIMPImage.frameCount) or (source < 0) or (source >= FIMPImage.frameCount) then
    exit;
  if FIMPImage.frame[source].dataLen > 0 then
    SetFrame(dest, @FIMPImage.frame[source].data[0], FIMPImage.frame[source].dataLen, FIMPImage.frame[source].xOffset, FIMPImage.frame[source].yOffset)
  else
    SetFrame(dest, nil, 0, FIMPImage.frame[source].xOffset, FIMPImage.frame[source].yOffset);
end;

procedure TIMPImageFile.SetFrame(index: Integer; data: PByte; len, xOffset, yOffset: Integer);
begin
  SetFrameData(index, data, len);
  SetFrameOffset(index, xOffset, yOffset);
end;

procedure TIMPImageFile.SetFrameData(index: Integer; data: PByte; len: Integer);
begin
   if (index < 0) or (index >= FIMPImage.frameCount) then
    exit;
  if (len < 0) then
    len := 0;
  FIMPImage.frame[index].dataLen := len;
  setlength(FIMPImage.frame[index].data, len);
  if (len > 0) then
  begin
    copymemory(@FIMPImage.frame[index].data[0], data, len);
  end;
end;

procedure TIMPImageFile.SetFrameOffset(index: Integer; xOffset, yOffset: Integer);
begin
  FIMPImage.frame[index].xOffset := xOffset;
  FIMPImage.frame[index].yOffset := yOffset;
end;

procedure TIMPImageFile.InsertFrame(index: Integer; data: PByte; len, xOffset, yOffset: Integer);
var
  I: Integer;
begin
  if index < 0 then
    index := 0;
  if index > FIMPImage.frameCount then
    index := FIMPImage.frameCount;
  inc(FIMPImage.frameCount);
  SetLength(FIMPImage.frame, FIMPImage.frameCount);
  for I := FIMPImage.frameCount - 1 downto index + 1 do
  begin
    CopyFrame(I, I - 1);
  end;
  SetFrame(index, data, len, xOffset, yOffset);
end;

procedure TIMPImageFile.AddFrame(data: PByte; len, xOffset, yOffset: Integer);
begin
  InsertFrame(FIMPImage.frameCount, data, len, xOffset, yOffset);
end;

procedure TIMPImageFile.DeleteFrame(index: Integer);
var
  I: Integer;
begin
  if (index < 0) or (index >= FIMPImage.frameCount) then
    exit;
  for I := index to FIMPImage.frameCount - 2 do
  begin
    CopyFrame(I, I + 1);
  end;
  SetFrame(FIMPImage.frameCount - 1, nil, 0, 0, 0);
  dec(FIMPImage.frameCount);
  SetLength(FIMPImage.frame, FIMPImage.frameCount);
end;

Constructor TpicFile.Create;
begin
  //
  inherited;
  Filenum := 0;
  setlength(MPCFile, 0);
  Pak := TPakFile.create;
end;

destructor TpicFile.Destroy;
begin
  Release;
  Pak.Destroy;
  inherited;
end;

procedure TpicFile.Clear;
var
  I, I2: integer;
begin

  if Filenum <= 0 then
    exit;
  for I := 0 to Filenum - 1 do
  begin
    if MPCFile[I].Pictype = PIC then
    begin
      if MPCFile[I].MPCFileHead.PicCount > 0 then
        for I2 := 0 to MPCFile[I].MPCFileHead.PicCount - 1 do
        begin
          try
            if MPCFile[I].MPCpic[I2].PIC.IMGType <> IMG_NONE then
            begin
              case MPCFile[I].MPCpic[I2].PIC.IMGType of
                IMG_JPG:
                  begin
                    PJPEGImage(MPCFile[I].MPCpic[I2].Pic.Pic).Destroy;
                  end;
                IMG_PNG:
                  begin
                    PPNGImage(MPCFile[I].MPCpic[I2].Pic.Pic).Destroy;
                  end;
                IMG_BMP:
                  begin
                    PBITMAP(MPCFile[I].MPCpic[I2].Pic.Pic).Destroy;
                  end;
              end;
              MPCFile[I].MPCpic[I2].PIC.Pic := nil;
              MPCFile[I].MPCpic[I2].PIC.IMGType := IMG_NONE;
            end;
          except

          end;
        end;
    end;
  end;
  Filenum := 0;
  setlength(MPCFile, 0);

end;

procedure TpicFile.Release;
begin
  Clear;
  Pak.Release;
end;

function TpicFile.LoadIMPImage(IMPImage: PIMPImage; fileName: string): boolean;
var
  fh: integer;
  I: integer;
  offset, len: integer;
  find: boolean;
begin
  //
  try
  result := false;
  if IMPImage = NIL then
  begin
    exit;
  end;
  fh := fileopen(fileName, fmopenread);
  len := fileseek(fh, 0, 2);
  fileseek(fh, 0, 0);
  if len < 48 then
  begin
    fileclose(fh);
    exit;
  end;

  fileread(fh, IMPImage.Headstr[0], 16);
  find := false;
  for I := 0 to 16 - 1 do
  begin
    if (IMPImage.Headstr[i] <> self.IMGHead[i + 1]) then
    begin
      find := true;
      break;
    end;
  end;
  if find then
  begin
    fileclose(fh);
    exit;
  end;

  fileread(fh, IMPImage.frameCount, 4);
  fileread(fh, IMPImage.directions, 4);
  fileread(fh, IMPImage.interval, 4);
  fileread(fh, IMPImage.NULL[0], 4 * 5);

  if IMPImage.frameCount > 0 then
  begin

     setlength(IMPImage.frame, IMPImage.frameCount);

     for I := 0 to IMPImage.frameCount - 1 do
     begin

       fileread(fh, IMPImage.frame[i].dataLen, 4);
       fileread(fh, IMPImage.frame[i].xOffset, 4);
       fileread(fh, IMPImage.frame[i].yOffset, 4);
       fileread(fh, IMPImage.frame[i].Null, 4);

       if IMPImage.frame[i].datalen > 0 then
       begin
         setlength(IMPImage.frame[i].data, IMPImage.frame[i].dataLen);
         fileread(fh, IMPImage.frame[i].data[0], IMPImage.frame[i].dataLen);
       end
       else
       begin
         IMPImage.frame[i].datalen := 0;
         setlength(IMPImage.frame[i].data, IMPImage.frame[i].dataLen);
       end;
     end;
     result := true;
  end;
  fileclose(fh);
  exit;
  except
    result := false;
  end;
end;

procedure TpicFile.SaveIMPImage(IMPImage: PIMPImage; fileName: string);
var
  fh: integer;
  I: integer;
  offset, len: integer;
begin
  //
  if IMPImage = NIL then
  begin
    exit;
  end;
  deleteFile(@fileName[1]);
  fh := filecreate(fileName);
  copymemory(@IMPImage.Headstr[0], @self.IMGHead[1], 16);
  filewrite(fh, IMPImage.Headstr[0], 16);
  filewrite(fh, IMPImage.frameCount, 4);
  filewrite(fh, IMPImage.directions, 4);
  filewrite(fh, IMPImage.interval, 4);
  filewrite(fh, IMPImage.NULL[0], 4 * 5);
  for I := 0 to IMPImage.frameCount - 1 do
  begin
    filewrite(fh, IMPImage.frame[I].dataLen, 4);
    filewrite(fh, IMPImage.frame[I].xOffset, 4);
    filewrite(fh, IMPImage.frame[I].yOffset, 4);
    filewrite(fh, IMPImage.frame[I].Null, 4);
    filewrite(fh, IMPImage.frame[I].data[0], IMPImage.frame[I].dataLen);
  end;
  fileclose(fh);
end;

//判断图片文件类型（重载）
function TpicFile.CalPicTYPE(Pdata: Pbyte): TPICTYPE;   //参数为数据第一字节指针
begin
  result := NONE;
  if CampareFileHead(Pdata, MPChead) then
    result := MPC
  else if CampareFileHead(Pdata, PIChead) then
    result := PIC
  else if CampareFileHead(Pdata, SHDhead) then
    result := SHD
  else if CampareFileHead(Pdata,ASF100head) then
    result := ASF100
  else if CampareFileHead(Pdata, ASF101head) then
    result := ASF101
  else if CampareFileHead(Pdata, PNGhead) then
    result := IMG
  else if CampareFileHead(Pdata, JPGhead) then
    result := IMG
  else if CampareFileHead(Pdata, BMPhead) then
    result := IMG
  else
    result := NONE;
end;

function TpicFile.CalPicTYPE(filename: String): TPICTYPE;    //参数为文件名
var
  picfile, len: integer;
  pichead: array[0..15] of Ansichar;
begin
  result := NONE;
  if not fileexists(filename) then
    exit;
  picfile := fileopen(filename, fmopenread);
  len := fileseek(picfile, 0, 2);
  fileseek(picfile, 0, 0);
  if len >= 16 then
    fileread(picfile, pichead[0], 16)
  else
  begin
    result := NONE;
    fileclose(picfile);
    exit;
  end;

  fileclose(picfile);
  result := CalPicTYPE(@PicHead[0]);
end;

procedure TPicFIle.ClearMPCFile(picMPCFile: PMPCFile);
var
  I: integer;
begin
  if picMPCFile.Pictype = PIC then
  begin
    if picMPCFile.MPCFileHead.PicCount > 0 then
      for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
      begin
        try
          if picMPCFile.MPCpic[I].PIC.IMGType <> IMG_NONE then
          begin
            case picMPCFile.MPCpic[I].PIC.IMGType of
              IMG_JPG:
                begin
                  PJPEGImage(picMPCFile.MPCpic[I].Pic.Pic).Destroy;
                end;
              IMG_PNG:
                begin
                  PPNGImage(picMPCFile.MPCpic[I].Pic.Pic).Destroy;
                end;
              IMG_BMP:
                begin
                  PBITMAP(picMPCFile.MPCpic[I].Pic.Pic).Destroy;
                end;
            end;
            picMPCFile.MPCpic[I].PIC.Pic := nil;
            picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
          end;
        except

        end;
      end;
  end;
  picMPCFile.Pictype := NONE;
  picMPCFile.MPCFileHead.PicCount := 0;
  picMPCFile.ASFFileHead.PicCount := 0;
  setlength(picMPCFile.MPCpic, 0);
end;

function TpicFile.LoadPicFromFile(picMPCFile: PMPCFile; filename: String): boolean;
var
  FH, I: THandle;
  rs: TmemoryStream;

begin
  result := false;
  if not Fileexists(Filename) then
  begin
    exit;
  end;
  ClearMPCFile(picMPCFile);
  picMPCFile.Pictype := calPicType(Filename);
  if picMPCFile.Pictype = NONE then
  begin
    exit;
  end;
  try
    case picMPCFile.Pictype of
      MPC:
        begin
          try

            FH := Fileopen(Filename, fmopenread);
            fileread(FH, picMPCFile.MPCFileHead, sizeof(TMPCFileHead){112 + 16});
            //读取MPC调色板
            picMPCFile.MPCpalette.len := picMPCFile.MPCFileHead.PaletteLen;
            setlength(picMPCFile.MPCpalette.col, picMPCFile.MPCFileHead.PaletteLen);
            fileread(FH, picMPCFile.MPCpalette.col[0], picMPCFile.MPCpalette.len shl 2);
            fileseek(FH, picMPCFile.MPCFileHead.PicCount shl 2, 1);

            //读取图片信息
            setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);

            for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
            begin
              fileread(FH, picMPCFile.MPCpic[I].MPCpichead, sizeof(TMPCpichead));
              setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
              fileread(FH ,picMPCFile.MPCpic[I].data[0], picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
              picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
              picMPCFile.MPCpic[I].PIC.Pic := nil;
            end;

            result := true;
          finally
            fileclose(FH);
          end;
        end;
      SHD:
        begin
          try
            FH := Fileopen(Filename, fmopenread);
            fileread(FH, picMPCFile.MPCFileHead, sizeof(TMPCFileHead){112 + 16});
            fileseek(FH, picMPCFile.MPCFileHead.PicCount shl 2, 1);

            //读取图片信息
            setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
            for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
            begin
              fileread(FH, picMPCFile.MPCpic[I].MPCpichead, sizeof(TMPCpichead));
              setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
              fileread(FH ,picMPCFile.MPCpic[I].data[0], picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
              picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
              picMPCFile.MPCpic[I].PIC.Pic := nil;
            end;

            result := true;
          finally
            fileclose(FH);
          end;
        end;
      ASF100, ASF101:
        begin
          try
            FH := Fileopen(Filename, fmopenread);
            fileread(FH, picMPCFile.ASFFileHead, sizeof(TASFFileHead));
            picMPCFile.MPCpalette.len := picMPCFile.ASFFileHead.PaletteLen;
            setlength(picMPCFile.MPCpalette.col, picMPCFile.MPCpalette.len);
            fileread(FH, picMPCFile.MPCpalette.col[0], picMPCFile.MPCpalette.len shl 2);
            picMPCFile.MPCFileHead.PicCount := picMPCFile.ASFFileHead.PicCount;
            setlength(picMPCFile.MPCpic, picMPCFile.ASFFileHead.PicCount);

            for I := 0 to picMPCFile.ASFFileHead.PicCount - 1 do
            begin
              fileseek(FH, 4, 1);
              fileread(FH, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, 4);
              picMPCFile.MPCpic[I].MPCpichead.Width := picMPCFile.ASFFileHead.Width;
              picMPCFile.MPCpic[I].MPCpichead.Height := picMPCFile.ASFFileHead.Height;
              picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
              picMPCFile.MPCpic[I].PIC.Pic := nil;
            end;
            for I := 0 to picMPCFile.ASFFileHead.PicCount - 1 do
            begin
              setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
              fileread(FH, picMPCFile.MPCpic[I].data[0], picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
            end;

            result := true;
          finally
            fileclose(FH);
          end;
        end;
      PIC:
        begin
          FH := Fileopen(Filename, fmopenread);
          fileseek(FH, sizeof(PICHEAD), 1);
          fileread(FH, picMPCFile.MPCFileHead.PicCount, 4);
          if picMPCFile.MPCFileHead.PicCount > 0 then
          begin
            setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
            for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
            begin
              fileread(FH, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, 4);
            end;
            for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
            begin
              try
                rs := TMemoryStream.Create;
                try

                  rs.SetSize(picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                  rs.Position := 0;
                  //试用数据流的memory指针，不知是否有问题
                  fileread(FH, rs.Memory^, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                  rs.Position := 0;
                  picMPCFile.MPCpic[I].PIC.IMGType := CalIMGType(rs.Memory);
                  case picMPCFile.MPCpic[I].PIC.IMGType of
                    IMG_PNG:
                      begin
                        PICOBJ := TPNGImage.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PPNGImage(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                    IMG_JPG:
                      begin
                        PICOBJ := TJPEGImage.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PJPEGImage(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                    IMG_BMP:
                      begin
                        PICOBJ := TBITMAP.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PBITMAP(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                  end;
                  //picMPCFile.MPCpic[I].PIC := CreatePicFromData(rs.Memory, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                  //picMPCFile.MPCpic[I].PIC.LoadFromStream(rs);
                except

                  picMPCFile.MPCpic[I].PIC.Pic := nil;
                  picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
                end;
              finally
                rs.Free;
              end;
            end;
          end;
          fileclose(FH);
          result := true;
        end;
      IMG:
        begin
          picMPCFile.MPCFileHead.PicCount := 1;
          setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
          try
            picMPCFile.MPCpic[0].PIC.IMGType := CalIMGType(Filename);
            case picMPCFile.MPCpic[0].PIC.IMGType of
              IMG_PNG:
                begin
                  PICOBJ := TPNGImage.Create;
                  picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                  PPNGImage(picMPCFile.MPCpic[0].PIC.Pic).LoadFromFile(Filename);
                end;
              IMG_JPG:
                begin
                  PICOBJ := TJPEGImage.Create;
                  picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                  PJPEGImage(picMPCFile.MPCpic[0].PIC.Pic).LoadFromFile(Filename);
                end;
              IMG_BMP:
                begin
                  PICOBJ := TBITMAP.Create;
                  picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                  PBITMAP(picMPCFile.MPCpic[0].PIC.Pic).LoadFromFile(Filename);
                end;
            end;
            result := true;
            //picMPCFile.MPCpic[I].PIC := CreatePicFromData(rs.Memory, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
            //picMPCFile.MPCpic[I].PIC.LoadFromStream(rs);
          except
            picMPCFile.MPCpic[0].PIC.Pic := nil;
            picMPCFile.MPCpic[0].PIC.IMGType := IMG_NONE;
          end;
        end;
    end;

  except

    fileclose(FH);

    picMPCfile.MPCFileHead.PicCount := 0;
    picMPCfile.MPCFileHead.PaletteLen := 0;

    picMPCfile.ASFFileHead.PicCount := 0;
    picMPCfile.ASFFileHead.PaletteLen := 0;

    setlength(picMPCfile.MPCpalette.col, 0);
    setlength(picMPCfile.MPCpic, 0);

  end;

end;

function TpicFile.LoadPicFromBuffer(picMPCFile: PMPCFile; Fdata: Pbyte; Bufferlen: integer): boolean;
var
  I: THandle;
  rs: TmemoryStream;
  temptype: TPictype;

begin
  result := false;
  picMPCFile.Pictype := calPicType(Fdata);
  if picMPCFile.Pictype = NONE then
  begin
    exit;
  end;
  temptype := picMPCFile.Pictype;
  ClearMPCFile(picMPCFile);
  picMPCFile.Pictype := temptype;
  try
    case picMPCFile.Pictype of
      MPC:
        begin
          copymemory(@picMPCFile.MPCFileHead, Fdata, sizeof(TMPCFileHead){112 + 16});
          Fdata := Fdata + sizeof(TMPCFileHead);
          //读取MPC调色板
          picMPCFile.MPCpalette.len := picMPCFile.MPCFileHead.PaletteLen;
          setlength(picMPCFile.MPCpalette.col, picMPCFile.MPCFileHead.PaletteLen);
          copymemory(@picMPCFile.MPCpalette.col[0], Fdata, picMPCFile.MPCpalette.len shl 2);
          Fdata := Fdata + picMPCFile.MPCpalette.len shl 2;
          Fdata := Fdata + picMPCFile.MPCFileHead.PicCount shl 2;

          //读取图片信息
          setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);

          for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
          begin
            copymemory(@picMPCFile.MPCpic[I].MPCpichead, Fdata, sizeof(TMPCpichead));
            Fdata := Fdata + sizeof(TMPCpichead);
            setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
            copymemory(@picMPCFile.MPCpic[I].data[0], Fdata, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
            Fdata := Fdata + picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead);
            picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
            picMPCFile.MPCpic[I].PIC.Pic := nil;
          end;

          result := true;
        end;
      SHD:
        begin
          copymemory(@picMPCFile.MPCFileHead, Fdata, sizeof(TMPCFileHead){112 + 16});
          Fdata := Fdata + sizeof(TMPCFileHead);
          Fdata := Fdata + picMPCFile.MPCFileHead.PicCount shl 2;

          //读取图片信息
          setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
          for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
          begin
            copymemory(@picMPCFile.MPCpic[I].MPCpichead, Fdata, sizeof(TMPCpichead));
            Fdata := Fdata + sizeof(TMPCpichead);
            setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
            copymemory(@picMPCFile.MPCpic[I].data[0], Fdata, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead));
            Fdata := Fdata + picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen - sizeof(TMPCpichead);
            picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
            picMPCFile.MPCpic[I].PIC.Pic := nil;
          end;

          result := true;
        end;
      ASF100, ASF101:
        begin
          copymemory(@picMPCFile.ASFFileHead, Fdata, sizeof(TASFFileHead));
          Fdata := Fdata + Sizeof(TASFFileHead);
          picMPCFile.MPCpalette.len := picMPCFile.ASFFileHead.PaletteLen;
          setlength(picMPCFile.MPCpalette.col, picMPCFile.MPCpalette.len);
          copymemory(@picMPCFile.MPCpalette.col[0], Fdata, picMPCFile.MPCpalette.len shl 2);
          Fdata := Fdata + picMPCFile.MPCpalette.len shl 2;
          picMPCFile.MPCFileHead.PicCount := picMPCFile.ASFFileHead.PicCount;
          setlength(picMPCFile.MPCpic, picMPCFile.ASFFileHead.PicCount);
          for I := 0 to picMPCFile.ASFFileHead.PicCount - 1 do
          begin
            Fdata := Fdata + 4; //offset
            copymemory(@picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, Fdata, 4);
            Fdata := Fdata + 4;
            picMPCFile.MPCpic[I].MPCpichead.Width := picMPCFile.ASFFileHead.Width;
            picMPCFile.MPCpic[I].MPCpichead.Height := picMPCFile.ASFFileHead.Height;
            picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
            picMPCFile.MPCpic[I].PIC.Pic := nil;
          end;
          for I := 0 to picMPCFile.ASFFileHead.PicCount - 1 do
          begin
            setlength(picMPCFile.MPCpic[I].data, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
            copymemory(@picMPCFile.MPCpic[I].data[0], Fdata, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
            Fdata := Fdata + picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen;
          end;

          result := true;
        end;
      PIC:
        begin
          Fdata := Fdata + Sizeof(PICHEAD);
          Move(Fdata^, picMPCFile.MPCFileHead.PicCount, 4);
          Fdata := Fdata + 4;
          setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
          //所有图片的长度
          for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
          begin
            Move(Fdata^, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen, 4);
            Fdata := Fdata + 4;
          end;

          for I := 0 to picMPCFile.MPCFileHead.PicCount - 1 do
          begin
            try
              try
                //picMPCFile.MPCpic[I].PIC := CreatePicFromData(Fdata, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                rs := TMemoryStream.Create;

                if picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen > 16 then
                begin
                  rs.Position := 0;
                  rs.SetSize(picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                  rs.Position := 0;
                  rs.Write(FData^, picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen);
                  rs.Position := 0;
                  picMPCFile.MPCpic[I].PIC.IMGType := CalIMGType(Fdata);
                  case picMPCFile.MPCpic[I].PIC.IMGType of
                    IMG_PNG:
                      begin
                        PICOBJ := TPNGImage.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PPNGImage(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                    IMG_JPG:
                      begin
                        PICOBJ := TJPEGImage.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PJPEGImage(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                    IMG_BMP:
                      begin
                        PICOBJ := TBITMAP.Create;
                        picMPCFile.MPCpic[I].PIC.Pic := @PICOBJ;
                        PBITMAP(picMPCFile.MPCpic[I].PIC.Pic).LoadFromStream(rs);
                      end;
                    else
                      begin
                        picMPCFile.MPCpic[I].PIC.Pic := nil;
                      end;
                  end;
                end
                else
                begin
                  picMPCFile.MPCpic[I].PIC.Pic := nil;
                  picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
                end;
                //picMPCFile.MPCpic[I].PIC.LoadFromStream(rs);
              except
                picMPCFile.MPCpic[I].PIC.Pic := nil;
                picMPCFile.MPCpic[I].PIC.IMGType := IMG_NONE;
              end;
            finally
              rs.Free;
              Fdata := Fdata + picMPCFile.MPCpic[I].MPCpichead.MPCpicDataLen;
            end;
          end;
          result := true;
        end;
      IMG:
        begin
          picMPCFile.MPCFileHead.PicCount := 1;
          setlength(picMPCFile.MPCpic, picMPCFile.MPCFileHead.PicCount);
          try
            try
              rs := TMemoryStream.Create;

              if Bufferlen > 16 then
              begin
                rs.Position := 0;
                rs.SetSize(Bufferlen);
                rs.Position := 0;
                rs.Write(FData^, Bufferlen);
                rs.Position := 0;
                picMPCFile.MPCpic[0].PIC.IMGType := CALIMGType(Fdata);
                case picMPCFile.MPCpic[0].PIC.IMGType of
                  IMG_PNG:
                    begin
                      PICOBJ := TPNGImage.Create;
                      picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                      PPNGImage(picMPCFile.MPCpic[0].PIC.Pic).LoadFromStream(rs);
                    end;
                  IMG_JPG:
                    begin
                      PICOBJ := TJPEGImage.Create;
                      picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                      PJPEGImage(picMPCFile.MPCpic[0].PIC.Pic).LoadFromStream(rs);
                    end;
                  IMG_BMP:
                    begin
                      PICOBJ := TBITMAP.Create;
                      picMPCFile.MPCpic[0].PIC.Pic := @PICOBJ;
                      PBITMAP(picMPCFile.MPCpic[0].PIC.Pic).LoadFromStream(rs);
                    end;
                  else
                    begin
                      picMPCFile.MPCpic[0].PIC.Pic := nil;
                    end;
                end;
                //picMPCFile.MPCpic[0].PIC. := CreatePicFromData(Fdata, Bufferlen);
              end
              else
              begin
                picMPCFile.MPCpic[0].PIC.Pic := nil;
                picMPCFile.MPCpic[0].PIC.IMGType := IMG_NONE;
              end;
              //picMPCFile.MPCpic[0].PIC.LoadFromStream(rs);
              result := true;
            except
              picMPCFile.MPCpic[0].PIC.Pic := nil;
              picMPCFile.MPCpic[0].PIC.IMGType := IMG_NONE;
            end;
          finally
            
          end;
        end;
    end;

  except

    picMPCfile.MPCFileHead.PicCount := 0;
    picMPCfile.MPCFileHead.PaletteLen := 0;

    picMPCfile.ASFFileHead.PicCount := 0;
    picMPCfile.ASFFileHead.PaletteLen := 0;

    setlength(picMPCfile.MPCpalette.col, 0);
    setlength(picMPCfile.MPCpic, 0);

  end;

end;
 {
function TpicFile.LoadPicFromPak(picMPCFile: PMPCFile; filename, PakFile: String): boolean;
begin
  result := false;
  if Pak.ReadFromFileToBuffer(PakFile, Filename) <= 0 then
  begin
    exit;
  end;
  result := loadPicFromBuffer(picMPCFile, @Pak.databuffer[0], Pak.bufferlen);
  Pak.ClearBuffer;
end;
}
function TpicFile.ARGB32(A, R, G, B: byte): Cardinal;
begin
  result := (A shl 24) or (R shl 16) or (G shl 8) or B;
end;

function TpicFile.RGB32(R, G, B: byte): Cardinal;
begin
  result := (R shl 16) or (G shl 8) or B;
end;
{
function TpicFile.AddNewFile(filename: Ansistring): boolean;
begin
  result := AddNewFile(filename, '');
end;

function TpicFile.AddNewFile(filename, PakFile: Ansistring): boolean;
begin
  //
  if Filenum < 0 then
    Filenum := 0;
  inc(Filenum);
  setlength(MPCFile, Filenum);
  if PakFile <> '' then
  begin
    if (filename <> '') and LoadPicFromPak(@MPCFile[Filenum - 1], filename, PakFile) then
    begin
      result := true;
    end
    else
    begin
      //dec(Filenum);
      //setlength(MPCFile, Filenum);
      //GameDebug.AddDebugInfo('....');
      MPCFile[Filenum - 1].Pictype := NONE;
      result := false;
    end;
  end
  else
  begin
    if (filename <> '') and LoadPicFromFile(@MPCFile[Filenum - 1], filename) then
    begin
      result := true;
    end
    else
    begin
      //dec(Filenum);
      //setlength(MPCFile, Filenum);
      MPCFile[Filenum - 1].Pictype := NONE;
      result := false;
    end;
  end;
end;
}
function TpicFile.RemoveFile(filename: AnsiString): boolean;
begin
  result := RemoveFile(Pak.HashFileName(PAnsichar(filename)));
end;

function TpicFile.RemoveFile(fileID: Cardinal): boolean;
var
  I, num: integer;
begin
  if self.filenum <= 0 then
  begin
    result := false;
    exit;
  end;
  num := -1;
  for I := 0 to self.Filenum do
  begin
    if self.MPCFile[I].FileID = FileID then
    begin
      num := I;
      break;
    end;
  end;
  if num >= 0 then
  begin
    result := RemoveFile2(num);
  end
  else
    result := false;
end;

function TpicFile.RemoveFile2(Num: integer): boolean;
var
  I: integer;
begin
  if (num < 0) or (num >= self.Filenum) then
  begin
    result := false;
    exit;
  end;
  for I := num to self.Filenum - 2 do
  begin
    CopyMPCFile(@self.MPCFile[I], @self.MPCFile[I + 1]);
  end;
  ClearMPCFile(@self.MPCFile[self.Filenum - 1]);
  dec(self.Filenum);
  setlength(self.MPCFile, Self.Filenum);
end;

function TpicFile.CopyMPCFile(PSource, PDest: PMPCFile): boolean;
var
  I, len: integer;
begin
  ClearMPCFile(Pdest);
  Pdest.Pictype := PSource.Pictype;
  Pdest.FileID := PSource.FileID;
  Pdest.Filename := PSource.Filename;
  Pdest.ASFFileHead := PSource.ASFFileHead;
  Pdest.MPCFileHead := PSource.MPCFileHead;
  case Pdest.pictype of
    MPC:
      begin
        COPYPalette(@PSource.MPCpalette, @PDest.MPCpalette);
        len := PSource.MPCFileHead.PicCount;
      end;
    SHD:
      begin
        len := PSource.MPCFileHead.PicCount;
      end;
    ASF100, ASF101:
      begin
        COPYPalette(@PSource.MPCpalette, @PDest.MPCpalette);
        len := PSource.ASFFileHead.PicCount;
      end;
    PIC:
      begin
        len := PSource.MPCFileHead.PicCount;
      end;
    IMG:
      begin
        len := PSource.MPCFileHead.PicCount;
      end;
    RLE8:
      begin
        COPYPalette(@PSource.MPCpalette, @PDest.MPCpalette);
        len := PSource.MPCFileHead.PicCount;
      end;
    else
      begin
        len := 0;
      end;
  end;
  setlength(PDest.MPCpic, len);
  if len > 0 then
  begin
    for I := 0 to len - 1 do
    begin
      copyMPCPic(@PSource.MPCpic[I], @PDest.MPCpic[I]);
    end;
  end;
  result := true;
end;

function TpicFile.CopyMPCPic(PSource, PDest: PMPCPic): boolean;
begin
  PDest.MPCpichead := PSource.MPCpichead;
  if length(PSource.data) > 0 then
  begin
    Setlength(PDest.data, length(PSource.data));
    MOVE(PSource.data[0], PDest.data[0], length(PSource.data));
  end;

  PDest.PIC := PSource.pic;

  result := true;
end;

function TpicFile.CopyPalette(PSource, PDest: PMPCPalette): boolean;
begin
  Pdest.len := PSource.len;
  if Pdest.Len > 0 then
  begin
    setlength(PDest.col, Pdest.len);
    MOVE(PSource.col[0], PDest.col[0], Pdest.len * Sizeof(TCOL));
  end;
end;

end.
