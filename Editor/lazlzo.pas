//
//-------------------------------------------------------------------------
//
//
//    QuickBurro Middleware Application Development Package for Lazarus
//
//    Version 4.28
//    Update Date: 2013.8.5.
//    QQ: 779545524
//    QQ groups: 79114999,33286656,116166458,84945607,18594635
//    Email: Jopher@189.cn  or wpy020327@163.com
//    Homepage: http://www.quickburro.com/
//
//-------------------------------------------------------------------------
//
unit lazlzo;
interface
uses
   Classes,
   Sysutils;
   function lzo_compressdestlen(in_len: integer): integer;
   function lzo_compress(in_p: PAnsiChar; in_len: integer; out_p: PAnsiChar): integer;
   function lzo_decompressdestlen(in_p: PAnsiChar): integer;
   function lzo_decompress(in_p: PAnsiChar; in_len: integer; out_p: PAnsiChar): Integer;
   function LzoCompressStream(SourceStream: TMemoryStream; TargetStream: TMemoryStream): boolean;
   function LzoDecompressStream(SourceStream: TMemoryStream; TargetStream: TMemoryStream): boolean;
implementation
const
  M2_MAX_LEN=8;
  M4_MAX_LEN=9;
  M4_MARKER=16;
  M3_MARKER=M4_MARKER+16;
  M2_MARKER=M3_MARKER+32;
  M2_MAX_OFFSET=$0800;
  M3_MAX_OFFSET=$4000;
  M4_MAX_OFFSET=$C000;
  MAX_OFFSET=M4_MAX_OFFSET-1;
  M4_OFF_BITS=11;
  D_BITS=14;
  D_MASK=(1 shl D_BITS) - 1;
  D_HIGH=(D_MASK shr 1)+1;
  D_MUL_SHIFT=5;
  D_MUL=(1 shl D_MUL_SHIFT)+1;
procedure movechars(s,d: PAnsiChar; t: cardinal);
var
   i: cardinal;
begin
  for i := 1 to t do
     begin
       d^ := s^;
       inc(d);
       inc(s);
     end;
end;
function do_compress(in_p: PAnsiChar; in_len: Integer; out_p: PAnsiChar; out out_len: integer): integer;
var in_end, ip_end, ii, end_p, m_pos, out_beg: PAnsiChar;
    m_off, m_len, dindex, t, tt: Integer;
    dict: array[0..D_MASK] of PAnsiChar;
label
   lit, try_match, match, same4, m3_m4_len, m1;
begin
  in_end := in_p+in_len;
  ip_end := in_end-9;
  ii := in_p;
  inc(in_p,4);
  out_beg := out_p;
  FillChar(dict,sizeof(dict),0);
  repeat
    dindex := ((D_MUL * ((((((ord(in_p[3]) shl 6) xor ord(in_p[2])) shl 5)
      xor ord(in_p[1])) shl 5) xor ord(in_p[0]))) shr D_MUL_SHIFT) and D_MASK;
    if dict[dindex]=nil then
       begin
lit:     dict[dindex] := in_p;
         inc(in_p);
         if in_p<ip_end then
           continue
         else
           break;
       end
    else
      m_pos := dict[dindex];
    m_off := in_p-m_pos;
    if (m_off>MAX_OFFSET) then
      goto lit else
      if (m_off<=M2_MAX_OFFSET) or (m_pos[3]=in_p[3]) then
        goto try_match;
    dindex := (dindex and (D_MASK and $7ff)) xor (D_HIGH or $1f);
    if dict[dindex]=nil then
      goto lit else
      m_pos := dict[dindex];
    m_off := in_p-m_pos;
    if (m_off>MAX_OFFSET) then
      goto lit else
    if (m_off<=M2_MAX_OFFSET) or (m_pos[3]=in_p[3]) then
       goto try_match else
       goto lit;
try_match:
   if (pWord(m_pos)^<>pWord(in_p)^) or (m_pos[2]<>in_p[2]) then
     goto lit;
match:
    dict[dindex] := in_p;
    t := in_p-ii;
    if t<>0 then
       begin
         if t<=3 then
            begin
              PByte(out_p-2)^ := PByte(out_p-2)^ or t;
              pInteger(out_p)^ := pInteger(ii)^;
              inc(out_p,t);
              inc(ii,t);
            end
         else
         if t<=18 then
            begin
              out_p^ := ansichar(t-3);
              inc(out_p);
              movechars(ii,out_p,t);
              inc(out_p,in_p-ii);
              inc(ii,in_p-ii);
            end
         else
            begin
              tt := t-18;
              out_p^ := #0; inc(out_p);
              while tt>255 do
                 begin
                   dec(tt,255);
                   out_p^ := #0;
                   inc(out_p);
                 end;
              out_p^ := ansichar(tt);
              inc(out_p);
              system.move(ii^,out_p^,t);
              inc(out_p,in_p-ii);
              inc(ii,in_p-ii);
            end;
       end;
    if (m_pos[3]=in_p[3]) then
      if (m_pos[4]=in_p[4]) then
        if (m_pos[5]=in_p[5]) then
          if (m_pos[6]=in_p[6]) then
same4:      if (m_pos[7]=in_p[7]) then
              if (m_pos[8]=in_p[8]) then
              begin
                inc(in_p,9);
                end_p := in_end;
                inc(m_pos,M2_MAX_LEN+1);
                while (in_p<end_p) and (m_pos^=in_p^) do
                   begin
                     inc(in_p);
                     inc(m_pos);
                   end;
                m_len := in_p-ii;
                if m_off<=M3_MAX_OFFSET then
                   begin
                     dec(m_off);
                     if m_len<=33 then
                        begin
                          out_p^ := ansichar(integer(M3_MARKER or (m_len-2)));
                          inc(out_p);
                          pWord(out_p)^ := m_off shl 2;
                          inc(out_p,2);
                          ii := in_p;
                          if in_p<ip_end then
                             continue
                          else
                             break;
                        end
                     else
                        begin
                          dec(m_len,33);
                          out_p^ := ansichar(M3_MARKER);
                          goto m3_m4_len;
                        end;
                   end
                else
                   begin
                     dec(m_off,M3_MAX_OFFSET);
                     if (m_len<=M4_MAX_LEN) then
                        begin
                          out_p^ := ansichar(integer(M4_MARKER or
                            ((m_off and M3_MAX_OFFSET) shr M4_OFF_BITS) or (m_len-2)));
                          inc(out_p);
                        end
                     else
                        begin
                          dec(m_len,M4_MAX_LEN);
                          out_p^ := ansichar(integer(M4_MARKER or ((m_off and M3_MAX_OFFSET)shr M4_OFF_BITS)));
m3_m4_len:                inc(out_p);
                          while (m_len>255) do
                             begin
                               dec(m_len,255);
                               out_p^ := #0;
                               inc(out_p);
                             end;
                          out_p^ := ansichar(m_len);
                          inc(out_p);
                        end;
                   end;
                pWord(out_p)^ := m_off shl 2;
                inc(out_p,2);
                ii := in_p;
                if in_p<ip_end then
                   continue
                else
                   break;
              end else inc(in_p,8)
            else inc(in_p,7)
          else inc(in_p,6)
        else inc(in_p,5)
      else inc(in_p,4)
    else inc(in_p,3);
    if m_off<=M2_MAX_OFFSET then
       begin
         dec(m_off);
         pWord(out_p)^ := integer(((in_p-ii-1)shl 5) or ((m_off and 7)shl 2) or ((m_off shr 3) shl 8));
         inc(out_p,2);
         ii := in_p;
         if in_p<ip_end then
            continue
         else
            break;
       end
    else
    if m_off<=M3_MAX_OFFSET then
       begin
         dec(m_off);
         pInteger(out_p)^ := integer(M3_MARKER or (in_p-ii-2) or (m_off shl 10));
         inc(out_p,3);
         ii := in_p;
         if in_p<ip_end then
            continue
         else
            break;
       end
    else
       begin
         dec(m_off,M3_MAX_OFFSET);
         out_p^ := ansichar(integer(M4_MARKER or (in_p-ii-2) or ((m_off and M3_MAX_OFFSET)shr M4_OFF_BITS)));
m1:      inc(out_p);
         pWord(out_p)^ := m_off shl 2;
         inc(out_p,2);
         ii := in_p;
         if in_p<ip_end then
            continue
         else
            break;
       end;
  until false;
  out_len := out_p-out_beg;
  result := in_end-ii;
end;
function lzo_compressdestlen(in_len: integer): integer;
begin
  result := in_len+(in_Len shr 3)+(64+7);
end;
function lzo_compress(in_p: PAnsiChar; in_len: integer; out_p: PAnsiChar): integer;
var
   out_beg: PAnsiChar;
   t, tt: Integer;
label mov;
begin
  out_beg := out_p;
  if in_len>=$8000 then
     begin
       pWord(out_p)^ := $8000 or (in_len and $7fff);
       pWord(out_p+2)^ := in_len shr 15;
       inc(out_p,4);
     end
  else
     begin
       pWord(out_p)^ := in_len;
       if in_len=0 then
          begin
            result := 2;
            exit;
          end;
       inc(out_p,2);
     end;
  if in_len<=M2_MAX_LEN+5 then
     begin
       t := in_len;
       out_p^ := ansichar(t+17);
       goto mov;
     end
  else
     begin
       t:= do_compress(in_p, in_len, out_p, result);
       inc(out_p,result);
     end;
  if t>0 then
     begin
       if t<=3 then
         inc(out_p[-2],t)
       else
       if t<=18 then
          begin
            out_p^ := ansichar(t-3);
            inc(out_p);
          end
       else
          begin
            tt := t-18;
            out_p^ := #0;
            inc(out_p);
            while tt>255 do
               begin
                 dec(tt,255);
                 out_p^ := #0;
                 inc(out_p);
               end;
            out_p^ := ansichar(tt);
mov:        inc(out_p);
          end;
       system.move((in_p+in_len-t)^,out_p^,t);
       inc(out_p,t);
     end;
  result := out_p-out_beg;
end;
function lzo_decompressdestlen(in_p: PAnsiChar): integer;
begin
  result := pWord(in_p)^;
  inc(in_p,2);
  if result and $8000<>0 then
    result := (result and $7fff) or (integer(pWord(in_p)^) shl 15);
end;
function lzo_decompress(in_p: PAnsiChar; in_len: integer; out_p: PAnsiChar): Integer;
var
   ip_end, m_pos, out_end: PAnsiChar;
   t: Integer;
label
   match_next, first_literal_run, match, match_done, copy_m, m1;
begin
  ip_end := in_p+in_len;
  result := pWord(in_p)^;
  if result=0 then
     exit;
  inc(in_p,2);
  if result and $8000<>0 then
     begin
       result := (result and $7fff) or (integer(pWord(in_p)^) shl 15);
       inc(in_p,2);
     end;
  out_end := out_p+result;
  t := ord(in_p[0]);
  if t>17 then
     begin
       dec(t,17);
       inc(in_p);
       if t<4 then
         goto match_next;
       movechars(in_p,out_p,t);
       inc(out_p,t);
       inc(in_p,t);
       goto first_literal_run;
     end;
  while in_p<ip_end do
     begin
       t := ord(in_p[0]);
       inc(in_p);
       if t>=16 then
         goto match
       else
          if t=0 then
             begin
               while in_p[0]=#0 do
                  begin
                    inc(t,255);
                    inc(in_p);
                  end;
               inc(t,15+ord(in_p[0]));
               inc(in_p);
             end;
       inc(t,3);
       system.Move(in_p^,out_p^,t);
       inc(in_p,t);
       inc(out_p,t);
first_literal_run:
       if in_p>=ip_end then
         break;
       t := ord(in_p[0]);
       inc(in_p);
       repeat
match:   if t>=M2_MARKER then
            begin
              m_pos := out_p-1-((t shr 2) and 7)-(ord(in_p[0])shl 3);
              inc(in_p);
              t := (t shr 5)+1;
              if out_p+t>out_end then
                t := out_end-out_p;
              goto copy_m;
            end
          else
            if t>=M3_MARKER then
               begin
                 t := t and 31;
                 if t=0 then
                    begin
                      while in_p[0]=#0 do
                         begin
                           inc(t,255);
                           inc(in_p);
                         end;
                      inc(t,31+ord(in_p[0]));
                      inc(in_p);
                    end;
                 m_pos := out_p-1-(pWord(in_p)^ shr 2);
                 inc(in_p,2);
               end
            else
               if t>=M4_MARKER then
                  begin
                    m_pos := out_p-((t and 8)shl M4_OFF_BITS)-M3_MAX_OFFSET;
m1:                 t := t and 7;
                    if t=0 then
                       begin
                         while in_p[0]=#0 do
                            begin
                              inc(t,255);
                              inc(in_p);
                            end;
                         inc(t,7+ord(in_p[0]));
                         inc(in_p);
                       end;
                    dec(m_pos,pWord(in_p)^ shr 2);
                    inc(in_p,2);
                  end;
         inc(t,2);
         if out_p+t>out_end then
           t := out_end-out_p;
         if (t>=6) and (out_p-m_pos>=t) then
           system.Move(m_pos^,out_p^,t)
         else
copy_m:    movechars(m_pos,out_p,t);
         inc(out_p,t);
match_done:
         t := ord(in_p[-2]) and 3;
         if t=0 then
            break;
match_next:
         out_p^ := in_p^;
         inc(out_p);
         inc(in_p);
         if t<>1 then
            begin
              out_p^ := in_p^;
              inc(out_p);
              inc(in_p);
              if t=3 then
                 begin
                   out_p^ := in_p^;
                   inc(out_p);
                   inc(in_p);
                 end;
            end;
         t := ord(in_p[0]);
         inc(in_p);
       until in_p>=ip_end;
     end;
end;
function LzoCompressStream(SourceStream: TMemoryStream; TargetStream: TMemoryStream): boolean;
var
   DataLen, len, newlen: integer;
begin
      DataLen := SourceStream.Size;
      len:= lzo_compressdestlen(DataLen);
      TargetStream.SetSize(len);
      SourceStream.Position:=0;
      TargetStream.Position:=0;
      newlen := lzo_compress(SourceStream.Memory,DataLen,TargetStream.Memory);
      if (newlen<>len) and (newlen>=0) then
         TargetStream.SetSize(newlen);
      result:=true;
end;
function LzoDecompressStream(SourceStream: TMemoryStream; TargetStream: TMemoryStream): boolean;
var
   len, newlen: integer;
begin
   try
      SourceStream.Position:=0;
      len:=lzo_decompressdestlen(SourceStream.Memory);
      TargetStream.SetSize(len);
      TargetStream.Position:=0;
      newlen:=lzo_decompress(SourceStream.Memory,SourceStream.Size,TargetStream.Memory);
      if (newlen<>len) and (newlen>=0) then
         TargetStream.SetSize(newlen);
      Result:=true;
   except
      result:=false;
   end;
end;
end.
