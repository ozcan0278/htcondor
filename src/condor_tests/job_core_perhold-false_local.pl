#!/usr/bin/env perl
##**************************************************************
##
## Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
## University of Wisconsin-Madison, WI.
## 
## Licensed under the Apache License, Version 2.0 (the "License"); you
## may not use this file except in compliance with the License.  You may
## obtain a copy of the License at
## 
##    http://www.apache.org/licenses/LICENSE-2.0
## 
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##
##**************************************************************

##
## PERIOIDIC_HOLD - False
## Check to make sure that the job never goes on hold
##
use CondorTest;

$cmd = 'job_core_perhold-false_local.cmd';
$testname = 'Condor submit policy test for PERIOIDIC_HOLD - local U';

my %info;
my $cluster;

# truly const variables in perl
sub IDLE{1};
sub HELD{5};
sub RUNNING{2};

##
## executed
## Just announce that the job began execution
##
$executed = sub {
	%info = @_;
	$cluster = $info{"cluster"};
	$job = $info{"job"};
	CondorTest::debug("Good - Job $cluster.$job began execution.\n",1);
};

##
## held
## If the job went on hold, we need to abort
##
$held = sub {
	my $done;
	%info = @_;
	$cluster = $info{"cluster"};
	$job = $info{"job"};
	CondorTest::debug("Bad - Job $cluster.$job should not be on hold.\n",1);
	exit(1);
};

##
## success
##
$success = sub {
	my %info = @_;
	my $cluster = $info{"cluster"};
	my $job = $info{"job"};
	
	##
	## This probably isn't necessary, but just ot be safe we need to
	## check the status of the job and if it's on hold, call
	## the held() method
	##	
	if ( CondorTest::getJobStatus($cluster) == HELD ) {
		&$held( %info ) if defined $held;
		return;
	}
	CondorTest::debug("Good - Job $cluster.$job finished executing and exited.\n",1);
	CondorTest::debug("Policy Test Completed\n",1);
};

CondorTest::RegisterExecute($testname, $executed);
CondorTest::RegisterExitedSuccess( $testname, $success );
CondorTest::RegisterHold( $testname, $held );

if( CondorTest::RunTest($testname, $cmd, 0) ) {
	CondorTest::debug("$testname: SUCCESS\n",1);
	exit(0);
} else {
	die "$testname: CondorTest::RunTest() failed\n";
}
