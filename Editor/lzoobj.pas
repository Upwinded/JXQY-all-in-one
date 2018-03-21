unit lzoobj;

interface

uses
  SysUtils;

type
  LZO_Byte = Byte;
  LZO_Bytep = ^Byte;
  LZO_Int = LongInt;
  LZO_Intp = ^LongInt;
  LZO_UInt = Cardinal;
  LZO_UIntp = ^Cardinal;
  LZO_Voidp = Pointer;

function lzo_compress(Src: LZO_Bytep; SrcLen: LZO_UInt;
    Dst: LZO_Bytep; DstLen: LZO_UIntp;
    WorkMem: LZO_Voidp): LZO_Int; cdecl; external 'pak.dll';

function lzo_compress_mem(Src: LZO_Bytep; SrcLen: LZO_UInt;
    Dst: LZO_Bytep; DstLen: LZO_UIntp): LZO_Int;

function lzo_decompress(Src: LZO_Bytep; SrcLen: LZO_Int;
    Dst: LZO_Bytep; DstLen: LZO_Intp): LZO_Int; cdecl; external 'pak.dll';

function lzo_decompress_safe(Src: LZO_Bytep; SrcLen: LZO_Int;  //经常出错，最好不用
    Dst: LZO_Bytep; DstLen: LZO_Intp): LZO_Int; cdecl; external 'pak.dll';

var
  Work: array[0..65535] of Byte;

implementation

function lzo_compress_mem(Src: LZO_Bytep; SrcLen: LZO_UInt;
    Dst: LZO_Bytep; DstLen: LZO_UIntp): LZO_Int;
begin
  result := lzo_compress(Src, SrcLen,
    Dst, DstLen,
    @Work[0]);
end;

end.
