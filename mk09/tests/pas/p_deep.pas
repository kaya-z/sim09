program deep;
var x, y: integer;
    s: string;
procedure l1;
  procedure l2;
    procedure l3;
      procedure l4;
      begin writeln('l4 ', x) end;
    begin l4; writeln('l3') end;
  begin l3; writeln('l2') end;
begin l2; writeln('l1') end;
begin
  x := 100;
  y := ((((((1 + 2) + 2) + 2) + 2) + 2) + 2);
  writeln(y);
if x > 0 then begin
if x > 1 then begin
if x > 2 then begin
if x > 3 then begin
if x > 4 then begin
if x > 5 then begin
  writeln('inner');
end;
end;
end;
end;
end;
end;
  s := 'a' + 'b' + 'c' + 'd' + 'e' + 'f';
  writeln(s, ' ', copy(s, 2, 3) + copy(s, 1, 2));
  writeln(((((x + 1) * 2) - 3) div 4) mod 50);
  l1
end.
