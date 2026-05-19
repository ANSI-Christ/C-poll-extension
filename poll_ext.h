/* * * * * * * * * * * * * * * * * */
/* MIT License                     */
/* Copyright (c) 2024 ANSI-Christ  */
/* * * * * * * * * * * * * * * * * */

#ifndef POLL_EXT_H
#define POLL_EXT_H

#include <stddef.h>

/* poll part */

typedef struct{
    void*(*allocator)(size_t size);
    void(*deallocator)(void *ptr);
    size_t(*resizer)(size_t size);
    void(*init)(int err,void *arg);
    void *iarg;
    unsigned int reserv;
}poll_config_t;

int poll_config(void *(*loops_buffer)[2],unsigned int max_loops,int WSA); /* WSA is for Winapi only: 0 -> (2,2), -1 -> no use */

void poll_loop(const poll_config_t cfg[1]);
void poll_unloop(int wait); /* signal to all loops to stop */
void poll_cleanup(void); /* cleanup global state */

/* poll_ functions below return [ 0, #INVAL, #LOOP, #NOBUFS^1 ], where '#' is OS native prefix [ E, WSAE ]. ^1: only on 'select' impl */
int poll_recv(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_send(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_both(void *os_socket,void(*callback)(int err,void *arg),void *arg);

int poll_recv_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_send_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_both_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);






/* usefull helpers */

struct netaddr{
    unsigned char len; /* [4,16] */
    unsigned char ip[16];
};

int DNS_request(void *os_socket,int family,const char *host_name); /* return number of requests sended or [-1, SOCKET_ERROR].*/
int DNS_response(void *os_socket,struct netaddr *buffer,unsigned int size); /* return  number of addresses received or [-1, SOCKET_ERROR]. */

int socket_mode(void *os_socket,int blocking); /* return result of fcntl / ioctlsocket */

void d2net(void *base_type_address);
void d2host(void *base_type_address);





/* macro detail */

#define d2net d2host
#define d2host(_p_) do{\
    struct static_assert_bad_type{char _1[(sizeof((_p_)[0]<9)) ? 1 : -1], _2[sizeof((_p_)[0]+=0.1)];};\
    if(sizeof(long)!=1){\
        const union{long _; struct{char ltl,pdp;}v;}_e_={1};\
        if(_e_.v.ltl)\
            switch(sizeof(*(_p_))){\
                case 2:{char * const _b_=(char*)(_p_); _BPSWAP(0,1); break;}\
                case 4:{char * const _b_=(char*)(_p_); _BPSWAP(0,3); _BPSWAP(1,2); break;}\
                case 8:{char * const _b_=(char*)(_p_); _BPSWAP(0,7); _BPSWAP(1,6); _BPSWAP(2,5); _BPSWAP(3,4); break;}\
            }\
        else if(sizeof(long)==4 && _e_.v.pdp)\
            switch(sizeof(*(_p_))){\
                case 2:{char * const _b_=(char*)(_p_); _BPSWAP(0,1); break;}\
                case 4:{char * const _b_=(char*)(_p_); _BPSWAP(0,1); _BPSWAP(3,2); break;}\
            }\
    }\
}while(0)
#define _BPSWAP(_1_,_2_) do{const char _x_=_b_[_1_]; _b_[_1_]=_b_[_2_]; _b_[_2_]=_x_;}while(0)

#endif /* POLL_EXT_H */
