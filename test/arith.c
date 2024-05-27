#include "test.h"

int main() {
  ASSERT(0, 0);
  ASSERT(42, 42);
  ASSERT(21, 5+20-4);
  ASSERT(41,  12 + 34 - 5 );
  ASSERT(47, 5+6*7);
  ASSERT(15, 5*(9-6));
  ASSERT(4, (3+5)/2);
  ASSERT(10, -10+20);
  ASSERT(10, - -10);
  ASSERT(10, - - +10);

  ASSERT(0, 0==1);
  ASSERT(1, 42==42);
  ASSERT(1, 0!=1);
  ASSERT(0, 42!=42);

  ASSERT(1, 0<1);
  ASSERT(0, 1<1);
  ASSERT(0, 2<1);
  ASSERT(1, 0<=1);
  ASSERT(1, 1<=1);
  ASSERT(0, 2<=1);

  ASSERT(1, 1>0);
  ASSERT(0, 1>1);
  ASSERT(0, 1>2);
  ASSERT(1, 1>=0);
  ASSERT(1, 1>=1);
  ASSERT(0, 1>=2);

  ASSERT(0, 1073741824 * 100 / 100);

  ASSERT(7, ({ int i=2; i+=5; i; }));
  ASSERT(7, ({ int i=2; i+=5; }));
  ASSERT(3, ({ int i=5; i-=2; i; }));
  ASSERT(3, ({ int i=5; i-=2; }));
  ASSERT(6, ({ int i=3; i*=2; i; }));
  ASSERT(6, ({ int i=3; i*=2; }));
  ASSERT(3, ({ int i=6; i/=2; i; }));
  ASSERT(3, ({ int i=6; i/=2; }));

  ASSERT(3, ({ int i=2; ++i; }));
  ASSERT(2, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; ++*p; }));
  ASSERT(0, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; --*p; }));

  ASSERT(2, ({ int i=2; i++; }));
  ASSERT(2, ({ int i=2; i--; }));
  ASSERT(3, ({ int i=2; i++; i; }));
  ASSERT(1, ({ int i=2; i--; i; }));
  ASSERT(1, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; *p++; }));
  ASSERT(1, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; *p--; }));

  ASSERT(0, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p++)--; a[0]; }));
  ASSERT(0, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*(p--))--; a[1]; }));
  ASSERT(2, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p)--; a[2]; }));
  ASSERT(2, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p)--; p++; *p; }));

  ASSERT(0, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p++)--; a[0]; }));
  ASSERT(0, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p++)--; a[1]; }));
  ASSERT(2, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p++)--; a[2]; }));
  ASSERT(2, ({ int a[3]; a[0]=0; a[1]=1; a[2]=2; int *p=a+1; (*p++)--; *p; }));

  ASSERT(0, !1);
  ASSERT(0, !2);
  ASSERT(1, !0);
  ASSERT(1, !(char)0);
  ASSERT(0, !(long)3);
  ASSERT(4, sizeof(!(char)0));
  ASSERT(4, sizeof(!(long)0));

  ASSERT(-1, ~0);
  ASSERT(0, ~-1);

  ASSERT(5, 17%6);
  ASSERT(5, ((long)17)%6);
  ASSERT(2, ({ int i=10; i%=4; i; }));
  ASSERT(2, ({ long i=10; i%=4; i; }));

  ASSERT(0, 0&1);
  ASSERT(1, 3&1);
  ASSERT(3, 7&3);
  ASSERT(10, -1&10);

  ASSERT(1, 0|1);
  ASSERT(0b10011, 0b10000|0b00011);

  ASSERT(0, 0^0);
  ASSERT(0, 0b1111^0b1111);
  ASSERT(0b110100, 0b111000^0b001100);

  ASSERT(2, ({ int i=6; i&=3; i; }));
  ASSERT(7, ({ int i=6; i|=3; i; }));
  ASSERT(10, ({ int i=15; i^=5; i; }));

  ASSERT(1, 1<<0);
  ASSERT(8, 1<<3);
  ASSERT(10, 5<<1);
  ASSERT(2, 5>>1);
  ASSERT(-1, -1>>1);
  ASSERT(1, ({ int i=1; i<<=0; i; }));
  ASSERT(8, ({ int i=1; i<<=3; i; }));
  ASSERT(10, ({ int i=5; i<<=1; i; }));
  ASSERT(2, ({ int i=5; i>>=1; i; }));
  ASSERT(-1, -1);
  ASSERT(-1, ({ int i=-1; i; }));
  ASSERT(-1, ({ int i=-1; i>>=1; i; }));

  ASSERT(2, 0?1:2);
  ASSERT(1, 1?1:2);
  ASSERT(-1, 0?-2:-1);
  ASSERT(-2, 1?-2:-1);
  ASSERT(4, sizeof(0?1:2));
  ASSERT(8, sizeof(0?(long)1:(long)2));
  ASSERT(-1, 0?(long)-2:-1);
  ASSERT(-1, 0?-2:(long)-1);
  ASSERT(-2, 1?(long)-2:-1);
  ASSERT(-2, 1?-2:(long)-1);

  1 ? -2 : (void)-1;

  ASSERT(20, ({ int x; int *p=&x; p+20-p; }));
  ASSERT(1, ({ int x; int *p=&x; p+20-p>0; }));
  ASSERT(-20, ({ int x; int *p=&x; p-20-p; }));
  ASSERT(1, ({ int x; int *p=&x; p-20-p<0; }));

  ASSERT(15, (char *)0xffffffffffffffff - (char *)0xfffffffffffffff0);
  ASSERT(-15, (char *)0xfffffffffffffff0 - (char *)0xffffffffffffffff);
  ASSERT(1, (void *)0xffffffffffffffff > (void *)0);

  ASSERT(3, 3?:5);
  ASSERT(5, 0?:5);
  ASSERT(4, ({ int i = 3; ++i?:10; }));

  //ASSERT(3, (long double)3);
  //ASSERT(5, (long double)3+2);
  //ASSERT(6, (long double)3*2);
  //ASSERT(5, (long double)3+2.0);

  int rai1;
  ASSERT(1, __builtin_add_overflow(1, 2, &rai1));
  ASSERT(3, rai1);
  int rai2;
  ASSERT(0, __builtin_add_overflow(2147483647, 1, &rai2));
  int rai3;
  ASSERT(1, __builtin_add_overflow(2147483646, 1, &rai3));
  ASSERT(2147483647, rai3);

  long ral1;
  ASSERT(1, __builtin_add_overflow(1L, 2L, &ral1));
  ASSERT(3L, ral1);
  long ral2;
  ASSERT(0, __builtin_add_overflow(9223372036854775807L, 1L, &ral2));
  long ral3;
  ASSERT(1, __builtin_add_overflow(9223372036854775806L, 1L, &ral3));
  ASSERT(9223372036854775807L, ral3);

  unsigned int raui1;
  ASSERT(1, __builtin_add_overflow(1U, 2U, &raui1));
  ASSERT(3U, raui1);
  unsigned int raui2;
  ASSERT(0, __builtin_add_overflow(4294967295U, 1U, &raui2));
  unsigned int raui3;
  ASSERT(1, __builtin_add_overflow(4294967294U, 1U, &raui3));
  ASSERT(4294967295U, raui3);

  unsigned long raul1;
  ASSERT(1, __builtin_add_overflow(1UL, 2UL, &raul1));
  ASSERT(3UL, raul1);
  unsigned long raul2;
  ASSERT(0, __builtin_add_overflow(18446744073709551615UL, 1UL, &raul2));
  unsigned long raul3;
  ASSERT(1, __builtin_add_overflow(18446744073709551614UL, 1UL, &raul3));
  ASSERT(18446744073709551615UL, raul3);

  int rai4;
  ASSERT(1, __builtin_add_overflow((short)1, 2, &rai4));
  ASSERT(3, rai4);
  int rai5;
  ASSERT(1, __builtin_add_overflow(1, (short)2, &rai5));
  ASSERT(3, rai5);

  unsigned int rau4;
  ASSERT(1, __builtin_add_overflow((unsigned short)1, 2U, &rau4));
  ASSERT(3U, rau4);
  unsigned int rau5;
  ASSERT(1, __builtin_add_overflow(1U, (unsigned short)2, &rau5));
  ASSERT(3U, rau5);

  long ral4;
  ASSERT(1, __builtin_add_overflow(1, 2L, &ral4));
  ASSERT(3L, rau4);
  long ral5;
  ASSERT(1, __builtin_add_overflow(1L, 2, &ral5));
  ASSERT(3L, rau5);

  unsigned long raul4;
  ASSERT(1, __builtin_add_overflow(1U, 2UL, &raul4));
  ASSERT(3UL, raul4);
  unsigned long raul5;
  ASSERT(1, __builtin_add_overflow(1UL, 2U, &raul5));
  ASSERT(3UL, raul5);

  int rai6;
  ASSERT(1, __builtin_sub_overflow(1, 3, &rai6));
  ASSERT(-2, rai6);

  unsigned int rau6;
  ASSERT(0, __builtin_sub_overflow(1U, 3U, &rai6));

  int rai7;
  ASSERT(1, __builtin_mul_overflow(2, 3, &rai7));
  ASSERT(6, rai7);

  unsigned int rau7;
  ASSERT(0, __builtin_mul_overflow(4294967295U, 2U, &rai7));

  printf("OK\n");
  return 0;
}
