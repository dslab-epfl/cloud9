float powif(float v, int e)
{
  if (e == 0)
    return 1;
  float res = 1;
  int pos_e = e;
  if(e < 0)
    pos_e = -e;
  unsigned long bit_flag = 1;
  while(bit_flag <= pos_e)
  {
    if(bit_flag & pos_e)
      res *= v;
    v *= v;
    bit_flag <<= 1;
  }
  if(e < 0)
    return 1/res;
  else
    return res;
}

double powi(double v, int e)
{
  if (e == 0)
    return 1;
  double res = 1;
  int pos_e = e;
  if(e < 0)
    pos_e = -e;
  unsigned long bit_flag = 1;
  while(bit_flag <= pos_e)
  {
    if(bit_flag & pos_e)
      res *= v;
    v *= v;
    bit_flag <<= 1;
  }
  if(e < 0)
    return 1/res;
  else
    return res;
}
