#include "test.h"

#define CMPLX(x,y) __builtin_complex((double)(x), (double)y)
#define CMPLXF(x,y) __builtin_complex((float)(x), (float)y)
#define CMPLXL(x,y) __builtin_complex((long double)(x), (long double)y)

extern float crealf(float _Complex fc);
extern float cimagf(float _Complex fc);
extern double creal(double _Complex fc);
extern double cimag(double _Complex fc);
extern double creall(long double _Complex fc);
extern double cimagl(long double _Complex fc);

int main() {
  float _Complex fc1 = CMPLXF(2.0f, 3.0f);
  ASSERT(1, 2.0f == crealf(fc1));
  ASSERT(1, 3.0f == cimagf(fc1));

  double _Complex dc1 = CMPLX(2.0, 3.0);
  ASSERT(1, 2.0 == creal(dc1));
  ASSERT(1, 3.0 == cimag(dc1));

  long double _Complex ldc1 = CMPLXL(2.0l, 3.0l);
  ASSERT(1, 2.0l == creall(ldc1));
  ASSERT(1, 3.0l == cimagl(ldc1));

  float _Complex fc2 = 3.0fi;
  ASSERT(1, 0.0f == crealf(fc2));
  ASSERT(1, 3.0f == cimagf(fc2));

  double _Complex dc2 = 3.0i;
  ASSERT(1, 0.0 == creal(dc2));
  ASSERT(1, 3.0 == cimag(dc2));

  long double _Complex ldc2 = 3.0li;
  ASSERT(1, 0.0l == creall(ldc2));
  ASSERT(1, 3.0l == cimagl(ldc2));

  float _Complex fc3 = 3.0if;
  long double _Complex ldc3 = 3.0il;

  float _Complex fc4 = 3.0f;
  ASSERT(1, 3.0f == crealf(fc4));

  double _Complex dc4 = 3.0;
  ASSERT(1, 3.0 == creal(dc4));

  long double _Complex ldc4 = 3.0l;
  ASSERT(1, 3.0l == creall(ldc4));

  float f1 = fc4;
  ASSERT(1, 3.0f == f1);

  double d1 = dc4;
  ASSERT(1, 3.0 == d1);

  long double ld1 = ldc4;
  ASSERT(1, 3.0l == ld1);

  float _Complex fc10 = CMPLXF(2.0f, 3.0f);
  float _Complex fc11 = fc10 + CMPLXF(1.0f, 2.0f);
  ASSERT(1, 3.0f == crealf(fc11));
  ASSERT(1, 5.0f == cimagf(fc11));

  double _Complex dc10 = CMPLX(2.0, 3.0);
  double _Complex dc11 = dc10 + CMPLX(1.0, 2.0);
  ASSERT(1, 3.0 == creal(dc11));
  ASSERT(1, 5.0 == cimag(dc11));

  long double _Complex ldc10 = CMPLXL(2.0l, 3.0l);
  long double _Complex ldc11 = ldc10 + CMPLXL(1.0l, 2.0l);
  ASSERT(1, 3.0l == creall(ldc11));
  ASSERT(1, 5.0l == cimagl(ldc11));

  float _Complex fc12 = fc10 - CMPLXF(1.0f, -2.0f);
  ASSERT(1, 1.0f == crealf(fc12));
  ASSERT(1, 5.0f == cimagf(fc12));

  double _Complex dc12 = dc10 - CMPLX(1.0, -2.0);
  ASSERT(1, 1.0 == creal(dc12));
  ASSERT(1, 5.0 == cimag(dc12));

  long double _Complex ldc12 = ldc10 - CMPLXL(1.0l, -2.0l);
  ASSERT(1, 1.0l == creall(ldc12));
  ASSERT(1, 5.0l == cimagl(ldc12));

  float _Complex fc13 = fc10 * CMPLXF(1.0f, -2.0f);
  ASSERT(1, 8.0f == crealf(fc13));
  ASSERT(1, -1.0f == cimagf(fc13));

  double _Complex dc13 = dc10 * CMPLX(1.0, -2.0);
  ASSERT(1, 8.0 == creal(dc13));
  ASSERT(1, -1.0 == cimag(dc13));

  long double _Complex ldc13 = ldc10 * CMPLXL(1.0l, -2.0l);
  ASSERT(1, 8.0l == creall(ldc13));
  ASSERT(1, -1.0l == cimagl(ldc13));

  float _Complex fc14 = fc10 / CMPLXF(1.0f, -2.0f);
  ASSERT(1, -0.8f == crealf(fc14));
  ASSERT(1, 1.4f == cimagf(fc14));

  double _Complex dc14 = dc10 / CMPLX(1.0, -2.0);
  ASSERT(1, -0.8 == creal(dc14));
  ASSERT(1, 1.4 == cimag(dc14));

  long double _Complex ldc14 = ldc10 / CMPLXL(1.0l, -2.0l);
  ASSERT(1, -0.8l == creall(ldc14));
  ASSERT(1, 1.4l == cimagl(ldc14));

  printf("OK\n");
  return 0;
}
