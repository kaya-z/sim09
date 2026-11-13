/* list.c - struct, pointers, malloc and free: a linked list that is kept sorted */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct node {
    char name[12];
    int value;
    struct node *next;
};

struct node *head;

void insert(char *name, int value)
{
    struct node *n = (struct node *)malloc(sizeof(struct node));
    struct node **pp = &head;
    if (!n) { puts("out of memory"); return; }
    strncpy(n->name, name, 11);
    n->name[11] = 0;
    n->value = value;
    while (*pp && strcmp((*pp)->name, name) < 0) pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
}

int main(void)
{
    struct node *n, *t;
    insert("pear", 3);
    insert("apple", 7);
    insert("orange", 1);
    insert("banana", 12);
    insert("cherry", 5);
    for (n = head; n; n = n->next) printf("%-8s %3d\n", n->name, n->value);
    for (n = head; n; n = t) { t = n->next; free((char *)n); }
    return 0;
}
