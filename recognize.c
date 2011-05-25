/*
 *  Password managing proxy
 *  Search for password
 *
 *  See http://qaa.ath.cx/pproxy.html
 *
 *  (c) Copyright 2005, Christopher J. McKenzie under the terms of the
 *      GNU Public License, incorporated herein by reference
 *
 */

/* Not interested in parsing or tokenizing, just finding the phrase pass*/
/* Assumes null termination */
char search(unsigned char**in)
{	unsigned char*ptr_in=(*in);
	char	*match="pass",
		*ptr=match;
	while(ptr_in)
	{	*ptr_in|=0x20;	//Convert to lowercase
		if(*ptr_in==*ptr)
		{	ptr++;
		}else
		if(*ptr==0)
		{	printf("Matched!\n");
			ptr=match;
		}else
			ptr=match;
		ptr_in++;
	}
	return(1);
	return(0);
}
