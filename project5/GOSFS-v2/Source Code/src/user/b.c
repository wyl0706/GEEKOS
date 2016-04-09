/*
 * A test program for GeekOS user mode
 */

#include <conio.h>
#include <process.h>
#include <fileio.h>
#include <string.h>


int main(int argc, char** argv)
{
    int i;
    Print_String("I am the b program\n");
	Print("argc is %d\n", argc);
    for (i = 0; i < argc; ++i) {
	Print("Arg %d is %s\n", i,argv[i]);
    }

    return 1;
}
