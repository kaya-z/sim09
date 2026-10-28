program misc;
type
  point = record x, y: integer end;
  poly = array[1..3] of point;
  student = record name: string[12]; mark: array[1..3] of integer; avg: real end;
const
  days: array[0..6] of string[3] = ('Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat');
var
  tri: poly;
  s: student;
  i: integer;
  r: real;

procedure forwarded(k: integer); forward;

procedure first(k: integer);
begin
  if k > 0 then begin write('f', k, ' '); forwarded(k - 1) end
end;

procedure forwarded;
begin
  if k > 0 then begin write('g', k, ' '); first(k - 1) end
end;

function area(t: poly): integer;
var k, a: integer;
begin
  a := 0;
  for k := 1 to 3 do
    a := a + t[k].x * t[(k mod 3) + 1].y - t[(k mod 3) + 1].x * t[k].y;
  if a < 0 then a := -a;
  area := a div 2
end;

procedure shift(var t: poly; dx: integer);
var k: integer;
begin
  for k := 1 to 3 do t[k].x := t[k].x + dx
end;

function power(b: real; e: integer): real;
begin
  if e = 0 then power := 1.0
  else if e mod 2 = 0 then power := sqr(power(b, e div 2))
  else power := b * power(b, e - 1)
end;

function classify(c: char): integer;
begin
  case c of
    'a'..'f': classify := 1;
    '0'..'9', 'x': classify := 2;
    ' ': classify := 3
  else classify := 0
  end
end;

procedure counter;
var total: integer;
  function add(v: integer): integer;
  begin total := total + v; add := total end;
begin
  total := 10;
  writeln(add(5), ' ', add(7))
end;

procedure modify(s: string; var t: string);
begin s := s + '!'; t := s end;

procedure average(var st: student);
var k, sum: integer;
begin
  sum := 0;
  for k := 1 to 3 do sum := sum + st.mark[k];
  st.avg := sum / 3
end;

var a, b: string;
    sets: set of char;
    level: integer;
begin
  first(5); writeln;
  tri[1].x := 0; tri[1].y := 0; tri[2].x := 4; tri[2].y := 0; tri[3].x := 0; tri[3].y := 3;
  writeln(area(tri));
  shift(tri, 10);
  writeln(tri[2].x, ' ', area(tri));
  writeln(power(2.0, 10):0:1, ' ', power(1.5, 3):0:4);
  writeln(classify('c'), classify('7'), classify(' '), classify('z'), classify('x'));
  counter;
  a := 'hi'; modify(a, b);
  writeln(a, ' ', b);
  s.name := 'Alice';
  s.mark[1] := 90; s.mark[2] := 85; s.mark[3] := 77;
  average(s);
  writeln(s.name, ' ', s.avg:0:2);
  with s do begin
    mark[1] := 100;
    writeln(name, mark[1] + mark[2])
  end;
  for i := 0 to 6 do write(days[i], ' ');
  writeln;
  sets := ['a'..'e'] + ['x'];
  if 'c' in sets then writeln('c in') else writeln('c out');
  if 'z' in sets then writeln('z in') else writeln('z out');
  level := 0;
  for i := 1 to 3 do
    case i of
      1: level := level + 1;
      2: level := level * 10;
      3: level := level + 5
    end;
  writeln(level);
  r := 22 / 7;
  writeln(r:0:4, ' ', trunc(r * 1000), ' ', round(r * 1000));
  writeln(maxint, ' ', -maxint - 1, ' ', 32767 + 1)
end.
