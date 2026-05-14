/* * * * * * * * * * * * * * * * * */
/* MIT License                     */
/* Copyright (c) 2024 ANSI-Christ  */
/* * * * * * * * * * * * * * * * * */

#include "poll_ext.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef offsetof
#define _POLL_OFFSETOF offsetof
#else
#define _POLL_OFFSETOF(_1_,_2_) ((size_t)&((_1_*)0)->_2_)
#endif

#ifdef _WIN32

#define NOMINMAX
#include <winsock2.h>
#undef NOMINMAX

static int _poll_startup(int ver){
    WSADATA w;
    if(ver==-1) return 1;
    if(!ver) ver=MAKEWORD(2,2);
    return !WSAStartup(ver,&w);
}

static void _poll_cleanup(const int ver){
    if(ver!=-1) WSACleanup();
}

#define _poll_setopt(_s_,_o_,_p_,_l_) setsockopt((_s_),SOL_SOCKET,(_o_),(const char*)(_p_),(_l_))
static int _poll_getopt(SOCKET s,const int o,void * const p,const unsigned int l){
    int ls=l; return getsockopt(s,SOL_SOCKET,o,(char*)p,&ls);
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

#include <sys/types.h>
#include <sys/socket.h>
#ifdef POLL_BY_SELECT
#include <sys/time.h>
#include <sys/select.h>
#else
#include <poll.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#define closesocket close
#define WSASetLastError(_1_) do{errno=(_1_);}while(0)
#define WSAGetLastError() (errno)
#define WSAPoll poll
#define WSAELOOP ELOOP
#define WSAEINTR EINTR
#define WSAEBADF EBADF
#define WSAEINVAL EINVAL
#define WSAENOBUFS ENOBUFS
#define WSAEMSGSIZE EMSGSIZE
#define WSAETIMEDOUT ETIMEDOUT
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#define SD_BOTH SHUT_RDWR
#define _poll_startup(ver) (1)
#define _poll_cleanup(ver) while(0)

#define _poll_setopt(_s_,_o_,_p_,_l_) setsockopt((_s_),SOL_SOCKET,(_o_),(_p_),(_l_))
static int _poll_getopt(SOCKET s,const int o,void * const p,const unsigned int l){
    unsigned int ls=l; return getsockopt(s,SOL_SOCKET,o,p,&ls);
}

static int _poll_pipe(void * const s){
    return socketpair(AF_UNIX,SOCK_STREAM,0,(SOCKET*)s)!=SOCKET_ERROR;
}

int socket_unblock(void * const _s){
    SOCKET s=*(SOCKET*)_s; return fcntl(s,F_SETFL, (O_NONBLOCK | fcntl(s,F_GETFL)) );
}

#endif

#define _WSAEUNKNOWN -1

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef POLLIN
#define POLLIN 1
#define POLLOUT 2
#define POLLERR 4
#define POLLNVAL 8
#define POLLHUP 16
struct pollfd{
    SOCKET fd;
    short events, revents;
};
static int WSAPoll(struct pollfd * const p,const int cnt,const int timeout){
    struct timeval t={timeout/1000,1000*(timeout%1000)};
    int i,s,c;
    SOCKET max=p->fd;
    fd_set set[3];
    FD_ZERO(set);FD_ZERO(set+1);FD_ZERO(set+2);
    for(i=0;i<cnt;++i){
        p[i].revents=0;
        if(p[i].events & POLLIN) FD_SET(p[i].fd,set);
        if(p[i].events & POLLOUT) FD_SET(p[i].fd,set+1);
        FD_SET(p[i].fd,set+2);
#ifdef _WIN32
#define _POLL_BY_SELECT 1
#else
#define _POLL_BY_SELECT 2
        if(p[i].fd>max) max=p[i].fd;
#endif
    }
    if((s=select(max+1,set,set+1,set+2,timeout<0?NULL:&t))!=SOCKET_ERROR)
        for(c=s,i=s=0;c && i<cnt;++i){
            unsigned char e=0;
            if(FD_ISSET(p[i].fd,set+0)){--c; e=1; p[i].revents|=POLLIN;}
            if(FD_ISSET(p[i].fd,set+1)){--c; e=1; p[i].revents|=POLLOUT;}
            if(FD_ISSET(p[i].fd,set+2)){--c; e=1; p[i].revents|=POLLERR;}
            s+=e;
        }
    return s;
}
#else
#define _POLL_BY_SELECT 0
#endif


struct pollcb{
    void(*f)(int,void*);
    void *a;
    time_t t;
};

struct pollcmd{
    struct pollcb cb;
    SOCKET fd;
    short ev;
    char size;
};

static struct _poll_ctrl{
    SOCKET r,w;;
}_poll_ctrl[1]={{INVALID_SOCKET,INVALID_SOCKET}};


int _poll_socket_error(void * const s){
    int e=0;
    _poll_getopt(*(SOCKET*)s,SO_ERROR,&e,sizeof(e));
    return e;
}

static int _poll_getcmd(SOCKET s,struct pollcmd * const cmd){
    unsigned int l=_POLL_OFFSETOF(struct pollcmd,size);
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
        const int c=send(s,(const char*)cmd,_POLL_OFFSETOF(struct pollcmd,size),MSG_NOSIGNAL);
        if(c==_POLL_OFFSETOF(struct pollcmd,size)) break;
        if(c==SOCKET_ERROR && WSAGetLastError()!=WSAEINTR) return SOCKET_ERROR;
        else if(c!=0){WSASetLastError(WSAEMSGSIZE); return SOCKET_ERROR;}
    }
    return 0;
}

static void _poll_mtx(const int lock){return;if(lock)_poll_setopt(INVALID_SOCKET,0,NULL,0);}
static size_t _poll_inc(const size_t s){
    return s<<1;
}

void poll_loop(poll_config_t * const cfg){
    static unsigned int _loops=0;
    struct _poll_ctrl * const g=_poll_ctrl;
    void*(* const allocator)(size_t)=((cfg && cfg->allocator) ? cfg->allocator : malloc);
    void(* const deallocator)(void*)=((cfg && cfg->deallocator) ? cfg->deallocator : free);
    size_t(* const inc)(size_t)=((cfg && cfg->increase) ? cfg->increase : _poll_inc);
    void(* const mtx)(int)=((cfg && cfg->mutex) ? cfg->mutex : _poll_mtx);
    struct pollfd fd_stack[192], *fd=fd_stack;
    struct pollcb cb_stack[192], *cb=cb_stack;
    unsigned int t=0, size=192, count=1;
    int err=0, wsa=cfg ? cfg->WSA : 0;

    mtx(1);
    if(++_loops==1){
        if((_poll_startup(wsa) || !(wsa=-1)) && _poll_pipe(g)){
            #ifdef SO_NOSIGPIPE
            const int opt=1;
            _poll_setopt(g->r,SO_NOSIGPIPE,&opt,sizeof(opt));
            _poll_setopt(g->w,SO_NOSIGPIPE,&opt,sizeof(opt));
            #endif
        }else err=WSAGetLastError();
    }
    mtx(0);

    if(cfg && cfg->reserv>size && !err){
        size=cfg->reserv;
        fd=(struct pollfd*)allocator(sizeof(*fd)*size);
        cb=(struct pollcb*)allocator(sizeof(*cb)*size);
        if(!fd || !cb) err=WSAENOBUFS;
    }
    if(!err){
        fd->fd=g->r;
        fd->events=POLLIN;
        fd->revents=0;
    }
    if(cfg && cfg->init)
        cfg->init(err,cfg->iarg);

    while(!err){
        const int _c=WSAPoll(fd,count,(t?1000:-1));
        unsigned int repeat=20, i=count, c=((_c!=SOCKET_ERROR)?_c:0);

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
                    #if _POLL_BY_SELECT
                    if(count>=FD_SETSIZE) cmd.cb.f(WSAENOBUFS,cmd.cb.a); else
                    #endif
                    do{
                        cb[count]=cmd.cb;
                        fd[count].fd=cmd.fd;
                        fd[count].events=cmd.ev;
                        fd[count].revents=0;
                        t+=(cmd.cb.t!=0);
                        ++count;
                    }while(0);
                }
                if(repeat && WSAPoll(fd,1,0)>0){
                    --repeat; goto _mark;
                }
            }else err=-1;
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
    if(!--_loops){
        if(g->w!=INVALID_SOCKET){
            closesocket(g->w);
            g->w=INVALID_SOCKET;
        }
        if(g->r!=INVALID_SOCKET){
            closesocket(g->r);
            g->r=INVALID_SOCKET;
        }
        _poll_cleanup(wsa);
    }
    mtx(0);
}

void poll_unloop(void){
    struct _poll_ctrl * const g=_poll_ctrl;
    if(g->r!=INVALID_SOCKET) shutdown(g->w,SD_BOTH);
}

static int _poll_add(void * const s,const int e,void(* const f)(int,void*),void * const a,const time_t t){
    struct _poll_ctrl * const g=_poll_ctrl;
    if(g->r==INVALID_SOCKET){WSASetLastError(WSAELOOP); return SOCKET_ERROR;}
    if(!s || *(SOCKET*)s==INVALID_SOCKET || !f || !a){WSASetLastError(WSAEINVAL); return SOCKET_ERROR;}
    #if _POLL_BY_SELECT==2
    if(*(SOCKET*)s>=FD_SETSIZE){WSASetLastError(WSAENOBUFS); return SOCKET_ERROR;}
    #endif
    {struct pollcmd cmd={{f,a,t},*(SOCKET*)s,e,0};
    return _poll_setcmd(g->w,&cmd);}
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
};

static char *_dns_parse_request(const char *s,char *p){
    unsigned int b=0; char x=*s;
    while(x){
        char * const c=p;
        unsigned int l=0;
        do{
            *(++p)=*(s++);
            if(++b==512 || ++l==64) return NULL;
        }while(*p && *p!='.');
        *c=l-1; x=*p;
    } *p=0;
    return p;
}

int DNS_request(void * const _s,const int family,const char * const host){
    if(_s && host){
        SOCKET s=*(SOCKET*)_s;
        struct{ struct _dns_header h; unsigned char data[512]; }req;
        int st=SOCK_DGRAM;
        char * const p=_dns_parse_request(host,(char*)req.data);
        if(_poll_getopt(s,SO_TYPE,&st,sizeof(st))==SOCKET_ERROR)
            return SOCKET_ERROR;
        if(p){
            struct tmp{char _[4];};
            union{unsigned short x[2]; struct tmp c;} u={{0,1}};
            const unsigned short l=(size_t)(p-(char*)&req)+5;
            unsigned short c=0,i,af[2], nl=l;
            d2net(&u.x[1]); d2net(&nl);
            req.h.i=0xdb42; d2net(&req.h.i);
            req.h.f=0x0100; d2net(&req.h.f);
            req.h.qdc=1; d2net(&req.h.qdc);
            req.h.anc=req.h.nsc=req.h.arc=0;
            if(family==AF_INET){i=1; af[0]=1;}
#ifdef AF_INET6
            else if(family==AF_INET6){i=1; af[0]=28;}
#endif
#ifdef AF_UNSPEC
            else if(family==AF_UNSPEC){i=1; af[0]=28; af[1]=1;}
#endif
            else{WSASetLastError(WSAEAFNOSUPPORT); return SOCKET_ERROR;}

            while(i){
                u.x[0]=af[--i]; d2net(&u.x[0]);
                *((struct tmp*)(p+1))=u.c;
                c+=(st==SOCK_DGRAM || send(s,(const char*)&nl,2,MSG_NOSIGNAL)==2) && (send(s,(const char*)&req,l,MSG_NOSIGNAL)==l);
            }
            return c;
        }
    }
    WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

static int _dns_parse_answer(struct _dns_header * const h,struct netaddr * const a,const unsigned int size){
    unsigned int count=0;
    d2host(&h->i); d2host(&h->f);
    if(h->i==0xdb42 && !(h->f & 15) && h->anc){
        const unsigned char *p=(const unsigned char*)(h+1);
        unsigned int i; d2host(&h->qdc); d2host(&h->anc);
        for(i=h->qdc;i;--i,p+=5)
            while(*p) p+=(*p)+1;
        for(i=h->anc;i && count<size;--i){
            if((*p & 0xC0)==0xC0) ++p;
            else while(*p) p+=(*p)+1;
            {union addr{
                struct{char _[10];}buf;
                struct{unsigned short t; char _[6]; unsigned short l;}ip;
            }addr={((union addr*)++p)->buf}; p+=10;
            d2host(&addr.ip.t);
            d2host(&addr.ip.l);
            if((addr.ip.t==1 && addr.ip.l==4) || (addr.ip.t==28 && addr.ip.l==16)){
                memcpy(a[count].ip,p,(a[count].len=addr.ip.l));
                ++count;
            }
            p+=addr.ip.l;}
        }
    }
    return count;
}

int DNS_response(void * const _s,struct netaddr * const a,const unsigned int size){
    if(_s && a){
        struct{ struct _dns_header h; unsigned char data[1024*8]; }res;
        SOCKET s=*(SOCKET*)_s;
        int st=SOCK_DGRAM, bytes=sizeof(res);
        if(_poll_getopt(s,SO_TYPE,&st,sizeof(st))==SOCKET_ERROR)
            return SOCKET_ERROR;
        if(st!=SOCK_DGRAM){
            unsigned short sz;
            bytes=recv(s,(char*)&sz,2,MSG_NOSIGNAL);
            if(bytes==SOCKET_ERROR) return SOCKET_ERROR;
            if(bytes<2){WSASetLastError(_WSAEUNKNOWN); return 0;}
            d2host(&sz); bytes=sz;
        }
        bytes=recv(s,(char*)&res,bytes,MSG_NOSIGNAL);
        if(bytes==SOCKET_ERROR) return SOCKET_ERROR;
        if(bytes>11) return _dns_parse_answer(&res.h,a,size);
        WSASetLastError(_WSAEUNKNOWN); return 0;
    }else WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}
