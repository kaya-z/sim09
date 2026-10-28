program sets;
type digits = set of 0..9;
     cs = set of char;
const names: array[1..3] of string[5] = ('one', 'two', 'three');
      limit: integer = 42;
      flags: array[0..3] of byte = (1, 2, 4, 8);
var a, b, c: digits;
    letters: cs;
    ch: char;
    i, n: integer;

function isvowel(c: char): boolean;
begin isvowel := c in ['a', 'e', 'i', 'o', 'u'] end;

procedure count;
var k: integer;
label 99;
begin
  k := 0;
  while true do begin
    k := k + 1;
    if k = 3 then continue;
    if k > 5 then break;
    write(k, ' ')
  end;
  writeln;
  for k := 1 to 10 do begin
    if k = 4 then goto 99;
    write(k, ' ')
  end;
99: writeln('jumped');
  repeat
    k := k - 1;
    if odd(k) then continue;
    write(k, ',')
  until k <= 0;
  writeln;
  if k = 0 then exit;
  writeln('not printed')
end;

begin
  a := [1, 3, 5, 7];
  b := [3..6];
  c := a + b;
  for i := 0 to 9 do if i in c then write(i);
  writeln;
  c := a * b;
  for i := 0 to 9 do if i in c then write(i);
  writeln;
  c := a - b;
  for i := 0 to 9 do if i in c then write(i);
  writeln;
  writeln(a = b, ' ', a <> b, ' ', [3] <= a, ' ', a >= [1, 3], ' ', [] = []);
  letters := ['a'..'z'];
  n := 0;
  for ch := 'a' to 'z' do if isvowel(ch) then n := n + 1;
  writeln(n);
  writeln(names[2], ' ', limit, ' ', flags[3]);
  limit := limit + 1;
  writeln(limit);
  include(a, 9);
  exclude(a, 1);
  for i := 0 to 9 do if i in a then write(i);
  writeln;
  count;
  writeln('end')
end.
