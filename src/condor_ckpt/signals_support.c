/* 
** Copyright 1986, 1987, 1988, 1989, 1990, 1991 by the Condor Design Team
** 
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted,
** provided that the above copyright notice appear in all copies and that
** both that copyright notice and this permission notice appear in
** supporting documentation, and that the names of the University of
** Wisconsin and the Condor Design Team not be used in advertising or
** publicity pertaining to distribution of the software without specific,
** written prior permission.  The University of Wisconsin and the Condor
** Design Team make no representations about the suitability of this
** software for any purpose.  It is provided "as is" without express
** or implied warranty.
** 
** THE UNIVERSITY OF WISCONSIN AND THE CONDOR DESIGN TEAM DISCLAIM ALL
** WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE UNIVERSITY OF
** WISCONSIN OR THE CONDOR DESIGN TEAM BE LIABLE FOR ANY SPECIAL, INDIRECT
** OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
** OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
** OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
** OR PERFORMANCE OF THIS SOFTWARE.
** 
** Author: Todd Tannenbaum, Computer Aided Engineering Center
** 
*/ 

#if !defined(SUNOS41)
#define _POSIX_SOURCE
#endif

#include <signal.h>
#include <sys/types.h>
#include <stdio.h>
#include <sys/syscall.h>
#include "condor_sys.h"
#include "errno.h"
#include "debug.h"

extern	char	*strerror();

#if defined(HPUX9)
extern void _sigreturn();
#endif

#if defined(HPUX9)
#define MASK_TYPE long
#else
#define MASK_TYPE int
#endif

#if defined(NSIG)
#define NUM_SIGS NSIG
#else
#define NUM_SIGS 50   /* error on the high side... */
#endif

typedef struct sigstack SS_TYPE;

struct signal_states_struct {
	int nsigs;		/* the number of signals supported on this platform +1 */
	sigset_t user_mask;
	struct sigaction actions[NUM_SIGS];
	sigset_t pending;
	SS_TYPE sig_stack; 
} ;

static struct signal_states_struct signal_states;


/* Here we save the signal state of the user process.  This is called
 * when we are checkpointing.  */
void
condor_save_sigstates()
{
	sigset_t mask;
	int scm;
	int sig;

    scm = SetSyscalls( SYS_LOCAL | SYS_UNRECORDED );

	/* Save the user's signal mask */
	sigprocmask(SIG_SETMASK,NULL,&(signal_states.user_mask));

	/* Save pending signal information */
	sigpending( &(signal_states.pending) );

	if ( signal_states.nsigs == 0 ) {
		/* This code only runs once; here we figure out how many signals
		 * this OS supports.  If signal.h defines NSIG, we believe it, else
		 * we do some useless work to try to figure it out for ourselves. */
#if defined(NSIG)
		signal_states.nsigs = NSIG;
#else
		sigfillset(&mask);
		sig = 1;
		while ( sigismember(&mask,sig) != -1 ) sig++;
		signal_states.nsigs = sig;
		if ( sig > NUM_SIGS ) {
			EXCEPT("must recompile to support %d signals; current max=%d\n",
					sig, NUM_SIGS);
		}
#endif
	}

	/* Save handler information for each signal */
	for ( sig=1; sig < signal_states.nsigs; sig++ ) {
		sigaction(sig,NULL,&(signal_states.actions[sig]));
	}

	/* Save pointer to signal stack (not POSIX, but widely supported) */	
	sigstack( (struct sigstack *) 0, &(signal_states.sig_stack) ); 

    (void) SetSyscalls( scm );
}

/* Here we restore the signal state of the user process.  This is called
 * upon a restart of a checkpointed process.  The data space must be
 * restored _before_ calling this function (all signal state info is saved
 * in the static structure signal_states) */
void
condor_restore_sigstates()
{
	int scm;
	int sig;
	pid_t mypid;

	scm = SetSyscalls( SYS_LOCAL | SYS_UNRECORDED );

#if defined(HPUX9)
     /* Tell the kernel to fill in our process's "trampoline" return
	  * code function with _sigreturn, so that signal handlers can
	  * return properly.  HPUX normally does this in _start().  But
	  * since we are restarting and thus not calling _start(), we must
	  * do it here.  (The constant is a magic cookie to prove we are
	  * post pa-risc 89 revision era) -Todd Tannenbaum, 12/94  
	  */
     _sigsetreturn(_sigreturn,0x06211988,sizeof(struct sigcontext));
#endif

	/* Restore signal actions */
	for ( sig=1; sig < signal_states.nsigs; sig++ ) {
		switch (sig) {
			/* Do not fiddle with the special Condor signals under
			 * any circumstance! */
			case SIGUSR1:
			case SIGTSTP:
			case SIGKILL:
			case SIGSTOP:
			case SIGCONT:
				continue;
		}
		sigaction(sig, &(signal_states.actions[sig]), NULL);
	}

	/* Restore signal mask, making _certain_ that Condor & special
	   signals are not blocked */
	/* seems to me my logic here is flawed, since this is running as 
	 * a TSTP signal handler right now, and as soon as this signal
	 * handler returns, the previous mask will be replaced. So this
	 * probably need not be here....  -Todd 12/94 */
	sigdelset(&(signal_states.user_mask),SIGUSR1);
	sigdelset(&(signal_states.user_mask),SIGTSTP);
	sigdelset(&(signal_states.user_mask),SIGKILL);
	sigdelset(&(signal_states.user_mask),SIGSTOP);
	sigdelset(&(signal_states.user_mask),SIGCONT);
	sigprocmask(SIG_SETMASK,&(signal_states.user_mask),NULL);

	/* Restore signal stack pointer */
	sigstack( &(signal_states.sig_stack), (struct sigstack *) 0 );  

	/* Restore pending signals, again ignoring special Condor sigs */
	mypid = getpid();
	for ( sig=1; sig < signal_states.nsigs; sig++ ) {
		switch (sig) {
			/* Do not fiddle with the special Condor signals under
			 * any circumstance! */
			case SIGUSR1:
			case SIGTSTP:
			case SIGKILL:
			case SIGSTOP:
			case SIGCONT:
				continue;
		}
		if ( sigismember(&(signal_states.pending),sig) == 1 ) {
			kill(mypid,sig);
		}
	}

	(void) SetSyscalls( scm );
}

#if defined (SYS_sigvec)
#if defined(HPUX9)
sigvector( sig, vec, ovec )
#else
sigvec( sig, vec, ovec )
#endif
int sig;
const struct sigvec *vec;
struct sigvec *ovec;
{
	int rval;
	struct	sigvec	nvec;

	if( ! MappingFileDescriptors() ) {
#if defined(HPUX9)
			rval = syscall( SYS_sigvector, sig, vec, ovec );
#elif defined(SYS_sigvec)
			rval = syscall( SYS_sigvec, sig, vec, ovec );
#else
			rval = SIGVEC( sig, vec, ovec );
#endif
	} else {
		switch (sig) {
				/* disallow the special Condor signals */
				/* this list could be shortened */
		case SIGUSR1:
		case SIGTSTP:
		case SIGCONT:
			rval = -1;
			errno = EINVAL;
			break;
		default:
			if (vec) {
				bcopy( (char *) vec, (char *) &nvec, sizeof nvec );
				/* while in the handler, block checkpointing */
				nvec.sv_mask |= sigmask( SIGTSTP );
				nvec.sv_mask |= sigmask( SIGUSR1 );
			}
#if defined(HPUX9)
			rval = syscall( SYS_sigvector, sig, vec ? &nvec : vec, ovec );
#elif defined(SYS_sigvec) && !defined(SUNOS41)
			/* even though SunOS 4.x has a SYS_sigvec kernel call, all the
			 * trampoline code is setup in libc.a, not the kernel!  thus,
			 * we cannot call syscall here on SunOS; call SIGVEC() below. */
			rval = syscall( SYS_sigvec, sig, vec ? &nvec : vec, ovec );
#else
			rval = SIGVEC( sig, vec ? &nvec : vec, ovec );
#endif
			break;
		}
	}

	return( rval );
}
#endif

#if defined (SYS_sigvec)
#if defined(HPUX9)
_sigvector( sig, vec, ovec )
#else
_sigvec( sig, vec, ovec )
#endif
int sig;
const struct sigvec *vec;
struct sigvec *ovec;
{
	int rval;
	struct	sigvec	nvec;

	if( ! MappingFileDescriptors() ) {
#if defined(HPUX9)
			rval = syscall( SYS_sigvector, sig, vec, ovec );
#elif defined(SYS_sigvec)
			rval = syscall( SYS_sigvec, sig, vec, ovec );
#else
			rval = SIGVEC( sig, vec, ovec );
#endif
	} else {
		switch (sig) {
				/* disallow the special Condor signals */
				/* this list could be shortened */
		case SIGUSR1:
		case SIGTSTP:
		case SIGCONT:
			rval = -1;
			errno = EINVAL;
			break;
		default:
			if (vec) {
				bcopy( (char *) vec, (char *) &nvec, sizeof nvec );
				/* while in the handler, block checkpointing */
				nvec.sv_mask |= sigmask( SIGTSTP );
				nvec.sv_mask |= sigmask( SIGUSR1 );
			}
#if defined(HPUX9)
			rval = syscall( SYS_sigvector, sig, vec ? &nvec : vec, ovec );
#elif defined(SYS_sigvec) && !defined(SUNOS41)
			rval = syscall( SYS_sigvec, sig, vec ? &nvec : vec, ovec );
#else
			rval = SIGVEC( sig, vec ? &nvec : vec, ovec );
#endif
			break;
		}
	}

	return( rval );
}
#endif

#if defined(SYS_sigblock)
MASK_TYPE 
sigblock( mask )
MASK_TYPE mask;
{
	MASK_TYPE condor_sig_mask;
	MASK_TYPE rval;

	if ( ! MappingFileDescriptors() ) {
		condor_sig_mask = ~0;
	} else {
		/* mask out the special Condor signals */
		condor_sig_mask = ~(sigmask( SIGUSR1 ) |
			 sigmask( SIGTSTP ) | sigmask(SIGCONT));
		mask &= condor_sig_mask;
	}
#if defined(SYS_sigblock)
	rval =  syscall( SYS_sigblock, mask ) & condor_sig_mask;
#else
	rval =  SIGBLOCK( mask ) & condor_sig_mask;
#endif
	return rval;
}
#endif

#if defined(SYS_sigpause)
MASK_TYPE 
sigpause( mask )
MASK_TYPE mask;
{
	MASK_TYPE condor_sig_mask;
	MASK_TYPE rval;

	if ( MappingFileDescriptors() ) {
		/* mask out the special Condor signals */
		condor_sig_mask = ~(sigmask( SIGUSR1 ) |
			 sigmask( SIGTSTP ) | sigmask(SIGCONT));
		mask &= condor_sig_mask;
	}
#if defined(SYS_sigpause)
	rval =  syscall( SYS_sigpause, mask );
#else
	rval =  SIGPAUSE( mask );
#endif
	return rval;
}
#endif

#if defined(SYS_sigsetmask)
MASK_TYPE 
sigsetmask( mask )
MASK_TYPE mask;
{
	MASK_TYPE condor_sig_mask;
	MASK_TYPE rval;

	if ( ! MappingFileDescriptors() ) {
		condor_sig_mask = ~0;
	} else {
		/* mask out the special Condor signals */
		condor_sig_mask = ~(sigmask( SIGUSR1 ) |
			 sigmask( SIGTSTP ) | sigmask(SIGCONT));
		mask &= condor_sig_mask;
	}
#if defined(SYS_sigsetmask)
	rval =  syscall( SYS_sigsetmask, mask ) & condor_sig_mask;
#else
	rval =  SIGSETMASK( mask ) & condor_sig_mask;
#endif
	return rval;
}
#endif

#if defined(SYS_sigaction)
int
sigaction( sig, act, oact )
int sig;
const struct sigaction *act;
struct sigaction *oact;
{
	if ( MappingFileDescriptors() ) {	
		switch (sig) {
			/* don't let user code mess with these signals */
			case SIGUSR1:
			case SIGTSTP:
			case SIGCONT:
				if ( act != NULL ) {
					errno = EINVAL;
					return -1;
				}
		}
		if ( act ) {
			/* block checkpointing inside users signal handler */
			sigaddset( &(act->sa_mask), SIGTSTP );
			sigaddset( &(act->sa_mask), SIGUSR1 );
		}
	}
#if defined(SYS_sigaction)
	return syscall(SYS_sigaction, sig, act, oact);
#else
	return SIGACTION( sig, act, oact);
#endif
}
#endif

#if defined(SYS_sigprocmask)
int
sigprocmask(how,set,oset)
int how;
const sigset_t *set;
sigset_t *oset;
{
	if ( MappingFileDescriptors() ) {
		if ( (how == SIG_BLOCK) || (how == SIG_SETMASK) ) {
			sigdelset(set,SIGUSR1);
			sigdelset(set,SIGTSTP);
			sigdelset(set,SIGCONT);
		}
	}
#if defined(SYS_sigprocmask)
	return syscall(SYS_sigprocmask,how,set,oset);
#else
	SIGPROCMASK(how,set,oset);
#endif
}
#endif

#if defined (SYS_sigsuspend)
int
sigsuspend(set)
#if defined(SUNOS41)
sigset_t *set;
#else
const sigset_t *set;
#endif
{
	if ( MappingFileDescriptors() ) {
		sigdelset(set,SIGUSR1);
		sigdelset(set,SIGTSTP);
		sigdelset(set,SIGCONT);
	}
#if defined(SYS_sigsuspend)
	return syscall(SYS_sigsuspend,set);
#else
	SIGSUSPEND(set);
#endif
}
#endif

#if defined(SYS_signal)
#if defined(VOID_SIGNAL_RETURN)
void (*
#else
int (*
#endif
signal(sig,func))(int)
int sig;
void (*func)(int);
{
	if ( MappingFileDescriptors() ) {	
		switch (sig) {
			/* don't let user code mess with these signals */
			case SIGUSR1:
			case SIGTSTP:
			case SIGCONT:
				errno = EINVAL;
				return SIG_ERR; 
		}
	}

#if defined(SYS_signal)

#if defined(VOID_SIGNAL_RETURN)
	return ( (void (*) (int)) syscall(SYS_signal,sig,func) );
#else
	return ( (int (*) (int)) syscall(SYS_signal,sig,func) );
#endif

#else   /* if !defined(SYS_signal) */

#if defined(VOID_SIGNAL_RETURN)
	return ( (void (*) (int)) SIGNAL(sig,func) );
#else
	return ( (int (*) (int)) SIGNAL(sig,func) );
#endif

#endif
}
#endif

