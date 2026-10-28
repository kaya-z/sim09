program primes;
{ the sieve of Eratosthenes }
const n = 100;
var sieve: array[2..n] of boolean;
    i, j, count: integer;
begin
  for i := 2 to n do sieve[i] := true;
  for i := 2 to n do
    if sieve[i] then begin
      j := i * i;
      while j <= n do begin sieve[j] := false; j := j + i end
    end;
  count := 0;
  for i := 2 to n do
    if sieve[i] then begin write(i:4); count := count + 1 end;
  writeln;
  writeln(count, ' primes up to ', n)
end.
