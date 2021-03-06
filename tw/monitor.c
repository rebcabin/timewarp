/*      Copyright (C) 1989, 1991, California Institute of Technology.
		U. S. Government Sponsorship under NASA Contract NAS7-918
		is acknowledged.        */

/*
 * $Log:	monitor.c,v $
 * Revision 1.3  91/07/17  15:10:32  judy
 * New copyright notice.
 * 
 * Revision 1.2  91/06/03  12:25:28  configtw
 * Tab conversion.
 * 
 * Revision 1.1  90/08/07  15:40:22  configtw
 * Initial revision
 * 
*/
char monitor_id [] = "@(#)monitor.c     1.12\t6/2/89\t12:44:41\tTIMEWARP";

/*      Copyright (C) 1989, California Institute of Technology.
		U. S. Government Sponsorship under NASA Contract NAS7-918
		is acknowledged.        */

#ifndef TRANSPUTER

/*

Purpose:

		This module contains code that supports a monitor function for
		Time Warp, allowing runtime examination of certain important
		values in the system.  The module contains one main monitor
		routine (mad_monitor()), and several supporting routines.
		format.c, another module, contains an important set of supporting
		routines for the monitor, routines that format output to the
		console, depending on the type of data being printed.

Functions:

		monon() - turn the monitoring function on
				Parameters - none
				Return - always returns zero

		monoff() - turn the monitoring function off
				Parameters - none
				Return - always returns zero

		find_nt(addr) - search the mon_array for a particular entry
				Parameters - long addr
				Return - an integer offset into the mon_func array

		bsearch(array,x,num,key) - perform a binary search on an array
				Parameters - long array[][2], int x, int num, long key
				Return - position in the array, or num, if not found

		set_level(name,level) - set a level field in the appropriate mon_strs
				array
				Parameters - char *name, int *level
				Return - always returns zero

		list_levels() - print the levels of all mon_strs entries
				Parameters - none
				Return - always returns zero

		mad_monitor(arg) - print the requested monitor message
				Parameters - int arg
				Return - always returns zero

Implementation:

		monon() and monoff() simply set and unset an enabling variable
		for the monitor.  If the variable is disabled, no monitor messages
		are printed.

		find_nt() calls bsearch() to find a particular element in the
		mon_array.  It uses the resulting index to extract an integer
		value from the array.

		bsearch() is a standard binary search function.  It is passed
		the value to be returned on failure as a parameter.  Another
		parameter indicates which column of an X-by-2 two-dimensional
		array is to be compared to the key.

		set_level() is used to set a level for individual monitor
		functions.  This allows such functions to be selectively
		turned on and off, or to produce more detailed output when
		called.  (Currently, only two level values seem to have
		special meaning.  A level of zero means that nothing is done
		when that function is called.  A level of 20 causes a trap
		to the tester after all other work is done.  Any level value
		between these two causes normal work to be done.)

		list_levels() just runs through the mon_strs array, printing
		out the levels of each entry in the array.

		mad_monitor() is the main body of monitor code.  First, it checks
		to see if it has been enabled, examining the variable used in
		monon() and monoff().  If disabled, it immediately returns.
		Otherwise, it begins to perform some villainous code.  Apparently,
		the idea is that mad_monitor() is called via inserting "Debug"
		in a routine in Time Warp, with the intention of printing out
		all of the arguments of the routine in question.  Only the word
		"Debug" is inserted, so mad_monitor() must be able to do its
		job without knowing, at call time, how many parameters there
		are, what their values are, etc.  Special data structures are
		kept to assist with this problem.  These data structures are
		filled at system initialization time by functions in moninit.c.

		Then, when a Debug statement is reached, what is actually performed
		is a parameterless call to mad_monitor().  mad_monitor() has
		one parameter declared.  Its only purpose is to get an address
		into stack space, so that it can look for the parameters to the
		function that called mad_monitor().  

		Time Warp, or perhaps Tester, has an array that contains the
		starting addresses of all functions in the system.  This array
		is created at compile time by running the Unix utility nm on
		the newly loaded module.  The results of the nm command, a list
		of all physical addresses of all modules in the system, is kept
		in a file called "names".  (Actually, it is currently kept in
		/u/mike/names.)  At system initialization time, this file is
		read into an array called mon_func[].

		When a parameterless call is made to mad_monitor(), and that
		function is told it is being given a parameter, the result is
		that its "parameter" is actually close to the return address 
		of the calling function.  So, mad_monitor() subtracts two from
		the address of its "parameter", to point further back into the
		stack, then it sets retaddr to the contents of that address plus
		one.  find_nt() is called to look in the mon_func[] table for
		the matching routine.  The return value is a pointer into the
		mon_func[] array.  One item of information that is kept in that
		array is a level for each function.  If the level is 0, the 
		monitor is disabled for that function.  If the level is non-zero,
		the monitor is enabled.  So, after finding the function's offset,
		mad_monitor() checks to see if the function is enabled for monitor
		output.  If not, mad_monitor() returns.

		If mad_monitor() is still executing, it next extracts another
		address from the stack, the address of the function that called
		the function that, in turn, called mad_monitor().  find_nt() is
		again used to determine which function it was, and its level is
		found.  Then formhdr() is called to create the first line of the
		monitoring output; it is not yet printed out.

		Another item kept in the structures held in the mon_func[] array
		is a list of the number and types of arguments to various 
		functions.  mad_monitor() next runs through all the arguments
		for the type of function that called it.  If the argument in 
		question is a pointer, it is dereferenced until the data it points
		to is obtained.  (The information about how often it needs to be
		dereferenced is also kept in the mon_func[] structure.)  If the
		data item is a short integer, the pointer to it is fiddled to
		make sure it points to the right thing.  Then format is called 
		on the argument, with the result added to the end of the buffer
		that contains the header.  As each argument is processed, it is
		added to the end of that buffer.

		After all arguments are formatted, mad_monitor() calls printf()
		to output the string containing the header and arguments.
		If level was set to 20, it then traps to tester().  Otherwise,
		it returns.
*/

/*
#define printf _pprintf
*/
#include "func.h"

extern FUNC mon_func[];

extern int num_mon_funcs;

extern char mon_strs[];

extern long mon_array[][2];

static int enabled = 0;

monon ()
{
	enabled = 1;
}

monoff ()
{
	enabled = 0;
}

find_nt ( addr )

	long addr;
{
	int n, h;

	h = bsearch ( mon_array, 0, num_mon_funcs, addr );
	n = (int) mon_array[h][1];
#if 0
	if ( addr > mon_func[n].end )
		n = 0;                          /* UNKNOWN */
#endif
	return ( n );
}

bsearch ( array, x, num, key )

	long array[][2], key;
	int x, num;
{
	int n, top, bottom, middle;

	top = num;
	bottom = 0;

	for ( ;; )
	{
		n = top - bottom;

		if ( n == 0 )
			return ( num );

		middle = n / 2 + bottom;

		if ( key >= array[middle][x] )
		{
			if ( key < array[middle+1][x] )
				return ( middle );

			bottom = middle + 1;
		}
		else
			top = middle;
	}
}

set_level ( name, level )

	char *name;
	int *level;
{
/*
	char *s;
*/
	int n;
/*
	for ( s = name; *s != 0; s++ )
		*s = toupper ( *s );
*/
	for ( n = 0; n < num_mon_funcs; n++ )
		if ( strcmp ( name, &mon_strs[mon_func[n].name] ) == 0 )
			break;

	if ( n < num_mon_funcs )
	{
		mon_func[n].level = *level;
	}
	else
		printf ( "Function %s Not Found\n", name );
}

list_levels ()
{
	int n;

	for ( n = 0; n < num_mon_funcs; n++ )
	{
		printf ( "%3d. %-30s %d\n", n, &mon_strs[mon_func[n].name],
								mon_func[n].level );
	}
}

int ok_to_monitor;
char monitor_object[20];

mad_monitor ( arg )

	int arg;
{
	static int fp = 0;
	int type, level, **data;
	char buff[200];
	char **bp; int size;
	int calladdr, *oofp, r;
	int i, j, n, retaddr, *ofp, *oap;

	if ( enabled == 0 )
		return;

	ofp = &arg - 2;

	retaddr = * (ofp + 1);

	n = find_nt ( retaddr );

	if ( mon_func[n].level == 0 )
		return;

	oofp = (int *) * (ofp + 0);

	calladdr = * (oofp + 1);

	oap = oofp + 2;

	r = find_nt ( calladdr );
/*
	if ( fp == 0 )
		fp = fopen ( "MONOUT", "w" );
*/
	level = mon_func[n].level;

	bp = (char **) buff;

	formhdr ( &bp, &mon_strs[mon_func[n].name], &mon_strs[mon_func[r].name]);

	ok_to_monitor = ( monitor_object[0] == 0 );

	for ( i = 0; mon_func[n].args[i] != 0; i++ )
	{
		type = mon_func[n].types[i];

		data = (int **) oap;
		for ( j = 0; j < mon_func[n].stars[i]; j++ )
			data = (int **) *data;

#ifdef SHORTS
		if ( ( type == 2 || type == 8 ) /* watch out for short ints!!! */
		&&   mon_func[n].stars[i] == 0 )
		{
			data = (int **) ((char *)data + 2);
		}
#endif
		size = format ( &bp, &mon_strs[mon_func[n].args[i]], data, type, level );

		if ( mon_func[n].stars[i] != 0 )
			size = 1;

		oap += size;
	}
/*
	fprintf ( fp, "%s\n", buff );
	fflush ( fp );
*/
	if ( ok_to_monitor )
	{
		printf ( "%s\n", buff );

		if ( level == 20 )
			tester ();
	}
}

monobj ( object_name )

	char * object_name;
{
	if ( strcmp ( object_name, "none" ) == 0 )
		monitor_object[0] = 0;
	else
		strcpy ( monitor_object, object_name );
}

#endif  /* TRANSPUTER */
