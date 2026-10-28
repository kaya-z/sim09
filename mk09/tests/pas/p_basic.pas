program basic;
const n = 5; msg = 'hello';
type color = (red, green, blue);
     rec = record a, b: integer; c: char end;
     arr = array[1..10] of integer;
var i, s: integer;
    c: color;
    r: rec;
    a: arr;
    p: ^rec;
    ok: boolean;

function fact(k: integer): integer;
begin
  if k <= 1 then fact := 1 else fact := k * fact(k - 1)
end;

procedure swap(var x, y: integer);
var t: integer;
begin t := x; x := y; y := t end;

procedure outer(k: integer);
  var local: integer;
  procedure inner(m: integer);
  begin
    local := local + m + k;
    writeln('inner ', local)
  end;
begin
  local := 100;
  inner(1);
  inner(2)
end;

begin
  writeln('Hello, Pascal!');
  writeln(msg, n:4);
  s := 0;
  for i := 1 to n do s := s + i;
  writeln('sum=', s);
  for i := 10 downto 8 do write(i, ' ');
  writeln;
  writeln(fact(6));
  r.a := 3; r.b := 4; r.c := 'x';
  writeln(r.a * r.b, ' ', r.c);
  for i := 1 to 10 do a[i] := i * i;
  writeln(a[3], ' ', a[10]);
  i := 1; s := 2;
  swap(i, s);
  writeln(i, ' ', s);
  c := green;
  case c of
    red: writeln('red');
    green, blue: writeln('green or blue')
  end;
  new(p);
  p^.a := 77;
  writeln(p^.a);
  dispose(p);
  ok := (i > 1) and (s < 5) or false;
  writeln(ok);
  i := 0;
  while i < 3 do begin write(i); i := i + 1 end;
  writeln;
  repeat i := i - 1 until i = 0;
  writeln(i);
  outer(5);
  with r do begin a := 9; writeln(a, b) end;
  writeln(odd(3), abs(-5), sqr(4), ord('A'), chr(66), succ(5), pred(5))
end.
