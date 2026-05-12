#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "poll_ext.c"


void sleepf(double sec){
    struct timespec t={(time_t)sec,0}; t.tv_nsec=(sec-t.tv_sec)*1000000000;
    while(nanosleep(&t,&t));
}

static void gmtx(const int lock){
    static pthread_mutex_t mtx[1]={PTHREAD_MUTEX_INITIALIZER};
    if(lock) pthread_mutex_lock(mtx);
    else pthread_mutex_unlock(mtx);
}

struct statemachine{
    int state, wait, err;
};

static void callback(int err,void *arg){
    struct statemachine * const m=(struct statemachine *)arg;
    m->err=err;
    m->wait=0;
}

#ifdef _WIN32

#define INPROGRESS() WSAGetLastError()==WSAEWOULDBLOCK
#define TIMEOUT WSAETIMEDOUT

#else

#define INPROGRESS() errno==EINPROGRESS
#define TIMEOUT ETIMEDOUT

#endif

static void on_init(int err,void *arg){
    int *x=(int*)arg;
    x[1]=err; x[0]=0;
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
        const char *req = "GET /delay/5 HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
        struct sockaddr_in a;
        struct statemachine m={0,0,0};
        SOCKET s=socket(AF_INET,SOCK_STREAM,0);
        socket_unblock(&s);


        {
            SOCKET sd=socket(AF_INET,SOCK_DGRAM,0);
            struct sockaddr_in ad;
            struct DNS_addr ip={0};
            char str[32];

            memset(&ad,0,sizeof(ad));
            ad.sin_family=AF_INET; ad.sin_port=53; b2net(&ad.sin_port);
            ad.sin_addr.s_addr=0x08080808; b2net(&ad.sin_addr.s_addr); /* 8.8.8.8 */
            connect(sd,(struct sockaddr*)&ad,sizeof(ad));

            if(DNS_request(&sd,AF_INET,"httpbin.org")!=1)
                printf("DNS request err\n");
            if(DNS_response(&sd,&ip,1)!=1)
                printf("DNS response err\n");
            closesocket(sd);

            if(ip.len){
                inet_ntop((ip.len==4?AF_INET:AF_INET6),ip.ip,str,32);
                printf("ip:%s\n",str);
                memset(&a,0,sizeof(a));
                a.sin_family=AF_INET; a.sin_port=80; b2net(&a.sin_port);
                memcpy(&a.sin_addr,ip.ip,ip.len);
            }else m.state=-1;
        }

        while(m.state!=-1){
            unsigned int sleepcnt=0;
            while(m.wait){sleepf(0.1); printf("\r... wait %f sec",++sleepcnt*0.1); fflush(stdout);}
            printf("\n");
            switch(m.state){
                case 0:
                    printf("state=%d\n",m.state);
                    if(m.err==TIMEOUT){
                        m.state=-1; printf("\tconnect timeout\n"); break;
                    }
                    if(connect(s,(struct sockaddr*)&a,sizeof(a))==-1 && INPROGRESS()){
                        m.wait=1; poll_both_tm(&s,callback,&m,10); break;
                    }
                    printf("\tconnect\n");
                    m.state=1;
                case 1:{
                    printf("state=%d\n",m.state);
                    send(s,req,strlen(req),MSG_NOSIGNAL);
                    m.state=2;
                }
                case 2:{
                    char buf[4096];
                    int c;
                    if(m.err==TIMEOUT){
                        m.state=-1; printf("\trecv timeout\n"); break;
                    }
                    c=recv(s,buf,sizeof(buf),0);
                    printf("state=%d\n",m.state);
                    printf("\trecv %d\n",c);
                    if(c==-1){
                        m.wait=1; poll_recv_tm(&s,callback,&m,10); break;
                    }
                    buf[c]=0;
                    printf("\n\n\"%s\"\n\n",buf);
                    m.state=-1;
                }
            }
        }
        closesocket(s);
    }

    poll_unloop();

    for(i=0;i<N;++i)
        pthread_join(t[i],NULL);

    return 0;
}


