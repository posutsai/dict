#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include "bench.c"
#include "tst.h"

#define NUM_TESTING 1000
#define IN_FILE "cities.txt"
enum { INS, DEL, WRDMAX = 256, STKMAX = 512, LMAX = 1024 };

#ifdef __TEST_CPY__
#define REF INS
#define CPY DEL
#endif

static long perf_event_open(struct perf_event_attr *hw_event,
                            pid_t pid,
                            int cpu,
                            int group_fd,
                            unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

long long cpu_cycles(pid_t pid, unsigned int microseconds)
{
    struct perf_event_attr pe;
    long long count;
    int fd;

    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    fd = perf_event_open(&pe, pid, -1, -1, 0);
    if (fd == -1) {
        return -1;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    usleep(microseconds);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    read(fd, &count, sizeof(long long));

    close(fd);
    return count;
}


int main(int argc, char *argv[])
{
    int status;
    srand(time(NULL));
    long poolsize = 2000000 * WRDMAX;
    char *pool = (char *) malloc(poolsize * sizeof(char));
    char *Top = pool;
#ifdef __TEST_CPY__
    char word[WRDMAX] = "";
    char *sgl[LMAX] = {NULL};
    tst_node *root = NULL, *res = NULL;
    int rtn = 0, idx = 0, sidx = 0;
    double t1, t2;

    FILE *fp = fopen(IN_FILE, "r");


    if (!fp) { /* prompt, open, validate file for reading */
        fprintf(stderr, "error: file open failed '%s'.\n", argv[1]);
        return 1;
    }
    while ((rtn = fscanf(fp, "%s", Top)) != EOF) {
        char *p = Top;
        if (!tst_ins_del(&root, &p, INS, CPY)) {
            fprintf(stderr, "error: memory exhausted, tst_insert.\n");
            fclose(fp);
            return 1;
        }
        idx++;
        Top += (strlen(word) + 1);
    }
    fclose(fp);
    char *record_file = "cpy_random_search.csv";
#endif
    FILE *f_record = fopen(record_file, "w");
    for (int i = 0; i < NUM_TESTING; i++) {
        pid_t pid;
        FILE *file = fopen(IN_FILE, "r");
        char search_target[WRDMAX];
        do {
            fseek(file, 0, SEEK_SET);
            int line = rand() % idx;
            for (int l = 0; l < line; l++) {
                // move to that line
                fscanf(file, "%s", search_target);
            }
        } while (strcmp(",", search_target) == 0);
        fclose(file);
        pid = fork();
        if (pid < 0) {
            printf("forking child process error\n");
            exit(-1);
        } else if (pid == 0) {
            // child process
            res = tst_search_prefix(root, search_target, sgl, &sidx, LMAX);
            exit(0);
        } else {
            // parent process
            double t_start = tvgetf();
            long long ipc = cpu_cycles(pid, 1000) * 1000;
            do {
                pid_t w = waitpid(pid, &status, WUNTRACED | WCONTINUED);
                if (w == -1) {
                    perror("waitpid");
                    exit(EXIT_FAILURE);
                }
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
            double t_end = tvgetf();
            fprintf(f_record, "%s, %.6f, %lld\n", search_target,
                    t_end - t_start, ipc);
        }
    }
    fclose(f_record);
    tst_free_all(root);
    free(pool);
    return 0;
}
