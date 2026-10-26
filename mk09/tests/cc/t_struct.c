/* t_struct.c - struct, union, enum, typedef */
int printf(const char *fmt, ...);

struct point { int x, y; };
typedef struct { char name[8]; int age; } person;
struct node { int v; struct node *next; };
union u { int i; char c[2]; };
enum color { RED, GREEN = 5, BLUE };
typedef int (*binop)(int, int);

int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int apply(binop f, int a, int b) { return f(a, b); }
int dist2(struct point *p) { return p->x * p->x + p->y * p->y; }
struct point pt = { 3, 4 };
person people[2] = { { "ann", 30 }, { "bob", 41 } };

int main(void)
{
    struct point a, b;
    struct node n1, n2, n3, *p;
    union u uu;
    enum color c = BLUE;
    person me;
    int s = 0;
    a.x = 1; a.y = 2;
    b = a;
    b.x += 10;
    printf("%d %d %d %d\n", a.x, a.y, b.x, b.y);
    printf("%d %d\n", dist2(&pt), sizeof(struct point));
    n1.v = 1; n2.v = 2; n3.v = 3;
    n1.next = &n2; n2.next = &n3; n3.next = 0;
    for (p = &n1; p; p = p->next) s += p->v;
    printf("%d\n", s);
    uu.i = 0x4142;
    printf("%d %d\n", uu.c[0], uu.c[1]);
    printf("%d %d %d\n", RED, GREEN, c);
    printf("%s %d %s %d\n", people[0].name, people[0].age, people[1].name, people[1].age);
    me = people[1];
    me.age++;
    printf("%s %d %d\n", me.name, me.age, sizeof(person));
    printf("%d %d\n", apply(add, 3, 4), apply(mul, 3, 4));
    {
        binop ops[2];
        ops[0] = add; ops[1] = mul;
        printf("%d %d\n", ops[0](5, 6), (*ops[1])(5, 6));
    }
    return 0;
}
