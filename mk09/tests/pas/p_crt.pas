program crtdemo;
{$M 3000,6000}
uses crt;
type list = ^cell;
     cell = record v: integer; next: list end;
var i: integer;
    n: longint;
    p, head: list;
    s: string;
begin
  clrscr;
  gotoxy(3, 2);
  textcolor(4);
  writeln('colored');
  textcolor(7);
  randomize;
  i := random(10);
  if (i >= 0) and (i < 10) then writeln('random ok');
  writeln(paramcount, ' ', length(paramstr(0)) > 0);
  head := nil;
  for i := 1 to 700 do begin new(p); p^.v := i; p^.next := head; head := p end;
  n := 0;
  p := head;
  while p <> nil do begin n := n + p^.v; p := p^.next end;
  writeln(n);
  s := paramstr(1);
  writeln('[', s, ']')
end.
