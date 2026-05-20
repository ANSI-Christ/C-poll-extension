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
    SOCKET r,w;
    struct sockaddr_in a[2];
    int l[2]={sizeof(*a),sizeof(*a)}, opt=1;
    unsigned char * const ip=(unsigned char*)&a->sin_addr;
    if( (r=socket(AF_INET,SOCK_DGRAM,0))==INVALID_SOCKET ) goto _clean1;
    if( (w=socket(AF_INET,SOCK_DGRAM,0))==INVALID_SOCKET ) goto _clean2;
    memset(a,0,sizeof(*a)); a->sin_family=AF_INET; a->sin_port=0; ip[0]=127; ip[1]=1; a[1]=*a;
    if(bind(r,(struct sockaddr*)(a+0),*l)==SOCKET_ERROR || bind(w,(struct sockaddr*)(a+1),*l)==SOCKET_ERROR) goto _clean3;
    if(getsockname(r,(struct sockaddr*)(a+0),l+0)==SOCKET_ERROR || getsockname(w,(struct sockaddr*)(a+1),l+1)==SOCKET_ERROR) goto _clean3;
    if(connect(r,(struct sockaddr*)(a+1),l[1])==SOCKET_ERROR || connect(w,(struct sockaddr*)(a+0),l[0])==SOCKET_ERROR) goto _clean3;
    ((SOCKET*)s)[0]=r; ((SOCKET*)s)[1]=w;
    return 1;
_clean3:
    closesocket(w);
_clean2:
    closesocket(r);
_clean1:
    return 0;
}

int socket_mode(void * const s,const int b){
    u_long opt=!b; return ioctlsocket(*(SOCKET*)s,FIONBIO,&opt);
}

#define _poll_hash(_1_) ((sizeof(SOCKET) == 2 ? _SOCK_HASH16 : (sizeof(SOCKET) == 4 ? _SOCK_HASH32 : _SOCK_HASH64) ) * (size_t)(_1_))
#define _SOCK_HASH16 ((size_t)0x9E37u)
#define _SOCK_HASH32 (_SOCK_HASH16*256*(size_t)256 + 0x79B9u)
#define _SOCK_HASH64 (_SOCK_HASH32*256*(size_t)256 + (size_t)0x7F4Au*256*(size_t)256 + 0x7C15u)

#else

#include <sys/types.h>
#include <sys/socket.h>
#ifdef POLL_BY_SELECT
#include <sys/time.h>
#include <sys/select.h>
#define _POLL_BY_SELECT_UNIX
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
#define WSASetLastError(_1_) errno=(_1_)
#define WSAGetLastError() errno
#define WSAPoll poll
#define WSAELOOP ELOOP
#define WSAEINTR EINTR
#define WSAEBADF EBADF
#define WSAEINVAL EINVAL
#define WSAENOBUFS ENOBUFS
#define WSAETIMEDOUT ETIMEDOUT
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#define SD_RECEIVE SHUT_RD
#define _poll_startup(ver) (1)
#define _poll_cleanup(ver) while(0)
#define _poll_hash(_1_) (_1_)

#define _poll_setopt(_s_,_o_,_p_,_l_) setsockopt((_s_),SOL_SOCKET,(_o_),(_p_),(_l_))
static int _poll_getopt(SOCKET s,const int o,void * const p,const unsigned int l){
    unsigned int ls=l; return getsockopt(s,SOL_SOCKET,o,p,&ls);
}

static int _poll_pipe(void * const s){
    return socketpair(AF_UNIX,SOCK_DGRAM,0,(SOCKET*)s)!=SOCKET_ERROR;
}

int socket_mode(void * const _s,const int b){
    SOCKET s=*(SOCKET*)_s;
    int m=fcntl(s,F_GETFL);
    if(m==SOCKET_ERROR) return SOCKET_ERROR;
    if(b) m&=~O_NONBLOCK;
    else m|=O_NONBLOCK;
    return fcntl(s,F_SETFL,m);
}

#endif

#define _WSAEUNKNOWN -1

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef POLLIN
static void WARNING___POLL_BY_SELECT(int WARNING___POLL_BY_SELECT_){char _WARNING___POLL_BY_SELECT[1]={0,0};}
#define POLLIN 1
#define POLLOUT 2
#define POLLERR 4
#define POLLNVAL 8
struct pollfd{
    SOCKET fd;
    short events, revents;
};
static int WSAPoll(struct pollfd * const p,const int cnt,const int timeout){
    struct timeval t={timeout/1000,1000*(timeout%1000)};
    SOCKET max=p->fd; int i,s,c;
    fd_set set[3]; FD_ZERO(set);FD_ZERO(set+1);FD_ZERO(set+2);
    for(i=0;i<cnt;++i){
        p[i].revents=0;
        if(p[i].events & POLLIN) FD_SET(p[i].fd,set);
        if(p[i].events & POLLOUT) FD_SET(p[i].fd,set+1);
        FD_SET(p[i].fd,set+2);
        #ifdef _POLL_BY_SELECT_UNIX
        if(p[i].fd>max) max=p[i].fd;
        #endif
    }
    if((s=select(max+1,set,set+1,set+2,timeout<0?NULL:&t))!=SOCKET_ERROR)
        for(c=s,i=s=0;c && i<cnt;++i){
            int e=0;
            if(FD_ISSET(p[i].fd,set+0)){--c; e=1; p[i].revents|=POLLIN;}
            if(FD_ISSET(p[i].fd,set+1)){--c; e=1; p[i].revents|=POLLOUT;}
            if(FD_ISSET(p[i].fd,set+2)){--c; e=1; p[i].revents|=POLLERR;}
            s+=e;
        }
    return s;
}
static unsigned int _poll_szlim(const unsigned int size){
    return size>FD_SETSIZE ? FD_SETSIZE : size;
}
#else
#define _poll_szlim(_1_) (_1_)
#endif

#define _poll_time() time(NULL)

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

static struct pollsp{
    SOCKET r,w;
}_gpoll_sp[1];

static struct{
    struct pollsp *sp;
    unsigned int size, count;
    int WSA;
}_gpoll={_gpoll_sp,sizeof(_gpoll_sp)/sizeof(*_gpoll_sp),0,0};

static size_t _poll_inc(const size_t s){
    return s<<1;
}

static int _poll_getcmd(SOCKET s,struct pollcmd * const cmd){
    int c; while( (c=recv(s,(char*)cmd,_POLL_OFFSETOF(struct pollcmd,size)+1,MSG_NOSIGNAL))==SOCKET_ERROR && WSAGetLastError()==WSAEINTR );
    return c==_POLL_OFFSETOF(struct pollcmd,size);
}

static int _poll_setcmd(SOCKET s,const struct pollcmd * const cmd){
    while(send(s,(const char*)cmd,_POLL_OFFSETOF(struct pollcmd,size),MSG_NOSIGNAL)!=_POLL_OFFSETOF(struct pollcmd,size))
        if(WSAGetLastError()!=WSAEINTR) return WSAELOOP;
    return 0;
}

static int _poll_add(void * const _s,const int e,void(* const f)(int,void*),void * const a,const time_t t){
    SOCKET s; if(!_gpoll.count) return WSAELOOP;
    if(!_s || (s=*(SOCKET*)_s)==INVALID_SOCKET || !f) return WSAEINVAL;
    #ifdef _POLL_BY_SELECT_UNIX
    if(s>=FD_SETSIZE) return WSAENOBUFS;
    #endif
    {const struct pollcmd cmd={{f,a,t},s,e,0}; return _poll_setcmd(_gpoll.sp[_poll_hash(s)%_gpoll.count].w,&cmd);}
}

void poll_unloop(const int wait){
    struct pollsp * const sp=_gpoll.sp;
    struct pollcmd cmd; unsigned int i;
    for(i=_gpoll.count,cmd.ev=0;i--;)
        _poll_setcmd(sp[i].w,&cmd);
    if(!wait) return;
    for(i=_gpoll.count;i--;){
        _poll_getcmd(sp[i].w,&cmd);
        shutdown(sp[i].w,SD_RECEIVE);
    }
}

void poll_cleanup(void){
    struct pollsp * const sp=_gpoll.sp;
    unsigned int i=_gpoll.count;
    poll_unloop(1);
    while(i--){
        closesocket(sp[i].r);
        closesocket(sp[i].w);
    }
    _poll_cleanup(_gpoll.WSA);
    _gpoll.count=0;
}

int poll_config(void *(* const p)[2],const unsigned int c,const int WSA){
    if(c>1){
        if(!p) return WSAEINVAL;
        _gpoll.sp=(struct pollsp*)p; _gpoll.size=c;
    }else{
        _gpoll.sp=_gpoll_sp; _gpoll.size=sizeof(_gpoll_sp)/sizeof(*_gpoll_sp);
    } _gpoll.WSA=WSA; return 0;
}

void poll_loop(const poll_config_t cfg[1]){
    void*(* const allocator)(size_t)=(cfg->allocator ? cfg->allocator : malloc);
    void(* const deallocator)(void*)=(cfg->deallocator ? cfg->deallocator : free);
    size_t(* const inc)(size_t)=(cfg->resizer ? cfg->resizer : _poll_inc);
    struct pollfd fd_stack[192], *fd=fd_stack;
    struct pollcb cb_stack[192], *cb=cb_stack;
    struct pollsp * const sp=_gpoll.sp+_gpoll.count;
    unsigned int t=_gpoll.count, size=_poll_szlim(192), count=1;
    int tm=-1;

    if(t==_gpoll.size) return cfg->init(WSAELOOP,cfg->iarg);

    if(!t && !_poll_startup(_gpoll.WSA)){
        const int e=WSAGetLastError();
        return cfg->init(e?e:_WSAEUNKNOWN,cfg->iarg);
    }

    if(_poll_pipe(sp)){
        #ifdef SO_NOSIGPIPE
        const int opt=1;
        _poll_setopt(sp->r,SO_NOSIGPIPE,&opt,sizeof(opt));
        _poll_setopt(sp->w,SO_NOSIGPIPE,&opt,sizeof(opt));
        #endif
        socket_mode(&sp->r,0);
    }else{
        const int e=WSAGetLastError();
        if(!t) _poll_cleanup(_gpoll.WSA);
        return cfg->init(e?e:_WSAEUNKNOWN,cfg->iarg);
    }

    if(_poll_szlim(cfg->reserv)>size){
        size=_poll_szlim(cfg->reserv);
        fd=(struct pollfd*)allocator(sizeof(*fd)*size);
        cb=(struct pollcb*)allocator(sizeof(*cb)*size);
        if(!fd || !cb){
            if(fd) deallocator(fd); else deallocator(cb);
            closesocket(sp->r); closesocket(sp->w);
            if(!t) _poll_cleanup(_gpoll.WSA);
            return cfg->init(WSAENOBUFS,cfg->iarg);
        }
    }

    ++_gpoll.count;
    cfg->init(0,cfg->iarg);

    for(fd->fd=sp->r, fd->events=POLLIN, t=0; size;){
        const int _c=WSAPoll(fd,count,tm);
        unsigned int i=count, dt=0, c=((_c!=SOCKET_ERROR)?_c:0); tm=86400;

        if(c && fd->revents){
            struct pollcmd cmd; unsigned int repeat=20;
            while(_poll_getcmd(fd->fd,&cmd) && --repeat){
                if(!cmd.ev){
                    shutdown(sp->w,SD_SEND);
                    while(_poll_getcmd(fd->fd,&cmd))
                        if(cmd.ev) cmd.cb.f(WSAELOOP,cmd.cb.a);
                    size=0; break;
                }
                if(count==size){
                    const unsigned int size_new=_poll_szlim(inc(size));
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
                            goto _mark;
                        }
                    }else{
_mark:
                        cmd.cb.f(WSAENOBUFS,cmd.cb.a);
                        continue;
                    }
                }
                cb[count]=cmd.cb;
                fd[count].fd=cmd.fd;
                fd[count].events=cmd.ev;
                ++count;
                if(cmd.cb.t){
                    const long d=cmd.cb.t-_poll_time();
                    if(d<tm){tm=d;} ++dt;
                }
            }
            --c;
        }

        while((c|t) && --i){
            const short ev=fd[i].revents;
            if(ev){
                const struct pollcb f=cb[i]; SOCKET s=fd[i].fd; int err=0; --c; t-=(f.t!=0);
                if(i!=--count){fd[i]=fd[count]; cb[i]=cb[count];}
                if(ev & POLLERR) _poll_getopt(s,SO_ERROR,&err,sizeof(err));
                if(ev & POLLNVAL) err=WSAEBADF;
                f.f(err,f.a);
            }else if(cb[i].t){
                const long d=cb[i].t-_poll_time();
                if(d<=0){
                    const struct pollcb f=cb[i]; --t;
                    if(i!=--count){fd[i]=fd[count]; cb[i]=cb[count];}
                    f.f(WSAETIMEDOUT,f.a);
                }else if(d<tm) tm=d;
            }
        }

        if((t+=dt)) tm*=(tm>0)*1000; else tm=-1;
    }

    while(--count) cb[count].f(WSAELOOP,cb[count].a);
    if(fd!=fd_stack) deallocator(fd);
    if(cb!=cb_stack) deallocator(cb);
    {struct pollcmd cmd; _poll_setcmd(sp->r,&cmd);}
}

int poll_recv(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLIN,f,a,0);
}

int poll_recv_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLIN,f,a,_poll_time()+t);
}

int poll_send(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLOUT,f,a,0);
}

int poll_send_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLOUT,f,a,_poll_time()+t);
}

int poll_both(void * const s,void(* const f)(int,void*),void * const a){
    return _poll_add(s,POLLIN|POLLOUT,f,a,0);
}

int poll_both_tm(void * const s,void(* const f)(int,void*),void * const a,const unsigned int t){
    return _poll_add(s,POLLIN|POLLOUT,f,a,_poll_time()+t);
}


/* ------------------- useful things --------------------- */

#define _CALL_BODY(_c_,_act_) {do{ const int b=_c_; if(b==SOCKET_ERROR){if(WSAGetLastError()==WSAEINTR){continue;} return 0;} if(!b){return !c;} _act_ }while(c); return 1;}
static int _recv_trash(SOCKET s,char *p,unsigned int d,unsigned int c) _CALL_BODY(recv(s,p,d,MSG_NOSIGNAL),c-=b;)
static int _recv_exact(SOCKET s,char *p,unsigned int c) _CALL_BODY(recv(s,p,c,MSG_NOSIGNAL),c-=b; p+=b;);
static int _send_all(SOCKET s,const char *p,unsigned int c) _CALL_BODY(send(s,p,c,MSG_NOSIGNAL),c-=b; p+=b;);
#undef _CALL_BODY

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
            if(++b==509 || ++l==64) return NULL;
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
                c+=(st==SOCK_DGRAM || _send_all(s,(const char*)&nl,2)) && _send_all(s,(const char*)&req,l);
            }
            return c;
        }
    }
    WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

static unsigned int _dns_parse_answer(struct _dns_header * const h,struct netaddr * const a,const unsigned int size){
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
        struct{ struct _dns_header h; unsigned char data[1024]; }_res, *res=&_res;
        SOCKET s=*(SOCKET*)_s;
        int st=SOCK_DGRAM, bytes=sizeof(*res);
        if(_poll_getopt(s,SO_TYPE,&st,sizeof(st))==SOCKET_ERROR)
            return SOCKET_ERROR;
        if(st==SOCK_STREAM){
            unsigned short sz;
            if(!_recv_exact(s,(char*)&sz,2)) return 0;
            d2host(&sz); bytes=sz;
            if( bytes>sizeof(res) && !((*(void**)&res)=malloc(bytes)) ){
                _recv_trash(s,(char*)&_res,sizeof(_res),bytes); return 0;
            }
            if(!_recv_exact(s,(char*)res,bytes)) return 0;
        }else if(st==SOCK_DGRAM){
            bytes=recv(s,(char*)res,bytes,MSG_NOSIGNAL);
            if(bytes==SOCKET_ERROR) return SOCKET_ERROR;
        }else{WSASetLastError(WSAEINVAL); return SOCKET_ERROR;}
        if(bytes>11){
            st=(int)_dns_parse_answer(&res->h,a,size);
            if(res!=&_res){free(res);} return st;
        }return 0;
    }else WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}
