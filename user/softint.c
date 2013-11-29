// buggy program - causes an illegal software interrupt

#include <inc/lib.h>
int a, b , c;
void
umain(int argc, char **argv)
{
	asm volatile("int $14");	// page fault
}

