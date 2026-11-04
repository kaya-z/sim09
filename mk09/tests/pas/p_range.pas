program range;
{$R+}
type
  small = 1..10;
var
  a: array[1..5] of integer;
  i, j: integer;
  s: small;
  b: byte;
begin
  for i := 1 to 5 do a[i] := i * i;
  j := 3;
  writeln(a[j], ' ', a[5]);
  s := 10;
  b := 200;
  j := 10;
  s := j;
  b := b + 55;
  writeln(s, ' ', b);
  j := 6;
  writeln('before');
  writeln(a[j]);
  writeln('not reached');
end.
