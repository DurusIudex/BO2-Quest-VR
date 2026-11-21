#include "relay_transport.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv){
    if(argc!=3)return 2;
    if(!dlopen(argv[2],RTLD_NOW|RTLD_GLOBAL)){fprintf(stderr,"load test shim: %s\n",dlerror());return 3;}
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(4977);
    if(fd<0||bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))){perror("bind");return 4;}
    timeval timeout{20,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    bo2vr::installRelay(argv[1],"zm");
    using RecvChecked=ssize_t(*)(int,void*,size_t,size_t,int,sockaddr*,socklen_t*);
    using SendChecked=ssize_t(*)(int,const void*,size_t,size_t,int,const sockaddr*,socklen_t);
    auto recvChecked=reinterpret_cast<RecvChecked>(dlsym(RTLD_DEFAULT,"__recvfrom_chk"));
    auto sendChecked=reinterpret_cast<SendChecked>(dlsym(RTLD_DEFAULT,"__sendto_chk"));
    if(!recvChecked||!sendChecked)return 9;
    char buffer[128]{};sockaddr_in remote{};socklen_t length=sizeof(remote);
    ssize_t n=recvChecked(fd,buffer,sizeof(buffer),sizeof(buffer),0,reinterpret_cast<sockaddr*>(&remote),&length);
    if(n!=13||memcmp(buffer,"quest-adapter",13)||ntohl(remote.sin_addr.s_addr)!=0x0afd0002||ntohs(remote.sin_port)!=3074){fprintf(stderr,"native receive failed: n=%zd\n",n);return 5;}
    if(sendChecked(fd,"quest-reply",11,12,0,reinterpret_cast<sockaddr*>(&remote),length)!=11){perror("native send");return 6;}
    // A direct UDP datagram also must survive the interceptor unchanged.
    int direct=socket(AF_INET,SOCK_DGRAM,0);local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    sendto(direct,"direct",6,0,reinterpret_cast<sockaddr*>(&local),sizeof(local));
    n=recvfrom(fd,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&remote),&length);
    if(n!=6||memcmp(buffer,"direct",6)||ntohl(remote.sin_addr.s_addr)!=0x7f000001){fprintf(stderr,"direct receive failed\n");return 7;}
    char large[20000];memset(large,'x',sizeof(large));
    sendto(direct,large,sizeof(large),0,reinterpret_cast<sockaddr*>(&local),sizeof(local));
    n=recvfrom(fd,buffer,sizeof(buffer),MSG_TRUNC,reinterpret_cast<sockaddr*>(&remote),&length);
    if(n!=sizeof(large)||buffer[0]!='x'||buffer[127]!='x'){fprintf(stderr,"truncation failed\n");return 8;}
    puts("PASS: Android native receive/send virtual peer translation and direct UDP passthrough.");
    fflush(stdout);close(direct);close(fd);return 0;
}
