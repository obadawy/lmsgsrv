/*! \file
*   \copyright obadawy
*   \brief     Allows processes to accept requests (e.g. commands, queries,
*              etc) from other processes, process them, and reply with a
*              response
*
*   \detail    To use the services provided by this modules, follow this example:
*
*               msgsrv_t server;        
*               server.port = PORT;
*               server.ncommands = 3;      
*               server.commands[0] = 0x10; 
*               server.commands[1] = 0x20; 
*               server.commands[2] = 0x30; 
*               server.handler[0] = f1;    
*               server.handler[1] = f2;    
*               server.handler[2] = f3;    
*
*               start_msg_server( &server );
*/
#ifndef MSGSRV_H 
#define MSGSRV_H

#ifdef __cplusplus
extern "C"{
#endif 

#include <stdint.h>
#include <stddef.h>    // size_t

#define MAX_COMMANDS       (32)
#define REQUEST_SIZE       (32)
#define RESPONSE_SIZE      (32)
#define RESPONSE_OK        (200)
#define RESPONSE_NOTCMD    (100)
#define RESPONSE_NOTIMP    (110)
#define RESPONSE_ERROR     (120)

#ifdef _WIN32
#if defined(VSPROJECT_EXPORTS)
#  define MSGSRV_API __declspec(dllexport)
#else
#  define MSGSRV_API __declspec(dllimport)
#endif
#else
#define MSGSRV_API
#endif



typedef struct {
    unsigned int port;        //! port the server process would like to listen on
    unsigned int ncommands;   //! number of commands the server process would like to accept
    uint32_t command[MAX_COMMANDS];   //! the commands
    int (*handler[MAX_COMMANDS]) ( const uint32_t request[], size_t reqsize, 
                                         uint32_t response[], size_t ressize );  //! command handlers
} msgsrv_t;

/*! Start Message Server.
 *  \param[in]  server  A pointer to the server structure
 *  \warning    server has to live for the life of the calling program
 */
#ifdef _WIN32
MSGSRV_API int __cdecl start_msg_server( const msgsrv_t *server );
#else
int start_msg_server( const msgsrv_t *server );
#endif

#ifdef __cplusplus
}
#endif

#endif /* MSGSRV_H */
