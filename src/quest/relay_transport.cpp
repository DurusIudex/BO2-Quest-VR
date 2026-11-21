#include "relay_transport.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <dlfcn.h>
#include <dobby.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>

namespace {
constexpr size_t headerSize=27,maxPayload=16384;
using Send=ssize_t(*)(int,const void*,size_t,int,const sockaddr*,socklen_t);
using Recv=ssize_t(*)(int,void*,size_t,int,sockaddr*,socklen_t*);
using SendChecked=ssize_t(*)(int,const void*,size_t,size_t,int,const sockaddr*,socklen_t);
using RecvChecked=ssize_t(*)(int,void*,size_t,size_t,int,sockaddr*,socklen_t*);
using Bind=int(*)(int,uint32_t,int);
Send realSend=nullptr;Recv realRecv=nullptr;Bind realGuestBind=nullptr;
SendChecked realSendChecked=nullptr;RecvChecked realRecvChecked=nullptr;
std::mutex relayMutex;
std::map<int,uint16_t> sockets;
sockaddr_in relayAddress{};
std::array<unsigned char,16> token{};
unsigned peer=0;
bool installed=false,join=false,commandSent=false;
using Command=bool(*)(const char*);
Command command=nullptr;
std::chrono::steady_clock::time_point commandReady;
void log(const char* text){__android_log_print(ANDROID_LOG_INFO,"BO2QuestVR","Relay: %s",text);}
uint16_t read16(const unsigned char* p){return uint16_t(p[0])*256+p[1];}
void write16(unsigned char* p,uint16_t n){p[0]=n>>8;p[1]=n&255;}
std::array<unsigned char,headerSize> header(unsigned kind,unsigned target,uint16_t source,uint16_t dest){
    std::array<unsigned char,headerSize> h{};memcpy(h.data(),"B2VR",4);h[4]=1;h[5]=kind;
    memcpy(h.data()+6,token.data(),16);h[22]=target;write16(h.data()+23,source);write16(h.data()+25,dest);return h;
}
uint16_t socketPort(int fd){sockaddr_in a{};socklen_t n=sizeof(a);int type=0;socklen_t tn=sizeof(type);
    if(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&tn)||type!=SOCK_DGRAM||getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)||a.sin_family!=AF_INET)return 0;
    return ntohs(a.sin_port);
}
void registerSocket(int fd){
    uint16_t port=socketPort(fd);if(!port)return;
    std::lock_guard<std::mutex> hold(relayMutex);
    if(sockets.size()>=8&&!sockets.count(fd))return;
    if(!sockets.count(fd))__android_log_print(ANDROID_LOG_INFO,"BO2QuestVR","Relay: game socket fd=%d port=%u",fd,port);
    sockets[fd]=port;
    auto h=header(0,0,port,0);realSend(fd,h.data(),h.size(),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&relayAddress),sizeof(relayAddress));
}
int hookedGuestBind(int fd,uint32_t address,int size){int result=realGuestBind(fd,address,size);if(result==0)registerSocket(fd);return result;}
ssize_t hookedSend(int fd,const void* buffer,size_t length,int flags,const sockaddr* address,socklen_t size){
    if(!address||size<sizeof(sockaddr_in)||address->sa_family!=AF_INET)return realSend(fd,buffer,length,flags,address,size);
    const auto& a=*reinterpret_cast<const sockaddr_in*>(address);uint32_t ip=ntohl(a.sin_addr.s_addr);
    if((ip&0xffffff00u)!=0x0afd0000u)return realSend(fd,buffer,length,flags,address,size);
    unsigned target=ip&255;
    if(!target||target>18||target==peer||length>maxPayload){errno=EINVAL;return -1;}
    registerSocket(fd);uint16_t source=socketPort(fd);if(!source){errno=ENOTSOCK;return -1;}
    auto h=header(1,target,source,ntohs(a.sin_port));std::vector<unsigned char> packet(h.begin(),h.end());
    const auto* bytes=static_cast<const unsigned char*>(buffer);packet.insert(packet.end(),bytes,bytes+length);
    ssize_t result=realSend(fd,packet.data(),packet.size(),flags,reinterpret_cast<sockaddr*>(&relayAddress),sizeof(relayAddress));
    return result<0?result:static_cast<ssize_t>(length);
}
ssize_t hookedRecv(int fd,void* buffer,size_t length,int flags,sockaddr* address,socklen_t* size){
    {std::lock_guard<std::mutex> hold(relayMutex);if(!sockets.count(fd))return realRecv(fd,buffer,length,flags,address,size);}
    std::array<unsigned char,maxPayload+headerSize> packet{};sockaddr_in from{};socklen_t fromSize=sizeof(from);
    ssize_t result=realRecv(fd,packet.data(),packet.size(),flags,reinterpret_cast<sockaddr*>(&from),&fromSize);
    if(result<0)return result;
    bool fromRelay=from.sin_family==AF_INET&&from.sin_addr.s_addr==relayAddress.sin_addr.s_addr&&from.sin_port==relayAddress.sin_port;
    size_t offset=0;
    if(fromRelay){
        bool valid=size_t(result)>=headerSize&&size_t(result)<=packet.size()&&!memcmp(packet.data(),"B2VR",4)&&packet[4]==1&&packet[5]==1&&!memcmp(packet.data()+6,token.data(),16)&&packet[22]>=1&&packet[22]<=18;
        if(!valid){errno=EAGAIN;return -1;}
        from.sin_addr.s_addr=htonl(0x0afd0000u|packet[22]);from.sin_port=htons(read16(packet.data()+23));offset=headerSize;
    }
    // MSG_TRUNC can return the original datagram length, beyond our buffer.
    size_t payload=size_t(result)-offset;size_t copy=std::min({length,payload,packet.size()-offset});
    memcpy(buffer,packet.data()+offset,copy);
    if(address&&size){memcpy(address,&from,std::min(size_t(*size),sizeof(from)));*size=sizeof(from);}
    return flags&MSG_TRUNC?ssize_t(payload):ssize_t(copy);
}
ssize_t hookedSendChecked(int fd,const void* buffer,size_t length,size_t capacity,int flags,const sockaddr* address,socklen_t size){
    if(length>capacity)return realSendChecked(fd,buffer,length,capacity,flags,address,size);
    return hookedSend(fd,buffer,length,flags,address,size);
}
ssize_t hookedRecvChecked(int fd,void* buffer,size_t length,size_t capacity,int flags,sockaddr* address,socklen_t* size){
    if(length>capacity)return realRecvChecked(fd,buffer,length,capacity,flags,address,size);
    return hookedRecv(fd,buffer,length,flags,address,size);
}
template<class T> bool hook(void* lib,const char* name,void* replacement,T& original){
    void* fn=dlsym(lib,name);return fn&&DobbyHook(fn,reinterpret_cast<dobby_dummy_func_t>(replacement),reinterpret_cast<dobby_dummy_func_t*>(&original))==0;
}
}
namespace bo2vr {
void installRelay(const std::string& files,const char* mode){
    if(installed)return;
    std::ifstream input(files+"/quest_online.ini");if(!input)return;
    std::map<std::string,std::string> config;std::string line;
    while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto p=line.find('=');if(p!=std::string::npos)config[line.substr(0,p)]=line.substr(p+1);}
    if(!mode||config["mode"]!=mode){log("room mode differs from game; transport disabled");return;}
    if(config["token"].size()!=32)return;
    auto nibble=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(unsigned i=0;i<16;++i){int a=nibble(config["token"][i*2]),b=nibble(config["token"][i*2+1]);if(a<0||b<0)return;token[i]=a*16+b;}
    try{peer=std::stoul(config["peer"]);relayAddress.sin_port=htons(std::stoul(config["port"]));}catch(...){return;}
    if(peer<1||peer>18||!relayAddress.sin_port)return;
    relayAddress.sin_family=AF_INET;if(inet_pton(AF_INET,config["host"].c_str(),&relayAddress.sin_addr)!=1)return;
    void* host=dlopen("libt6host.so",RTLD_NOW|RTLD_NOLOAD);void* libc=dlopen("libc.so",RTLD_NOW);if(!host||!libc)return;
    if(!hook(libc,"sendto",reinterpret_cast<void*>(hookedSend),realSend)||!hook(libc,"recvfrom",reinterpret_cast<void*>(hookedRecv),realRecv)) {log("socket hook failed");return;}
    if(!hook(libc,"__sendto_chk",reinterpret_cast<void*>(hookedSendChecked),realSendChecked)||!hook(libc,"__recvfrom_chk",reinterpret_cast<void*>(hookedRecvChecked),realRecvChecked)){log("checked socket hook failed");return;}
    if(!hook(host,"nxhost_sock_bind",reinterpret_cast<void*>(hookedGuestBind),realGuestBind)){log("bind hook failed");return;}
    command=reinterpret_cast<Command>(dlsym(host,"_ZN2t618input_push_commandEPKc"));
    installed=true;join=peer!=1;commandReady=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    // The engine can open its UDP socket before Vulkan initializes.
    for(int fd=3;fd<1024;++fd){uint16_t port=socketPort(fd);if(port==3074||port==3075||(port>=4976&&port<=4979))registerSocket(fd);}
    std::thread([]{
        for(;;){
            {std::lock_guard<std::mutex> hold(relayMutex);
                for(auto i=sockets.begin();i!=sockets.end();){
                    if(socketPort(i->first)!=i->second){i=sockets.erase(i);continue;}
                    auto h=header(0,0,i->second,0);realSend(i->first,h.data(),h.size(),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&relayAddress),sizeof(relayAddress));++i;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }).detach();
    log(join?"guest transport ready":"host transport ready");
}
void pollRelayCommand(){
    if(!installed||!command||commandSent)return;
    if(std::chrono::steady_clock::now()<commandReady)return;
    // This port's NetDirect lobby transport uses 4976..4979, not retail 3074.
    // 'join' joins a lobby; 'connect' queries an already-running match.
    // Private room sessions must not publish themselves to the public index.
    const char* privacy="set net_direct 1; set net_index_publish 0; set net_stun 0; set net_upnp 0; set net_dht 0; ";
    std::string text=privacy;
    text+=join?"join 10.253.0.1:4976":"xstartprivateparty";
    commandSent=command(text.c_str());
    if(commandSent)log(join?"join command queued":"private party command queued");
}
}
