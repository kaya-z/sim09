C samples for the C compiler of MK09 (cc, see the manual 5.3)

  cc /c/hello.c -o /hello.mx      compile (the output name is the source name with .mx if -o is left out)
  /hello.mx a b                   run it

  hello.c    the arguments of main
  fib.c      recursion, a loop, long
  primes.c   the sieve of Eratosthenes (primes [limit])
  wc.c       files: fopen, fgets (wc file ...)
  fcopy.c    a binary copy: fread, fwrite (fcopy from to)
  strings.c  the string functions, ctype, sprintf
  list.c     struct, pointers, malloc, a sorted linked list
  sort.c     function pointers: qsort; rand
  float.c    floating point: sqrt, pi, printf %f %e %g

A program of one source file: the compiler needs a while (a minute or so in the simulator) and memory (Level 2 of the FM-11, or the sbc09 board).
Use only the headers that you need: the table of names of a source file is small (about 130).
