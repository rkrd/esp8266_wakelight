void print_tm(struct tm *t)
{
	printf("%04d-%02d-%02d %d %02d:%02d:%02d\n",
			1900 + t->tm_year,
			t->tm_mon,
			t->tm_mday,
			t->tm_wday,
			t->tm_hour,
			t->tm_min,
			t->tm_sec);
}

