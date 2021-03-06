/*      Copyright (C) 1989, 1991, California Institute of Technology.
		U. S. Government Sponsorship under NASA Contract NAS7-918
		is acknowledged.        */

/*
 * $Log:	critpath.c,v $
 * Revision 1.4  91/12/27  09:52:14  reiher
 * Turned off queue shortening when critical path computation is on.
 * 
 * Revision 1.3  91/12/27  09:02:45  pls
 * Add support for variable length address table (SCR 214).
 * 
 * Revision 1.2  91/11/06  11:09:30  configtw
 * Add copyright and log entry.
 * 
 */

#include <stdio.h>
#include "twcommon.h"
#include "twsys.h"
#include "machdep.h"
#include "tester.h"

/* The code in this module is related to the computation of the critical
	path of a simulation by TWOS.  It contains two components, each with
	several functions.  One is used to actually do the calculation of
	the critical path at the end of the run.  The other is used to prune
	off some of the states and messages that must be saved to perform
	the final critical path calculation. */



long highEpt = 0;
long highestEpt = -2;
long critNode = -1;
long nodesReporting;
Name critObject ;
long critEnabled = FALSE;
long statesPruned = 0;
long msgsPruned = 0;
extern int aggressive;
extern int dlm;
extern long shortenQueues;
int endCritFlag = FALSE;
int msgsOnCritPath = 0;
long statesTruncated = 0;
long msgsTruncated = 0;
extern long maxLocalEpt;

#define CRITPATHMASTER 0

/* This function is called by each node when GVT reaches posinf.   The
	node runs through all of its local OCB's, looking at the EPT timetag
	of the last state for each ocb.  The highest EPT is chosen and sent to
	the critical path master node (currently node 0).  If this node is the
	master, it simply maxes in its local contribution.  Otherwise, the
	data is packaged as a system message to the master.  Throughout, watch
	for the possibility of equal EPT tags.  Flag them when they arise, as
	the algorithm does not allow for their occurence.   The critical path
	algorithm also does not work with either dynamic load management or
	lazy cancellation.  As both these facilities merely improve run time,
	without affecting the critical path, they can be safely turned off for
	the critical path run without affecting correctness of the simulation
	or the computation of the critical path.  */

FUNCTION calculateCritPath()
{
	Ocb * o, *critOcb;
	State *s, *nxtState;
	truncState * lastState;
	Byte duplicateEpt;

	if ( tw_node_num == CRITPATHMASTER )
	{
		_pprintf("Calculating critical path\n");
	}

	duplicateEpt = 0;

	/* Look through each object to find the local object with the state with
		the highest ept. */

	for ( o = fstocb_macro; o != NULLOCB; o = nxtocb_macro ( o ) )
	{

		/* Ignore the stdout object. */

		if ( o->runstat == ITS_STDOUT )
			continue;


		/* Truncate any states still in the state queue.  They're only
			needed for critical path purposes, at this point, and having
			all the states in one queue simplifies matters. */


		for ( s = fststate_macro ( o ) ; s != NULLSTATE; s = nxtState )
		{
			nxtState = nxtstate_macro ( s );

			truncateState ( s );

		}

		lastState = l_prev_macro ( o->tsqh );
		
		/* We expect all objects to have at least one state. */

		if ( l_ishead_macro ( lastState ))
		{
			_pprintf("ocb %s has no state in truncated state queue\n", o->name );
			tester();
		}


		/* Now check to see if this state's Ept is the highest seen so far.
			If so, change both highEpt and critOcb.  In any case, watch for
			duplicates of the highest Ept using duplicateEpt. */

		if ( lastState->Ept > highEpt )
		{	
			highEpt = lastState->Ept; 
			critOcb = o;
			duplicateEpt = 0;
		}
		else
		if ( lastState->Ept < highEpt )
			duplicateEpt = 0;
		else
			duplicateEpt++ ;
	}

	/* We're not prepared to deal with duplicate Epts yet. */

	if ( duplicateEpt )
	{
		_pprintf("duplicate final EPT's = %d\n", highEpt );
		tester();
	}
		
	if ( tw_node_num == CRITPATHMASTER )
	{
		/* If this node is the master, check its contribution immediately. */

		/* highEpt keeps the recently calculated contribution for this node.
			highestEpt, used only on the CRITPATHMASTER node, keeps the
			globally highest Ept for all nodes that have reported, so far. */

		if ( highestEpt < highEpt )
		{
			highestEpt = highEpt;
			strcpy ( critObject, critOcb->name );
			critNode = CRITPATHMASTER;
		}
		else
		if ( highestEpt == highEpt )
		{
			_pprintf("duplicate high Epts %d and %d\n", highestEpt, highEpt);
			tester();
		}	

		nodesReporting--;

		/* The master was the last node to report (or is the only node). 
			Call startCritPath().  (A function is used here because the
			same code in startCritPath() will also be used by checkCritPath().
			So both routines call the function startCritPath(), rather than
			duplicate the code.) */

		if ( nodesReporting == 0 )
		{
			startCritPath ();
		}
	}
	else
	{

		/* This is not the master node.  Send the CP contribution to the
			master node. */

		Msgh * p;
		Critmsg * q;

		p = sysbuf (); 
		q = ( Critmsg *) (p + 1);

		sprintf ( p->snder, "CRIT%d", tw_node_num );
		sprintf ( p->rcver, "CRIT%d", CRITPATHMASTER );
		q->Ept = highEpt;
		q->node = tw_node_num;
		sprintf ( q->object, critOcb->name );

		sysmsg ( CRITMSG, p, sizeof (Critmsg), CRITPATHMASTER );
	}
}

/* checkCritPath() determines if a particular highest local Ept contribution
	is the highest for all nodes.  It takes one input message with one
	node's ept contribution and maxes it with all others seen so far.  Once
	the global maximum is found, the node holding it is informed.   This
	routine is run only at CRITPATHMASTER.  */

FUNCTION checkCritPath ( critMsg )
	Critmsg	*critMsg;
{
	long highEpt;

	nodesReporting--;

	highEpt = critMsg->Ept;

	/* Check the new contribution against the highest seen so far.  Switch
		if necessary. */

	if ( highEpt > highestEpt )
	{
		highestEpt = highEpt;
		critNode = critMsg->node;
		strcpy ( critObject, critMsg->object );
	}

	/* If this was the last node to report, send a message to the node with
		the final event in the critical path. */

	if ( nodesReporting == 0 )
	{
		startCritPath ();
	}
}

/*  This routine starts computation of the critical path, once the 
	CRITPATHMASTER has received all contributions.  It checks to see if the
	critical path starts on the master or not.  If it starts on the master,
	it dummies up a message (to match the interface to takeCritStep()).
	Otherwise, it sends a CRITSTEP message to whatever node has the final
	event in the critical path.  This routine is run only at CRITPATHMASTER. */

FUNCTION startCritPath ()
{

	if ( critNode == tw_node_num )
	{
		Msgh   *onNode;
		Critmsg * q;

		/* Last event on CP local - handle it.  Fake up a message (which 
			will never be sent) to match the interface of the normal 
			case.  */

		onNode = (Msgh *) m_create ( msgdefsize, posinfPlus1, CRITICAL );

		clear ( onNode, sizeof (Msgh) ); /* clear it */

		q = ( Critmsg *) (onNode + 1);
		q->Ept = highestEpt;
		sprintf ( onNode->snder, "CRIT%d", CRITPATHMASTER );
		sprintf ( onNode->rcver, "CRIT%d", CRITPATHMASTER );

		if ( strcmp ( critObject, "" ) == 0 )
		{
			_pprintf("startCritPath: Null name for critObject\n");
			tester();
		}

		strcpy ( q->object, critObject ); 
		takeCritStep ( q );
		l_destroy ( onNode );
	}
	else
	{
		Msgh * p;
		Critmsg * q;

		/* send message to node storing last event on CP */

		p = (Msgh *) m_create ( msgdefsize, posinfPlus1, CRITICAL );
		q = ( Critmsg *) (p + 1);

		q->Ept = highestEpt;
		sprintf ( p->snder, "CRIT%d", CRITPATHMASTER );
		sprintf ( p->rcver, "CRIT%d", critNode );
		if ( strcmp ( critObject, "" ) == 0 )
		{
			_pprintf("startCritPath: Null name for critObject\n");
			tester();
		}
		strcpy ( q->object, critObject );
		sysmsg ( CRITSTEP, p, sizeof ( Critmsg ), critNode );
	}
}

/* takeCritStep() tries to find the next step backward in the critical path.
	It examines the Ept time tags of all input messages to an event and the
	tag of the previous state.  The highest such time tag is the next step
	backward in the critical path, and critStep() prepares to make it.
	Either it goes another step backward in the state queue, or it sends a
	system message to the object sending the input message with the highest
	Ept. */

FUNCTION takeCritStep ( critMsg )
	Critmsg * critMsg;
{
	Ocb *o;
	truncState *s, *prev;

	
	/* Find the right object. */

	for ( o = fstocb_macro; o != NULLOCB; o = nxtocb_macro ( o ) )
	{
	
		/* The standard out object isn't the one we want. */

		if ( o->runstat == ITS_STDOUT )
			continue;
		
		/* Check the object name against the name sent in the message. */

		if ( strcmp ( critMsg->object, o->name ) == 0 )
		{
			/* Find the right state. */


			for ( s = l_next_macro ( o->tsqh ); ! l_ishead_macro ( s );
					s = l_next_macro ( s ) )
			{
				if ( s->Ept >= critMsg->Ept )
				{
					/* This is the one.  No two states in the same state
						queue can possibly have the same Ept.  (There is
						an assumption here that an event cannot be run in
						less than one clock tick on the timing clock.  If
						that assumption is false, then two states in the
						same queue could have the same Ept.  The assumption
						is true for the GP1000 and any machine with a decent
						clock, but if the resolution is on the order of
						milliseconds, we might have a problem.) 
						The test above is made for >=.  The Ept on a state
						is the time at which the state's event was completed.
						In certain cases, the Ept we're looking for is 
						associated with a message that was sent before the
						event was complete.  Thus, the Ept in critMsg may
						be less than the Ept of the state associated with
						critMsg.  By starting at the front of the state queue,
						if the Ept on a state ever exceeds the Ept on a 
						message, we know we've found the right state. */

					makeCrit ( s );
					break;

				} /* end of if comparing Epts */
			} /* end of state for loop */

			/* Don't look at any more ocb's, since we've found the one we
				want. */

			break;
		} /* end of strcmp if */ 
	} /* end of ocb for loop */

	if ( o == NULLOCB )
	{
		_pprintf("crit event not found\n");
		tester();
	}
}

/* This routine finishes makeCrit() when that function has to send a message
	off to the home node before proceeding.  It is defined before makeCrit()
	to avoid compiler complaints. */

FUNCTION finishCrit ( m, location)
	Msgh * m;
	Objloc * location;
{
	sprintf ( m->rcver, "CRIT%d", location->node );
	sysmsg ( CRITSTEP, m, sizeof ( Critmsg ), location->node );
}

/* makeCrit() marks a state as a critical path state and finds the input that 
	caused it to become critical.  Whatever event caused that input is then
	instructed to make itself critical.  When a critical state with no
	predecessors is found, stop. */

FUNCTION makeCrit ( critState )
	truncState	*critState;
{
	truncState * prev;
	Msgh * eventMsg, * maxEptMsg;
	VTime eventTime;
	Ocb *o;
	long maxInputEpt;

	/* First mark the state as a critical path state. */

	BITSET ( critState->sflag, CRITSTATE );

	/* Now find the input that caused it to be a critical path state. */

	prev = l_prev_macro ( critState );
					
	/* Scan the input queue for the bundle for this event.  Find the 
		largest ept in the bundle of messages and compare it to the 
		ept of the previous state.  If the state makes the contribution,
		mark it and do this again.  If it's the message, send a CRITSTEP 
		system message to it. */

	eventTime = critState->sndtim;
	o = critState->ocb;

	/* Find the input bundle for this event. */

	for ( eventMsg = fstimsg_macro ( o ); eventMsg != NULLMSGH; 
			eventMsg = nxtibq ( eventMsg ) )
	{
		if ( eqVTime ( eventMsg->rcvtim, eventTime ) )
			break;
	}

	/* If we found an input bundle, consider it. */

	if ( eventMsg != NULLMSGH )
	{
		Msgh *m;
	
		/* Now find the maximum Ept for all messages in the bundle. */

		maxInputEpt = eventMsg->Ept;
		maxEptMsg = eventMsg;

		for ( m = nxtigb ( eventMsg ) ; m != NULLMSGH; m = nxtigb ( m ) )
		{
			if ( m->Ept > maxInputEpt )
			{
				maxInputEpt = m->Ept;
				maxEptMsg = m;
			}
		}

		/* Compare the max msg ept with the input state ept.  If there is
			no state, or the msg ept is larger, the message is the critical
			path contributor. */

		if ( l_ishead_macro ( prev ) || maxInputEpt > prev->Ept )
		{
			Msgh * p;
			Critmsg * q;
			Objloc         *location;
		
			/* The event sending the message with the highest Ept is on the 
				critical path. Send that event a system message to notify
				it. */

			critState->causingSelector = maxEptMsg->selector;
	
			p = (Msgh *) m_create ( msgdefsize, posinfPlus1, CRITICAL );

			clear ( p, sizeof (Msgh) ); /* clear it */

			q = ( Critmsg *) (p + 1);
	
			q->Ept = maxInputEpt;
			strcpy ( q->object, maxEptMsg->snder );
			sprintf ( p->snder, "CRIT%d", CRITPATHMASTER );

			/* The TW object isn't a real object, so don't go looking for 
				it.  Instead, end the critical path calculation, since the 
				first item on it is a message from the config file.  */

			if ( strcmp ( maxEptMsg->snder, "TW" ) == 0 )
			{
				/* First, write out any local critical path states.  */

				outputCritPath();

				/*  Then, tell everyone else to write out their critical path
					states. */

				endCritComputation ();
				
				l_destroy ( p );
				
				return;
			}

		 	if ( strcmp ( maxEptMsg->snder, "IH" ) == 0 )
			{
				_pprintf("IH msg %x found on critical path\n",maxEptMsg );
				tester();
			}

			/* The message is a normal message, so we have to follow it
				to its source to make that event critical, and further
				backtrack along the critical path. */

			location = GetLocation ( maxEptMsg->snder, maxEptMsg->sndtim );
				
			/* We may have to send away for the location of this object. */
	
			if ( location == NULL )
			{
				FindObject ( maxEptMsg->snder, maxEptMsg->sndtim, p, 
						finishCrit, MSG );
			}
			else
			if ( location->node == tw_node_num )
			{
				/* The next object is on this node.  Let's not bother with
					sending a system message.  Just find it and take the
					next step.  Then release the message buffer we just
					grabbed.  */

				takeCritStep ( q );
				l_destroy ( p );
			}
			else
			{
				/* The next object is on another node.  Send the message
					to that object.  The message buffer p will be released
					as a side effect of sysmsg().  */

				sprintf ( p->rcver, "CRIT%d", location->node );
				sysmsg ( CRITSTEP, p, sizeof ( Critmsg ), location->node );
			}
		}	
		else
		{
			/* There is an input message, there is a previous state, and
				the previous state's Ept is bigger than the input message's.
				The previous state is on the critical path.  This state is on 
				the critical path because of that previous state, not because 
				it was waiting for one of its input messages.  Recursively call
				this function.  */

			critState->causingSelector = 0;
			makeCrit ( prev );
		}
	}
	else
	{
		/* There is no earlier message. */

		if ( l_ishead_macro ( prev ) )
		{
			/* There's no earlier message, and no earlier state.  This must
				be the start of the critical path.  Output the local part
				of it.  */

			outputCritPath();

			/*  Then, tell everyone else to write out their critical path
					states. */

			endCritComputation ();
				
		}
		else
		{
			/* Since there is no previous message, but there is a previous
				state, that state is on the critical path.  Recursively call
				this function.  */

			critState->causingSelector = 0;
			makeCrit ( prev );
		}
	}

}

/*  This function outputs all events on the critical path of a single
	node.  It iteratively examines all objects, and, for each object, all
	states.  Any state that is marked as critical causes a critical path
	message to be sent to the IH.  This message contains the name of the
	object, the virtual time of the event, and the Ept of the event. 
	Note that, even within a single node, the critical path events may
	not be sent to the IH in critical path order, so postprocessing of
	that file is necessary (the more so when multiple nodes are involved).
	That file need merely be sorted by Ept to get the proper ordering. */

FUNCTION outputCritPath ()
{
	Ocb *o;
	truncState *s;
	char buff[MINPKTL];
	Msgh * m;

	if ( endCritFlag == TRUE )
		return;

	endCritFlag = TRUE;

	if ( tw_node_num == CRITPATHMASTER )
	{
		_pprintf("Writing critical path to CRIT_LOG\n");
	}

	for ( o = fstocb_macro; o != NULLOCB; o = nxtocb_macro ( o ) )
	{
	
		/* The standard out object has no critical path events. */

		if ( o->runstat == ITS_STDOUT )
			continue;

		/* The TW object doesn't count, either. */

		if ( strcmp ( o->name, "TW" )  == 0 )
			continue;


#ifdef CRITPATHVERIFY
_pprintf("%s --	", o->name);
#endif CRITPATHVERIFY
		for ( s = l_next_macro ( o->tsqh ) ; ! l_ishead_macro ( s );
				s = l_next_macro ( s ) )
		{
#ifdef CRITPATHVERIFY
printf("vt %f fl %d ept %d  ",s->sndtim.simtime, s->sflag, s->Ept);	
#endif CRITPATHVERIFY

			if ( BITTEST ( s->sflag, CRITSTATE ) )
			{
				/* This state is on the critical path.  Create a message
					containing the object name, the virtual time of the event,
					the event's Ept, and the selector of the message which
					caused the event to be on the critical path. */

					sprintf ( buff, "%s %f %d %d %d %d\n", o->name,
							s->sndtim.simtime, s->sndtim.sequence1,
							s->sndtim.sequence2, s->Ept, 
							s->causingSelector );

					send_to_IH ( buff, strlen ( buff ) + 1, CRIT_LOG );
			}
		}
#ifdef CRITPATHVERIFY
printf("\n");
#endif CRITPATHVERIFY
	}

	/* Don't let nodes exit from TWOS until they've done all their critical
		path work - that's here. */

	send_to_IH ( "Simulation End\n", 16, SIM_END_MSG );
}


/* CRITLEN is a garbage parameter put in because I'm not sure make_message
	will work with a zero length message.  */

#define CRITLEN 4

/* endCritComputation() sends a message to all other nodes requesting that
	they output their critical path events, using outputCritPath().  */

FUNCTION endCritComputation ()
{
	Msgh *endCrit;
	Name snder;
	char buff[CRITLEN];
	int node;

	/* create the message ??? need to test for NULL */
		endCrit = make_message ( (Byte) CRITEND,
				snder,     /* snder */
				posinfPlus1,      /* sndtim */
				"CRITEND",     /* rcver */
				posinfPlus1,      /* rcvtim */
				CRITLEN,    /* txtlen */
				(Byte *) buff
				);

		if ( endCrit == NULL )
		{
			twerror("endCritComputation: No memory available for endCrit msg\n");
			tester();
		}
	
		endCrit->flags |= SYSMSG ;
	
		sprintf ( endCrit->snder, "CRITEND%d",tw_node_num);
		sysmsg ( CRITEND, endCrit, CRITLEN, BCAST );

	_pprintf("Finished calculation of critical path\n");

}

/* This function turns on the critical path computation facility for
	TWOS. */

FUNCTION enableCritPath ()
{
	critEnabled = TRUE;

	/* If a critical path run is being made, aggressive cancellation must
		be turned on and dynamic load management turned off.  Regardless of
		what the user requested otherwise, if critEnabled is true and a
		critical path is to be calculated, the switches must be properly
		set. */

	if ( !aggressive )
	{
		if ( tw_node_num == 0 )
			_pprintf("aggressive cancellation turned on due to critical path computation\n");
		aggressive = 1;
	}

	if ( dlm )
	{
		if ( tw_node_num == 0 )
			_pprintf ( "dynamic load management turned off due to critical path computation\n");
		dlm = FALSE;
	}

	/* The queue shortening code adds complexity to dealing with the
		critical path, since it can cause two adjacent phases to sit on
		the same node, leading to confusion.  Therefore, turn it off. */

	shortenQueues = FALSE;
}

/*  The remainder of this module contains routines related to pruning
	events and messages off the critical path before the critical path
	algorithm runs.  This code is invoked from storage.c, in objpast().  */

/*  informNonCritPredecessors() looks through all of the predecessors to this 
	event.  Those not having the highest input Ept to this event are not on the 
	critical path, at least as far as this event is concerned.  So inform 
	them. */


FUNCTION informNonCritPredecessors ( tState )
	truncState * tState;
{
	Ocb * o;
	truncState * lastTState;
	Msgh * mostCrit, * m, * marker, * n;
	long maxInputEpt;
	VTime	eventVTime;
	Byte 	stateType;

	eventVTime = tState->sndtim;

	/* Find the state's ocb and the previous state.  */

	o = ( Ocb * ) tState->ocb;

	/* This function is only called for truncated states, so the previous
		state will also be a truncated state, if there is one. */

	lastTState = l_prev_macro ( tState );
	stateType = tState->stype;

	/* Set maxInputEpt to the Ept of the last state, if there is one. */

	if ( lastTState != NULL )
	{
		maxInputEpt = lastTState->Ept;
	}
	else
	{
		/* If there is no last state in the truncated state queue, set 
			maxInputEpt to the earliest Ept in the system. */

		lastTState = NULL;
		maxInputEpt = 0;
	}

	/* Now check to see if any of the input messages have a higher Ept than
		the state. */

	mostCrit = NULLMSGH;

	/* First, skip over any messages not for this event. */

	m = fstimsg_macro ( o );

	while ( m != NULLMSGH &&
			( neVTime ( m->rcvtim, eventVTime ) || m->mtype != stateType )  )
	{
		m = nxtimsg_macro ( m );

	}

	/*  We'll want to loop through messages starting here again, so save
		the place. */

	marker = m;

	/* Then, look at each message for the event.  If the message has a 
		bigger Ept than any seen so far, keep track of it. */

	while ( m != NULLMSGH &&
			eqVTime ( m->rcvtim, eventVTime) && m->mtype == stateType )
	{
		if ( m->Ept > maxInputEpt )
		{
			maxInputEpt = m->Ept;
			mostCrit = m;
		}

		m = nxtimsg_macro ( m );
	}

	/* If the previous state doesn't make the critical contribution,
		decrement its count of possibly critical successors.  
		state has a resultingEvents field set to zero, all of its
		predecessors (regardless of Ept) should be informed that this
		state isn't on the critical path.  */

	if (  mostCrit != NULLMSGH  && lastTState != NULLSTATE  ) 
	{
		/* The previous state is not on the critical path because of the
			event under consideration.  If that previous state has not
			already been told to scratch it off, do so now. */

		if ( !BITTST ( lastTState->sflag, STATECPCLR ) )
		{
			lastTState->resultingEvents--;

			BITSET ( lastTState->sflag, STATECPCLR );
		
			/* If, as a result of being informed that its next state isn't
				critical, the last state also becomes non-critical, call
				the routine to remove it from the critical path. */

			if ( lastTState->resultingEvents == 0 )
			{
				nonCritEvent ( o, NULLSTATE, lastTState );
			}
			else
			if ( lastTState->resultingEvents < 0 )
			{	
				_pprintf("informNonCritPredecessors: event count for %s at %f negative (%d)\n",	
				o->name, lastTState->sndtim.simtime, lastTState->resultingEvents);
				tester();
			}
		}
	}

	/* Inform any non-critical messages' events that the messages do not make
		critical path contributions, then mark them as noncritical.
		(If the state's resultingEvents field is zero, none of the input
		messages make critical path contributions.) */

	for ( m = marker; m != NULLMSGH && eqVTime ( m->rcvtim, eventVTime) &&
			m->mtype == stateType; m = n )
	{
		n = nxtimsg_macro ( m );

		if ( m->Ept != maxInputEpt || tState->resultingEvents == 0 )
		{
			if ( ! BITTEST ( m->flags, NONCRITMSG ) )
			{
				BITSET ( m->flags, NONCRITMSG );
				nonCritPathMsg ( m );
			}
		}
	}

}

/* This function completes the work of nonCritPathMsg(), when that
	function must resort to getting object location information off node.
	This function is defined first to keep the compiler happy. */

FUNCTION finishNonCritPathMsg ( m, location)
	Msgh * m;
	Objloc * location;
{
	sprintf ( m->rcver, "CRITRM%d", location->node );
	sysmsg ( CRITRM, m, sizeof ( Critmsg ), location->node );
}

/* nonCritPathMsg() tells a message's sender that this msg is not on the
	critical path. */

FUNCTION nonCritPathMsg ( m )
	Msgh * m;
{
	Msgh * nonCritInform;
	Critmsg * q;
	Objloc         *location;

	/* Set up a system message and fill in its fields. */

	nonCritInform = (Msgh *) m_create ( msgdefsize, posinfPlus1, CRITICAL );

	clear ( nonCritInform, sizeof (Msgh) ); /* clear it */

	q = ( Critmsg *) (nonCritInform + 1);

	printf ( nonCritInform->snder, "CRITRM%d", tw_node_num );
	q->sndtim = m->sndtim;
	q->etype = m->stype;
	sprintf ( q->object, m->snder );

	/* Now we have to find the object that sent the message that isn't
		on the critical path. */

	/* The TW object isn't a real object, so don't go looking for it. */

	if ( strcmp ( m->snder, "TW" ) == 0 ||
		 strcmp ( m->snder, "IH" ) == 0 )
		return;

	location = GetLocation ( m->snder, m->sndtim );
			
	/* We may have to send away for the location of this object. */

	if ( location == NULL )
	{
		FindObject ( m->snder, m->sndtim, nonCritInform, finishNonCritPathMsg, 
						MSG );
	}
	else
	if ( location->node == tw_node_num )
	{
		/* The next object is on this node.  Let's not bother with
			sending a system message.  Just find it and take the
			next step. */

		successorNotOnCP ( q );
	}
	else
	{
		sprintf ( nonCritInform->rcver, "CRITRM%d", location->node );
		sysmsg ( CRITRM, nonCritInform, sizeof ( Critmsg ), location->node );
	}

}

/* This function is called to handle an incoming CRITRM message (or to
	deal with a dummied one, locally).  It decrements the resultingEvents
	field in the state for the event that is to be informed.  If, as a result,
	that state becomes totally non-critical, the state will be fossil 
	collected and any predecessors it hasn't yet informed will be removed from 
	the critical path. */

FUNCTION successorNotOnCP ( critRm )
	Critmsg * critRm;
{
	Objloc * location;
	State	* s;
	truncState * trunc;

	/* Find the object locally. */

	/* The TW object and IH object aren't real objects, so don't go looking 
			for them. */

	if ( strcmp ( critRm->object, "TW" ) == 0  || 
		 strcmp ( critRm->object, "IH" ) == 0 )
		return;

	location = GetLocation ( critRm->object, critRm->sndtim );

	if ( location == NULL || location->po == NULLOCB )
	{
		_pprintf( " successorNotOnCP: can't find object %s, critmsg %x\n",
				critRm->object, critRm );
		tester();
	}

	/* Now find the state for the right event. */

	for ( s = fststate_macro ( location->po ); s != NULLSTATE; 
			s = nxtstate_macro ( s ) )
	{
		if ( eqVTime ( s->sndtim, critRm->sndtim ) &&
				s->stype == critRm->etype)
			break;
	}

	if ( s == NULLSTATE ) 
	{
		/* If there's no matching state in the state queue, look in the
			truncated state queue. */

		for ( trunc = l_next_macro ( location->po->tsqh ); 
				!l_ishead_macro ( trunc ); trunc = l_next_macro ( trunc ) )
		{
			if ( eqVTime ( trunc->sndtim, critRm->sndtim ) &&
					trunc->stype == critRm->etype )
				break;
		}

		/* If there's no matching truncated state, either, something's wrong. */

		if ( l_ishead_macro ( trunc ) )
		{
			_pprintf ( "successorNotOnCP: can't find state for %s, time %f, type %d\n",
				critRm->object, critRm->sndtim.simtime, critRm->etype );
			_pprintf("	critRm msg ptr %d\n",critRm );
			tester();
		}
	}
	else
	{
		trunc = NULL;
	}
	
	/* If the resulting event field of the previous state is not already 0,
		decrement the state's resultingEvents field.  It's possible that the
		state's resulting event count is already zero, because it was already
		determined that it was not on the critical path. */
	
	if ( s != NULLSTATE && s->resultingEvents > 0  && ! BITTST ( s->sflag,
			STATECPCLR ))
	{
		BITSET ( s->sflag, STATECPCLR) ;

		if ( --( s->resultingEvents) == 0 )
		{
			nonCritEvent ( location->po, s, NULL);
		}
		
		return;
	}

	if ( trunc != NULL && trunc->resultingEvents > 0 && ! BITTST ( trunc->sflag,
			STATECPCLR ) )
	{
		BITSET ( trunc->sflag, STATECPCLR );

		if ( --(trunc->resultingEvents) == 0 )
		{
			nonCritEvent ( location->po, NULLSTATE, trunc );
		}
	}

}

/* nonCritEvent is called when a particular event's state (in either
	truncated or non-truncated form) has its resultingEvents field set to
	zero.  In such cases, we are guaranteed that this state is not on the
	critical path.  Any of its predecessors should be informed that this
	resulting event does not cause them to be on the critical path.   This
	function is rather general purpose.  Either state or tstate, but not
	both, will be non-null when it is called.*/

FUNCTION nonCritEvent ( o, state, tstate )
	Ocb * o;
	State * state;
	truncState * tstate;
{
	Msgh *m;
	VTime time;
	int type;
	truncState * ts;
	State	*prvState;

	if ( state != NULLSTATE )
	{
		time = state->sndtim;
		type = state->stype;
	}
	else
	if ( tstate != NULL )
	{
		time = tstate->sndtim;
		type = tstate->stype;
	}
	else
	{
		twerror("nonCritEvent: called with both state and tstate NULL\n");
		tester();
	}

	/* Look for any messages associated with this event.  If they have not
		already been removed from critical path consideration, remove
		them now. */

	for ( m = fstimsg_macro ( o ); m != NULLMSGH ; m = nxtimsg_macro ( m ) )
	{
		if ( eqVTime ( m->rcvtim, time ) && m->mtype == type  &&
				!BITTST ( m->flags, NONCRITMSG))
		{
			BITSET ( m->flags, NONCRITMSG );
			nonCritPathMsg ( m );
		}

		/* Once the vtime of the message exceeds the vtime of this event,
			we've dealt with all the messages, so break for efficiency. */

		if ( gtVTime ( m->rcvtim, time ) )
			break;
	}

	/* Find the previous state.  It might be in either the state queue or
		the truncated state queue.  Decrement its resultingEvents field.
		If that field goes to zero, recursively call this function to 
		make that state's predecessors non-critical. */

	if ( state != NULLSTATE )
	{
		if ( ( prvState = prvstate_macro ( state )) != NULLSTATE )
		{
			/* The previous state is in the state queue.  If it has 
				any resulting events and we haven't already told it that
				the next state doesn't make a critical path contribution,
				do so now.  */ 

			if ( prvState->resultingEvents > 0  && 
					!BITTST ( prvState->sflag, STATECPCLR ))
			{
				BITSET ( prvState->sflag, STATECPCLR );

				if ( --( prvState->resultingEvents) == 0 )
				{
					nonCritEvent ( o, prvState, NULL );
				}

			}
			/* We're done with making the current event noncritical. */

			return;
		}
		else
		{
			/* The previous state must be in the truncated state queue. */
			
			if ( !l_ishead_macro ( ts = l_prev_macro ( o->tsqh ) ) )
			{

			/* The previous state is in the truncated state queue.  If it has 
				any resulting events and we haven't already told it that
				the next state doesn't make a critical path contribution,
				do so now.  */ 

				if ( ts->resultingEvents > 0 &&
					!BITTST ( ts->sflag, STATECPCLR ))
				{
					BITSET ( ts->sflag, STATECPCLR );

					if ( --( ts->resultingEvents) == 0 )
					{
						nonCritEvent ( o, NULLSTATE, ts );
					}
			
				}
				/* We're done with making the current event noncritical. */

				return;
			}
			else
			{
				_pprintf("1. No previous state in either state or truncated state queue\n");
			}
		}
	}	/* if ( state != NULLSTATE ) */
	else
	{
		/* The state for the current event is in the truncated queue.  In
			this case, the previous state is definitely not in the state
			queue. */

		if ( ! l_ishead_macro ( ts = l_prev_macro ( tstate ) ) )
		{
			if ( ts->resultingEvents > 0 &&
				!BITTST ( ts->sflag, STATECPCLR ))
			{
				BITSET (ts->sflag, STATECPCLR );

				if ( --(ts->resultingEvents) == 0 )
				{
					nonCritEvent ( o, NULLSTATE, ts );
				}
			}
		
			/* We're done with making the current event noncritical. */

			return;
		}
		else
		{
/*
				_pprintf("2. No previous state in either state or truncated state queue\n");
*/
		}
	}
}

/* truncateState() releases as much memory associated with a state as possible,
	making sure that enough remains to deal with critical path computation.
	It must also be careful that the size associated with the remaining block
	of allocated memory is correct. */

FUNCTION truncateState( state, ocb )
	State * state;
	Ocb	*	ocb;
{
	int i;
	register Byte * addr;
	truncState *truncSt;

	/* Also release any dynamic memory segments attached to the state,
		and the state's address table, if it has one. */

	statesTruncated++;

	if ( state->address_table != NULL )
	{
		for ( i = 0; i < l_size(state->address_table) / sizeof(Address); i++ )
		{
			addr = state->address_table[i];

			if ( addr != NULL && addr != DEFERRED )
			{   /* release all memory in the address table */
				l_destroy ( addr );
			}
		}

		/* release the address table */
		l_destroy ( state->address_table );
	}

	/* Now allocate a truncated state structure, copy the necessary 
		information into it, put it in the proper place in the truncated
		state queue, and free the state itself. */

	truncSt = (truncState *) m_create ( sizeof(truncState), gvt, 
					NONCRITICAL );
	truncSt->sndtim = state->sndtim;
	truncSt->Ept = state->Ept;
	truncSt->sflag = state->sflag;
	truncSt->stype = state->stype;
	truncSt->resultingEvents = state->resultingEvents;
	truncSt->ocb = state->ocb;
		
	nqTruncState ( ocb, truncSt );
		
	l_remove ( state );

	l_destroy ( state );

	/* Now inform all non-critical predecessors to this state that they
		do not make a critical contribution through this path. */

	informNonCritPredecessors ( truncSt );

}

/* This function chops off all of the text of a message, saving only the
	header for critical path computation purposes.  The FOSSILMSG flag
	set before calling this function from objpast() already indicates
	that the message has been truncated, so we don't need the analog of
	the STATETRUNC flag used in truncateState(), above.   Actually, we
	don't need to take any care at all, as the macro delimsg() used to
	release the message when it is finally fossil collected doesn't
	care about the size of what it's releasing. */

truncateMessage ( msg )
	Msgh * msg;
{
	Msgh * truncMsg;

	msgsTruncated++;

	truncMsg = ( Msgh * ) m_create ( sizeof ( Msgh ), gvt, NONCRITICAL );

	clear ( truncMsg, sizeof (Msgh) ); /* clear it */

	entcpy ( truncMsg, msg, sizeof ( Msgh ) );

	l_insert ( msg, truncMsg );

	l_remove ( msg );

	l_destroy ( msg );

}

FUNCTION nqTruncState ( o, ts )
	Ocb * o;
	truncState * ts;
{

	truncState * tsPtr;

	for ( tsPtr = l_prev_macro ( o->tsqh ); !l_ishead_macro ( tsPtr );
			tsPtr = l_prev_macro ( tsPtr ) ) 
	{
		/* If the time of the queued truncated state being looked at is
			less than the time of the one we're enqueueing, or the times
			are the same and the type of the state is less, here's the
			place to put the one to be enqueued. */

		if ( gtVTime ( ts->sndtim, tsPtr->sndtim) ||
			( eqVTime (ts->sndtim, tsPtr->sndtim) && ts->stype > tsPtr->stype ))
		{
			l_insert ( tsPtr, ts );
			break;
		}

	}

	/* The truncated state to be enqueued should be the first one in the
		queue.  */

	if ( l_ishead_macro ( tsPtr ) )
	{
		l_insert ( o->tsqh, ts );
	} 
}

FUNCTION showTruncSQ ( o )
	Ocb * o;
{
	truncState * s;
	
	showstate_head ();

	for (s = l_next_macro (o->tsqh); !l_ishead_macro ( s ); s = l_next_macro(s))
	{
			char            sndtim[12];

/*
xx--mmmmmmmm tttttt tttttttttt
*/
			ttoc ( sndtim, s->sndtim );
			dprintf ( "%8x  %6s serror = noerr  Ept = %d, stype = %d, flag = %d, resEv = %d\n",
				s, sndtim, s->Ept, s->stype, s->sflag, s->resultingEvents );
	}
}

FUNCTION showTruncSQ_by_name ( name )
	char * name;
{
	Ocb * o;

	if ( ( o = general_ocb_by_name ( name ) ) != NULLOCB )
	{
		showTruncSQ ( o );
	}
}
