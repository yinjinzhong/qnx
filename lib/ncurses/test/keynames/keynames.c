/*
 * $Id: keynames.c 153052 2007-11-02 21:10:56Z coreos $
 */

#include <test.priv.h>

int main(int argc, char *argv[])
{
	int n;
	for (n = -1; n < 512; n++) {
		printf("%d(%5o):%s\n", n, n, keyname(n));
	}
	return EXIT_SUCCESS;
}
