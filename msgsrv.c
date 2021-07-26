/*! \file
*   \copyright obadawy
*   \brief     
*/
#include "msgsrv.h"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define EXIT_THREAD WSACleanup(); ExitThread(1)
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#define EXIT_THREAD pthread_exit(NULL)
#endif

static void serve_request( const uint32_t request[], uint32_t response[], const msgsrv_t *p_server );

/*! \brief The thread function the server process should call to start accepting commands */
#ifdef _WIN32
static DWORD WINAPI command_serve( LPVOID arg );
#else
static void *command_serve( void *arg );
#endif

int  start_msg_server( const msgsrv_t *server ) {
#ifdef _WIN32
    DWORD tid;
    HANDLE th = CreateThread( NULL, 0, command_serve, (LPVOID) server, 0, &tid );
#else
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init( &attr );
    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
    pthread_create( &tid, &attr, &command_serve, (void *) server );
#endif
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI command_serve( LPVOID arg ) {
#else
void *command_serve( void *arg ) {
#endif

    const msgsrv_t *p_server = NULL;
    int port = 0;
    if( arg ) {
        p_server = (const msgsrv_t *) arg;
        port = p_server->port;
    } else {
        EXIT_THREAD;
    }

    // Create the socket
#ifdef _WIN32
    WSADATA wsaData;
    int res = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
    if( res != 0 ) {
        fprintf( stderr, "WSAStartup failed: %d\n", res );
        EXIT_THREAD;
    }
    SOCKET sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( sock == INVALID_SOCKET ) {
        fprintf( stderr, "Error at socket(): %ld\n", WSAGetLastError() );
        EXIT_THREAD;
    }
#else
    int sock = socket( AF_INET, SOCK_STREAM, 0 );
    if( sock < 0 ) {
        perror( "socket (server)" );
        close( sock );
        fprintf( stderr, "Exiting thread\n" );
        EXIT_THREAD;
    }
#endif

    // Bind to port
    struct sockaddr_in servername;
    servername.sin_family = AF_INET;
    servername.sin_port = htons( port );
    servername.sin_addr.s_addr = htonl( INADDR_ANY );
#ifdef _WIN32
    if( SOCKET_ERROR == bind( sock, ( struct sockaddr * ) &servername, sizeof( servername ) ) ) {
        closesocket( sock );
        fprintf( stderr, "bind failed with error: %d\n", WSAGetLastError() );
        EXIT_THREAD;
    }
#else
    if( 0 != bind( sock, (struct sockaddr *) &servername, sizeof( servername ) ) ) {
        perror( "bind (server)" );
        close( sock );
        fprintf( stderr, "Exiting thread\n" );
        EXIT_THREAD;
    }
#endif

    // Enable up to 5 connection requests (i.e. listen)
#ifdef _WIN32
    if( SOCKET_ERROR == listen( sock, 5 ) ) {
        closesocket( sock );
        fprintf( stderr, "Listen failed with error: %ld\n", WSAGetLastError() );
        EXIT_THREAD;
    }
#else
    if( 0 != listen( sock, 5 ) ) {
        perror( "listen (server)" );
        close( sock );
        fprintf( stderr, "Exiting thread\n" );
        EXIT_THREAD;
    }
#endif
    printf( "Listening for messages on port %d\n", port );

    // Initialize the set of active sockets.
    fd_set active_fd_set;
    FD_ZERO( &active_fd_set );
    FD_SET( sock, &active_fd_set );

    // WINDOWS
#ifdef _WIN32
    while( 1 ) {

        fd_set read_fd_set = active_fd_set;
        fd_set write_fd_set = active_fd_set;
        fd_set except_fd_set = active_fd_set;

        // Block until input arrives on one or more active sockets.
        int nselect = select( FD_SETSIZE, &read_fd_set, 0, &except_fd_set, NULL );
        if( nselect == SOCKET_ERROR ) {
            if( WSAGetLastError() == WSAEINTR ) {
                fprintf( stderr, "Ingoring interrupt." );
                continue;
            }
            closesocket( sock );
            fprintf( stderr, "Listen failed with error: %ld\n", WSAGetLastError() );
            EXIT_THREAD;
        }
        //fprintf( stderr, "%d clients need attention.\n", nselect );

        // Service all the sockets with input pending.
        for( int i = 0; i < FD_SETSIZE; ++i ) {
            if( FD_ISSET( i, &read_fd_set ) ) {
                if( i == sock ) {
                    // Connection request on original socket.
                    struct sockaddr_in clientname;
                    socklen_t size = sizeof( clientname );
                    int newc = accept( sock, (struct sockaddr *) &clientname, &size );
                    if( newc < 0 ) {
                        perror( "accept" );
                    } else {
                        fprintf( stderr, "." );
                        /*fprintf( stderr, "\nMessage Server connection from %s, port %u, socket %d\n",
                                          inet_ntoa( clientname.sin_addr ), 
                                          ntohs( clientname.sin_port ), 
                                          newc );*/
                        FD_SET( newc, &active_fd_set );
                    }
                } else {
                    uint32_t request[REQUEST_SIZE] = {0};
                    /*! \todo: unblock and report if no response received after a few seconds */
                    /*! \todo: include recv in a loop until all expected bytes are received */
                    int nread = recv( i, (char *) request, sizeof( request ), 0 );
                    if( nread == 0 ) {
                        fprintf( stderr, "Closing socket %d\n", i );
                        closesocket( i );
                        FD_CLR( i, &active_fd_set );
                    } else if( nread == SOCKET_ERROR ) {
                        fprintf( stderr, "recv failed with error: %ld\n", WSAGetLastError() );
                        fprintf( stderr, "Closing socket %d\n", i );
                        closesocket( i );
                        FD_CLR( i, &active_fd_set );
                    } else {
                        uint32_t response[RESPONSE_SIZE] = {0};
                        serve_request( request, response, p_server );
                        int nbytes = send( i, (char *) response, sizeof( response ), 0 );
                        if( nbytes == SOCKET_ERROR ) {
                            fprintf( stderr, "send failed with error: %ld\n", WSAGetLastError() );
                            if( WSAGetLastError() == EPIPE || errno == WSAECONNRESET ) {
                                fprintf( stderr, "Closing socket %d\n", i );
                                closesocket( i );
                                FD_CLR( i, &active_fd_set );
                            } else {
                                fprintf( stderr, "Error: %d\n", WSAGetLastError() );
                                fprintf( stderr, "Closing socket %d\n", i );
                                closesocket( i );
                                closesocket( sock );
                                fprintf( stderr, "Exiting thread\n" );
                                EXIT_THREAD;
                            }
                        } 
                    }
                }
            }
            // Service all the sockets ready for output.
            if( FD_ISSET( i, &write_fd_set ) ) {
            }

            // Service all the sockets with exceptions
            if( FD_ISSET( i, &except_fd_set ) ) {
                fprintf( stderr, "Socket %d has an exception\n", i );
            }

        }
    }
#else
    // POSIX
    while( 1 ) {

        fd_set read_fd_set = active_fd_set;
        fd_set write_fd_set = active_fd_set;
        fd_set except_fd_set = active_fd_set;

        // Block until input arrives on one or more active sockets.
        int nselect = select( FD_SETSIZE, &read_fd_set, 0, &except_fd_set, NULL );
        if( 0 > nselect ) {
            perror( "select" );
            if (errno == EINTR) {
                fprintf(stderr, "Ingoring interrupt.");
                continue;
            }
            close( sock );
            fprintf( stderr, "Exiting thread\n" );
            EXIT_THREAD;
        }
        //fprintf( stderr, "%d clients need attention.\n", nselect );

        // Service all the sockets with input pending.
        for( int i = 0; i < FD_SETSIZE; ++i ) {
            if( FD_ISSET( i, &read_fd_set ) ) {
                if( i == sock ) {
                    // Connection request on original socket.
                    struct sockaddr_in clientname;
                    socklen_t size = sizeof( clientname );
                    int newc = accept( sock, (struct sockaddr *) &clientname, &size );
                    if( newc < 0 ) {
                        perror( "accept" );
                    } else {
                        fprintf( stderr, "." );
                        /*fprintf( stderr, "\nMessage Server connection from %s, port %u, socket %d\n",
                                          inet_ntoa( clientname.sin_addr ), 
                                          ntohs( clientname.sin_port ), 
                                          newc );*/
                        FD_SET( newc, &active_fd_set );
                    }
                } else {
                    uint32_t request[REQUEST_SIZE] = {0};
                    /*! \todo: unblock and report if no response received after a few seconds */
                    /*! \todo: include recv in a loop until all expected bytes are received */
                    ssize_t nread = recv( i, request, sizeof( request ), 0 );
                    if( nread == 0 ) {
                        fprintf( stderr, "Closing socket %d\n", i );
                        close( i );
                        FD_CLR( i, &active_fd_set );
                    } else if( nread < 0 ) {
                        perror( "recv" );
                        fprintf( stderr, "Closing socket %d\n", i );
                        close( i );
                        FD_CLR( i, &active_fd_set );
                    } else {
                        uint32_t response[RESPONSE_SIZE] = {0};
                        serve_request( request, response, p_server );
                        ssize_t nbytes = send( i, response, sizeof( response ), 0 );
                        if( nbytes < 0 ) {
                            perror( "server - send()" );
                            if( errno == EPIPE || errno == ECONNRESET ) {
                                fprintf( stderr, "Closing socket %d\n", i );
                                close( i );
                                FD_CLR( i, &active_fd_set );
                            } else {
                                fprintf( stderr, "Error: %d\n", errno );
                                fprintf( stderr, "Closing socket %d\n", i );
                                close( i );
                                close( sock );
                                fprintf( stderr, "Exiting thread\n" );
                                EXIT_THREAD;
                            }
                        } 
                    }
                }
            }
            // Service all the sockets ready for output.
            if( FD_ISSET( i, &write_fd_set ) ) {
            }

            // Service all the sockets with exceptions
            if( FD_ISSET( i, &except_fd_set ) ) {
                fprintf( stderr, "Socket %d has an exception\n", i );
            }

        }
    }
#endif

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/*! \todo Consider making handlers return RESPONSE_* directly.
  */
static void serve_request( const uint32_t request[], uint32_t response[], const msgsrv_t *p_server ) {

    response[0] = RESPONSE_NOTCMD;
    unsigned int i;

    assert( p_server->ncommands <= MAX_COMMANDS );

    for( i=0; i < p_server->ncommands; i++ ) {
        if( p_server->command[i] == request[0] ) {
            if( p_server->handler[i] ) {
                response[0] = RESPONSE_ERROR;
                int ret = p_server->handler[i]( request, REQUEST_SIZE, response, RESPONSE_SIZE );
                if( ret == 0 ) {
                    response[0] = RESPONSE_OK;
                }
            } else {
                response[0] = RESPONSE_NOTIMP;
            }
            break;
        } 
    }
}

