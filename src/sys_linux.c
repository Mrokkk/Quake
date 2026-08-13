#define _GNU_SOURCE 1
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>

#include "quakedef.h"

qboolean			isDedicated;

int nostdout = 0;

char *basedir = ".";
char *cachedir = "/tmp";

cvar_t  sys_linerefresh = {"sys_linerefresh","0"};// set for entity display

// =======================================================================
// General routines
// =======================================================================

void Sys_DebugNumber(int y, int val)
{
}

/*
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);
	fprintf(stderr, "%s", text);

	Con_Print (text);
}

void Sys_Printf (char *fmt, ...)
{

    va_list     argptr;
    char        text[1024], *t_p;
    int         l, r;

	if (nostdout)
		return;

    va_start (argptr,fmt);
    vsprintf (text,fmt,argptr);
    va_end (argptr);

    l = strlen(text);
    t_p = text;

// make sure everything goes through, even though we are non-blocking
    while (l)
    {
        r = write (1, text, l);
        if (r != l)
            sleep (0);
        if (r > 0)
        {
            t_p += r;
            l -= r;
        }
    }

}
*/

static const char *colors[] = {
	/* COLOR_BLACK		*/ "\e[30m",
	/* COLOR_RED		*/ "\e[31m",
	/* COLOR_GREEN		*/ "\e[32m",
	/* COLOR_YELLOW		*/ "\e[33m",
	/* COLOR_BLUE		*/ "\e[34m",
	/* COLOR_CYAN		*/ "\e[35m",
	/* COLOR_MAGENTA	*/ "\e[36m",
	/* COLOR_WHITE		*/ "\e[37m",
	/* COLOR_ORANGE		*/ "\e[33m",
	/* COLOR_GREY		*/ "\e[38;5;245m",
};

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	const char	*p;

	va_start(argptr, fmt);
	vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);

	if (nostdout)
		return;

	for (p = text; *p; p++)
	{
		if (*p == Q_COLOR_ESCAPE)
		{
			fputs(colors[ColorIndex(*++p)], stdout);
		}
		else
		{
			putc(*p, stdout);
		}
	}
}

#if 0
static char end1[] =
"\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H                QUAKE: The Doomed Dimension \x1b[33mby \x1b[44mid\x1b[41m Software                      \x1b[2;1H  ----------------------------------------------------------------------------  \x1b[3;1H           CALL 1-800-IDGAMES TO ORDER OR FOR TECHNICAL SUPPORT                 \x1b[4;1H             PRICE: $45.00 (PRICES MAY VARY OUTSIDE THE US.)                    \x1b[5;1H                                                                                \x1b[6;1H  \x1b[37mYes! You only have one fourth of this incredible epic. That is because most   \x1b[7;1H   of you have paid us nothing or at most, very little. You could steal the     \x1b[8;1H   game from a friend. But we both know you'll be punished by God if you do.    \x1b[9;1H        \x1b[33mWHY RISK ETERNAL DAMNATION? CALL 1-800-IDGAMES AND BUY NOW!             \x1b[10;1H             \x1b[37mRemember, we love you almost as much as He does.                   \x1b[11;1H                                                                                \x1b[12;1H            \x1b[33mProgramming: \x1b[37mJohn Carmack, Michael Abrash, John Cash                \x1b[13;1H       \x1b[33mDesign: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits         \x1b[14;1H                     \x1b[33mArt: \x1b[37mAdrian Carmack, Kevin Cloud                           \x1b[15;1H               \x1b[33mBiz: \x1b[37mJay Wilbur, Mike Wilson, Donna Jackson                      \x1b[16;1H            \x1b[33mProjects: \x1b[37mShawn Green   \x1b[33mSupport: \x1b[37mBarrett Alexander                  \x1b[17;1H              \x1b[33mSound Effects: \x1b[37mTrent Reznor and Nine Inch Nails                   \x1b[18;1H  For other information or details on ordering outside the US, check out the    \x1b[19;1H     files accompanying QUAKE or our website at http://www.idsoftware.com.      \x1b[20;1H    \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.        \x1b[21;1H     All rights reserved. NIN logo is a registered trademark licensed to        \x1b[22;1H                 Nothing Interactive, Inc. All rights reserved.                 \x1b[40m\x1b[23;1H\x1b[0m";
static char end2[] =
"\x1b[?7h\x1b[40m\x1b[2J\x1b[0;1;41m\x1b[1;1H        QUAKE \x1b[33mby \x1b[44mid\x1b[41m Software                                                    \x1b[2;1H -----------------------------------------------------------------------------  \x1b[3;1H        \x1b[37mWhy did you quit from the registered version of QUAKE? Did the          \x1b[4;1H        scary monsters frighten you? Or did Mr. Sandman tug at your             \x1b[5;1H        little lids? No matter! What is important is you love our               \x1b[6;1H        game, and gave us your money. Congratulations, you are probably         \x1b[7;1H        not a thief.                                                            \x1b[8;1H                                                           Thank You.           \x1b[9;1H        \x1b[33;44mid\x1b[41m Software is:                                                         \x1b[10;1H        PROGRAMMING: \x1b[37mJohn Carmack, Michael Abrash, John Cash                    \x1b[11;1H        \x1b[33mDESIGN: \x1b[37mJohn Romero, Sandy Petersen, American McGee, Tim Willits        \x1b[12;1H        \x1b[33mART: \x1b[37mAdrian Carmack, Kevin Cloud                                        \x1b[13;1H        \x1b[33mBIZ: \x1b[37mJay Wilbur, Mike Wilson     \x1b[33mPROJECTS MAN: \x1b[37mShawn Green              \x1b[14;1H        \x1b[33mBIZ ASSIST: \x1b[37mDonna Jackson        \x1b[33mSUPPORT: \x1b[37mBarrett Alexander             \x1b[15;1H        \x1b[33mSOUND EFFECTS AND MUSIC: \x1b[37mTrent Reznor and Nine Inch Nails               \x1b[16;1H                                                                                \x1b[17;1H        If you need help running QUAKE refer to the text files in the           \x1b[18;1H        QUAKE directory, or our website at http://www.idsoftware.com.           \x1b[19;1H        If all else fails, call our technical support at 1-800-IDGAMES.         \x1b[20;1H      \x1b[0;41mQuake is a trademark of Id Software, inc., (c)1996 Id Software, inc.      \x1b[21;1H        All rights reserved. NIN logo is a registered trademark licensed        \x1b[22;1H             to Nothing Interactive, Inc. All rights reserved.                  \x1b[23;1H\x1b[40m\x1b[0m";

#endif
void Sys_Quit (void)
{
	Host_Shutdown();
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
#if 0
	if (registered.value)
		printf("%s", end2);
	else
		printf("%s", end1);
#endif
	fflush(stdout);
	exit(0);
}

void Sys_Init(void)
{
#if id386
	Sys_SetFPCW();
#endif
}

void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];

// change stdin to non blocking
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);
	Sys_StacktraceDump ();
	Host_Shutdown ();
	exit (1);

}

void Sys_Warn (char *warning, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,warning);
	vsprintf (string,warning,argptr);
	va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;

	if (stat (path,&buf) == -1)
		return -1;

	return buf.st_mtime;
}


void Sys_mkdir (char *path)
{
    mkdir (path, 0777);
}

int Sys_FileOpenRead (char *path, int *handle)
{
	int	h;
	struct stat	fileinfo;


	h = open (path, O_RDONLY, 0666);
	*handle = h;
	if (h == -1)
		return -1;

	if (fstat (h,&fileinfo) == -1)
		Sys_Error ("Error fstating %s", path);

	return fileinfo.st_size;
}

int Sys_FileOpenWrite (char *path)
{
	int     handle;

	umask (0);

	handle = open(path,O_RDWR | O_CREAT | O_TRUNC
	, 0666);

	if (handle == -1)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));

	return handle;
}

int Sys_FileWrite (int handle, void *src, int count)
{
	return write (handle, src, count);
}

void Sys_FileClose (int handle)
{
	close (handle);
}

void Sys_FileSeek (int handle, int position)
{
	lseek (handle, position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
    return read (handle, dest, count);
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    va_list argptr;
    static char data[1024];
    int fd;

    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
//    fd = open(file, O_WRONLY | O_BINARY | O_CREAT | O_APPEND, 0666);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}

void Sys_EditFile(char *filename)
{

	char cmd[256];
	char *term;
	char *editor;

	term = getenv("TERM");
	if (term && !strcmp(term, "xterm"))
	{
		editor = getenv("VISUAL");
		if (!editor)
			editor = getenv("EDITOR");
		if (!editor)
			editor = getenv("EDIT");
		if (!editor)
			editor = "vi";
		sprintf(cmd, "xterm -e %s %s", editor, filename);
		system(cmd);
	}

}

double Sys_FloatTime (void)
{
    struct timeval tp;
    struct timezone tzp;
    static int      secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase)
    {
        secbase = tp.tv_sec;
        return tp.tv_usec/1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec/1000000.0;
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void alarm_handler(int x)
{
	oktogo=1;
}

void Sys_LineRefresh(void)
{
}

void floating_point_exception_handler(int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

char *Sys_ConsoleInput(void)
{
    static char text[256];
    int     len;
	fd_set	fdset;
    struct timeval timeout;

	if (cls.state == ca_dedicated) {
		FD_ZERO(&fdset);
		FD_SET(0, &fdset); // stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
			return NULL;

		len = read (0, text, sizeof(text));
		if (len < 1)
			return NULL;
		text[len-1] = 0;    // rip off the /n and terminate

		return text;
	}
	return NULL;
}

#if !id386
void Sys_HighFPPrecision (void)
{
}

void Sys_LowFPPrecision (void)
{
}
#endif

#include <ucontext.h>

#ifdef USE_BACKTRACE

#include <backtrace.h>

static void BacktraceErrorCallback(void *data, const char *message, int error)
{
	if (error == -1)
	{
		Sys_Printf(S_COLOR_RED "Debug info missing\n");
		return;
	}

	Sys_Printf(S_COLOR_RED "Backtrace error %d: %s\n", error, message);
}

static int BacktraceCallback(void *data, uintptr_t pc, const char *pathname, int lineNumber, const char *function)
{
	int *index = (int *)(data);
	if (pathname != NULL || function != NULL)
	{
		Sys_Printf("#%-2u " S_COLOR_BLUE "%p " S_COLOR_WHITE "in " S_COLOR_YELLOW "%s " S_COLOR_WHITE "at " S_COLOR_GREEN "%s" S_COLOR_WHITE ":%d\n",
			*index,
			(void*)pc,
			function,
			pathname,
			lineNumber);
	}
	else
	{
		Sys_Printf("#%-2u " S_COLOR_BLUE "%p " S_COLOR_WHITE "in ??\n", *index, (void*)pc);
	}
	(*index)++;
	return 0;
}

static struct backtrace_state *backtrace_state;

#endif

static void SignalHandler(int sig, siginfo_t *info, void *context)
{
	ucontext_t *m = (ucontext_t*)context;

	fprintf(stderr, "Received SIG%s at %p\n",
		sigabbrev_np(sig),
#ifdef __x86_64__
		(void*)m->uc_mcontext.gregs[REG_RIP]
#elif defined(__i386__)
		(void*)m->uc_mcontext.gregs[REG_EIP]
#else
		NULL
#endif
	);

	fprintf(stderr, "Reason: ");

	switch (sig)
	{
		case SIGILL:
			switch (info->si_code)
			{
				case ILL_ILLOPC:
					fprintf(stderr, "illegal opcode\n");
					break;
				case ILL_ILLOPN:
					fprintf(stderr, "illegal operand\n");
					break;
				case ILL_ILLADR:
					fprintf(stderr, "illegal addressing mode\n");
					break;
				case ILL_ILLTRP:
					fprintf(stderr, "illegal trap\n");
					break;
				case ILL_PRVOPC:
					fprintf(stderr, "privileged opcode\n");
					break;
				case ILL_PRVREG:
					fprintf(stderr, "privileged register\n");
					break;
				case ILL_COPROC:
					fprintf(stderr, "coprocessor error\n");
					break;
				case ILL_BADSTK:
					fprintf(stderr, "internal stack error\n");
					break;
				default:
					goto unknown;
			}
			break;
		case SIGSEGV:
			switch (info->si_code)
			{
				case SEGV_MAPERR:
					fprintf(stderr, "%p not mapped to an object\n", info->si_addr);
					break;
				case SEGV_ACCERR:
					fprintf(stderr, "invalid permissions for object at %p\n", info->si_addr);
					break;
				case SEGV_BNDERR:
					fprintf(stderr, "failed address bound checks for %p\n", info->si_addr);
					break;
				case SEGV_PKUERR:
					fprintf(stderr, "access to %p denied by memory protection keys\n", info->si_addr);
					break;
				default:
					goto unknown;
			}
			break;
		case SIGBUS:
			switch (info->si_code)
			{
				case BUS_ADRALN:
					fprintf(stderr, "invalid address alignment at %p\n", info->si_addr);
					break;
				case BUS_ADRERR:
					fprintf(stderr, "nonexistent physical address %p\n", info->si_addr);
					break;
				case BUS_OBJERR:
					fprintf(stderr, "object-specific hardware error for address %p\n", info->si_addr);
					break;
				case BUS_MCEERR_AR:
					fprintf(stderr, "hardware memory error consumed on a machine check\n");
					break;
				case BUS_MCEERR_AO:
					fprintf(stderr, "hardware memory error detected in process but not consumed\n");
					break;
				default:
					goto unknown;
			}
			break;

		default:
			switch (info->si_code)
			{
				case SI_USER:
					fprintf(stderr, "user\n");
					break;
				case SI_KERNEL:
					fprintf(stderr, "kernel\n");
					break;
				unknown:
				default:
					fprintf(stderr, "unknown\n");
					break;
			}
	}

	Sys_StacktraceDump ();

	Host_Shutdown ();
}

void Sys_StacktraceDump(void)
{
#ifdef USE_BACKTRACE
	Sys_Printf("Backtrace:\n");
	int index = 0;
	backtrace_full(backtrace_state, 1, BacktraceCallback, BacktraceErrorCallback, (void *)&index);
#endif
}

int main (int c, char **v)
{
	double		time, oldtime, newtime;
	quakeparms_t parms;
	extern int vcrFile;
	extern int recording;
	int j;

#ifdef USE_BACKTRACE
	backtrace_state = backtrace_create_state(NULL, 0, BacktraceErrorCallback, NULL);
#endif

	{
		struct sigaction sa = {};
		sa.sa_handler	= SIG_IGN;
		sigaction(SIGINT, &sa, NULL);

		sa.sa_sigaction	= &SignalHandler;
		sa.sa_flags		= SA_RESETHAND | SA_SIGINFO;
		sigaction(SIGINT,	&sa, NULL);
		sigaction(SIGSEGV,	&sa, NULL);
		sigaction(SIGBUS,	&sa, NULL);
		sigaction(SIGILL,	&sa, NULL);
		sigaction(SIGFPE,	&sa, NULL);
		sigaction(SIGABRT,	&sa, NULL);
		sigaction(SIGTERM,	&sa, NULL);
	}

	memset(&parms, 0, sizeof(parms));

	COM_InitArgv(c, v);
	parms.argc = com_argc;
	parms.argv = com_argv;

#ifdef GLQUAKE
	parms.memsize = 16*1024*1024;
#else
	parms.memsize = 8*1024*1024;
#endif

	j = COM_CheckParm("-mem");
	if (j)
		parms.memsize = (int) (Q_atof(com_argv[j+1]) * 1024 * 1024);
	parms.membase = malloc (parms.memsize);

	parms.basedir = basedir;
// caching is disabled by default, use -cachedir to enable
//	parms.cachedir = cachedir;

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

    Host_Init(&parms);

	Sys_Init();

	if (COM_CheckParm("-nostdout"))
		nostdout = 1;
	else {
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
		printf ("Linux Quake -- Version %0.3f\n", LINUX_VERSION);
	}

    oldtime = Sys_FloatTime () - 0.1;
    while (1)
    {
// find time spent rendering last frame
        newtime = Sys_FloatTime ();
        time = newtime - oldtime;

        if (cls.state == ca_dedicated)
        {   // play vcrfiles at max speed
            if (time < sys_ticrate.value && (vcrFile == -1 || recording) )
            {
				usleep(1);
                continue;       // not time to run a server only tic yet
            }
            time = sys_ticrate.value;
        }

        if (time > sys_ticrate.value*2)
            oldtime = newtime;
        else
            oldtime += time;

        Host_Frame (time);

// graphic debugging aids
        if (sys_linerefresh.value)
            Sys_LineRefresh ();
    }

}


/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{

	int r;
	unsigned long addr;
	int psize = getpagesize();

	addr = (startaddr & ~(psize-1)) - psize;

//	fprintf(stderr, "writable code %lx(%lx)-%lx, length=%lx\n", startaddr,
//			addr, startaddr+length, length);

	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);

	if (r < 0)
		Sys_Error("Protection change failed\n");

}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
