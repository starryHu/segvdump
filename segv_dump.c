#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <elf.h>

#define NOFF __attribute__ ((no_instrument_function))
#define MAX_TRACE_THREAD 20
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned char init_flg = 0;

typedef struct func_node_{
    void *func;
    void *call_site;
    struct func_node_ *prev;
    struct func_node_ *next;
}func_node;

struct thrd_nodes_{
    pthread_t tid;
    func_node *tail;
}thrd_nodes[MAX_TRACE_THREAD];

static char * NOFF read_file_data(char *file)
{
    if(file == NULL)
        return NULL;
    int fd = -1;
    int file_len = -1;
    char *file_buf = NULL;
    if((fd = open(file, O_RDONLY)) < 0){
        printf("Open file %s failed, %s.\n", file, strerror(errno));
        return NULL;
    }
    if((file_len = lseek(fd, 0, SEEK_END)) < 0){
        printf("Seek to end of file failed, %s.\n", strerror(errno));
        goto exit;
    }
    if((lseek(fd, 0, SEEK_SET)) < 0){
        printf("Seek to start of file failed, %s.\n", strerror(errno));
        goto exit;
    }
    if((file_buf = malloc(file_len)) < 0){
        printf("Malloc failed, %s.\n", strerror(errno));
        goto exit;
    }
    if((read(fd, file_buf, file_len)) < 0){
        printf("Read data from file failed, %s.\n", strerror(errno));
        free(file_buf);
        file_buf = NULL;
        goto exit;
    }
exit:
    if(fd > 0)
        close(fd);
    return file_buf;
}

#define BUF_LEN 1024*1024*100
static char * NOFF addr2name(void *func)
{
    char *exec = "/proc/self/exe";
    static char *data = NULL;
    if(data == NULL){
        if((data = read_file_data(exec)) == NULL){
            printf("Read cotent of file failed.\n");
            return NULL;
        }
    }
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;
    Elf64_Shdr *shdr = (Elf64_Shdr *)(data + ehdr->e_shoff);
    Elf64_Shdr sh_str = shdr[ehdr->e_shstrndx];
    char *shstr_base = NULL;
    shstr_base = data + sh_str.sh_offset;
    unsigned int shsym_ndx = 0;
    unsigned int shsymstr_ndx = 0;
    int i = 0;
    for(i = 0; i < ehdr->e_shnum; i++){
        if((strcmp(shstr_base+shdr[i].sh_name, ".symtab")) == 0)
            shsym_ndx = i;
        if((strcmp(shstr_base+shdr[i].sh_name, ".strtab")) == 0)
            shsymstr_ndx = i;
    }
    if(shsym_ndx == 0 || shsymstr_ndx == 0){
        printf("There is no symbol section or symstr section.\n");
        return NULL;
    }
    Elf64_Shdr sh_sym = shdr[shsym_ndx];
    Elf64_Shdr sh_symstr = shdr[shsymstr_ndx];
    Elf64_Sym *symbol = (Elf64_Sym *)(data + sh_sym.sh_offset);
    unsigned int sym_num = sh_sym.sh_size / sh_sym.sh_entsize;
    char *symstr_base = data + sh_symstr.sh_offset;
    for(i = 0; i < sym_num; i++){
        if(symbol[i].st_value == (Elf64_Addr)func)
            return symstr_base+symbol[i].st_name;
    }
    return NULL;
}

static void NOFF trace_dump(void)
{
    int i;
    func_node *node = NULL;
    lockf(1, F_LOCK, 0);
    lockf(2, F_LOCK, 0);
    printf("---------------Trace dump---crash tid %lu---------------\n", syscall(SYS_gettid));
    for(i = 0; i < MAX_TRACE_THREAD; i++){
        if(thrd_nodes[i].tid != 0){
            printf("=====Trace thread:%lu\n", thrd_nodes[i].tid);
            node = thrd_nodes[i].tail;
            while(node != NULL){
                printf("*****func:%p\t symbol:%s\n", node->func, addr2name(node->func));
                node = node->prev;
            }
        }
    }
    printf("---------------------Trace dump end----------------------\n");
    lockf(1, F_ULOCK, 0);
    lockf(2, F_ULOCK, 0);
}

static void NOFF sig_handler(int signo)
{
    trace_dump();
    exit(0);
}

static void NOFF init(void)
{
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    init_flg = 1;
}

static void NOFF add_node(pthread_t thrd_id, void *func, void *call_site)
{
    int idx = -1;
    int i = 0;
    pthread_mutex_lock(&mutex);
    if(!init_flg) init();
    for(i = 0; i < MAX_TRACE_THREAD; i++){
        if(thrd_nodes[i].tid == 0 && idx == -1){
            idx = i;
        }
        if(thrd_nodes[i].tid == thrd_id){
            idx = i;
            break;
        }
    }
    if(idx == -1){
        printf("Reach the max thread trace.\n");
        goto exit;
    }

    func_node *node  = malloc(sizeof(func_node));
    if(node == NULL){
        printf("Malloc for func node failed.\n");
        goto exit;
    }
    node->func = func;
    node->call_site = call_site;
    node->prev = node->next = NULL;

    thrd_nodes[idx].tid = thrd_id;
    if(thrd_nodes[idx].tail != NULL){
        thrd_nodes[idx].tail->next = node;
        node->prev = thrd_nodes[idx].tail;
    }
    thrd_nodes[idx].tail = node;
exit:
    pthread_mutex_unlock(&mutex);
    return;
}

static void NOFF del_node(pthread_t thrd_id, void *func, void *call_site)
{
    int i = 0;
    pthread_mutex_lock(&mutex);
    if(!init_flg) init();
    for(i = 0; i < MAX_TRACE_THREAD; i++){
        if(thrd_nodes[i].tid == thrd_id)
            break;
    }
    if(i >= MAX_TRACE_THREAD){
        printf("Did not find trace thread, maybe destroyed.\n");
        goto exit;
    }
    if(thrd_nodes[i].tail->func != func){
        printf("Func value is not match, maybe destroyed.\n");
        goto exit;
    }
    func_node *node = thrd_nodes[i].tail;
    if(node == NULL){
        printf("There is no func node, maybe destroyed.\n");
        goto exit;
    }
    thrd_nodes[i].tail = node->prev;
    if(thrd_nodes[i].tail == NULL){
        thrd_nodes[i].tid = 0;
    }else{
        node->prev->next = NULL;
        free(node);
    }
exit:
    pthread_mutex_unlock(&mutex);
    return;
}

void NOFF __cyg_profile_func_enter(void *this_func, void *call_site)
{
    pthread_t tid;
    tid = syscall(SYS_gettid);
    add_node(tid, this_func, call_site);
}

void NOFF __cyg_profile_func_exit(void *this_func, void *call_site)
{
    pthread_t tid;
    tid = syscall(SYS_gettid);
    del_node(tid, this_func, call_site);
}
