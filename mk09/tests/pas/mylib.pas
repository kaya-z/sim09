unit mylib;
{ a small unit: the interface is the declarations, the implementation the bodies }
interface
uses util;
const limit = 10;
type counter = record n: integer; name: string[8] end;
     visit = procedure(k: integer);
var total: integer;
procedure bump(var c: counter; by: integer);
function twice(x: integer): integer;
function clamp(x: integer): integer;
procedure each(first, last: integer; action: visit);

implementation

procedure bump(var c: counter; by: integer);
begin
  c.n := c.n + by;
  total := total + by
end;

function twice(x: integer): integer;
begin twice := triple(x) - x end;

function clamp(x: integer): integer;
begin
  if x > limit then clamp := limit else clamp := x
end;

procedure each(first, last: integer; action: visit);
var k: integer;
begin
  for k := first to last do action(k)
end;

begin
  total := 100
end.
