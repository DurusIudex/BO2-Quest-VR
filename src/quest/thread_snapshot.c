// Read-only ARM64 thread snapshot for a debuggable app owned by the caller.
#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <asm/ptrace.h>
#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

static unsigned long long peek(pid_t tid, uintptr_t address) {
    errno = 0;
    unsigned long long v = (unsigned long long)ptrace(PTRACE_PEEKDATA, tid, (void*)address, 0);
    if (errno) fprintf(stderr, "read %llx failed: %d\n", (unsigned long long)address, errno);
    return v;
}
int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) return 2;
    pid_t tid = (pid_t)strtol(argv[1], 0, 10);
    if (tid <= 0) return 2;
    if (ptrace(PTRACE_SEIZE, tid, 0, 0)) { perror("seize"); return 1; }
    if (ptrace(PTRACE_INTERRUPT, tid, 0, 0)) { perror("interrupt"); ptrace(PTRACE_DETACH,tid,0,0); return 1; }
    int status;
    if (waitpid(tid, &status, __WALL) < 0) { perror("wait"); ptrace(PTRACE_DETACH,tid,0,0); return 1; }
    if (argc == 4) {
        uintptr_t address = strtoull(argv[2], 0, 0);
        size_t length = strtoul(argv[3], 0, 0);
        int failed = !address || !length || length > 131072;
        for (size_t i=0; !failed && i<length; i+=sizeof(long)) {
            errno=0;
            long word=ptrace(PTRACE_PEEKDATA,tid,(void*)(address+i),0);
            if(errno){perror("read memory");failed=1;break;}
            size_t n=length-i; if(n>sizeof(word))n=sizeof(word);
            if(fwrite(&word,1,n,stdout)!=n){failed=1;break;}
        }
        if(ptrace(PTRACE_DETACH,tid,0,0)){perror("detach");return 1;}
        return failed;
    }
    struct user_pt_regs regs = {0};
    struct iovec io = {&regs, sizeof(regs)};
    int result = ptrace(PTRACE_GETREGSET,tid,(void*)NT_PRSTATUS,&io);
    if (result) perror("registers");
    else {
        printf("tid=%d pc=%llx sp=%llx fp=%llx\n",tid,regs.pc,regs.sp,regs.regs[29]);
        for (unsigned offset=0; offset<0x70; offset+=8)
            printf("sp+%02x: %016llx\n", offset, peek(tid, regs.sp+offset));
        // Only interpret the verified decompressor frame for this exact image.
        if (regs.pc >= 0xa10395a4ULL && regs.pc <= 0xa10395b8ULL) {
            uintptr_t msg = (uint32_t)peek(tid, regs.sp+0x38);
            printf("inflate_status=%d z_stream_message_pointer=%lx\n", (int32_t)peek(tid,regs.sp+0x68),(unsigned long)msg);
            if (msg) {
                char text[129] = {0};
                for (unsigned i=0; i<128; i+=8) {
                    unsigned long long word = peek(tid,msg+i);
                    __builtin_memcpy(text+i,&word,8);
                    if (__builtin_memchr(text+i,0,8)) break;
                }
                printf("inflate_message=%s\n",text);
            }
        }
    }
    if (ptrace(PTRACE_DETACH,tid,0,0)) { perror("detach"); return 1; }
    return result != 0;
}
