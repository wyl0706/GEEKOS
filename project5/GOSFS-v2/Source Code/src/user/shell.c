/*
 * A really, really simple shell program
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.18 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <conio.h>
#include <process.h>
#include <string.h>
#include <fileio.h> // ERROR: for Open_Dir

#define BUFSIZE 79
#define DEFAULT_PATH "/c:/a"
#define GOSFS_ROOT "/d/"

#define INFILE	0x1
#define OUTFILE	0x2
#define PIPE	0x4

#define ISSPACE(c) ((c) == ' ' || (c) == '\t')

struct Process {
    int flags;
    char program[BUFSIZE+1];
    char infile[BUFSIZE+1];
    char outfile[BUFSIZE+1];
    char *command;
    int pid;
    int readfd, writefd;
    int pipefd;
};

char *Strip_Leading_Whitespace(char *s);
void Trim_Newline(char *s);
char *Copy_Token(char *token, char *s);
int Build_Pipeline(char *command, struct Process procList[]);
void Spawn_Single_Command(struct Process procList[], int nproc, const char *path);
int Patch_Path(char *command, char *currentPath);
int FS_Ops_Path_Patch(char *fsopName, char *command, char *currentPath);
int Change_Dir(char *dirString, char *currentPath);
int Patch_Cat(char *command, char *currentPath);
int Patch_Cp(char *command, char *currentPath);

/* Maximum number of processes allowed in a pipeline. */
#define MAXPROC 5

int exitCodes = 0;

int main(int argc, char **argv)
{
    int nproc;
    char commandBuf[BUFSIZE+1];
    struct Process procList[MAXPROC];
    char path[BUFSIZE+1] = DEFAULT_PATH;
    char *command;
    char currentPath[1024+79] = GOSFS_ROOT;

    /* Set attribute to gray on black. */
    Print("\x1B[37m");

    while (true) {
	/* Print shell prompt (bright cyan on black background) */
	Print("\x1B[1;36m$\x1B[37m ");

	/* Read a line of input */
	Read_Line(commandBuf, sizeof(commandBuf));
	command = Strip_Leading_Whitespace(commandBuf);
	Trim_Newline(command);

	/*
	 * Handle some special commands
	 */
	if (strcmp(command, "exit") == 0) {
	    /* Exit the shell */
	    Sync(); // first sync the fs
	    break;
	} else if (strcmp(command, "pid") == 0) {
	    /* Print the pid of this process */
	    Print("%d\n", Get_PID());
	    continue;
	} else if (strcmp(command, "exitCodes") == 0) {
	    /* Print exit codes of spawned processes. */
	    exitCodes = 1;
	    continue;
	} else if (strncmp(command, "path=", 5) == 0) {
	    /* Set the executable search path */
	    strcpy(path, command + 5);
	    continue;
	} else if (strcmp(command, "") == 0) {
	    /* Blank line. */
	    continue;
	}

	Print("test shell command:%s\n", command);
	/*
	 * Parse the command string and build array of
	 * Process structs representing a pipeline of commands.
	 */
	nproc = Build_Pipeline(command, procList);
	if (nproc <= 0)
	    continue;

	nproc = FS_Ops_Path_Patch(procList[0].program, procList[0].command, currentPath);
	if (nproc < 0)
	{
	    Print("ERROR:no%d,%s\n", nproc, Get_Error_String(nproc));
	    continue;
	}
	// take chdir as a special case
	if (strcmp(procList[0].program, "chdir")==0 || strcmp(procList[0].program, "/c/chdir.exe")==0)
		continue;

	Print("after patch:%s\n", procList[0].command);
	Spawn_Single_Command(procList, nproc, path);
    }

    Print_String("DONE!\n");
    return 0;
}

/*
 * Skip leading whitespace characters in given string.
 * Returns pointer to first non-whitespace character in the string,
 * which may be the end of the string.
 */
char *Strip_Leading_Whitespace(char *s)
{
    while (ISSPACE(*s))
	++s;
    return s;
}

/*
 * Destructively trim newline from string
 * by changing it to a nul character.
 */
void Trim_Newline(char *s)
{
    char *c = strchr(s, '\n');
    if (c != 0)
	*c = '\0';
}

/*
 * Copy a single token from given string.
 * If a token is found, returns pointer to the
 * position in the string immediately past the token:
 * i.e., where parsing for the next token can begin.
 * If no token is found, returns null.
 */
char *Copy_Token(char *token, char *s)
{
    char *t = token;

    while (ISSPACE(*s))
	++s;
    while (*s != '\0' && !ISSPACE(*s))
	*t++ = *s++;
    *t = '\0';

    return *token != '\0' ? s : 0;
}

/*
 * Build process pipeline.
 */
int Build_Pipeline(char *command, struct Process procList[])
{
    int nproc = 0, i;

    while (nproc < MAXPROC) {
        struct Process *proc = &procList[nproc];
        char *p, *s;

        proc->flags = 0;

        command = Strip_Leading_Whitespace(command);
        p = command;

        if (strcmp(p, "") == 0)
	    break;

        ++nproc;

        s = strpbrk(p, "<>|");

        /* Input redirection from file? */
        if (s != 0 && *s == '<') {
	    proc->flags |= INFILE;
	    *s = '\0';
	    p = s+1;
	    s = Copy_Token(proc->infile, p);
	    if (s == 0) {
	        Print("Error: invalid input redirection\n");
	        return -1;
	    }
	    p = s;

	    /* Output redirection still allowed for this command. */
	    p = Strip_Leading_Whitespace(p);
	    s = (*p == '>' || *p == '|') ? p : 0;
        }

        /* Output redirection to file or pipe? */
        if (s != 0 && (*s == '>' || *s == '|')) {
	    bool outfile = (*s == '>');
	    proc->flags |= (outfile ? OUTFILE : PIPE);
	    *s = '\0';
	    p = s+1;
	    if (outfile) {
	        s = Copy_Token(proc->outfile, p);
	        if (s == 0) {
		    Print("Error: invalid output redirection\n");
		    return -1;
	        }
	        p = s;
	    }
        }

        proc->command = command;
        Print("command=%s\n", command);
        if (!Copy_Token(proc->program, command)) {
	    Print("Error: invalid command\n");
	    return -1;
        }

        if (p == command)
	    command = "";
        else
	    command = p;
    }

    if (strcmp(command,"") != 0) {
	Print("Error: too many commands in pipeline\n");
	return -1;
    }

#if 0
    for (i = 0; i < nproc; ++i) {
        struct Process *proc = &procList[i];
        Print("program=%s, command=\"%s\"\n", proc->program, proc->command);
        if (proc->flags & INFILE)
	    Print("\tinfile=%s\n", proc->infile);
        if (proc->flags & OUTFILE)
	    Print("\toutfile=%s\n", proc->outfile);
        if (proc->flags & PIPE)
	    Print("\tpipe\n");
    }
#endif

    /*
     * Check commands for validity
     */
    for (i = 0; i < nproc; ++i) {
	struct Process *proc = &procList[i];
	if (i > 0 && (proc->flags & INFILE)) {
	    Print("Error: input redirection only allowed for first command\n");
	    return -1;
	}
	if (i < nproc-1 && (proc->flags & OUTFILE)) {
	    Print("Error: output redirection only allowed for last command\n");
	    return -1;
	}
	if (i == nproc-1 && (proc->flags & PIPE)) {
	    Print("Error: unterminated pipeline\n");
	    return -1;
	}
    }

    return nproc;
}

/*
 * Spawn a single command.
 */
void Spawn_Single_Command(struct Process procList[], int nproc, const char *path)
{
    int pid;

    if (nproc > 1) {
	Print("Error: pipes not supported yet\n");
	return;
    }
    if (procList[0].flags & (INFILE|OUTFILE)) {
	Print("Error: I/O redirection not supported yet\n");
	return;
    }

	Print("program:%s\ncommand%s\npath%s\n", procList[0].program, procList[0].command,
	path );
    pid = Spawn_With_Path(procList[0].program, procList[0].command,
	path);
    if (pid < 0)
	Print("Could not spawn process: %s\n", Get_Error_String(pid));
    else {
	int exitCode = Wait(pid);
	if (exitCodes)
	    Print("Exit code was %d\n", exitCode);
    }
}

int Patch_Path(char *command, char *currentPath)
{

	Print("command:%s\n", command);
	Print("currentPath:%s\n", currentPath);
	char *pstr = strchr(command, ' ');
	char *temp = NULL;
	if (pstr == 0)
		return EINVALID;

	pstr = Strip_Leading_Whitespace(pstr);
	Print("args:%s\n", pstr);
	if(*pstr == '/' && (*(pstr+1) == 'd' || *(pstr+1) == 'c')) // '/' indicate it's a direct path
		return 0;
	else if(*pstr == '/' && *(pstr+1) != 'd')
		return EINVALID;
	else if(*pstr != '/' )
	{
		int pathLen = strlen(currentPath);
		int argLen = strlen(pstr)+1; // one for '\0'
		Print("pl:%d, al:%d\n", pathLen, argLen);
		if( (strlen(command) + pathLen) < BUFSIZE )
		{
			pstr += argLen-1;
			// ERROR: pstr = strchr(pstr, '\0');
			temp = pstr + pathLen;
			//Print("tmp:%s\n", temp);
			while(argLen)
			{
				*temp = *pstr;
				//Print("%d,%c", argLen, *pstr);
				temp--; pstr--;
				argLen--;
			}

			pstr = currentPath + pathLen - 1;
			while(pathLen)
			{
				*temp = *pstr;
				temp--; pstr--;
				pathLen--;
			}
		}
		else
			return ENAMETOOLONG;
	}


	return 0;
}

int FS_Ops_Path_Patch(char *fsopName, char *command, char *currentPath)
{
	int rc = 0;
	// 1 ls
	if(strcmp("ls", fsopName)==0 || strcmp("/c/ls.exe", fsopName)==0)
		goto patch;
	// 2 mkdir
	else if (strcmp("mkdir", fsopName)==0 || strcmp("/c/mkdir.exe", fsopName)==0)
		goto patch;
	// 3 rmdir
	else if (strcmp("rmdir", fsopName)==0 || strcmp("/c/rmdir.exe", fsopName)==0)
		goto patch;
	// 4 chdir
	else if (strcmp("chdir", fsopName)==0 || strcmp("/c/chdir.exe", fsopName)==0)
	{
		rc = Change_Dir(command, currentPath);
		if(rc < 0)
			return rc;
		
		goto patch;
	}
	// 5 mkfile
	else if (strcmp("mkfile", fsopName)==0 || strcmp("/c/mkfile.exe", fsopName)==0)
		goto patch;
	// 6 rmfile
	else if (strcmp("rmfile", fsopName)==0 || strcmp("/c/rmfile.exe", fsopName)==0)
		goto patch;
	// 7 wrtfile
	else if (strcmp("wrtfile", fsopName)==0 || strcmp("/c/wrtfile.exe", fsopName)==0)
		goto patch;
	// 8 rdfile
	else if (strcmp("rdfile", fsopName)==0 || strcmp("/c/rdfile.exe", fsopName)==0)
		goto patch;
	// 9 cat
	else if (strcmp("cat", fsopName)==0 || strcmp("/c/cat.exe", fsopName)==0)
	{
		Patch_Cat(command, currentPath);
		goto done;
	}
	// 10 cp
	else if (strcmp("cp", fsopName)==0 || strcmp("/c/cp.exe", fsopName)==0)
	{
		Patch_Cp(command, currentPath);
		goto done;
	}
	// 11 type
	else if (strcmp("type", fsopName)==0 || strcmp("/c/type.exe", fsopName)==0)
		goto patch;

	else
		goto done;

patch:
	rc = Patch_Path(command, currentPath);		
done:
	return rc;
}


// pass the direct path to command string
// change the currentPath to the direct path passed to the cmd string
int Change_Dir(char *command, char *currentPath)
{
	char *dirString = strchr(command, ' ');
	dirString = Strip_Leading_Whitespace(dirString);
	char *pPath = currentPath;
	int dirStrLen = strlen(dirString);
	int fd = 0;
	
	if (dirStrLen > 1024 + 79)
		return ENAMETOOLONG;

	//check if the path is valid
	fd = Open_Directory(dirString);
	if (fd < 0)
		return ENOTDIR;
	Close(fd);

	// change the currentPath
	while(dirStrLen)
	{
		*pPath = *dirString;
		pPath++; dirString++;
		dirStrLen--;
	}
	*pPath++ = '/';
	*pPath = '\0';
	return dirStrLen;
}

int Patch_Cat(char * command, char * currentPath)
{
	char *pStr = command;
	int rc = 0;

	// patch the arguments one by one
	while(pStr != 0)
	{
		rc = Patch_Path(pStr, currentPath);
		if (rc < 0)
			return EUNSPECIFIED;

		// skip the patched argument; the first argument skipped is the name of the exefile
		pStr = strchr(pStr, ' ');
		pStr = Strip_Leading_Whitespace(pStr);
		pStr = strchr(pStr, ' ');
	}

	return rc;
}

int Patch_Cp(char * command, char * currentPath)
{
	return Patch_Cat(command, currentPath);
}

