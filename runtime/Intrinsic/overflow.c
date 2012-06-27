#include <sys/types.h>
#include <stdbool.h>

#define max(a,b) ((a>b) ? a : b)

typedef struct{
  u_int16_t v;
  bool c;
}u_int16_carry;

u_int16_carry uadds(u_int16_t a, u_int16_t b)
{
  u_int16_carry res;
  res.v = a+b;
  res.c = (res.v < max(a,b));
  return res;
}

typedef struct{
  u_int32_t v;
  bool c;
}u_int32_carry;

u_int32_carry uadd(u_int32_t a, u_int32_t b)
{
  u_int32_carry res;
  res.v = a+b;
  res.c = (res.v < max(a,b));
  return res;
}

typedef struct{
  u_int64_t v;
  bool c;
}u_int64_carry;

u_int64_carry uaddl(u_int64_t a, u_int64_t b)
{
  u_int64_carry res;
  res.v = a+b;
  res.c = (res.v < max(a,b));
  return res;
}

u_int64_carry umull(u_int64_t a, u_int64_t b)
{
  u_int64_carry res;
  res.v = a*b;
  if (!a || !b)
    res.c = 0;
  else
    res.c = (res.v / b != a);

  return res;
}
