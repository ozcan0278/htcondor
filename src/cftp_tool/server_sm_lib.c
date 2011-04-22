#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// For disk quota checks
#include <sys/statvfs.h>

#include "server_sm_lib.h"

#include "server_sm/discovery.h"
#include "server_sm/negotiation.h"
#include "server_sm/transfer.h"
#include "server_sm/teardown.h"
#include "server_sm/error.h"

#include "client_lib.h"


const int STATE_NUM = 100; // Will need to change this later
const int ANNOUNCE_INTERVAL = 5; // As long as we don't have a client, we should announce presence every X seconds

void run_server( ServerArguments* arg)
{

	StateAction SM_States[STATE_NUM];
	ServerState  session_state;

		//-------------- Initialize SM States -----------------
	
	SM_States[S_UNKNOWN_ERROR]              = &State_UnknownError;
	SM_States[S_SEND_SESSION_CLOSE]         = &State_SendSessionClose;

	SM_States[S_RECV_SESSION_PARAMETERS]    = &State_ReceiveSessionParameters;
	SM_States[S_CHECK_SESSION_PARAMETERS]   = &State_CheckSessionParameters;
	SM_States[S_ACK_SESSION_PARAMETERS]     = &State_AcknowledgeSessionParameters;
	SM_States[S_RECV_CLIENT_READY]          = &State_ReceiveClientReady;

	SM_States[S_RECV_DATA_BLOCK]            = &State_ReceiveDataBlock;
	SM_States[S_ACK_DATA_BLOCK]             = &State_AcknowledgeDataBlock;

	SM_States[S_RECV_FILE_FINISH]           = &State_ReceiveFileFinish;
	SM_States[S_ACK_FILE_FINISH]            = &State_AcknowledgeFileFinish;

      //------------------------------------------------------- 

	memset( &session_state, 0, sizeof( ServerState ));

	session_state.server_info.server_name = (char*)malloc(256);
	session_state.server_info.server_port = (char*)malloc(16);
	session_state.oob_info.server_name = (char*)malloc(256);
	session_state.oob_info.server_port = (char*)malloc(16);
	
	memcpy( session_state.server_info.server_name, arg->lhost, 256);
	memcpy( session_state.server_info.server_port, arg->lport, 16);
	memcpy( session_state.oob_info.server_name, arg->lhost, 256);
	memcpy( session_state.oob_info.server_port, arg->lport, 16);

	session_state.server_info.server_socket = -1;
	session_state.oob_info.server_socket = -1;
	

	if( confirm_arguments( arg ) != 0)
	  {
	    return;
	  }

	session_state.arguments = arg;
	session_state.data_buffer = NULL;

	start_server( &session_state );
	if( session_state.last_error )
		{
				//Error has happened, TODO: handle here
			return;
		}
	
	start_oob( &session_state );
	if( session_state.last_error )
		{
				//Error has happened, TODO: handle here
			return;
		}



	handle_client( &session_state );
	if( session_state.last_error )
		{
				//Error happened while accepting client, TODO: handle here
			return;
		}



		//Prep structure for initial state
	session_state.state = S_RECV_SESSION_PARAMETERS;	
	session_state.phase = NEGOTIATION;
	session_state.last_error = 0;
	memset( session_state.error_string, 0, 256 );


	while( run_state_machine( SM_States, &session_state )==0 )
		{
				// Do nothing here, we wait till the SM kills us.
		}

		// We may want some clean up based on
	    // the final condition of session_state.
	
	
	post_transfer_loop( &session_state );
	
		// Clean up allocated memory

	if( session_state.oob_info.server_name )	
		free( session_state.oob_info.server_name );

	if( session_state.oob_info.server_port )	
		free( session_state.oob_info.server_port );

	if( session_state.server_info.server_name )	
		free( session_state.server_info.server_name );

	if( session_state.server_info.server_port )	
		free( session_state.server_info.server_port );

	if( session_state.client_info.client_name)
		free( session_state.client_info.client_name );

	if( session_state.client_info.client_port )
		free( session_state.client_info.client_port );

	if( session_state.data_buffer )
		free( session_state.data_buffer );

	if( session_state.local_file.filename )
		free( session_state.local_file.filename );

	if( session_state.session_parameters )
		free( session_state.session_parameters );

	

	return;
}



/*

  confirm_arguments


 */

int confirm_arguments( ServerArguments* arg)
{
	struct statvfs fiData;	
	long int disk_space;

		// Do we have a quota to check for?
	if( arg->quota != -1 )
		{
			if( statvfs( "./", &fiData ) < 0 )
				{
					fprintf( stderr, "Unable to confirm quota. Stat failed for './' \n" );
					return -1;
				}

			disk_space = fiData.f_bsize*fiData.f_bavail; 
			
			if( arg->debug )
				fprintf( stderr, "Diskspace: %ld, Quota: %ld \n", disk_space, arg->quota*1024l );

			if( disk_space < arg->quota*1024l )
				{
					fprintf( stderr, "Unable to satisfy quota. Insufficent disk space available. \n");
					return -1;
				}		
		}



		// Check for transfer path	

	if( access(arg->tpath, R_OK | W_OK ) != 0 )
		{
			fprintf(stderr, "Unable to access transfer path with READ/WRITE permissions.\n");
			fprintf(stderr, "Error: %s\n", strerror( errno ) );
			return -1;
		}



	return 0;
}



/*

  start_server


 */
void start_server( ServerState* state )
{

//Really rusty on socket api - refered to Beej's guide for this
//http://beej.us/guide/bgnet/output/html/multipage/syscalls.html	

	int status;	
	int sock;
	int results;
	int yes;
	struct addrinfo hints;
	struct addrinfo *servinfo;  // will point to the results
	struct sockaddr localsock_name;
	socklen_t addr_size;

	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

	if ((status = getaddrinfo(state->server_info.server_name,
							  state->server_info.server_port,
							  &hints,
							  &servinfo)) != 0) {
		sprintf(state->error_string, "Unable to resolve server: %s\n", gai_strerror(status));
		state->last_error = 1;
		return;
	}

	sock = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if( sock == -1)
		{
			sprintf( state->error_string, "Error on creating socket: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);
			return;
		}	


	// Clear up any old binds on the port
    // lose the pesky "Address already in use" error message
    yes = 1;
	if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
		sprintf( state->error_string, "Error on clearing old port bindings: %s\n", strerror( errno ) );
		state->last_error = 1;
		freeaddrinfo(servinfo);	
		return;
	} 


	results = bind( sock, servinfo->ai_addr, servinfo->ai_addrlen);
	if( results == -1)
		{
			sprintf( state->error_string, "Error on binding socket: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);	
			return;
		}

	if( strcmp( state->server_info.server_port , "0" ) == 0 )
		{
				// We grabbed a random port, so now we determine what it is
			addr_size = sizeof( localsock_name );
			getsockname( sock, &localsock_name, &addr_size );
			memset( state->server_info.server_port, 0, 16 );
			if( localsock_name.sa_family == AF_INET)
					// extract the address from the raw value
				sprintf( state->server_info.server_port, "%d", 
						 ntohs( ((struct sockaddr_in*)(&localsock_name))->sin_port) ); 
			if( localsock_name.sa_family == AF_INET6 )
				sprintf( state->server_info.server_port, "%d", 
						 ntohs( ((struct sockaddr_in6*)(&localsock_name))->sin6_port) ); 
		}

	
	results = listen( sock, 1 ); // Using a backlog of 1 for this simple server
	if( results == -1)
		{
			sprintf( state->error_string, "Error on listening: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);
			return;
		}	

    state->server_info.server_socket = sock;
	//We fill in a ServerRecord so we don't need this anymore.
	freeaddrinfo(servinfo);

	return;
}

/*

  start_server


 */
void start_oob( ServerState* state )
{
	
	int sockfd;
    struct addrinfo hints, *servinfo;
    int rv;
	int yes;
 	int results;

	memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

		// We are re-using the address and port of our TCP transfer socket
	memcpy( state->oob_info.server_name, state->server_info.server_name, 256);
	memcpy( state->oob_info.server_port, state->server_info.server_port, 16);


    if ((rv = getaddrinfo( state->oob_info.server_name,
						   state->oob_info.server_port,
						   &hints, &servinfo)) != 0)
		{
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
			return;
		}

	sockfd = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if( sockfd == -1)
		{
			sprintf( state->error_string, "Error on creating OOB socket: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);
			return;
		}	


	// Clear up any old binds on the port
    // lose the pesky "Address already in use" error message
    yes = 1;
	if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1)
		{
			sprintf( state->error_string, "Error on clearing old port bindings: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);	
			return;
		} 
	

	results = bind( sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
	if( results == -1)
		{
			sprintf( state->error_string, "Error on binding OOB socket: %s\n", strerror( errno ) );
			state->last_error = 1;
			freeaddrinfo(servinfo);	
			return;
		}
	
	state->oob_info.server_socket = sockfd;
		//We fill in a ServerRecord so we don't need this anymore.
	freeaddrinfo(servinfo);
	
	
}



/*



announce_server

*/

void announce_server( ServerState* state )
{
	
	int numbytes;
	char message[512];
    struct addrinfo hints, *servinfo;
	int rv;
	
	
	memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
	if ((rv = getaddrinfo( state->arguments->ahost,
						   state->arguments->aport,
						   &hints, &servinfo)) != 0)
		{
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
			return;
		}

	memset( message, 0, 512 );
	sprintf( message, "%s %s %s\n",
			 state->server_info.server_name,
			 state->server_info.server_port,
			 state->arguments->uuid
			 );
	
	if ((numbytes = sendto(state->oob_info.server_socket,
						   message, strlen( message ), 0,
						   servinfo->ai_addr, servinfo->ai_addrlen)) == -1)
		{
			perror("announce server: sendto");
			return;
		}
	
	freeaddrinfo(servinfo);

    return;

}




/*

handle_client

*/
void handle_client( ServerState* state )
{

	int results;

	struct sockaddr_in* caddr_ip4;
	struct sockaddr_in6* caddr_ip6;

	struct sockaddr_storage client_addr;
    socklen_t addr_size;	
	fd_set accept_set;
	struct timeval timeout_tv;
	unsigned long int total_time_waited;
	unsigned long int max_wait;

	VERBOSE( "Waiting for client connection...\n");
	if( state->arguments->announce )
		VERBOSE( "\tAnnouncing server presence every %d seconds.\n", ANNOUNCE_INTERVAL );

	fcntl(state->server_info.server_socket, F_SETFL, O_NONBLOCK);
	
	total_time_waited = 0;
	if( state->arguments->itimeout == -1 )
		{
			max_wait = ULONG_MAX;
			VERBOSE( "\tMax client wait time is forever.\n");
		}
	else
		{
			max_wait = state->arguments->itimeout;
			VERBOSE( "\tMax client wait time is %ld seconds.\n", max_wait );
		}
	results = 0;


	while( total_time_waited < max_wait && results == 0)
		{
			DEBUG( "\tTotal Time: %ld\n", total_time_waited );
			FD_ZERO(&accept_set);
			FD_SET( state->server_info.server_socket, &accept_set );
			timeout_tv.tv_sec = ANNOUNCE_INTERVAL;
			timeout_tv.tv_usec = 0;

			results = select(state->server_info.server_socket+1, &accept_set,
							 NULL, NULL, &timeout_tv);
			
			if( state->arguments->announce )
				announce_server(  state );
			
			total_time_waited += ANNOUNCE_INTERVAL;
		}

	if( results == -1 )	
		{	
			sprintf( state->error_string, "Error on accepting: %s\n", strerror( errno ) );
			state->last_error = 1;
			return;
		}	
	if( results == 0 )
		{
			sprintf( state->error_string, "No client connected within initial wait period. Exiting.\n" );
			state->last_error = 1;
			return;
		}

	addr_size = sizeof( client_addr );
	results = accept( state->server_info.server_socket,
					  (struct sockaddr *)&client_addr,
					  &addr_size );

	if( results == -1)
		{
			sprintf( state->error_string, "Error on accepting: %s\n", strerror( errno ) );
			state->last_error = 1;
			return;
		}	
	
	state->client_info.client_socket = results;
	state->client_info.client_name = NULL;
	state->client_info.client_port = NULL;

		// Test if the client is using a IP4 address
	if( client_addr.ss_family == AF_INET )
		{
			DEBUG( "Client is using IP4.\n" );

			caddr_ip4 = (struct sockaddr_in*)(&client_addr);

			// Allocate enough space for an IP4 address
			state->client_info.client_name = (char*) malloc( INET_ADDRSTRLEN ); 
			memset( state->client_info.client_name, 0, INET_ADDRSTRLEN );
			
			inet_ntop( AF_INET, &(caddr_ip4->sin_addr),
					   state->client_info.client_name, INET_ADDRSTRLEN);

			// 10 chars will hold a IP4 port
			state->client_info.client_port = (char*) malloc( 10 ); 
			memset( state->client_info.client_port, 0, 10 );
			
			// extract the address from the raw value
			sprintf( state->client_info.client_port, "%d", 
					 ntohs( caddr_ip4->sin_port) );   // port number
		}

		// Test if the client is using a IP6 address
	if( client_addr.ss_family == AF_INET6 )
		{
			DEBUG( "Client is using IP6.\n" );


			caddr_ip6 = (struct sockaddr_in6*)(&client_addr);

			// Allocate enough space for an IP6 address
			state->client_info.client_name = (char*) malloc( INET6_ADDRSTRLEN ); 
			memset( state->client_info.client_name, 0, INET6_ADDRSTRLEN );
			
			inet_ntop( AF_INET6, &(caddr_ip6->sin6_addr),
					   state->client_info.client_name, INET6_ADDRSTRLEN);

			// 10 chars will hold a IP6 port
			state->client_info.client_port = (char*) malloc( 10 ); 
			memset( state->client_info.client_port, 0, 10 );
			
			// extract the address from the raw value
			sprintf( state->client_info.client_port, "%d", 
					 ntohs( caddr_ip6->sin6_port) );   // port number
		}


		if( state->client_info.client_name && state->client_info.client_port )
			DEBUG( "The client is %s at port %s.\n\n", state->client_info.client_name, state->client_info.client_port );


	VERBOSE( "Client accepted.\n\n" )
	state->last_error = 0;
	return;

}





/*


run_state_machine 

*/
int run_state_machine( StateAction* states, ServerState* state )
{
	int condition;


	if( state->state == -1 )
		return 1; // We are done here
	else	
		{
			condition = states[state->state]( state );
			recv_cftp_frame( state );
			state->state = transition_table( state, condition );
			return 0;
		}
}






/* 

   post_transfer_loop

*/

int post_transfer_loop( ServerState* state )
{

	int numbytes;
	char message[512];
    struct addrinfo hints, *servinfo;
	int rv;
	fd_set accept_set;
	struct timeval timeout_tv;
	unsigned long int total_time_waited;
	unsigned long int max_wait;
	unsigned long int next_wait;
	int results;
	
	if( !state->arguments->announce )
		{
			VERBOSE( "Goodbye!\n\n" );
			return 0;
		}

	memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
	if ((rv = getaddrinfo( state->arguments->ahost,
						   state->arguments->aport,
						   &hints, &servinfo)) != 0)
		{
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
			return -1;
		}



		// Create socket set for select
	fcntl(state->server_info.server_socket, F_SETFL, O_NONBLOCK);
	
	total_time_waited = 0;
	max_wait = state->arguments->ptimeout;
	results = 0;

	if(max_wait)
		{
			VERBOSE( "Announcing server continuing presence...\n" );
			VERBOSE( "\tAnnouncing server presence every %d seconds.\n", ANNOUNCE_INTERVAL );
			VERBOSE( "\tServer persisting for %ld more seconds.\n", max_wait );
		}

	while( total_time_waited < max_wait )
		{
			DEBUG( "\tTotal Time: %ld\n", total_time_waited );
			FD_ZERO(&accept_set);
			FD_SET( state->oob_info.server_socket, &accept_set );
			next_wait = (max_wait - total_time_waited) < ANNOUNCE_INTERVAL ? (max_wait - total_time_waited) : ANNOUNCE_INTERVAL;
			timeout_tv.tv_sec = next_wait;
			timeout_tv.tv_usec = 0;

			results = select(state->oob_info.server_socket+1, &accept_set, NULL, NULL, &timeout_tv);
			
			if(results > 0)
				handle_oob_command( state );
			if(results == -1)
				printf( "Error: %s\n", strerror( errno ) );

			total_time_waited += next_wait;

			memset( message, 0, 512 );
			sprintf( message, "%s %s %s %s %ld\n",
					 state->server_info.server_name,
					 state->server_info.server_port,
					 state->arguments->uuid,
					 state->local_file.filename,
					 max_wait - total_time_waited
					 );
			
			if ((numbytes = sendto(state->oob_info.server_socket,
						   message, strlen( message ), 0,
								   servinfo->ai_addr,
								   servinfo->ai_addrlen)) == -1)
				{
					perror("announce server: sendto");
					freeaddrinfo(servinfo);
					return -1;
				}
			
			
		}

		// Final Death message
	memset( message, 0, 512 );
	sprintf( message, "%s %s %s %s %s\n",
			 state->server_info.server_name,
			 state->server_info.server_port,
			 state->arguments->uuid,
			 state->local_file.filename,
			 "KILLED"
			 );
	
	if ((numbytes = sendto(state->oob_info.server_socket,
						   message, strlen( message ), 0,
						   servinfo->ai_addr,
						   servinfo->ai_addrlen)) == -1)
		{
			perror("announce server: sendto");
			freeaddrinfo(servinfo);
			return -1;
		}
	


	VERBOSE( "Goodbye!\n\n" );
	freeaddrinfo(servinfo);
	return 0;

}


/*


 handle_oob_command 

*/
void handle_oob_command( ServerState* state )
{

	int byte_count;
	socklen_t fromlen;
	struct sockaddr_storage addr;
	char buf[1024];
	char command[100];
	char arg1[100];
	char arg2[100];
	char arg3[100];
	char arg4[100];
	char arg5[100];
	int num_args;

	DEBUG( "OOB CHANNEL ACTIVATED\n" );

		// Check to see if we are actually getting a command
	fromlen = sizeof addr;
	buf[8] = 0;
	byte_count = readOOB(state->oob_info.server_socket,
						 buf, 1024, 0,
						 &addr, &fromlen,
						 2, '\n'
						 );

    // If this is not a command we will return
	if( strncmp( buf, "COMMAND:", 8 )) 
		{
			DEBUG( "OOB CHANNEL TERMINATED: No Command Phrase\n" );
			return;
		}


	VERBOSE( "Received Command: %s...", buf );
	num_args = sscanf( buf+8, "%s %s %s %s %s %s", command,
					   arg1, arg2, arg3, arg4, arg5 );

	if( strcmp( command, "SENDTO" ) == 0 && num_args == 3)
		{
			VERBOSE( "Executing.\n");
			
			
			transfer_file( arg1,
						   arg2,
						   state->local_file.filename );
		}
	else
		{
			VERBOSE( "Not Recognised.\n" );
		}
			

	DEBUG( "OOB CHANNEL TERMINATED\n" );
	return;

}

/*


transition_table

*/
int transition_table( ServerState* state, int condition )
{

	switch( state->state )
		{


		case S_RECV_SESSION_PARAMETERS:
			if( state->frecv_buf.MessageType != SIF )
				{
					sprintf( state->error_string, 
							 "No frame received from client or bad frame type.");
					return S_UNKNOWN_ERROR;
				}
			else		
				return S_CHECK_SESSION_PARAMETERS;
			


		case S_CHECK_SESSION_PARAMETERS:
			if( condition == -1 ) // Something bad happened here
				return S_UNKNOWN_ERROR;

				//TODO: Need to handle parameter constraint failures

			return S_ACK_SESSION_PARAMETERS;



		case S_ACK_SESSION_PARAMETERS:
			if( state->frecv_buf.MessageType == SRF )
				return S_RECV_CLIENT_READY;
			
			if( state->frecv_buf.MessageType == SCF )
				return S_UNKNOWN_ERROR; // TODO: Create a state for client connection close

			return S_UNKNOWN_ERROR; 



		case S_RECV_CLIENT_READY:
			if( condition == -1 ) //Something bad happened with the file pointer
				return S_UNKNOWN_ERROR;
					
			return S_RECV_DATA_BLOCK;



		case S_RECV_DATA_BLOCK:
			if( state->frecv_buf.MessageType == DTF )
				return S_ACK_DATA_BLOCK;
			
			if( state->frecv_buf.MessageType == FFF )
				return S_ACK_FILE_FINISH;

			return S_UNKNOWN_ERROR;


		case S_ACK_DATA_BLOCK:
			if( condition == -1 )
				return S_UNKNOWN_ERROR;
			if( condition == 1 )
				return S_RECV_FILE_FINISH;

			return S_RECV_DATA_BLOCK;

			
		case S_RECV_FILE_FINISH:
			if( state->frecv_buf.MessageType == FFF )
				return S_ACK_FILE_FINISH;


		case S_ACK_FILE_FINISH:
			return -1; // Probably need to do more here, but thats for later

					

			
		case S_UNKNOWN_ERROR:
			return -1; // If we have completed an unknown error, we should die.



		default:
			return S_UNKNOWN_ERROR;
			break;
		}
}



/*


recv_cftp_frame


*/
int recv_cftp_frame( ServerState* state )
{
	if( !state->recv_rdy )
		return -1;
	state->recv_rdy = 0;

	memset( &state->frecv_buf, 0, sizeof( cftp_frame ) );

	recv( state->client_info.client_socket, 	
		  &state->frecv_buf,	
		  sizeof( cftp_frame ),
		  MSG_WAITALL );

	DEBUG("Read %ld bytes from client on frame_recv.\n", sizeof( cftp_frame ) );
	desc_cftp_frame(state, 0 );

	return sizeof( cftp_frame );
}



/*


recv_data_frame


*/
int recv_data_frame( ServerState* state )
{
	if( !state->recv_rdy )
		return -1;
	state->recv_rdy = 0;

	int recv_bytes;
		//int i;

	if( state->data_buffer_size <= 0 )
		return 0;

	if( state->data_buffer )
		free( state->data_buffer );

	state->data_buffer = (char*) malloc( state->data_buffer_size );
	memset( state->data_buffer, 0, state->data_buffer_size );

	recv_bytes = recv( state->client_info.client_socket, 	
					   state->data_buffer,	
					   state->data_buffer_size,
					   MSG_WAITALL );

	DEBUG("Read %d bytes from client on data_recv.\n", recv_bytes );
		/*
    for( i = 0; i < recv_bytes; i ++ )
		{
			if( i % 32 == 0 )
				fprintf( stderr, "\n" );
			fprintf( stderr, "%c", (char)*(state->data_buffer+i));
		}
	fprintf( stderr, "\n" );
		*/
	
	return recv_bytes;
}




/*


send_cftp_frame

*/
int send_cftp_frame( ServerState* state )
{
	int length = 0;
	int status = 0;

	if( !state->send_rdy )
		return -1;
	state->send_rdy = 0;

	length = sizeof( cftp_frame );
	status = sendall( state->client_info.client_socket,
					  (char*)(&state->fsend_buf),
					  &length );

	if( status == -1 )
		{
			state->last_error = 1;
			sprintf( state->error_string, 
					 "Could not send CFTP frame: %s", strerror(errno));
			return 0;
		}
	else
		{
			DEBUG("Sent %d bytes to client on frame_send.\n", length );
			desc_cftp_frame(state, 1 );
			return length;
		}
}


/*


send_data_frame

*/
int send_data_frame( ServerState* state )
{
	int length = 0;
	int status = 0;

	if( !state->send_rdy )
		return -1;
	state->send_rdy = 0;

	length = state->data_buffer_size;
	if( length <= 0 )
		return 0;
	
	if( !state->data_buffer )
		{
			state->last_error = 1;
			sprintf( state->error_string, 
					 "Could not send data: Data buffer null");
			return 0;
		}

	status = sendall( state->client_info.client_socket,
					  (char*)(&state->data_buffer),
					  &length );

	if( status == -1 )
		{
			state->last_error = 1;
			sprintf( state->error_string, 
					 "Could not send data: %s", strerror(errno));
			return 0;
		}
	else
		{ 
			DEBUG("Sent %d bytes to client on data_send.\n", length );
			return length;
		}
}




/*

desc_cftp_frame




 */
void desc_cftp_frame( ServerState* state, int send_or_recv)
{
	cftp_frame* frame;

	if( send_or_recv == 1 )
		{
			frame = &state->fsend_buf;
			DEBUG("\tMessage type of Send Frame is " );
		}
	else
		{
			frame = &state->frecv_buf;
			DEBUG("\tMessage type of Recv Frame is " );
		}
	
	switch( frame->MessageType )
		{
		case DSF:
			DEBUG("DSF frame.\n" );
			break;
		case DRF:
			DEBUG("DRF frame.\n" );
			break;
		case SIF:
			DEBUG("SIF frame.\n" );
			break;
		case SAF:
			DEBUG("SAF frame.\n" );
			break;
		case SRF:
			DEBUG("SRF frame.\n" );
			break;
		case SCF:
			DEBUG("SCF frame.\n" );
			break;
		case DTF:
			DEBUG("DTF frame.\n" );
			DEBUG("\tChunk Number: %ld\n" , ntohll(((cftp_dtf_frame*)frame)->BlockNum) );
			break;
		case DAF:
			DEBUG("DAF frame.\n" );
			DEBUG("\tChunk Number: %ld\n" , ntohll(((cftp_daf_frame*)frame)->BlockNum) );	
			break;
		case FFF:
			DEBUG("FFF frame.\n" );
			break;
		case FAF:
			DEBUG("FAF frame.\n" );
			break;
		default:
			DEBUG("Unknown frame.\n" );
			break;
		}


}
