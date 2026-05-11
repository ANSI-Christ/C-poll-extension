/* * * * * * * * * * * * * * * * * */
/* MIT License                     */
/* Copyright (c) 2024 ANSI-Christ  */
/* * * * * * * * * * * * * * * * * */

#include "poll_ext.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#undef NOMINMAX

static int _poll_startup(int ver){
    WSADATA w;
    if(!ver) ver=MAKEWORD(2,2);
    return !WSAStartup(ver,&w);
}

static int _poll_getopt(SOCKET s,const int o,void * const p,const unsigned int l){
    int ls=l; return getsockopt(s,SOL_SOCKET,o,(char*)p,&ls);
}

static int _poll_setopt(SOCKET s,const int o,const void * const p,const unsigned int l){
    return setsockopt(s,SOL_SOCKET,o,(const char*)p,l);
}

static int _poll_pipe(void * const s){
    SOCKET l,r,w;
    struct sockaddr_in addr;
    int len=sizeof(addr);
    unsigned char * const ip=(unsigned char*)&addr.sin_addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_port=0;
    ip[0]=127; ip[1]=ip[2]=0; ip[3]=1;
    if( (l=socket(AF_INET,SOCK_STREAM,0))==INVALID_SOCKET )
        goto _clean1;
    if(bind(l,(struct sockaddr*)&addr,len)==SOCKET_ERROR || listen(l,1)==SOCKET_ERROR)
        goto _clean2;
    if( (w=socket(AF_INET,SOCK_STREAM,0))==INVALID_SOCKET)
        goto _clean2;
    if( getsockname(l,(struct sockaddr*)&addr,&len) || connect(w,(struct sockaddr*)&addr,len)==SOCKET_ERROR || (r=accept(l,NULL,NULL))==INVALID_SOCKET)
        goto _clean3;
    closesocket(l);
    ((SOCKET*)s)[0]=r;
    ((SOCKET*)s)[1]=w;
    return 1;
_clean3:
    closesocket(w);
_clean2:
    closesocket(l);
_clean1:
    return 0;

}

int socket_unblock(void * const s){
    u_long opt=1; return ioctlsocket(*(SOCKET*)s,FIONBIO,&opt);
}

#else

#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#define WSAGetLastError() (errno)
#define WSASetLastError(_1_) (errno=(_1_))
#define closesocket close
#define WSACleanup() while(0)
#define WSAPoll poll
#define WSAEINTR EINTR
#define WSAEBADF EBADF
#define WSAEINVAL EINVAL
#define WSAENOBUFS ENOMEM
#define WSAETIMEDOUT ETIMEDOUT
#define WSAELOOP ENXIO
#define SD_BOTH SHUT_RDWR
#define _poll_startup(ver) (1)

static int _poll_getopt(SOCKET s,const int o,void * const p,const unsigned int l){
    unsigned int ls=l; return getsockopt(s,SOL_SOCKET,o,p,&ls);
}

static int _poll_setopt(SOCKET s,const int o,const void * const p,const unsigned int l){
    return setsockopt(s,SOL_SOCKET,o,p,l);
}

static int _poll_pipe(void * const s){
    return socketpair(AF_UNIX,SOCK_STREAM,0,(SOCKET*)s)!=SOCKET_ERROR;
}

int socket_unblock(void * const _s){
    SOCKET s=*(SOCKET*)_s; return fcntl(s,F_SETFL, (O_NONBLOCK | fcntl(s,F_GETFL)) );
}

#endif

#ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
#endif

struct pollcb{
    void(*f)(int,void*);
    void *a;
    time_t t;
};

struct pollcmd{
    struct pollcb cb;
    struct pollfd fd;
    char size;
};

static struct _poll_glob{
    struct{SOCKET r,w;}ctrl;
    unsigned int count;
}_poll_glob[1]={{{INVALID_SOCKET,INVALID_SOCKET},0}};

int _poll_socket_error(void * const s){
    int e=0;
    if(_poll_getopt(*(SOCKET*)s,SO_ERROR,&e,sizeof(e)))
        return SOCKET_ERROR;
    return e;
}

static int _poll_getcmd(SOCKET s,struct pollcmd * const cmd){
    unsigned int l=offsetof(struct pollcmd,size);
    char *d=(char*)cmd;
    do{
        const int c=recv(s,d,l,MSG_NOSIGNAL);
        if(!c) return 0;
        if(c!=SOCKET_ERROR){d+=c; l-=c;}
        else if(WSAGetLastError()!=WSAEINTR) return 0;
    }while(l);
    return 1;
}

static int _poll_setcmd(SOCKET s,const struct pollcmd * const cmd){
    while(1){
        const int c=send(s,(const char*)cmd,offsetof(struct pollcmd,size),MSG_NOSIGNAL);
        if(c==offsetof(struct pollcmd,size)) break;
        if(c==SOCKET_ERROR && WSAGetLastError()!=WSAEINTR) return SOCKET_ERROR;
        else if(c!=0) return -2;
    }
    return 0;
}

static void _poll_mtx(const int lock){return;if(lock)_poll_setopt(INVALID_SOCKET,0,NULL,0);}
static size_t _poll_inc(const size_t s){
    return s<<1;
}

void poll_loop(poll_config_t * const cfg){
    struct _poll_glob * const g=_poll_glob;
    void*(* const allocator)(size_t)=((cfg && cfg->allocator) ? cfg->allocator : malloc);
    void(* const deallocator)(void*)=((cfg && cfg->deallocator) ? cfg->deallocator : free);
    size_t(* const inc)(size_t)=((cfg && cfg->increase) ? cfg->increase : _poll_inc);
    void(* const mtx)(int)=((cfg && cfg->mutex) ? cfg->mutex : _poll_mtx);
    struct pollfd fd_stack[192], *fd=fd_stack;
    struct pollcb cb_stack[192], *cb=cb_stack;
    unsigned int t=0, size=192, count=1;
    int run=-1;

    mtx(1);
    if(++g->count==1 && _poll_startup(cfg ? cfg->wsa : 0) && _poll_pipe(&g->ctrl)){
        #ifdef SO_NOSIGPIPE
        const int opt=1;
        _poll_setopt(g->ctrl.r,SO_NOSIGPIPE,&opt,sizeof(opt));
        _poll_setopt(g->ctrl.w,SO_NOSIGPIPE,&opt,sizeof(opt));
        #endif
    }
    mtx(0);

    if(cfg && cfg->reserv>size){
        size=cfg->reserv;
        fd=(struct pollfd*)allocator(sizeof(*fd)*size);
        cb=(struct pollcb*)allocator(sizeof(*cb)*size);
    }
    if(fd && cb && g->ctrl.r!=INVALID_SOCKET){
        fd->fd=g->ctrl.r;
        fd->events=POLLIN;
        fd->revents=0;
        run=1;
    }
    if(cfg) cfg->get_result=run;

    while(run==1){
        const int _c=WSAPoll(fd,count,(t?1000:-1));
        unsigned int repeat=20, i=count, c=(_c>0?_c:0);

        if(c && fd->revents){
            struct pollcmd cmd; --c;
_mark:
            if(_poll_getcmd(fd->fd,&cmd)){
                if(count==size){
                    const unsigned int size_new=inc(size);
                    if(size_new>size){
                        struct pollfd * const fd_new=(struct pollfd*)allocator(sizeof(*fd_new)*size_new);
                        struct pollcb * const cb_new=(struct pollcb*)allocator(sizeof(*cb_new)*size_new);
                        if(fd_new && cb_new){
                            memcpy(fd_new,fd,sizeof(*fd_new)*count); if(fd!=fd_stack){deallocator(fd);} fd=fd_new;
                            memcpy(cb_new,cb,sizeof(*cb_new)*count); if(cb!=cb_stack){deallocator(cb);} cb=cb_new;
                            size=size_new;
                        }else{
                            if(fd_new) deallocator(fd_new);
                            if(cb_new) deallocator(cb_new);
                            cmd.cb.f(WSAENOBUFS,cmd.cb.a);
                        }
                    }
                }
                if(count<size){
                    fd[count]=cmd.fd;
                    cb[count]=cmd.cb;
                    t+=(cmd.cb.t!=0);
                    ++count;
                }
                if(repeat && WSAPoll(fd,1,0)>0){
                    --repeat; goto _mark;
                }
            }else run=0;
        }

        while((c|t) && --i){
            const short e=fd[i].revents;
            if(e){
                int err=0; const struct pollcb f=cb[i]; SOCKET s=fd[i].fd; --c; t-=(f.t!=0);
                if(i!=--count){fd[i]=fd[count]; cb[i]=cb[count];}
                if(e&POLLERR) err=_poll_socket_error(&s);
                if(e&POLLNVAL) err=WSAEBADF;
                f.f(err,f.a);
            }else if(cb[i].t && time(NULL)>=cb[i].t){
                const struct pollcb f=cb[i]; --t;
                if(i!=--count){fd[i]=fd[count]; cb[i]=cb[count];}
                f.f(WSAETIMEDOUT,f.a);
            }
        }

    }

    while(--count) cb[count].f(WSAELOOP,cb[count].a);
    if(fd && fd!=fd_stack) deallocator(fd);
    if(cb && cb!=cb_stack) deallocator(cb);

    mtx(1);
    if(!--g->count){
        if(g->ctrl.w!=INVALID_SOCKET){
            closesocket(g->ctrl.w);
            g->ctrl.w=INVALID_SOCKET;
        }
        if(g->ctrl.r!=INVALID_SOCKET){
            closesocket(g->ctrl.r);
            g->ctrl.r=INVALID_SOCKET;
        }
        WSACleanup();
    }
    mtx(0);
}

void poll_unloop(void){
    struct _poll_glob * const g=_poll_glob;
    if(g->ctrl.r!=INVALID_SOCKET){
        struct pollfd fd[1]={{g->ctrl.r,POLLIN,0}};
        struct pollcmd cmd;
        shutdown(g->ctrl.w,SD_BOTH);
        while(WSAPoll(fd,1,0)>0 && _poll_getcmd(fd->fd,&cmd))
            cmd.cb.f(WSAELOOP,cmd.cb.a);
        shutdown(g->ctrl.r,SD_BOTH);
    }
}

static int _poll_add(void * const s,const short e,void(* const f)(int,void*),void * const a,const time_t t){
    struct _poll_glob * const g=_poll_glob;
    if(g->ctrl.r==INVALID_SOCKET){WSASetLastError(WSAELOOP); return SOCKET_ERROR;}
    if(!s || *(SOCKET*)s==INVALID_SOCKET || !f || !a){WSASetLastError(WSAEINVAL); return SOCKET_ERROR;}
    {struct pollcmd cmd={{f,a,t},{*(SOCKET*)s,e,0},0};
    return _poll_setcmd(g->ctrl.w,&cmd);}
}

int poll_recv(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLIN,f,a,0);
}

int poll_recv_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLIN,f,a,time(NULL)+t);
}

int poll_send(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLOUT,f,a,0);
}

int poll_send_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLOUT,f,a,time(NULL)+t);
}

int poll_both(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLIN|POLLOUT,f,a,0);
}

int poll_both_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLIN|POLLOUT,f,a,time(NULL)+t);
}

/* ------------------- useful things --------------------- */

struct _dns_header{
    unsigned short i, f, qdc, anc, nsc, arc;
    unsigned char data[512];
};

static int _dns_parse(const char * const s,char * const p){
    unsigned int a,b,c,d;
    if(sscanf(s,"%u.%u.%u.%u",&a,&b,&c,&d)!=4 || a>255 || b>255 || c>255 || d>255)
        return 0;
    p[0]=a; p[1]=b; p[2]=c; p[3]=d; return 1;
}

static char *_dns_host_encode(const char *s,char *p){
    char x=*s;
    while(x){
        char * const c=p;
        do{*(++p)=*(s++);}while(*p && *p!='.');
        {const size_t l=(size_t)(p-c)-1; if(l>63){return NULL;} *c=l;} x=*p;
    } *p=0;
    return p;
}

static int _dns_make_request(SOCKET s,const int af,const char * const host){
    struct _dns_header h;
    char * const p=_dns_host_encode(host,(char*)h.data);
    if(p && (size_t)(p-(char*)&h.data)<512){
        struct tmp{char _[4];};
        union{unsigned short x[2]; struct tmp c;} u={{(af==AF_INET)?1:28,1}};
        const unsigned int l=(size_t)(p-(char*)&h)+5;
        b2net(&u.x[0]); b2net(&u.x[1]);
        h.i=0xdb42; b2net(&h.i);
        h.f=0x0100; b2net(&h.f);
        h.qdc=1; b2net(&h.qdc);
        h.anc=h.nsc=h.arc=0;
        *((struct tmp*)(p+1))=u.c;
        return send(s,(char*)&h,l,MSG_NOSIGNAL)==l;
    }
    return 0;
}

int DNS_request(const char * dns,const int family,const char * const host,void * const out_socket){
    if(host && out_socket){
        if(!dns) dns="8.8.8.8";
        SOCKET s=socket(AF_INET,SOCK_DGRAM,0);
        if(s!=INVALID_SOCKET){
            struct sockaddr_in a;
            memset(&a,0,sizeof(a));
            if(_dns_parse(dns,(char*)&a.sin_addr)){
                a.sin_family=AF_INET;
                a.sin_port=53; b2net(&a.sin_port);
                connect(s,(struct sockaddr*)&a,sizeof(a));
                if(_dns_make_request(s,family,host)){
                    *(SOCKET*)out_socket=s;
                    return 0;
                }
            }
            closesocket(s);
        }
    }else WSASetLastError(WSAEINVAL);
    *(SOCKET*)out_socket=INVALID_SOCKET;
    return SOCKET_ERROR;
}


int DNS_response(void * const s,struct DNS_response * const a){
    struct _dns_header h;
    const int bytes=recv(*(SOCKET*)s,(char*)&h,sizeof(h),MSG_NOSIGNAL);
    closesocket(*(SOCKET*)s);
    a->count=0; b2host(&h.i); b2host(&h.f);
    if(bytes>11 && h.i==0xdb42 && !(h.f & 15) && h.anc){
        const unsigned char *p=h.data;
        unsigned int i; b2host(&h.qdc); b2host(&h.anc);
        for(i=h.qdc;i;--i,p+=5)
            while(*p) p+=(*p)+1;
        for(i=h.anc;i && a->count<(sizeof(a->ip)/sizeof(*a->ip));--i){
            if((*p & 0xC0)==0xC0) ++p;
            else while(*p) p+=(*p)+1;
            {union addr{
                struct{char _[10];}buf;
                struct{unsigned short t; char _[6]; unsigned short l;}ip;
            }addr={((union addr*)++p)->buf}; p+=10;
            b2host(&addr.ip.t);
            b2host(&addr.ip.l);
            if((addr.ip.t==1 && addr.ip.l==4) || (addr.ip.t==28 && addr.ip.l==16)){
                memcpy(a->ip[a->count].addr,p,(a->ip[a->count].len=addr.ip.l));
                ++a->count;
            }
            p+=addr.ip.l;}
        }
        return 0;
    }
    return SOCKET_ERROR;
}
