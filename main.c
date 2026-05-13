#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "poll_ext.c"

#ifdef _WIN32
#include <ws2tcpip.h>
    #define TIMEOUT WSAETIMEDOUT
#else
#include <netinet/in.h>
extern int inet_pton(int,const char*,void*);
   #define TIMEOUT ETIMEDOUT
#endif


#define make_addr4(_s_,_p_,_a_,_l_) do{\
    struct sockaddr_in * const _x_=(struct sockaddr_in*)(_a_);\
    _x_->sin_family=AF_INET; _x_->sin_port=(_p_); d2net(&_x_->sin_port);\
    *(_l_)=((!(_s_) || inet_pton(AF_INET,(_s_),&_x_->sin_addr)==1)?sizeof(*_x_):0);\
}while(0)

#define make_addr6(_s_,_p_,_a_,_l_) do{\
    struct sockaddr_in6 * const _x_=(struct sockaddr_in6*)(_a_);\
    _x_->sin6_family=AF_INET6; _x_->sin6_port=(_p_); d2net(&_x_->sin6_port);\
    *(_l_)=((!(_s_) || inet_pton(AF_INET6,(_s_),&_x_->sin6_addr)==1)?sizeof(*_x_):0);\
}while(0)



void sleepf(double sec){
    struct timespec t={(time_t)sec,0}; t.tv_nsec=(sec-t.tv_sec)*1000000000;
    while(nanosleep(&t,&t));
}

static void gmtx(const int lock){
    static pthread_mutex_t mtx[1]={PTHREAD_MUTEX_INITIALIZER};
    if(lock) pthread_mutex_lock(mtx);
    else pthread_mutex_unlock(mtx);
}

static void on_init(int err,void *arg){
    int *x=(int*)arg;
    x[1]=err; x[0]=0;
}



struct statemachine{
    int state, wait, err;
    SOCKET s;
};

static void smcallback(int err,void *arg){
    struct statemachine * const m=(struct statemachine *)arg;
    m->err=err;
    m->wait=0;
}

int statemachine(struct statemachine * const m){
    switch(m->state){
        case 0:{
            struct sockaddr_in a; int l;
            printf("state=%d\n",m->state);

            if( (m->s=socket(AF_INET,SOCK_DGRAM,0))==INVALID_SOCKET){
                printf("\tDNS socket err\n");
                return -1;
            }

            make_addr4("8.8.8.8",53,&a,&l);
            connect(m->s,(struct sockaddr*)&a,l);

            if(DNS_request(&m->s,AF_INET,"httpbin.org")!=1){
                closesocket(m->s);
                printf("\tDNS request err\n");
                return -1;
            }
            m->state=1;
            poll_recv_tm(&m->s,smcallback,m,10);
            return 0;
        }
        case 1:{
            struct sockaddr_in a; int l;
            printf("state=%d\n",m->state);
            if(m->err==TIMEOUT){
                closesocket(m->s);
                printf("\tDNS timeout\n");
                return -1;
            }
            if(DNS_response(&m->s,(struct netaddr*)(((char*)&a.sin_addr)-1),1)!=1){
                closesocket(m->s);
                printf("\tDNS response err\n");
                return -1;
            }
            closesocket(m->s);
            if( (m->s=socket(AF_INET,SOCK_STREAM,0))==INVALID_SOCKET){
                printf("\tclient socket err\n");
                return -1;
            }
            socket_unblock(&m->s);

            make_addr4(NULL,80,&a,&l);
            connect(m->s,(struct sockaddr*)&a,l);

            m->state=2;
            poll_both_tm(&m->s,smcallback,m,10);
            return 0;
        }
        case 2:{
            const char *req = "GET /delay/5 HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
            printf("state=%d\n",m->state);
            if(m->err==TIMEOUT){
                closesocket(m->s);
                printf("\tconnect timeout\n");
                return -1;
            }
            send(m->s,req,strlen(req),MSG_NOSIGNAL);
            m->state=3;
        }
        case 3:{
            char buf[4096]; int c;
            printf("state=%d\n",m->state);
            if(m->err==TIMEOUT){
                closesocket(m->s);
                printf("\trecv timeout\n");
                return -1;
            }
            c=recv(m->s,buf,sizeof(buf),0);
            printf("\trecv %d\n",c);
            if(c==SOCKET_ERROR){
                poll_recv_tm(&m->s,smcallback,m,10); return 0;
            }
            buf[c]=0; printf("\n\n\"%s\"\n\n",buf);
            closesocket(m->s);
        }break;
    }
    return 1;
}

int main(){
    pthread_t t[3];
    int i,N=sizeof(t)/sizeof(*t);
    poll_config_t cfg={
        .mutex=gmtx,
        .init=on_init
    };

    for(i=0;i<N;++i){
        int sync[2]={1,0};
        cfg.iarg=sync;
        if(pthread_create(t+i,NULL,(void*(*)(void*))poll_loop,&cfg)) break;
        while(sync[0]) sleepf(0.01);
        if(sync[1]){
            printf("loop error %d\n",sync[1]);
            break;
        }
    }

    if( (N=i) ){
        struct statemachine m={0,0,0};
        int ret=0;
        while(!ret){
            unsigned int sleepcnt=0;
            while(m.wait){sleepf(0.1); printf("\r... wait %f sec",++sleepcnt*0.1); fflush(stdout);}
            printf("\n");
            m.wait=1;
            ret=statemachine(&m);
        }
        printf("%s\n",ret==1?"ok":"fail");
    }

    poll_unloop();

    for(i=0;i<N;++i)
        pthread_join(t[i],NULL);

    return 0;
}


