program hanoi;
{ the towers of Hanoi: the moves of 4 disks }
var moves: integer;

procedure move(n: integer; from, to_, via: char);
begin
  if n > 0 then begin
    move(n - 1, from, via, to_);
    moves := moves + 1;
    writeln(moves:3, ': disk ', n, ' from ', from, ' to ', to_);
    move(n - 1, via, to_, from)
  end
end;

begin
  moves := 0;
  move(4, 'A', 'C', 'B')
end.
