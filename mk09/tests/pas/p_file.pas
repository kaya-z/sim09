program files;
type rec = record id: integer; name: string[10] end;
var f: text;
    g: file of integer;
    h: file of rec;
    s: string;
    i, n: integer;
    r: rec;
begin
  assign(f, '/pasf.txt');
  rewrite(f);
  writeln(f, 'first line');
  writeln(f, 42, ' ', 3.5:0:1);
  write(f, 'no newline');
  close(f);
  reset(f);
  while not eof(f) do begin
    readln(f, s);
    writeln('[', s, ']')
  end;
  close(f);
  reset(f);
  readln(f, s);
  readln(f, i);
  writeln(s, '|', i);
  close(f);
  assign(g, '/pasg.dat');
  rewrite(g);
  for i := 1 to 5 do write(g, i * i);
  close(g);
  reset(g);
  writeln(filesize(g));
  seek(g, 2);
  read(g, i);
  writeln(i);
  n := 0;
  seek(g, 0);
  while not eof(g) do begin read(g, i); n := n + i end;
  writeln(n);
  close(g);
  assign(h, '/pash.dat');
  rewrite(h);
  r.id := 7; r.name := 'seven';
  write(h, r);
  r.id := 8; r.name := 'eight';
  write(h, r);
  close(h);
  reset(h);
  read(h, r);
  writeln(r.id, ' ', r.name);
  read(h, r);
  writeln(r.id, ' ', r.name);
  writeln(eof(h));
  close(h)
end.
