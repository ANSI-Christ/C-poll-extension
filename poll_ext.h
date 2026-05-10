/* * * * * * * * * * * * * * * * * */
/* MIT License                     */
/* Copyright (c) 2024 ANSI-Christ  */
/* * * * * * * * * * * * * * * * * */

#ifndef POLL_EXT_H
#define POLL_EXT_H

#include <stddef.h>

typedef struct{
    void*(*allocator)(size_t size);
    void(*deallocator)(void *ptr);
    size_t(*increase)(size_t old_size);
    void(*mutex)(int cmd); /* cmd: 0 - unlock, 1 - lock */
    unsigned int reserv; /* default: 192 on stack */
    unsigned int wsa;
    int get_result; /* sets -1 on error and 1 on success*/
}poll_config_t;

void *poll_loop(poll_config_t *cfg);
void poll_unloop(void);

int poll_recv(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_send(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_both(void *os_socket,void(*callback)(int err,void *arg),void *arg);

int poll_recv_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_send_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_both_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);

/* usefull helpers */
int socket_unblock(void *os_socket);

#endif /* POLL_EXT_H */
