program reals;
const pi2 = 6.28318; big = 100000; eps = 1.0e-3;
var x, y: real;
    n: longint;
    i: integer;
    a: array[1..3] of real;

function half(v: real): real;
begin half := v / 2 end;

function sq(v: real): real;
begin sq := v * v end;

procedure show(r: real; w: integer);
begin writeln(r:w:2) end;

begin
  x := 1.5; y := 2;
  writeln(x + y:8:3);
  writeln(x * y:8:3, ' ', x / y:8:3, ' ', x - y:8:3);
  writeln(10 / 4:6:2);
  n := big; n := n * 3 + 1;
  writeln(n);
  i := 7;
  x := i;
  writeln(x:5:1);
  x := n;
  writeln(x:10:1);
  writeln(trunc(2.7), ' ', round(2.5), ' ', round(-2.5), ' ', trunc(-2.7));
  writeln(half(9):0:2, ' ', sq(1.5):0:3);
  show(3.14159, 10);
  a[1] := 0.5; a[2] := 0.25; a[3] := a[1] + a[2];
  writeln(a[3]:6:2);
  if x > y then writeln('greater') else writeln('smaller');
  if y = 2 then writeln('two');
  writeln(sqrt(2.0):8:5);
  writeln(sin(1.0):8:5, ' ', cos(1.0):8:5, ' ', arctan(1.0) * 4:8:5);
  writeln(exp(1.0):8:5, ' ', ln(10.0):8:5);
  writeln(1.5e3:10:1, ' ', pi * 2:9:5);
  writeln(abs(-2.5):6:2, ' ', sqr(3.0):6:1);
  writeln(x);
  writeln(-x:12:1);
  n := 70000;
  writeln(n div 7, ' ', n mod 7, ' ', -n);
  write(1.0/3.0:0:8);
  writeln
end.
