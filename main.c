#include <stdio.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

void func2(void)
{
    printf("Here is func2\n");
    int *tmp = NULL;
    *tmp = 1;
}

void func1(void)
{
    printf("Here is func1\n");
    func2();
}

void *thread1(void *param)
{
    printf("Hello thread1:%lu\n", syscall(SYS_gettid));
    int cnt = 0;
    while(1){
        printf("thread %lu, %d\n", syscall(SYS_gettid), cnt);
        sleep(1);
        if(cnt == 3){
            func1();
        }
        cnt++;
    }

    return NULL;
}

void func4(void)
{
    printf("Here is func4, tid:%lu\n", syscall(SYS_gettid));
    while(1)
        sleep(1);
}

void func3(void)
{
    printf("Here is func3, tid:%lu\n", syscall(SYS_gettid));
    func4();
}

void *thread2(void *param)
{
    printf("Hello thread2 %lu\n", syscall(SYS_gettid));
    func3();
    return NULL;
}

int main(void)
{
    printf("Hello world!\n");
    pthread_t pid1;
    pthread_t pid2;
    pthread_create(&pid1, NULL, thread1, NULL);
    pthread_create(&pid2, NULL, thread2, NULL);
    pthread_join(pid1, NULL);
    pthread_join(pid2, NULL);

    return 0;
}
