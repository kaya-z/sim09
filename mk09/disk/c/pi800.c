/* 800 digits of pi (the spigot algorithm, 4 digits at a time).  The original program has 32 bit ints; here int is 16 bit, so the numbers
 * that grow (a * f[b], d, e) are long. */
int b, c = 2800, g;
long a = 10000, d, e;
int f[2801];
main(){
for(;b-c;)f[b++]=a/5;
for(;d=0,g=c*2;c-=14,printf("%.4ld",e+d/a),e=d%a)
for(b=c;d+=f[b]*a,f[b]=d%--g,d/=g--,--b;d*=b);
}
