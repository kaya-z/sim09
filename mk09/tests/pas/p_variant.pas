program variant;
type
  kind = (kcircle, krect, kpoint);
  shape = record
    id: integer;
    case k: kind of
      kcircle: (r: integer);
      krect: (w, h: integer);
      kpoint: ()
  end;
  word2 = record
    case boolean of
      true: (w: integer);
      false: (lo, hi: byte)
  end;
  nest = record
    a: integer;
    case t: byte of
      0: (x: integer);
      1: (y: longint;
          case u: char of
            'a': (p: byte);
            'b': (q, z: integer))
  end;
var
  s: shape;
  v: word2;
  n: nest;
  i: integer;
  arr: array[1..3] of shape;
begin
  s.id := 7; s.k := krect; s.w := 30; s.h := 40;
  writeln(s.id, ' ', ord(s.k), ' ', s.w, ' ', s.h);
  s.k := kcircle; s.r := 5;
  writeln(s.r, ' ', s.w);
  v.w := $1234;
  writeln(v.lo, ' ', v.hi);
  n.t := 1; n.y := 100000; n.u := 'b'; n.q := 3; n.z := 4;
  writeln(n.t, ' ', n.y, ' ', n.u, ' ', n.q, ' ', n.z);
  for i := 1 to 3 do begin arr[i].id := i; arr[i].k := kpoint end;
  writeln(arr[1].id + arr[2].id + arr[3].id);
end.
