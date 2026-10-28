program procs;
type
  op = function(a, b: integer): integer;
  action = procedure(n: integer);
  tbl = array[1..3] of op;
var
  f: op;
  g: action;
  t: tbl;
  i: integer;
  total: integer;

function add(a, b: integer): integer; begin add := a + b end;
function mul(a, b: integer): integer; begin mul := a * b end;
function sub(a, b: integer): integer; begin sub := a - b end;
procedure show(n: integer); begin write('<', n, '>') end;
procedure twice(n: integer); begin write(n * 2, ' ') end;

function apply(fn: op; x, y: integer): integer;
begin apply := fn(x, y) end;

procedure each(first, last: integer; procedure action(n: integer));
var k: integer;
begin
  for k := first to last do action(k);
  writeln
end;

function integrate(function fx(x: real): real; a, b: real; n: integer): real;
var k: integer; h, s: real;
begin
  h := (b - a) / n;
  s := 0;
  for k := 0 to n - 1 do s := s + fx(a + h * (k + 0.5)) * h;
  integrate := s
end;

function square(x: real): real; begin square := x * x end;

begin
  f := add;
  writeln(f(3, 4));
  f := mul;
  writeln(f(3, 4), ' ', apply(sub, 10, 3), ' ', apply(add, 1, 2));
  g := show;
  g(5);
  g := twice;
  g(6);
  writeln;
  t[1] := add; t[2] := mul; t[3] := sub;
  for i := 1 to 3 do write(t[i](8, 2), ' ');
  writeln;
  each(1, 4, show);
  each(1, 3, twice);
  writeln(integrate(square, 0.0, 3.0, 300):0:3)
end.
