/*      Copyright (C) 1989, 1991, California Institute of Technology.
		U. S. Government Sponsorship under NASA Contract NAS7-918
		is acknowledged.        */

/*
 * $Log:	SUNtime.c,v $
 * Revision 1.2  91/07/17  15:07:01  judy
 * New copyright notice.
 * 
 * Revision 1.1  91/07/09  12:48:55  steve
 * Initial revision
 * 
 */

/*
 *      Since Unix counts in micro-seconds and node_cputime is 32 bits
 *      The running time overflows at about 70 minutes and if we are not
 *      careful will turn negative at 35 minutes.
 *
 *      An industrial strength counter would have to count in larger units.
 *      Hence the constant #define TICKS_SHIFT: (in machdep.h)
 *
 *      TICKS_SHIFT     micro-seconds per tick    time until overflow
 *      -----------     ----------------------    -------------------
 *          0                    1                4295- secs or 71+ mins
 *          1                    2                 143+ mins or  2.4- hrs
 *          2                    4                   4.8- hrs
 *          3                    8                   9.5+ hrs
 *          4                   16                  19.1- hrs or 0.8- days
 *         etc.
 */

#include "twcommon.h"
#include "twsys.h"
#include "machdep.h"
#include <errno.h>

#ifndef TICKS_SHIFT
#define TICKS_SHIFT 0
#endif

#define DEFAULT_SECONDS 365 * 86400 /* seconds in a year */
#define DEFAULT_MICROSECONDS 0

extern unsigned int node_cputime;

static struct itimerval last_timer;
static unsigned int delta_piece;

#ifdef PARANOID
static unsigned int last_cputime;
#endif

void MicroAlarmSigFun()
{
	register int time, delta;
	extern int timed_out;

	timed_out = 1;

	time = last_timer.it_value.tv_sec;
	delta = last_timer.it_value.tv_usec;

	if ( time )
	{
		delta += 1000000 * time;
	}

	delta >>= TICKS_SHIFT;
	node_cputime += delta;

	delta_piece += delta;

	last_timer.it_value.tv_sec = DEFAULT_SECONDS;
	last_timer.it_value.tv_usec = DEFAULT_MICROSECONDS;
}

struct sigvec alarmvec = { MicroAlarmSigFun, 0, SV_ONSTACK };

SunMicroTimeInit ()
{
	sigvec ( SIGALRM, &alarmvec, 0 );

	last_timer.it_interval.tv_sec = DEFAULT_SECONDS;
	last_timer.it_interval.tv_usec = DEFAULT_MICROSECONDS;
	last_timer.it_value.tv_sec = DEFAULT_SECONDS;
	last_timer.it_value.tv_usec = DEFAULT_MICROSECONDS;

	setitimer ( ITIMER_REAL, &last_timer, 0 );

	node_cputime = 0;

#ifdef PARANOID
	last_cputime = node_cputime;
#endif
/*
 *      Other machine timers are sychronized by this setitimer, which
 *      means they cannot be use here. But see `check_timeouts'.
 */
}

unsigned int MicroTime ()
{
	register int time, delta;
	struct itimerval this_timer;

	sigblock ( sigmask ( SIGALRM ) );

	getitimer ( ITIMER_REAL, &this_timer );

	time = last_timer.it_value.tv_sec - this_timer.it_value.tv_sec;
	delta = last_timer.it_value.tv_usec - this_timer.it_value.tv_usec;

	if ( time )
	{
		delta += 1000000 * time;
	}

	last_timer = this_timer;

	delta >>= TICKS_SHIFT;

	node_cputime += delta;

	if ( delta_piece )
	{
		delta += delta_piece;
		delta_piece = 0;
	}

#ifdef PARANOID
	if ( node_cputime < last_cputime )
	{
		printf ( "node_cputime wrap around\n" );
		tester();
	}

	last_cputime = node_cputime;

	if ( delta > 100000000 )
	{
		printf ( "delta > 100 second\n" );
		tester();
	}
#endif

	sigsetmask ( 0 );

	return delta;
}

static struct itimerval alarm_timer;
SetMicroAlarm ( ms )
int ms;
{
	register int time, delta;
	struct itimerval alarm_timer;
	struct itimerval this_timer;

	sigblock ( sigmask ( SIGALRM ) );

	alarm_timer.it_interval.tv_sec = DEFAULT_SECONDS;
	alarm_timer.it_interval.tv_usec = DEFAULT_MICROSECONDS;

	alarm_timer.it_value.tv_sec = ( ms / 1000 );
	alarm_timer.it_value.tv_usec = 1000 * ( ms % 1000 );
	/* setitimer bombs if tv_usec is >= 1,000,00 */

	if  ( setitimer ( ITIMER_REAL, &alarm_timer, &this_timer ) )
	{
		perror ("setitimer");
	}

	time = last_timer.it_value.tv_sec - this_timer.it_value.tv_sec;
	delta = last_timer.it_value.tv_usec - this_timer.it_value.tv_usec;

	if ( time )
	{
		delta += 1000000 * time;
	}

	last_timer = alarm_timer;

	delta >>= TICKS_SHIFT;
	node_cputime += delta;

	delta_piece += delta;

	sigsetmask ( 0 );

}

/* sort of stolen from itimer.c in the simulator */
/*
 * There are two advantages to this routine over the MicroTime:
 *	1. It measures time in this process only, not wall clock time.
 *  2. It doesn't need the sigblock, sigsetmask calls since it uses
 *     a different clock.
 */

static struct itimerval present_interval;    /* value read */
static struct itimerval new_interval;   /* new value set and old value */

unsigned long UserDeltaTime ()
{
    unsigned long delta, time;

    new_interval.it_value.tv_sec = DEFAULT_SECONDS;
    new_interval.it_value.tv_usec = 999000L;
    new_interval.it_interval.tv_sec = DEFAULT_SECONDS;
    new_interval.it_interval.tv_usec = 999000L;

	if ( setitimer(ITIMER_VIRTUAL, &new_interval, &present_interval) != 0)
		printf("setitimer failed\n");

    delta = new_interval.it_value.tv_usec - present_interval.it_value.tv_usec;
    time = new_interval.it_value.tv_sec - present_interval.it_value.tv_sec;

	if ( time )
	{
		delta += 1000000 * time;
	}

	delta >>= TICKS_SHIFT;

    return delta;
}

