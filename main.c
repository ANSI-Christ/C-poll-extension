#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "poll_ext.c"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>


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

static void callback(int err,struct statemachine * const m){
    m->err=err;
    m->wait=0;
}

int main(){
    pthread_t t[3];
    int i,N=sizeof(t)/sizeof(*t);
    poll_config_t cfg={
        .mutex=gmtx,
    };

    for(i=0;i<N;++i){
        cfg.get_result=0;
        if(pthread_create(t+i,NULL,(void*(*)(void*))poll_loop,&cfg)) break;
        while(!cfg.get_result) sleepf(0.01);
        if(cfg.get_result==-1) break;
    }

    {
        struct hostent *h=gethostbyname("httpbin.org");
        const char *req = "GET /delay/5 HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
        struct sockaddr_in a;
        struct statemachine m={0,0,0};
        SOCKET s=socket(AF_INET,SOCK_STREAM,0);
        socket_unblock(&s);

        memset(&a,0,sizeof(a));
        a.sin_family=AF_INET; a.sin_port=htons(80);
        memcpy(&a.sin_addr,h->h_addr,h->h_length);


        while(m.state!=-1){
            unsigned int sleepcnt=0;
            while(m.wait){sleepf(0.1); printf("\r... wait %f sec",++sleepcnt*0.1); fflush(stdout);}
            printf("\n");
            switch(m.state){
                case 0:
                    printf("state=%d\n",m.state);
                    if(m.err==ETIMEDOUT){
                        m.state=-1; printf("\tconnect timeout\n"); break;
                    }
                    if(connect(s,(struct sockaddr*)&a,sizeof(a))==-1 && errno==EINPROGRESS){
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
                    const int c=recv(s,buf,sizeof(buf),0);
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
        close(s);
    }

    poll_unloop();

    for(N=i,i=0;i<N;++i)
        pthread_join(t[i],NULL);

    return 0;
}
