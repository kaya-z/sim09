program units;
uses mylib;
var c: counter;
    i: integer;

procedure show(k: integer);
begin write(k, ' ') end;

{$I incl.inc}

begin
  c.n := 0;
  bump(c, 5);
  bump(c, 7);
  writeln(c.n, ' ', total);
  writeln(twice(21), ' ', clamp(50), ' ', clamp(3), ' ', limit);
  each(1, 5, show);
  writeln;
  included
end.
