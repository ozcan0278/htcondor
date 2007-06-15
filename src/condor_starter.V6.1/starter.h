/***************************Copyright-DO-NOT-REMOVE-THIS-LINE**
  *
  * Condor Software Copyright Notice
  * Copyright (C) 1990-2006, Condor Team, Computer Sciences Department,
  * University of Wisconsin-Madison, WI.
  *
  * This source code is covered by the Condor Public License, which can
  * be found in the accompanying LICENSE.TXT file, or online at
  * www.condorproject.org.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  * AND THE UNIVERSITY OF WISCONSIN-MADISON "AS IS" AND ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  * WARRANTIES OF MERCHANTABILITY, OF SATISFACTORY QUALITY, AND FITNESS
  * FOR A PARTICULAR PURPOSE OR USE ARE DISCLAIMED. THE COPYRIGHT
  * HOLDERS AND CONTRIBUTORS AND THE UNIVERSITY OF WISCONSIN-MADISON
  * MAKE NO MAKE NO REPRESENTATION THAT THE SOFTWARE, MODIFICATIONS,
  * ENHANCEMENTS OR DERIVATIVE WORKS THEREOF, WILL NOT INFRINGE ANY
  * PATENT, COPYRIGHT, TRADEMARK, TRADE SECRET OR OTHER PROPRIETARY
  * RIGHT.
  *
  ****************************Copyright-DO-NOT-REMOVE-THIS-LINE**/

#if !defined(_CONDOR_STARTER_H)
#define _CONDOR_STARTER_H

#include "../condor_daemon_core.V6/condor_daemon_core.h"
#include "list.h"
#include "user_proc.h"
#include "job_info_communicator.h"



/** The starter class.  Basically, this class does some initialization
	stuff and manages a set of UserProc instances, each of which 
	represent a running job.

	@see UserProc
 */
class CStarter : public Service
{
public:
		/// Constructor
	CStarter();
		/// Destructor
	virtual ~CStarter();

		/** This is called at the end of main_init().  It calls
			Config(), registers a bunch of signals, registers a
			reaper, makes the starter's working dir and moves there,
			sets resource limits, then calls StartJob()
		*/
	virtual bool Init( JobInfoCommunicator* my_jic, 
					   const char* orig_cwd, bool is_gridshell,
					   int stdin_fd, int stdout_fd, int stderr_fd );

		/** The starter is finally ready to exit, so handle some
			cleanup code we always need, then call DC_Exit() with the
			given exit code.
		*/
	virtual void StarterExit( int code );

		/** Params for "EXECUTE" and other useful stuff 
		 */
	virtual void Config();

	virtual int SpawnJob( void );

		/*************************************************************
		 * Starter Commands
		 * We now have two versions of the old commands that the Starter
		 * registers with daemon core. The "Remote" version of the 
		 * function is what will registered. These functions will
		 * first notify the JIC that an action is occuring then call
		 * the regular version of the function. The reason why we do this
		 * now is because we need to be able to do things internally
		 * without the JIC thinking it was told from the outside
		 *************************************************************/

		/** Call Suspend() on all elements in JobList */
	virtual int RemoteSuspend( int );
	virtual bool Suspend( void );

		/** Call Continue() on all elements in JobList */
	virtual int RemoteContinue( int );
	virtual bool Continue( void );

		/** To do */
	virtual int RemotePeriodicCkpt( int );
	virtual bool PeriodicCkpt( void );

		/** Call Remove() on all elements in JobList */
	virtual int RemoteRemove( int );
	virtual bool Remove( void );

		/** Call Hold() on all elements in JobList */
	virtual int RemoteHold(int);
	virtual bool Hold( void );

		/** Walk through list of jobs, call ShutDownGraceful on each.
			@return 1 if no jobs running, 0 otherwise 
		*/
	virtual int RemoteShutdownGraceful( int );
	virtual bool ShutdownGraceful( void );

		/** Walk through list of jobs, call ShutDownFast on each.
			@return 1 if no jobs running, 0 otherwise 
		*/
	virtual int RemoteShutdownFast( int );
	virtual bool ShutdownFast( void );

		/** Create the execute/dir_<pid> directory and chdir() into
			it.  This can only be called once user_priv is initialized
			by the JobInfoCommunicator.
		*/
	virtual bool createTempExecuteDir( void );
	
		/**
		 * Before a job is spawned, this method checks whether
		 * a job has a deferrral time, which means we will need
		 * to register timer to call SpawnPreScript()
		 * when it is the correct time to run the job
		 */
	virtual bool jobWaitUntilExecuteTime( void );
	
		/**
		 * Clean up any the timer that we have might
		 * have registered to put a job on hold. As of now
		 * there can only be one job on hold
		 */
	virtual bool removeDeferredJobs( void );
		
		/** Called by the JobInfoCommunicator whenever the job
			execution environment is ready so we can actually spawn
			the job.
		*/
	virtual int jobEnvironmentReady( void );
	
		/**
		 * 
		 * 
		 **/
	virtual int SpawnPreScript( void );

		/** Does final cleanup once all the jobs (and post script, if
			any) have completed.  This deals with everything on the
			CleanedUpJobList, notifies the JIC, etc.
		*/
	virtual bool allJobsDone( void );

		/** Handles the exiting of user jobs.  If we're shutting down
			and there are no jobs left alive, we exit ourselves.
			@param pid The pid that died.
			@param exit_status The exit status of the dead pid
		*/
	virtual int Reaper(int pid, int exit_status);

		/** Return the Working dir */
	const char *GetWorkingDir() const { return WorkingDir; }

		/** Publish all attributes we care about for our job
			controller into the given ClassAd.  Walk through all our
			UserProcs and have them publish.
            @param ad pointer to the classad to publish into
			@return true if we published any info, false if not.
		*/
	bool publishUpdateAd( ClassAd* ad );

	bool publishPreScriptUpdateAd( ClassAd* ad );
	bool publishPostScriptUpdateAd( ClassAd* ad );

		/** Put all the environment variables we'd want a Proc to have
			into the given Env object.  This will figure out what Proc
			objects we've got and will call their respective
			PublishToEnv() methods
			@param proc_env The environment to publish to
		*/
	void PublishToEnv( Env* proc_env );
	
		/** Pointer to our JobInfoCommuniator object, which abstracts
			away any details about our communications with whatever
			entity is controlling our job.  This way, the starter can
			easily run without talking to a shadow, by instantiating a
			different kind of jic.  We want this public, since lots of
			parts of the starter need to get to this thing, and it's
			just easier this way. :)
		*/
	JobInfoCommunicator* jic;

		/** Returns the slot number we're running on, or 0 if we're not
			running on a slot at all.
		*/
	int getMySlotNumber( void );

	bool isGridshell( void ) {return is_gridshell;};
	const char* origCwd( void ) {return (const char*) orig_cwd;};
	int starterStdinFd( void ) { return starter_stdin_fd; };
	int starterStdoutFd( void ) { return starter_stdout_fd; };
	int starterStderrFd( void ) { return starter_stderr_fd; };
	void closeSavedStdin( void );
	void closeSavedStdout( void );
	void closeSavedStderr( void );

		/** Command handler for ClassAd-only protocol commands */
	int classadCommand( int, Stream* );

	int updateX509Proxy( int cmd, Stream* );
protected:
	List<UserProc> JobList;
	List<UserProc> CleanedUpJobList;

private:

		// // // // // // // //
		// Private Methods
		// // // // // // // //

		/// Remove the execute/dir_<pid> directory
	virtual bool removeTempExecuteDir( void );

#if !defined(WIN32)
		/// Special cleanup for exiting after being invoked via glexec
	void exitAfterGlexec( int code );
#endif

		// // // // // // // //
		// Private Data Members
		// // // // // // // //

	int jobUniverse;

	char *Execute;
	char WorkingDir[_POSIX_PATH_MAX]; // The iwd given to the job
	char ExecuteDir[_POSIX_PATH_MAX]; // The scratch dir created for the job
	char *orig_cwd;
	bool is_gridshell;
	int ShuttingDown;
	int starter_stdin_fd;
	int starter_stdout_fd;
	int starter_stderr_fd;
	
		//
		// When set to true, that means the Starter was asked to
		// suspend all jobs. This is used when jobs are started up
		// after the Suspend call came in so that we don't start
		// jobs when we shouldn't
		//
	bool suspended;
	
		//
		// This is the id of the timer for when a job gets deferred
		//
	int deferral_tid;

	UserProc* pre_script;
	UserProc* post_script;
};

#endif


