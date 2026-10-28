program calc;
{ reads numbers and prints their sum, mean and the biggest (end with 0) }
var x, sum, big: real;
    n: integer;
begin
  sum := 0; n := 0; big := 0;
  write('numbers (0 ends): ');
  repeat
    read(x);
    if x <> 0 then begin
      sum := sum + x;
      n := n + 1;
      if (n = 1) or (x > big) then big := x
    end
  until x = 0;
  if n > 0 then begin
    writeln('count ', n);
    writeln('sum   ', sum:0:2);
    writeln('mean  ', sum / n:0:2);
    writeln('max   ', big:0:2)
  end
end.
