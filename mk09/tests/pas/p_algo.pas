program algorithms;
const n = 30;
type
  vec = array[1..10] of integer;
  matrix = array[1..2, 1..2] of integer;
  nodep = ^node;
  node = record key: integer; left, right: nodep end;
  link = ^item;
  item = record value: integer; next: link end;
  day = (mon, tue, wed, thu, fri, sat, sun);
var
  sieve: array[2..n] of boolean;
  v: vec;
  i, j, count: integer;
  root: nodep;
  head, p: link;
  d: day;
  m, mm: matrix;
  steps: longint;

procedure sort(var a: vec; lo, hi: integer);
var i, j, pivot, t: integer;
begin
  i := lo; j := hi; pivot := a[(lo + hi) div 2];
  repeat
    while a[i] < pivot do i := i + 1;
    while a[j] > pivot do j := j - 1;
    if i <= j then begin
      t := a[i]; a[i] := a[j]; a[j] := t;
      i := i + 1; j := j - 1
    end
  until i > j;
  if lo < j then sort(a, lo, j);
  if i < hi then sort(a, i, hi)
end;

procedure insert(var t: nodep; k: integer);
begin
  if t = nil then begin
    new(t);
    t^.key := k; t^.left := nil; t^.right := nil
  end else if k < t^.key then insert(t^.left, k)
  else insert(t^.right, k)
end;

procedure inorder(t: nodep);
begin
  if t <> nil then begin
    inorder(t^.left);
    write(t^.key, ' ');
    inorder(t^.right)
  end
end;

function height(t: nodep): integer;
var l, r: integer;
begin
  if t = nil then height := 0
  else begin
    l := height(t^.left); r := height(t^.right);
    if l > r then height := l + 1 else height := r + 1
  end
end;

procedure hanoi(k: integer; a, b, c: char);
begin
  if k > 0 then begin
    hanoi(k - 1, a, c, b);
    steps := steps + 1;
    hanoi(k - 1, c, b, a)
  end
end;

function gcd(a, b: integer): integer;
begin
  if b = 0 then gcd := a else gcd := gcd(b, a mod b)
end;

function fact(k: integer): longint;
begin
  if k <= 1 then fact := 1 else fact := k * fact(k - 1)
end;

procedure outer;
var total: integer;
    data: vec;
  procedure fill;
  var k: integer;
  begin for k := 1 to 10 do data[k] := k * 2 end;
  procedure sum;
  var k: integer;
  begin
    total := 0;
    for k := 1 to 10 do total := total + data[k]
  end;
begin
  fill; sum;
  writeln('total ', total)
end;

function reverse(s: string): string;
var k: integer; r: string;
begin
  r := '';
  for k := length(s) downto 1 do r := r + s[k];
  reverse := r
end;

begin
  for i := 2 to n do sieve[i] := true;
  for i := 2 to n do
    if sieve[i] then begin
      j := i * i;
      while j <= n do begin sieve[j] := false; j := j + i end
    end;
  count := 0;
  for i := 2 to n do if sieve[i] then begin write(i, ' '); count := count + 1 end;
  writeln('(', count, ')');
  for i := 1 to 10 do v[i] := (i * 7) mod 11;
  sort(v, 1, 10);
  for i := 1 to 10 do write(v[i], ' ');
  writeln;
  root := nil;
  for i := 1 to 10 do insert(root, (i * 5) mod 11);
  inorder(root); writeln;
  writeln(height(root));
  steps := 0; hanoi(10, 'a', 'b', 'c'); writeln(steps);
  writeln(gcd(84, 36), ' ', gcd(17, 5));
  writeln(fact(10), ' ', fact(12));
  head := nil;
  for i := 1 to 5 do begin new(p); p^.value := i * i; p^.next := head; head := p end;
  p := head;
  while p <> nil do begin write(p^.value, ' '); p := p^.next end;
  writeln;
  for d := mon to sun do if d in [sat, sun] then write('weekend ') else write(ord(d), ' ');
  writeln;
  writeln(succ(mon) = tue, ' ', pred(sun) = sat);
  m[1,1] := 1; m[1,2] := 2; m[2,1] := 3; m[2,2] := 4;
  for i := 1 to 2 do for j := 1 to 2 do
    mm[i,j] := m[i,1] * m[1,j] + m[i,2] * m[2,j];
  writeln(mm[1,1], ' ', mm[1,2], ' ', mm[2,1], ' ', mm[2,2]);
  outer;
  writeln(reverse('stressed'))
end.
