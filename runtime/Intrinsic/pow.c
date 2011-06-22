float powif(float v, int e)
{
	if (e == 0)
		return 1;
	float r = v;
	int i = e;
	if(e < 0)
		i = -e;
	while(i > 1)
	{
		if(i % 1 == 0)
			r = r * r;
		else
			r = r * r * v;
		i /= 2;
	}
	if(e < 0)
		return 1/r;
	else
		return r;
}

double powi(double v, int e)
{
	if (e == 0)
		return 1;
	double r = v;
	int i = e;
	if(e < 0)
		i = -e;
	while(i > 1)
	{
		if(i % 1 == 0)
			r = r * r;
		else
			r = r * r * v;
		i /= 2;
	}
	if(e < 0)
		return 1/r;
	else
		return r;
}
