unit PakFile;

interface

uses
  //Dlzo,
  //lazlzo,
  SysUtils,
  dialogs,
  Classes,
  lzoobj,
  Windows,
  Head,
  messages;

Type

  PStringList = ^TStringList;

  TPakType = (ptNONE, ptPAK, ptPACKAGE, ptSPAK);

  TPakdataHead = record
    FileID: Cardinal;
    Offset: integer;
    filelen: integer;  //原始大小
  end;

  TFileList = record
    Filenum: integer;
    Filename: array of Ansistring;
  end;

  TPakstruct = record
    Headstr: array[0..7] of byte;
    Filenum: integer;
    CompressType: integer;
    PakDataHead: array of Tpakdatahead;
  end;

  TPakFile = class
  var
    bufferlen : integer;
    databuffer: array of byte;
  const
    SPAK_HEAD: Ansistring = 'SPAK';
    PAKFILE_HEAD: AnsiString = 'PACK';
    PACKAGEFILE_HEAD: AnsiString = 'PACKAGE' + #0;
    BLOCK_SIZE: integer = $10000;
  public
    constructor Create;
    Destructor Destroy; override;
    procedure Release;
    procedure ClearBuffer;
    function CalPakType(FileName: String): TPakType; overload;
    function CalPakType(Pdata: Pbyte): TPakType; overload;
    function HashFileName(fn: PAnsichar): Cardinal;
    function HashUnicodeFileName(str: Widestring): Cardinal;
    function unpak(Psource: Pbyte; sourcelen: integer; Pdata: Pbyte; len, BlockCount: integer): integer;
    function pak(Pin_data: Pbyte; sourcelen: integer; Pout_data: Pbyte): integer;
    function pak_Package(Pin_data: Pbyte; sourcelen: integer; Pout_data: Pbyte; CompressType: integer): integer;
    function unpak_Package(Psource: Pbyte; sourcelen: integer; Pdata: Pbyte; len, BlockCount, CompressType: integer): integer;
    function ReadFromFile(PakFileName: String; FileName: AnsiString; Pout: Pbyte; buflen: integer): integer;
    function ReadFromFileToBuffer(PakFileName: String; FileName: Ansistring): integer; overload;
    function ReadFromFileToBuffer(PakFileName: String; FileID: Cardinal): integer; overload;
    function GetFilelen(PakFileName: string; FileName: Ansistring): integer; overload;
    function GetFilelen(PakFileName: string; FileID: cardinal): integer; overload;
    function LoadSPakToBuffer(FileName: String): integer;
    procedure SaveSPak(Pdata: Pbyte; len: integer; FileName: AnsiString); overload;
    procedure SaveSPak(PSList: PStringList; FileName: AnsiString); overload;
  end;

  PPakFile = ^TPakFile;

implementation

constructor TPakFile.Create;
begin
  inherited;
  bufferlen := 0;
  setlength(databuffer, 0);
end;

Destructor TPakFile.Destroy;
begin
  Release;
  inherited Destroy;
end;

procedure TPakFile.Release;
begin
  ClearBuffer;
end;

procedure TPakFile.ClearBuffer;
begin
  bufferlen := 0;
  setlength(databuffer, 0);
end;

procedure TPakFile.SaveSPak(PSList: PStringList; FileName: AnsiString);
var
  rs: TMemoryStream;
begin
  rs := TMemoryStream.Create;
  PSList.SaveToStream(rs);
  rs.Position := 0;
  SaveSPak(rs.Memory, rs.Size, FileName);
  rs.Free;
end;

procedure TPakFile.SaveSPak(Pdata: Pbyte; len: integer; FileName: AnsiString);
var
  data: array of byte;
  dlen, FH: integer;
begin
  if fileexists(AnsiToWideStr(FileName)) then
    DeleteFile(PWideChar(AnsiToWideStr(FileName)));
  if len <= 0 then
    exit;
  dlen := len + len shr 3 + 1024;
  setlength(data, dlen);
  dlen := self.pak(Pdata, len, @data[0]);
  if dlen <= 0 then
  begin
    setlength(data, 0);
    exit;
  end;
  FH := Filecreate(AnsiToWideStr(FileName));
  Filewrite(FH, SPAK_HEAD, length(SPAK_HEAD));
  fileWrite(FH, len, 4);
  fileWrite(FH, data[0], dlen);
  fileclose(FH);
  setlength(data, 0);
end;

function TPakFile.LoadSPakToBuffer(FileName: String): integer;
var
  FH, len, filelen, Block_num: integer;
  data: array of byte;
begin
  if not Fileexists(FileName) then
  begin
    result := -1;
    exit;
  end;
  FH := FileOpen(FileName, fmopenread);
  Filelen := Fileseek(FH, 0, 2);
  Fileseek(Fh, 0, 0);
  if Filelen <= 8 then
  begin
    fileclose(FH);
    result := -2;
    exit;
  end;
  fileread(FH, len, 4);
  if not CampareFileHead(@len, SPAK_HEAD) then
  begin
    fileclose(FH);
    result := -3;
    exit;
  end;
  fileread(FH, len, 4);
  if len <= 0 then
  begin
    fileclose(FH);
    result := -4;
    exit;
  end;

  setlength(data, filelen - 8);
  fileread(FH, data[0], filelen - 8);
  fileclose(FH);

  bufferlen := len;
  setlength(databuffer, len);
  Block_num := len div BLOCK_SIZE;
  if len mod BLOCK_SIZE <> 0 then
    inc(Block_num);

  Filelen := unpak(@data[0], Filelen - 8, @databuffer[0], len, Block_num);
  setlength(data, 0);
  if Filelen <> len then
  begin
    ClearBuffer;
    result := -5;
  end
  else
    result := Filelen;

end;

function TPakFile.HashFileName(fn: PAnsichar): Cardinal;
var
  cnt,data,remainder,total, u: cardinal;
  ch: Ansichar;//Shortint;
begin
  {cnt := 0;
  total := 0;
  ch := fn^;
  while (shortint(ch) <> 0) do
  begin
    if(ch=Ansichar('/')) then
      ch:=Ansichar('\');
    if((ch>='A') and (ch<='Z')) then
      shortint(ch):=$20 + shortint(ch);
    inc(cnt);
    data:=shortint(ch)*cnt+total ;
    remainder:= not(data mod $8000000B) +1;
    total:=((remainder) shl 4)+remainder;
    inc(fn);
    ch:=fn^;
  end;
  result := total xor $12345678;}
  {total := 0;
  ch := fn^;
  cnt := 0;
  while (shortint(ch) <> 0) do
  begin
    if(ch=Ansichar('/')) then
      ch:=Ansichar('\');
    total := (total + (cnt + 1) * shortint(ch)) mod $8000000B * $FFFFFFEF;
    cnt := cnt + 1;
    inc(fn);
    ch:=fn^;
  end;
  result := total xor $12345678;  }
  ch := fn^;
  cnt := 0;
  total := 0;
  while (shortint(ch) <> 0) do
  begin
    if(ch=Ansichar('/')) then
      ch:=Ansichar('\');
    if((ch>='A') and (ch<='Z')) then
      shortint(ch):= $20 + shortint(ch);
    u := shortint(ch);
    total := (u * (cnt + 1) + total) mod $8000000B;
    total := (total xor $FFFFFFFF + 1) shl 4 - total;
    cnt := cnt + 1;
    inc(fn);
    ch:=fn^;
  end;
  result := total xor $12345678;
end;

function TPakFile.HashUnicodeFileName(str: Widestring): Cardinal;
begin
  result := HashFileName(PAnsiChar(WideSTRtoAnsi(str)));
end;

function TPakFile.pak_Package(Pin_data: Pbyte; sourcelen: integer; Pout_data: Pbyte; CompressType: integer): integer;
var
  tempbuffer: array[0 .. (65536 + 65536 div 10 + 1024)] of byte;
  databuffer: array[0..65535] of byte;
  blockCount: integer;
  blocklen: array of word;
  I, I2, pos: integer;
  datalen, templen, totallen: integer;
  slen: integer;
  fileend: word;
begin
  if sourcelen <= 0 then
  begin
    result := -1;
    exit;
  end;
  I := 0;
  pos := 0;
  totallen := 0;

  case CompressType of
    0:
      begin
        fileend := 0;
        blockCount := sourcelen + 2 div BLOCK_SIZE;
        if (sourcelen + 2) mod BLOCK_SIZE <> 0 then
          inc(blockCount);
        setlength(Blocklen, BlockCount);
        slen := sourcelen;
        inc(pos, BlockCount * 2);
        inc(totallen, BlockCount * 2);
        for I2 := 0 to BlockCount - 1 do
        begin
          if I2 = BlockCount - 1 then
          begin
            Blocklen[I2] := slen + 2;
            Copymemory((Pout_data + pos), (Pin_data + I), slen);
            inc(pos, slen);
            Copymemory((Pout_data + pos), @fileend, 2);
            inc(pos, 2);
            inc(totallen, slen + 2);
            inc(I, slen);
            slen := 0;
          end
          else
          begin
            Blocklen[I2] := 0;
            Copymemory((Pout_data + pos), (Pin_data + I), BLOCK_SIZE);
            inc(I, BLOCK_SIZE);
            inc(POS, BLOCK_SIZE);
            inc(totallen, BLOCK_SIZE);
            dec(slen, BLOCK_SIZE);
          end;
        end;
        Copymemory(Pout_data, @Blocklen[0], blockcount * 2);
      end;
    2:
      begin
        blockCount := sourcelen div BLOCK_SIZE;
        if sourcelen mod BLOCK_SIZE <> 0 then
          inc(blockCount);
        setlength(Blocklen, BlockCount);
        slen := sourcelen;
        inc(pos, BlockCount * 2);
        inc(totallen, BlockCount * 2);
        for I2 := 0 to BlockCount - 1 do
        begin
          if I2 = BlockCount - 1 then
          begin
            copymemory(@databuffer[0], (Pin_data + I), slen);
            lzo_compress_mem(@databuffer[0], slen, @tempbuffer[0], @datalen);
            if datalen >= slen then
            begin
              BlockLen[I2] := 0;
              datalen := slen;
              copymemory((Pout_data + pos), (Pin_data + I), datalen);
            end
            else
            begin
              BlockLen[I2] := datalen;
              copymemory((Pout_data + pos), @tempbuffer[0], datalen);
            end;
            inc(I, slen);
            inc(pos, datalen);
            inc(totallen, datalen);
            slen := 0;
          end
          else
          begin
            copymemory(@databuffer[0], (Pin_data + I), BLOCK_SIZE);
            lzo_compress_mem(@databuffer[0], BLOCK_SIZE, @tempbuffer[0], @datalen);

            if datalen >= BLOCK_SIZE then
            begin
               BlockLen[I2] := 0;
               datalen := BLOCK_SIZE;
               copymemory((Pout_data + pos), (Pin_data + I), BLOCK_SIZE);
            end
            else
            begin
              BlockLen[I2] := datalen;
              copymemory((Pout_data + pos), @tempbuffer[0], datalen);
            end;
            inc(I, BLOCK_SIZE);
            inc(pos, datalen);
            inc(totallen, datalen);
            dec(slen, BLOCK_SIZE);
          end;
        end;
        Copymemory(Pout_data, @Blocklen[0], blockcount * 2);
      end
    else
      totallen := -1;
  end;
  result := totallen;
end;

function TPakFile.unpak_Package(Psource: Pbyte; sourcelen: integer; Pdata: Pbyte; len, BlockCount, CompressType: integer): integer;
var
  blocklen: array of word;
  I, I2, pos: integer;
  datalen, templen, totallen: integer;
  slen: integer;
begin
  if BlockCount <= 0 then
  begin
    result := -1;
    exit;
  end;
  I := 0;
  pos := 0;
  templen := 0;
  totallen := 0;
  setlength(blocklen, BlockCount);
  CopyMemory(@Blocklen[0], Psource, BlockCount * 2);
  inc(I, BlockCount * 2);
  case CompressType of
    0:
      begin
        for I2 := 0 to BlockCount - 1 do
        begin
          datalen := Blocklen[I2];
          if datalen = 0 then
            datalen := BLOCK_SIZE;
          //if I2 = BlockCount - 1 then
            //datalen := datalen - 2;
          Copymemory((Pdata + pos), (Psource + I), datalen);
          inc(pos, datalen);
          inc(I, datalen);
          inc(totallen, datalen);
        end;
      end;
    2:
      begin
        slen := sourcelen - BlockCount * 2;
        for I2 := 0 to BlockCount - 1 do
        begin
          datalen := Blocklen[I2];
          if datalen = 0 then
          begin
            if I2 = BlockCount - 1 then
            begin
              datalen := sourcelen - I;
            end
            else
              datalen := BLOCK_SIZE;
            templen := datalen;
            if pos + datalen > len then
              templen := len - pos;
            copymemory(Pbyte(Pdata + pos), Pbyte(Psource + I), templen);
            templen := datalen;
          end
          else
          begin
            lzo_decompress(LZO_Bytep(Psource + I), datalen, LZO_Bytep(Pdata + pos), @templen);

          end;
          inc(pos, templen);
          inc(I, datalen);
          inc(totallen, templen);
          dec(slen, datalen);
          if I >= sourcelen then
            break;
        end;
      end;
  end;
  setlength(blocklen, 0);
  result := totallen;
end;

function TPakFile.unpak(Psource: Pbyte; sourcelen: integer; Pdata: Pbyte;
  len, BlockCount: integer): integer;
var
  I, I2, pos, BlockNum: integer;
  datalen, templen, totallen: integer;
  slen: integer;
  Blocklen: array of word;
begin
  if BlockCount <= 0 then
  begin
    result := 0;
    exit;
  end;

  I := 0;
  pos := 0;
  templen := 0;
  totallen := 0;
  slen := sourcelen;

  BlockNum := 0;
  setlength(Blocklen, BlockCount);

  for I2 := 0 to BlockCount - 1 do
  begin
    BlockLen[I2] := PWord(Psource + I)^;
    inc(I, 2);
  end;

  slen := slen - BlockCount * 2;

  while True do
  begin
    datalen := BlockLen[BlockNum];
    inc(BlockNum);
    if datalen = 0 then
    begin
      if slen >= BLOCK_SIZE then
        datalen := BLOCK_SIZE
      else
        datalen := slen;
      templen := datalen;
      if pos + datalen > len then
        templen := len - pos;

      copymemory(Pbyte(Pdata + pos), Pbyte(Psource + I), templen);
      templen := datalen;
    end
    else
    begin
      lzo_decompress(LZO_Bytep(Psource + I), datalen, LZO_Bytep(Pdata + pos), @templen);

      //templen := lzo_Decompress(Pchar(Psource + I), datalen, Pchar(Pdata + pos));
      //Decompress(Pbyte(Psource + I), datalen, Pbyte(Pdata + pos), templen);
      //templen := LzoDecode(Pbyte(Psource + I), datalen, Pbyte(Pdata + pos));
    end;
    inc(I, datalen);
    inc(pos, templen);
    totallen := templen + totallen;
    dec(slen, datalen);
    if I >= sourcelen then
      break;
  end;
  result := totallen;// - len;
end;

function TPakFile.pak(Pin_data: Pbyte; sourcelen: integer; Pout_data: Pbyte): integer;
var
  tempbuffer: array[0 .. (65536 + 65536 div 10 + 1024)] of byte;
  databuffer: array[0..65535] of byte;
  Pdata,psource: Pbyte;
  Block_len: array of word;
  I, pos, blocknum, IB: integer;
  datalen, templen, totallen: integer;
begin
  //
  pdata := Pout_data;
  psource := Pin_data;
  templen := sourcelen;
  totallen := 0;

  Blocknum := sourcelen div BLOCK_SIZE;

  if sourcelen mod BLOCK_SIZE <> 0 then
    inc(Blocknum);

  setlength(Block_len, Blocknum);
  Pdata := Pdata + Blocknum shl 1;
  IB := 0;
  inc(totallen, Blocknum shl 1);

  while(templen >= BLOCK_SIZE) do
  begin
    CopyMemory(@databuffer[0], psource, BLOCK_SIZE);
    psource := psource + BLOCK_SIZE;
    lzo_compress_mem(@databuffer[0], BLOCK_SIZE, @tempbuffer[0], @datalen);

    //datalen := Lzo_Compress(@databuffer[0], BLOCK_SIZE, @tempbuffer[0]);
    //Compress(@databuffer[0], BLOCK_SIZE, @tempbuffer[0], datalen);
    //datalen := LzoEncode(@databuffer[0],BLOCK_SIZE, @tempbuffer[0]);
    if datalen >= BLOCK_SIZE then
    begin
      Block_len[IB] := 0;
      copymemory(Pdata, @databuffer[0], BLOCK_SIZE);
      Pdata:= Pdata+ BLOCK_SIZE;
      inc(totallen, BLOCK_SIZE);
    end
    else
    begin
      Block_len[IB] := Word(datalen);
      copymemory(Pdata, @tempbuffer[0], datalen);
      Pdata:= Pdata+ datalen;
      inc(totallen, datalen);
    end;
    inc(IB);
    DEC(templen, BLOCK_SIZE);
  end;
  if templen > 0 then
  begin
    CopyMemory(@databuffer[0], psource, templen);
    //psource := psource + BLOCK_SIZE;
    lzo_compress_mem(@databuffer[0],templen,@tempbuffer[0], @datalen);

    //datalen := Lzo_Compress(@databuffer[0],templen,@tempbuffer[0]);
    //Compress(@databuffer[0],templen,@tempbuffer[0], datalen);
    //datalen := LzoEncode(@databuffer[0],templen,@tempbuffer[0]);
    if datalen >= templen then
    begin
      Block_len[IB] := 0;
      copymemory(Pdata, @databuffer[0], templen);
      Pdata:= Pdata+ templen;
      inc(totallen, templen);
    end
    else
    begin
      Block_len[IB] := datalen;
      copymemory(Pdata, @tempbuffer[0], datalen);
      Pdata:= Pdata+ datalen;
      inc(totallen, datalen);
    end;
  end;
  copymemory(Pout_data, @Block_Len[0], Blocknum shl 1);
  result := totallen;

end;

function TPakFile.ReadFromFile(PakFileName: String; FileName: AnsiString; Pout: Pbyte; buflen: integer): integer;
var
  temp: integer;
begin
  temp := ReadFromFileToBuffer(PakFileName, FileName);
  if temp < 0 then
  begin
    result := temp;
    exit;
  end;

  if temp > buflen then
  begin
    Move(databuffer[0], Pout^, buflen);
  end
  else if temp > 0 then
  begin
    Move(databuffer[0], Pout^, temp);
  end;

  result := temp;

end;

function TPakFIle.CalPakType(FileName: String): TPakType;
var
  headstr: array[0..7] of byte;
  FH, len: integer;
begin
  if not fileexists(FileName) then
  begin
    result := ptNONE;
    exit;
  end;
  FH := fileopen(FileName, fmopenread);
  len := fileseek(FH, 0, 2);
  fileseek(FH, 0, 0);
  if len >= 8 then
  begin
    fileread(FH, headstr[0], 8);
    result := CalPakType(@headstr[0]);
  end
  else
    result := ptNONE;
  fileclose(FH);
end;

function TPakFIle.CalPakType(Pdata: Pbyte): TPakType;
begin
  if campareFileHead(Pdata, PACKAGEFILE_HEAD) then
    result := ptPACKAGE
  else if campareFileHead(Pdata, PAKFILE_HEAD) then
    result := ptPAK
  else if campareFileHead(Pdata, SPAK_HEAD) then
  begin
    result := ptSPAK;
  end
  else
    result := ptNONE;
end;

function TPakFile.ReadFromFileToBuffer(PakFileName: String; FileID: Cardinal): integer;
var
  FH, I, Filelen: Integer;
  Pakstruct: TPakstruct;
  tempdata: array of byte;
  isFind: boolean;
  PakFileType: TPakType;
  BlockCount: integer;
begin
  result := 0;
  PakFileType := CalPakType(PakFileName);
  if PakFileType = ptNONE then
  begin
    result := -1;
    exit;
  end;

  case PakFileType of
    ptPAK:
      begin

        FH := Fileopen(PakFileName, fmopenread);
        Filelen := FileSeek(FH, 0, 2);
        fileseek(FH, 0, 0);
        fileread(FH, Pakstruct.Headstr[0], length(PAKFILE_HEAD){4}{length(Pakstruct.Headstr)});
        fileread(FH, Pakstruct.Filenum, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0], Pakstruct.Filenum * sizeof(TPakDataHead));

        //FileID := HashFilename(PAnsichar(FileName));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          if I = PakStruct.FileNum - 1 then
          begin
            Filelen := Filelen - PakStruct.PakDataHead[I].Offset;
          end
          else
          begin
            Filelen := PakStruct.PakDataHead[I + 1].Offset - PakStruct.PakDataHead[I].Offset;
          end;
          fileseek(FH, PakStruct.PakDataHead[I].Offset, 0);
          setlength(tempdata, Filelen);
          fileread(FH, tempdata[0], Filelen);
          fileclose(FH);

          bufferlen := PakStruct.PakDataHead[I].filelen;
          setlength(databuffer, bufferlen);

          BlockCount := PakStruct.PakDataHead[I].filelen div BLOCK_SIZE;
          if PakStruct.PakDataHead[I].filelen mod BLOCK_SIZE <> 0 then
            inc(BlockCount);

          Filelen := unpak(@tempdata[0], Filelen, @databuffer[0], bufferlen, BlockCount);
          if Filelen <> PakStruct.PakDataHead[I].filelen then
          begin
            setlength(tempdata, 0);
            result := -2;
            exit;
          end;
          setlength(tempdata, 0);
          result := PakStruct.PakDataHead[I].filelen;
          exit;
        end
        else
        begin
          fileclose(FH);
          result := -3;
          exit;
        end;
      end;
    ptPACKAGE:
      begin
        FH := Fileopen(PakFileName, fmopenread);
        Filelen := FileSeek(FH, 0, 2);
        fileseek(FH, 0, 0);
        fileread(FH, Pakstruct.Headstr[0], length(PACKAGEFILE_HEAD){length(Pakstruct.Headstr)});
        fileread(FH, Pakstruct.Filenum, 4);
        fileread(FH, Pakstruct.CompressType, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0], Pakstruct.Filenum * sizeof(TPakDataHead));

        //FileID := HashFilename(PAnsichar(FileName));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          if I = PakStruct.FileNum - 1 then
          begin
            Filelen := Filelen - PakStruct.PakDataHead[I].Offset;
          end
          else
          begin
            Filelen := PakStruct.PakDataHead[I + 1].Offset - PakStruct.PakDataHead[I].Offset;
          end;

          fileseek(FH, PakStruct.PakDataHead[I].Offset, 0);
          setlength(tempdata, Filelen);
          fileread(FH, tempdata[0], Filelen);
          fileclose(FH);

          BlockCount := PakStruct.PakDataHead[I].filelen div BLOCK_SIZE;
          if PakStruct.PakDataHead[I].filelen mod BLOCK_SIZE <> 0 then
            inc(BlockCount);

          if Pakstruct.CompressType = 0 then
            PakStruct.PakDataHead[I].filelen := PakStruct.PakDataHead[I].filelen - 2;

          bufferlen := PakStruct.PakDataHead[I].filelen;

          setlength(databuffer, bufferlen);

          if unpak_Package(@tempdata[0], Filelen, @databuffer[0], bufferlen, BlockCount, Pakstruct.CompressType) <> PakStruct.PakDataHead[I].filelen then
          begin
            setlength(tempdata, 0);
            result := -2;
            exit;
          end;

          setlength(tempdata, 0);
          result := PakStruct.PakDataHead[I].filelen;
          exit;

        end
        else
        begin
          fileclose(FH);
          result := -3;
          exit;
        end;
      end;
  end;
end;


function TPakFile.ReadFromFileToBuffer(PakFileName: String; FileName: Ansistring): integer;
var
  FH, I, Filelen: Integer;
  Pakstruct: TPakstruct;
  tempdata: array of byte;
  FileID: Cardinal;
  isFind: boolean;
  PakFileType: TPakType;
  BlockCount: integer;
begin
  //
  {if not Fileexists(PakFileName) then
  begin
    GameDebug.AddDebugInfo('Load Pak File Fail! Pak not Exists!');
    result := -1;
    exit;
  end; }
  result := 0;
  PakFileType := CalPakType(PakFileName);
  if PakFileType = ptNONE then
  begin
    result := -1;
    exit;
  end;

  case PakFileType of
    ptPAK:
      begin

        FH := Fileopen(PakFileName, fmopenread);
        Filelen := FileSeek(FH, 0, 2);
        fileseek(FH, 0, 0);
        fileread(FH, Pakstruct.Headstr[0], length(PAKFILE_HEAD){4}{length(Pakstruct.Headstr)});
        fileread(FH, Pakstruct.Filenum, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0], Pakstruct.Filenum * sizeof(TPakDataHead));

        FileID := HashFilename(PAnsichar(FileName));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          if I = PakStruct.FileNum - 1 then
          begin
            Filelen := Filelen - PakStruct.PakDataHead[I].Offset;
          end
          else
          begin
            Filelen := PakStruct.PakDataHead[I + 1].Offset - PakStruct.PakDataHead[I].Offset;
          end;
          fileseek(FH, PakStruct.PakDataHead[I].Offset, 0);
          setlength(tempdata, Filelen);
          fileread(FH, tempdata[0], Filelen);
          fileclose(FH);

          bufferlen := PakStruct.PakDataHead[I].filelen;
          setlength(databuffer, bufferlen);

          BlockCount := PakStruct.PakDataHead[I].filelen div BLOCK_SIZE;
          if PakStruct.PakDataHead[I].filelen mod BLOCK_SIZE <> 0 then
            inc(BlockCount);

          Filelen := unpak(@tempdata[0], Filelen, @databuffer[0], bufferlen, BlockCount);
          if Filelen <> PakStruct.PakDataHead[I].filelen then
          begin
            setlength(tempdata, 0);
            result := -2;
            exit;
          end;
          setlength(tempdata, 0);
          result := PakStruct.PakDataHead[I].filelen;
          exit;
        end
        else
        begin
          fileclose(FH);
          result := -3;
          exit;
        end;
      end;
    ptPACKAGE:
      begin
        FH := Fileopen(PakFileName, fmopenread);
        Filelen := FileSeek(FH, 0, 2);
        fileseek(FH, 0, 0);
        fileread(FH, Pakstruct.Headstr[0], length(PACKAGEFILE_HEAD){length(Pakstruct.Headstr)});
        fileread(FH, Pakstruct.Filenum, 4);
        fileread(FH, Pakstruct.CompressType, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0].FileID, Pakstruct.Filenum * sizeof(TPakDataHead));

        FileID := HashFilename(PAnsichar(FileName));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          if I = PakStruct.FileNum - 1 then
          begin
            Filelen := Filelen - PakStruct.PakDataHead[I].Offset;
          end
          else
          begin
            Filelen := PakStruct.PakDataHead[I + 1].Offset - PakStruct.PakDataHead[I].Offset;
          end;

          fileseek(FH, PakStruct.PakDataHead[I].Offset, 0);
          setlength(tempdata, Filelen);
          fileread(FH, tempdata[0], Filelen);
          fileclose(FH);

          BlockCount := PakStruct.PakDataHead[I].filelen div BLOCK_SIZE;
          if PakStruct.PakDataHead[I].filelen mod BLOCK_SIZE <> 0 then
            inc(BlockCount);

          if Pakstruct.CompressType = 0 then
            PakStruct.PakDataHead[I].filelen := PakStruct.PakDataHead[I].filelen - 2;

          bufferlen := PakStruct.PakDataHead[I].filelen;

          setlength(databuffer, bufferlen);

          if unpak_Package(@tempdata[0], Filelen, @databuffer[0], bufferlen, BlockCount, Pakstruct.CompressType) <> PakStruct.PakDataHead[I].filelen then
          begin
            setlength(tempdata, 0);
            result := -2;
            exit;
          end;

          setlength(tempdata, 0);
          result := PakStruct.PakDataHead[I].filelen;
          exit;

        end
        else
        begin
          fileclose(FH);
          result := -3;
          exit;
        end;
      end;
  end;
end;

function TPakFile.GetFilelen(PakFileName: string; FileName: Ansistring): integer;
var
  FileID: Cardinal;
begin
  //
  FileID := HashFilename(PAnsichar(FileName));
  result := GetFilelen(PakFileName, FileID);
end;

function TPakFile.GetFilelen(PakFileName: string; FileID: cardinal): integer;
var
  FH, I, temp: Integer;
  Pakstruct: TPakstruct;
  isFind: boolean;
  PakFileType: TPakType;
begin
  //
  if not Fileexists(PakFileName) then
  begin
    result := -1;
    exit;
  end;

  PakFileType := CalPakType(PakFileName);
  case PakFileType of
    ptPAK:
      begin

        FH := Fileopen(PakFileName, fmopenread);
        fileread(FH, Pakstruct.Headstr[0], length(PAKFILE_HEAD));
        fileread(FH, Pakstruct.Filenum, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0], Pakstruct.Filenum * sizeof(TPakDataHead));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            temp := I;
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          result := PakStruct.PakDataHead[temp].filelen;
          fileclose(FH);
        end
        else
        begin
          fileclose(FH);
          result := -2;
          exit;
        end;
      end;
    ptPACKAGE:
      begin
        FH := Fileopen(PakFileName, fmopenread);
        fileread(FH, Pakstruct.Headstr[0], length(PACKAGEFILE_HEAD){length(Pakstruct.Headstr)});
        fileread(FH, Pakstruct.Filenum, 4);
        fileread(FH, Pakstruct.CompressType, 4);
        setlength(Pakstruct.PakDataHead, Pakstruct.Filenum);
        Fileread(FH, Pakstruct.PakDataHead[0].FileID, Pakstruct.Filenum * sizeof(TPakDataHead));

        isFind := false;
        for I := 0 to Pakstruct.Filenum - 1 do
        begin
          if FileID = Pakstruct.PakDataHead[I].FileID then
          begin
            temp := I;
            isFind := true;
            break;
          end;
        end;

        if isFind then
        begin
          result := PakStruct.PakDataHead[temp].filelen;
          fileclose(FH);
        end
        else
        begin
          fileclose(FH);
          result := -2;
          exit;
        end;
      end
    else
      begin
        result := -3;
        exit;
      end;
  end;

end;

end.
