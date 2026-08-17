#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "quakedef.h"

#define BASEDIR "."

qboolean isDedicated;
cvar_t  sys_linerefresh = {"sys_linerefresh","0"};// set for entity display

static int nostdout = 0;

static const char *colors[] = {
	/* COLOR_BLACK		*/ "\e[30m",
	/* COLOR_RED		*/ "\e[31m",
	/* COLOR_GREEN		*/ "\e[32m",
	/* COLOR_YELLOW		*/ "\e[33m",
	/* COLOR_BLUE		*/ "\e[34m",
	/* COLOR_CYAN		*/ "\e[35m",
	/* COLOR_MAGENTA	*/ "\e[36m",
	/* COLOR_WHITE		*/ "\e[37m",
	/* COLOR_GREY		*/ "\e[38;5;245m",
	/* COLOR_RESET		*/ "\e[0m",
};

// =======================================================================
// General routines
// =======================================================================

static void Sys_Print (const char *s)
{
	qboolean con_color = false;

	for (; *s; s++)
	{
		switch (*s)
		{
			case CON_COLOR_1:
			case CON_COLOR_2:
				fputs(colors[3], stdout);
				con_color = true;
				break;

			case CON_BAR_LEFT:
				putc('<', stdout);
				break;

			case CON_BAR_MIDDLE:
				putc('=', stdout);
				break;

			case CON_BAR_RIGHT:
				putc('>', stdout);
				break;

			case Q_COLOR_ESCAPE:
				fputs(colors[ColorIndex(*++s)], stdout);
				break;

			default:
				putc(*s, stdout);
		}
	}

	if (con_color)
	{
		fputs(colors[ColorIndex(COLOR_RESET)], stdout);
	}
}

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	if (nostdout)
	{
		return;
	}

	va_start(argptr, fmt);
	Q_vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);

	Sys_Print(text);
}

void Sys_DPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	if (nostdout || !developer.value)
	{
		return;
	}

	va_start(argptr, fmt);
	Q_vsnprintf(text, sizeof(text), fmt, argptr);
	va_end(argptr);

	Sys_Print(text);
}

void Sys_Quit (void)
{
	Host_Shutdown();
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
	fflush(stdout);
	exit(0);
}

void Sys_Init(void)
{
}

Q_NORETURN void Sys_Error (char *error, ...)
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
int Sys_FileTime (char *path)
{
	struct stat buf;

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
	int			h;
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
	int handle;

	umask (0);

	handle = open(path,O_RDWR | O_CREAT | O_TRUNC, 0666);

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
	int		fd;
	va_list	argptr;
	char	data[1024];

	va_start(argptr, fmt);
	Q_vsnprintf(data, sizeof(data), fmt, argptr);
	va_end(argptr);
	fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
	write(fd, data, strlen(data));
	close(fd);
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

void Sys_LineRefresh(void)
{
}

char *Sys_ConsoleInput(void)
{
	static char text[256];
	int				len;
	fd_set			fdset;
	struct timeval	timeout;

	if (cls.state == ca_dedicated)
	{
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

void Sys_HighFPPrecision (void)
{
}

void Sys_LowFPPrecision (void)
{
}

#include <ucontext.h>

#ifdef USE_BACKTRACE

#include <backtrace.h>

static void BacktraceErrorCallback(void *data, const char *message, int error)
{
	Q_UNUSED(data);

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
		Sys_Printf("#%-2u " S_COLOR_BLUE "%p " S_COLOR_RESET "in " S_COLOR_YELLOW "%s " S_COLOR_RESET "at " S_COLOR_GREEN "%s" S_COLOR_RESET ":%d\n",
			*index,
			(void*)pc,
			function,
			pathname,
			lineNumber);
	}
	else
	{
		Sys_Printf("#%-2u " S_COLOR_BLUE "%p " S_COLOR_RESET "in ??\n", *index, (void*)pc);
	}
	(*index)++;
	return 0;
}

static struct backtrace_state *backtrace_state;

#endif

#ifdef __linux__
#	define SIGNAME(s)	sigabbrev_np(s)
#elif __unix__
#	define SIGNAME(s)	sys_signame[s]
#else
#	define SIGNAME(s)	"(unknown)"
#endif

static const char *GetGenericReason(const siginfo_t *info)
{
	switch (info->si_code)
	{
		case SI_USER:	return "user";
		case SI_KERNEL:	return "kernel";
		default:		return "unknown";
	}
}

static void SignalHandler(int sig, siginfo_t *info, void *context)
{
	Q_UNUSED(context && context);
	Sys_Printf(S_COLOR_CYAN "Received SIG%s" S_COLOR_RESET "\nReason: %s\n", SIGNAME(sig), GetGenericReason(info));
	Sys_Quit();
}

static void CrashSignalHandler(int sig, siginfo_t *info, void *context)
{
	Q_UNUSED(context);

	Sys_Printf(S_COLOR_RED "Received SIG%s" S_COLOR_RESET "\nReason: ", SIGNAME(sig));

	switch (sig)
	{
		case SIGILL:
			switch (info->si_code)
			{
				case ILL_ILLOPC:
					Sys_Printf("illegal opcode\n");
					break;
				case ILL_ILLOPN:
					Sys_Printf("illegal operand\n");
					break;
				case ILL_ILLADR:
					Sys_Printf("illegal addressing mode\n");
					break;
				case ILL_ILLTRP:
					Sys_Printf("illegal trap\n");
					break;
				case ILL_PRVOPC:
					Sys_Printf("privileged opcode\n");
					break;
				case ILL_PRVREG:
					Sys_Printf("privileged register\n");
					break;
				case ILL_COPROC:
					Sys_Printf("coprocessor error\n");
					break;
				case ILL_BADSTK:
					Sys_Printf("internal stack error\n");
					break;
				default:
					goto unknown;
			}
			break;
		case SIGSEGV:
			switch (info->si_code)
			{
				case SEGV_MAPERR:
					Sys_Printf("%p not mapped to an object\n", info->si_addr);
					break;
				case SEGV_ACCERR:
					Sys_Printf("invalid permissions for object at %p\n", info->si_addr);
					break;
#ifdef __linux__
				case SEGV_BNDERR:
					Sys_Printf("failed address bound checks for %p\n", info->si_addr);
					break;
				case SEGV_PKUERR:
					Sys_Printf("access to %p denied by memory protection keys\n", info->si_addr);
					break;
#endif
				default:
					goto unknown;
			}
			break;
		case SIGBUS:
			switch (info->si_code)
			{
				case BUS_ADRALN:
					Sys_Printf("invalid address alignment at %p\n", info->si_addr);
					break;
				case BUS_ADRERR:
					Sys_Printf("nonexistent physical address %p\n", info->si_addr);
					break;
				case BUS_OBJERR:
					Sys_Printf("object-specific hardware error for address %p\n", info->si_addr);
					break;
#ifdef __linux__
				case BUS_MCEERR_AR:
					Sys_Printf("hardware memory error consumed on a machine check\n");
					break;
				case BUS_MCEERR_AO:
					Sys_Printf("hardware memory error detected in process but not consumed\n");
					break;
#endif
				default:
					goto unknown;
			}
			break;

		default:
		unknown:
			Sys_Printf("%s\n", GetGenericReason(info));
	}

	Sys_StacktraceDump();

	Host_Shutdown();
}

void Sys_StacktraceDump(void)
{
#ifdef USE_BACKTRACE
	Sys_Printf("Backtrace:\n");
	int index = 0;
	backtrace_full(backtrace_state, 1, BacktraceCallback, BacktraceErrorCallback, (void *)&index);
#endif
}

Q_NORETURN int main (int c, char **v)
{
	double		time, oldtime, newtime;
	quakeparms_t parms;
	extern int vcrFile;
	extern int recording;
	int j;

#ifdef USE_BACKTRACE
	backtrace_state = backtrace_create_state(NULL, 0, BacktraceErrorCallback, NULL);
#endif

	// install signal handlers
	{
		struct sigaction sa = {};
		sigfillset(&sa.sa_mask);

		sa.sa_handler		= SIG_IGN;
		sigaction(SIGINT,	&sa, NULL);
		sigaction(SIGTSTP,	&sa, NULL);
		sigaction(SIGUSR1,	&sa, NULL);
		sigaction(SIGUSR2,	&sa, NULL);
		sigaction(SIGIO,	&sa, NULL);

		sa.sa_sigaction		= &SignalHandler;
		sa.sa_flags			= SA_RESETHAND | SA_SIGINFO;
		sigaction(SIGHUP,	&sa, NULL);
		sigaction(SIGPIPE,	&sa, NULL);
		sigaction(SIGTERM,	&sa, NULL);
		sigaction(SIGALRM,	&sa, NULL);
		sigaction(SIGQUIT,	&sa, NULL);

		sa.sa_sigaction		= &CrashSignalHandler;
		sigaction(SIGSEGV,	&sa, NULL);
		sigaction(SIGBUS,	&sa, NULL);
		sigaction(SIGILL,	&sa, NULL);
		sigaction(SIGFPE,	&sa, NULL);
		sigaction(SIGABRT,	&sa, NULL);
	}

	memset(&parms, 0, sizeof(parms));

	COM_InitArgv(c, v);
	parms.argc = com_argc;
	parms.argv = com_argv;

	if (COM_CheckParm("-nostdout"))
	{
		nostdout = 1;
	}
	else
	{
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
		printf ("Linux Quake -- Version %0.3f\n", LINUX_VERSION);
	}

	j = COM_CheckParm("-mem");

	parms.memsize = j
		? (int)(Q_atof(com_argv[j + 1]) * 1024 * 1024)
		: 384*1024*1024;
	parms.membase = malloc (parms.memsize);

	parms.basedir = BASEDIR;
	// caching is disabled by default, use -cachedir to enable
	//	parms.cachedir = cachedir;

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	Host_Init(&parms);

	Sys_Init();

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

		if (time > sys_ticrate.value * 2)
			oldtime = newtime;
		else
			oldtime += time;

		Host_Frame (time);

		// graphic debugging aids
		if (sys_linerefresh.value)
			Sys_LineRefresh ();
	}
}

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
