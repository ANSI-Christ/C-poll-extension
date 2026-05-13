#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "poll_ext.c"

#ifdef _WIN32
    #define TIMEOUT WSAETIMEDOUT
#else
    #define TIMEOUT ETIMEDOUT
#endif

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
            struct sockaddr_in a;
            printf("state=%d\n",m->state);
            if( (m->s=socket(AF_INET,SOCK_DGRAM,0))==INVALID_SOCKET){
                printf("\tDNS socket err\n");
                return -1;
            }

            memset(&a,0,sizeof(a));
            a.sin_family=AF_INET;
            a.sin_port=53; b2net(&a.sin_port);
            a.sin_addr.s_addr=0x08080808; b2net(&a.sin_addr.s_addr); /* 8.8.8.8 */
            connect(m->s,(struct sockaddr*)&a,sizeof(a));

            if(DNS_request(&m->s,AF_INET,"httpbin.org")!=1){
                printf("\tDNS request err\n");
                return -1;
            }
            m->state=1; m->wait=1;
            poll_recv_tm(&m->s,smcallback,m,10);
            return 0;
        }
        case 1:{
            struct sockaddr_in a;
            struct netaddr ip;
            printf("state=%d\n",m->state);
            if(DNS_response(&m->s,&ip,1)!=1){
                closesocket(m->s);
                printf("\tDNS response err\n");
                return -1;
            }

            memset(&a,0,sizeof(a));
            a.sin_family=AF_INET;
            a.sin_port=80; b2net(&a.sin_port);
            memcpy(&a.sin_addr,ip.ip,ip.len);

            closesocket(m->s);
            if( (m->s=socket(AF_INET,SOCK_STREAM,0))==INVALID_SOCKET){
                printf("\tclient socket err\n");
                return -1;
            }
            socket_unblock(&m->s);
            connect(m->s,(struct sockaddr*)&a,sizeof(a));
            m->state=2; m->wait=1;
            poll_both_tm(&m->s,smcallback,m,10);
            return 0;
        }
        case 2:{
            printf("state=%d\n",m->state);
            if(m->err==TIMEOUT){
                closesocket(m->s);
                printf("\trecv timeout\n"); return -1;
            }
            {
                const char *req = "GET /delay/5 HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
                send(m->s,req,strlen(req),MSG_NOSIGNAL);
            }
            m->state=3;
        }
        case 3:{
            char buf[4096];
            int c;
            printf("state=%d\n",m->state);
            if(m->err==TIMEOUT){
                closesocket(m->s);
                printf("\trecv timeout\n");
                return -1;
            }
            c=recv(m->s,buf,sizeof(buf),0);
            printf("\trecv %d\n",c);
            if(c==SOCKET_ERROR){
                m->wait=1; poll_recv_tm(&m->s,smcallback,m,10); return 0;
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
            ret=statemachine(&m);
        }
        printf("%s\n",ret==1?"ok":"fail");
    }

    poll_unloop();

    for(i=0;i<N;++i)
        pthread_join(t[i],NULL);

    return 0;
}


