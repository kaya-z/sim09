program strings;
type name = string[20];
var s, t, u: string;
    n: name;
    c: char;
    i, code: integer;
    r: real;
    l: longint;

function greet(who: string): string;
begin greet := 'Hello, ' + who + '!' end;

procedure show(var x: string);
begin writeln('[', x, '] ', length(x)) end;

begin
  s := 'Turbo';
  t := ' Pascal';
  u := s + t;
  writeln(u);
  writeln(length(u), ' ', pos('Pas', u), ' ', pos('x', u));
  writeln(copy(u, 2, 3), '|', copy(u, 7, 100), '|');
  writeln(greet('MK09'));
  n := 'abc';
  n := n + 'def';
  writeln(n, ' ', length(n));
  if s = 'Turbo' then writeln('equal');
  if s < t then writeln('less') else writeln('not less');
  if 'abc' < 'abd' then writeln('abc<abd');
  s[1] := 't';
  writeln(s, ' ', s[2], upcase(s[3]));
  c := '!';
  s := s + c;
  writeln(s);
  insert('XY', s, 3);
  writeln(s);
  delete(s, 1, 2);
  writeln(s);
  i := 12345;
  str(i, t);
  writeln(t, '.');
  str(i:8, t);
  writeln(t, '.');
  str(3.25:7:2, t);
  writeln(t, '.');
  val('4321', i, code);
  writeln(i + 1, ' ', code);
  val('12x', i, code);
  writeln(code);
  val('2.5', r, code);
  writeln(r * 2:6:2, ' ', code);
  val('123456', l, code);
  writeln(l, ' ', code);
  show(s);
  s := '';
  show(s);
  writeln(concat('a', 'b', 'c'), 'd');
  writeln('x':5, 'yz':4)
end.
