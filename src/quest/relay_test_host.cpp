// Test-only symbol provider: never shipped or used by the game.
#include <cstdint>
extern "C" __attribute__((visibility("default"))) int nxhost_sock_bind(int,uint32_t,int){return -1;}
