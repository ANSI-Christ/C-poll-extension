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
    size_t(*increase)(size_t old_size);
    void(*mutex)(int cmd); /* cmd: 0 - unlock, 1 - lock */
    unsigned int reserv; /* default: 192 on stack */
    unsigned int wsa;
    int get_result; /* sets -1 on error and 1 on success*/
}poll_config_t;

void poll_loop(poll_config_t *cfg);
void poll_unloop(void);

int poll_recv(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_send(void *os_socket,void(*callback)(int err,void *arg),void *arg);
int poll_both(void *os_socket,void(*callback)(int err,void *arg),void *arg);

int poll_recv_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_send_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);
int poll_both_tm(void *os_socket,void(*callback)(int err,void *arg),void *arg,unsigned int seconds);





/* usefull helpers */

struct DNS_response{
    unsigned char count;
    struct{
        unsigned char len;
        unsigned char addr[16];
    }ip[16];
};

int DNS_request(const char *dns_ipv4,int family,const char *host_name,void *out_socket);
int DNS_response(void *request_socket,struct DNS_response *response);

int socket_unblock(void *os_socket);

void b2net(void *base_type_address);
void b2host(void *base_type_address);





/* macro detail */

#define b2net b2host
#define b2host(_p_) do{\
    struct static_assert_bad_type_##__LINE__{char _1[(sizeof((_p_)[0]<9)) ? 1 : -1], _2[sizeof((_p_)[0]+=0.1)];};\
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
