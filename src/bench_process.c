#include "mm.h"
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

enum { PROC_CMD_STOP=0, PROC_CMD_WAKE=1, PROC_CMD_WORK=2 };
enum { PROC_MSG_READY=1, PROC_MSG_LOADED=2, PROC_MSG_DONE=3, PROC_MSG_ERROR=4 };

typedef struct {
    int command;
    double sent_at;
} process_command;

typedef struct {
    int stage, status, pid;
    unsigned workers;
    size_t private_bytes, shared_bytes, stack_reserved, stack_pss;
    uint64_t accesses, checksum, iterations;
    double wakeup_ms, work_seconds;
} process_message;

typedef struct {
    mm_mutex mutex;
    mm_cond cond;
    unsigned generation, loaded, done, workers;
    int stop, failed;
    size_t private_size, shared_size, iterations, page_size;
    char *shared;
    unsigned process_index, process_count;
} child_gate;

typedef struct {
    child_gate *gate;
    unsigned id;
    void *private_data;
    void *stack_map, *stack_usable;
    size_t stack_map_size, stack_size;
    uint64_t checksum, accesses;
    double seconds;
    int failed;
} process_worker;

static int read_full(int fd, void *buffer, size_t size) {
    unsigned char *p=buffer;
    while (size) {
        ssize_t n=read(fd,p,size);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return -1;
        p+=(size_t)n; size-=(size_t)n;
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t size) {
    const unsigned char *p=buffer;
    while (size) {
        ssize_t n=write(fd,p,size);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return -1;
        p+=(size_t)n; size-=(size_t)n;
    }
    return 0;
}

static int smaps_totals(int pid, size_t *pss, size_t *rss) {
    char path[64], line[256];
    unsigned long long pss_kib=0, rss_kib=0;
    int have_pss=0, have_rss=0;
    snprintf(path,sizeof(path),"/proc/%d/smaps_rollup",pid);
    FILE *file=fopen(path,"r");
    if (!file) return -1;
    while (fgets(line,sizeof(line),file)) {
        unsigned long long value;
        if (sscanf(line,"Pss: %llu kB",&value)==1) { pss_kib=value; have_pss=1; }
        else if (sscanf(line,"Rss: %llu kB",&value)==1) { rss_kib=value; have_rss=1; }
    }
    fclose(file);
    if (!have_pss || !have_rss) return -1;
    *pss=(size_t)pss_kib*1024;
    *rss=(size_t)rss_kib*1024;
    return 0;
}

static int ranges_overlap(uintptr_t start, uintptr_t end, uintptr_t wanted, size_t size) {
    uintptr_t wanted_end=wanted+size;
    return start<wanted_end && wanted<end;
}

static size_t shared_bytes_for(size_t total, unsigned process_count, unsigned process_index) {
    size_t partition=(total/process_count)&~(size_t)7;
    size_t offset=partition*process_index;
    return process_index+1==process_count ? total-offset : partition;
}

static size_t stack_pss_bytes(const process_worker *workers, unsigned count) {
    FILE *file=fopen("/proc/self/smaps","r");
    char line[256];
    uintptr_t start=0, end=0;
    size_t mapping_pss=0, total=0;
    int selected=0;
    if (!file) return 0;
    while (fgets(line,sizeof(line),file)) {
        unsigned long a,b;
        if (sscanf(line,"%lx-%lx",&a,&b)==2) {
            if (selected) total+=mapping_pss;
            start=(uintptr_t)a; end=(uintptr_t)b; selected=0; mapping_pss=0;
            for (unsigned i=0;i<count;++i) {
                if (ranges_overlap(start,end,(uintptr_t)workers[i].stack_map,workers[i].stack_map_size)) selected=1;
            }
        } else if (selected) {
            size_t value;
            if (sscanf(line,"Pss: %zu kB",&value)==1) mapping_pss=value*1024;
        }
    }
    if (selected) total+=mapping_pss;
    fclose(file);
    return total;
}

static void worker_signal_loaded(child_gate *gate) {
    mm_lock(&gate->mutex);
    ++gate->loaded;
    mm_broadcast(&gate->cond);
    mm_unlock(&gate->mutex);
}

static void *process_worker_main(void *arg) {
    process_worker *worker=arg;
    child_gate *gate=worker->gate;
    mm_lock(&gate->mutex);
    while (gate->generation<1 && !gate->stop) mm_wait(&gate->cond,&gate->mutex);
    if (gate->stop) { mm_unlock(&gate->mutex); return NULL; }
    mm_unlock(&gate->mutex);

    worker->private_data=mm_alloc(gate->private_size);
    if (!worker->private_data) worker->failed=1;
    if (!worker->failed) {
        unsigned char *bytes=worker->private_data;
        for (size_t offset=0;offset<gate->private_size;offset+=gate->page_size) bytes[offset]=(unsigned char)(worker->id+1);
        if (worker->id==0 && gate->shared_size) {
            size_t partition=(gate->shared_size/gate->process_count)&~(size_t)7;
            size_t offset=partition*gate->process_index;
            size_t bytes_count=shared_bytes_for(gate->shared_size,gate->process_count,gate->process_index);
            for (size_t j=0;j<bytes_count;j+=gate->page_size) gate->shared[offset+j]=(char)(gate->process_index+1);
        }
    }
    worker_signal_loaded(gate);

    mm_lock(&gate->mutex);
    while (gate->generation<2 && !gate->stop) mm_wait(&gate->cond,&gate->mutex);
    if (gate->stop || worker->failed) { mm_unlock(&gate->mutex); return NULL; }
    mm_unlock(&gate->mutex);

    double start=mm_now();
    uint64_t checksum=0, accesses=0;
    size_t words=gate->private_size/sizeof(uint64_t);
    uint64_t *data=worker->private_data;
    for (uint64_t pass=0;pass<gate->iterations;++pass) {
        for (size_t i=0;i<words;++i) checksum+=data[i];
        accesses+=(uint64_t)words;
    }
    if (worker->id==0 && gate->shared_size) {
        size_t partition=(gate->shared_size/gate->process_count)&~(size_t)7;
        size_t offset=partition*gate->process_index;
        size_t bytes_count=shared_bytes_for(gate->shared_size,gate->process_count,gate->process_index);
        size_t shared_words=bytes_count/sizeof(uint64_t);
        const uint64_t *shared=(const uint64_t *)(gate->shared+offset);
        for (uint64_t pass=0;pass<gate->iterations;++pass) {
            for (size_t i=0;i<shared_words;++i) checksum+=shared[i];
            accesses+=(uint64_t)shared_words;
        }
    }
    worker->seconds=mm_now()-start;
    worker->checksum=checksum;
    worker->accesses=accesses;

    mm_lock(&gate->mutex);
    ++gate->done;
    mm_broadcast(&gate->cond);
    mm_unlock(&gate->mutex);
    return NULL;
}

static int create_worker_stack(process_worker *worker, size_t requested, long page_size) {
    size_t stack_size=(requested+(size_t)page_size-1)/(size_t)page_size*(size_t)page_size;
    if (stack_size>SIZE_MAX-(size_t)page_size) return -1;
    size_t mapping_size=stack_size+(size_t)page_size;
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_STACK
    flags|=MAP_STACK;
#endif
    worker->stack_map=mmap(NULL,mapping_size,PROT_READ|PROT_WRITE,flags,-1,0);
    if (worker->stack_map==MAP_FAILED) return -1;
    if (mprotect(worker->stack_map,(size_t)page_size,PROT_NONE)) {
        munmap(worker->stack_map,mapping_size); return -1;
    }
    worker->stack_map_size=mapping_size;
    worker->stack_size=stack_size;
    worker->stack_usable=(char *)worker->stack_map+page_size;
    return 0;
}

static int child_execute(int index, unsigned process_count, int control_fd, int result_fd,
                         const mm_config *config, char *shared, long page_size) {
    child_gate gate;
    process_worker *workers=calloc(config->process_threads,sizeof(*workers));
    pthread_t *threads=calloc(config->process_threads,sizeof(*threads));
    pthread_attr_t attr;
    unsigned launched=0;
    int rc=-1, mutex_ready=0, cond_ready=0;
    process_message message;
    memset(&gate,0,sizeof(gate));
    gate.workers=config->process_threads;
    gate.private_size=config->size;
    gate.shared_size=config->shared_size;
    gate.iterations=config->iterations ? config->iterations : 1;
    gate.page_size=(size_t)page_size;
    gate.shared=shared;
    gate.process_index=(unsigned)index;
    gate.process_count=process_count;
    if (!workers || !threads) goto cleanup;
    if (mm_mutex_init(&gate.mutex)) goto cleanup;
    mutex_ready=1;
    if (mm_cond_init(&gate.cond)) goto cleanup;
    cond_ready=1;
    if (pthread_attr_init(&attr)) goto cleanup;
    for (unsigned i=0;i<config->process_threads;++i) {
        workers[i].gate=&gate; workers[i].id=i;
        if (create_worker_stack(&workers[i],config->thread_stack,page_size)) break;
        if (pthread_attr_setstack(&attr,workers[i].stack_usable,workers[i].stack_size)) break;
        if (pthread_create(&threads[i],&attr,process_worker_main,&workers[i])) break;
        ++launched;
    }
    pthread_attr_destroy(&attr);
    if (launched!=config->process_threads) goto stop_workers;
    memset(&message,0,sizeof(message));
    message.stage=PROC_MSG_READY; message.status=0; message.pid=(int)getpid();
    message.workers=launched;
    if (write_full(result_fd,&message,sizeof(message))) goto stop_workers;

    process_command command;
    if (read_full(control_fd,&command,sizeof(command)) || command.command!=PROC_CMD_WAKE) goto stop_workers;
    double wake_started=mm_now();
    mm_lock(&gate.mutex); gate.generation=1; mm_broadcast(&gate.cond); mm_unlock(&gate.mutex);
    mm_lock(&gate.mutex);
    while (gate.loaded<gate.workers && !gate.stop) mm_wait(&gate.cond,&gate.mutex);
    mm_unlock(&gate.mutex);
    for (unsigned i=0;i<gate.workers;++i) if (workers[i].failed) gate.failed=1;
    if (gate.failed) {
        memset(&message,0,sizeof(message)); message.stage=PROC_MSG_ERROR; message.status=-2; message.pid=(int)getpid();
        write_full(result_fd,&message,sizeof(message));
        goto stop_workers;
    }
    memset(&message,0,sizeof(message));
    message.stage=PROC_MSG_LOADED; message.pid=(int)getpid(); message.workers=gate.workers;
    message.private_bytes=config->size*config->process_threads;
    message.shared_bytes=shared_bytes_for(config->shared_size,process_count,(unsigned)index);
    message.stack_reserved=(size_t)config->process_threads*((config->thread_stack+(size_t)page_size-1)/(size_t)page_size*(size_t)page_size+(size_t)page_size);
    message.stack_pss=stack_pss_bytes(workers,gate.workers);
    if (write_full(result_fd,&message,sizeof(message))) goto stop_workers;

    if (read_full(control_fd,&command,sizeof(command)) || command.command!=PROC_CMD_WORK) goto stop_workers;
    double wakeup_ms=(mm_now()-command.sent_at)*1000;
    mm_lock(&gate.mutex); gate.generation=2; mm_broadcast(&gate.cond); mm_unlock(&gate.mutex);
    mm_lock(&gate.mutex);
    while (gate.done<gate.workers && !gate.stop) mm_wait(&gate.cond,&gate.mutex);
    mm_unlock(&gate.mutex);
    memset(&message,0,sizeof(message));
    message.stage=PROC_MSG_DONE; message.pid=(int)getpid(); message.workers=gate.workers;
    message.wakeup_ms=wakeup_ms;
    for (unsigned i=0;i<gate.workers;++i) {
        message.checksum^=workers[i].checksum;
        message.accesses+=workers[i].accesses;
        message.iterations+=gate.iterations;
        if (workers[i].seconds>message.work_seconds) message.work_seconds=workers[i].seconds;
    }
    (void)wake_started;
    if (write_full(result_fd,&message,sizeof(message))) goto stop_workers;
    rc=0;
    goto join_threads;

stop_workers:
    mm_lock(&gate.mutex); gate.stop=1; mm_broadcast(&gate.cond); mm_unlock(&gate.mutex);
join_threads:
    for (unsigned i=0;i<launched;++i) pthread_join(threads[i],NULL);
cleanup:
    for (unsigned i=0;i<config->process_threads;++i) {
        mm_free(workers[i].private_data);
        if (workers[i].stack_map) munmap(workers[i].stack_map,workers[i].stack_map_size);
    }
    if (cond_ready) mm_cond_destroy(&gate.cond);
    if (mutex_ready) mm_mutex_destroy(&gate.mutex);
    free(threads); free(workers);
    return rc;
}

static size_t rlimit_value(int resource) {
    struct rlimit limit;
    if (getrlimit((__rlimit_resource_t)resource,&limit) || limit.rlim_cur==RLIM_INFINITY || limit.rlim_cur>(rlim_t)SIZE_MAX) return 0;
    return (size_t)limit.rlim_cur;
}

void mm_process_level_free(mm_process_level *result) {
    free(result->children); result->children=NULL;
}

int mm_run_process_level(const mm_config *config, unsigned processes, mm_process_level *result) {
    enum { MAX_FDS=MM_MAX_PROCESSES };
    int control[MAX_FDS][2], response[MAX_FDS][2];
    pid_t pids[MAX_FDS];
    char *shared=NULL;
    long page_size=sysconf(_SC_PAGESIZE);
    int rc=-1;
    memset(result,0,sizeof(*result));
    if (page_size<=0) { fprintf(stderr,"Unable to determine system page size\n"); return -1; }
    signal(SIGPIPE,SIG_IGN);
    size_t stack_size=(config->thread_stack+(size_t)page_size-1)/(size_t)page_size*(size_t)page_size;
    for (unsigned i=0;i<MAX_FDS;++i) { control[i][0]=control[i][1]=response[i][0]=response[i][1]=-1; pids[i]=-1; }
    result->requested=processes; result->threads_per_process=config->process_threads;
    result->private_per_worker=config->size; result->shared_bytes=config->shared_size;
    result->thread_stack=config->thread_stack; result->iterations=config->iterations ? config->iterations : 1;
    result->rlimit_nproc=rlimit_value(RLIMIT_NPROC); result->rlimit_as=rlimit_value(RLIMIT_AS);
    if (processes<1 || processes>MM_MAX_PROCESSES || config->process_threads<1 || config->process_threads>MM_MAX_PROCESS_THREADS || config->size%8 || config->thread_stack<(size_t)PTHREAD_STACK_MIN) {
        fprintf(stderr,"Invalid process benchmark sizing; use 8-byte private size and stack >= PTHREAD_STACK_MIN\n"); goto cleanup;
    }
    if (config->size>SIZE_MAX/config->process_threads/processes || stack_size>SIZE_MAX-(size_t)page_size) {
        fprintf(stderr,"Process benchmark size overflow\n"); goto cleanup;
    }
    size_t private_total=config->size*config->process_threads*processes;
    size_t stack_each=stack_size+(size_t)page_size;
    if (stack_each>SIZE_MAX/config->process_threads/processes) {
        fprintf(stderr,"Process thread-stack size overflow\n"); goto cleanup;
    }
    size_t stack_total=stack_each*config->process_threads*processes;
    if (private_total>SIZE_MAX-config->shared_size || private_total+config->shared_size>SIZE_MAX-stack_total ||
        private_total+config->shared_size+stack_total>config->memory_limit) {
        fprintf(stderr,"Process benchmark exceeds --memory-limit (%zu bytes required estimate)\n",private_total+config->shared_size+stack_total);
        goto cleanup;
    }
    if (smaps_totals(getpid(),&result->baseline_pss,&result->baseline_rss)) {
        fprintf(stderr,"Linux PSS/RSS is unavailable through smaps_rollup\n"); goto cleanup;
    }
    int flags=MAP_SHARED|MAP_ANONYMOUS;
    if (config->shared_size) shared=mmap(NULL,config->shared_size,PROT_READ|PROT_WRITE,flags,-1,0);
    if (config->shared_size && shared==MAP_FAILED) { shared=NULL; goto cleanup; }
    result->children=calloc(processes,sizeof(*result->children));
    if (!result->children) goto cleanup;
    double begin=mm_now();
    unsigned created=0;
    for (unsigned i=0;i<processes;++i) {
        if (pipe(control[i]) || pipe(response[i])) goto fork_limited;
        pid_t pid=fork();
        if (pid<0) goto fork_limited;
        if (pid==0) {
            close(control[i][1]); close(response[i][0]);
            for (unsigned j=0;j<i;++j) {
                close(control[j][0]); close(control[j][1]);
                close(response[j][0]); close(response[j][1]);
            }
            int child_rc=child_execute((int)i,processes,control[i][0],response[i][1],config,shared,page_size);
            close(control[i][0]); close(response[i][1]);
            _exit(child_rc ? 1 : 0);
        }
        close(control[i][0]); control[i][0]=-1;
        close(response[i][1]); response[i][1]=-1;
        pids[i]=pid;
        ++created;
        continue;
fork_limited:
        result->capacity_limited=1;
        break;
    }
    if (!created) { fprintf(stderr,"Unable to create any process worker\n"); goto cleanup_children; }
    for (unsigned i=0;i<created;++i) {
        process_message msg;
        if (read_full(response[i][0],&msg,sizeof(msg)) || msg.stage!=PROC_MSG_READY) {
            fprintf(stderr,"Process protocol failure: ready stage from child %u\n",i); goto children_failed;
        }
        result->children[i].pid=msg.pid; result->children[i].workers=msg.workers;
    }
    size_t parent_pss,parent_rss;
    if (smaps_totals(getpid(),&parent_pss,&parent_rss)) goto children_failed;
    result->idle_pss=parent_pss; result->idle_rss=parent_rss;
    for (unsigned i=0;i<created;++i) {
        if (smaps_totals(pids[i],&result->children[i].idle_pss,&result->children[i].idle_rss)) goto children_failed;
        result->idle_pss+=result->children[i].idle_pss; result->idle_rss+=result->children[i].idle_rss;
    }
    result->footprint_available=1;
    for (unsigned i=0;i<created;++i) {
        process_command cmd={PROC_CMD_WAKE,mm_now()};
        if (write_full(control[i][1],&cmd,sizeof(cmd))) goto children_failed;
    }
    for (unsigned i=0;i<created;++i) {
        process_message msg;
        if (read_full(response[i][0],&msg,sizeof(msg)) || msg.stage!=PROC_MSG_LOADED) {
            fprintf(stderr,"Process protocol failure: loaded stage from child %u (stage=%d status=%d)\n",i,msg.stage,msg.status); goto children_failed;
        }
        result->children[i].private_bytes=msg.private_bytes;
        result->children[i].shared_bytes=msg.shared_bytes;
        result->children[i].stack_reserved=msg.stack_reserved;
        result->children[i].stack_pss=msg.stack_pss;
        result->stack_reserved+=msg.stack_reserved; result->stack_pss+=msg.stack_pss;
    }
    if (smaps_totals(getpid(),&parent_pss,&parent_rss)) goto children_failed;
    result->loaded_pss=parent_pss; result->loaded_rss=parent_rss;
    for (unsigned i=0;i<created;++i) {
        if (smaps_totals(pids[i],&result->children[i].loaded_pss,&result->children[i].loaded_rss)) goto children_failed;
        result->loaded_pss+=result->children[i].loaded_pss; result->loaded_rss+=result->children[i].loaded_rss;
    }
    result->fixed_overhead_pss=result->idle_pss>result->baseline_pss ? result->idle_pss-result->baseline_pss : 0;
    for (unsigned i=0;i<created;++i) {
        process_command cmd={PROC_CMD_WORK,mm_now()};
        if (write_full(control[i][1],&cmd,sizeof(cmd))) goto children_failed;
    }
    double wakeups[MM_MAX_PROCESSES];
    for (unsigned i=0;i<created;++i) {
        process_message msg;
        if (read_full(response[i][0],&msg,sizeof(msg)) || msg.stage!=PROC_MSG_DONE) {
            fprintf(stderr,"Process protocol failure: done stage from child %u (stage=%d status=%d)\n",i,msg.stage,msg.status); goto children_failed;
        }
        result->children[i].ok=1;
        result->children[i].wakeup_ms=msg.wakeup_ms;
        result->children[i].work_seconds=msg.work_seconds;
        result->children[i].accesses=msg.accesses;
        result->children[i].checksum=msg.checksum;
        result->children[i].iterations=msg.iterations;
        result->total_accesses+=msg.accesses;
        wakeups[i]=msg.wakeup_ms;
    }
    mm_stats wake_stats=mm_statistics(wakeups,created);
    result->wakeup_min_ms=wake_stats.min; result->wakeup_median_ms=wake_stats.median;
    for (unsigned i=0;i<created;++i) if (wakeups[i]>result->wakeup_max_ms) result->wakeup_max_ms=wakeups[i];
    result->created=created;
    result->elapsed_seconds=mm_now()-begin;
    rc=0;
    goto cleanup;

children_failed:
    fprintf(stderr,"Process benchmark child protocol failed\n");
cleanup_children:
    for (unsigned i=0;i<created;++i) {
        process_command cmd={PROC_CMD_STOP,mm_now()};
        if (control[i][1]>=0) write_full(control[i][1],&cmd,sizeof(cmd));
    }
cleanup:
    for (unsigned i=0;i<processes;++i) {
        for (unsigned side=0;side<2;++side) {
            if (control[i][side]>=0) close(control[i][side]);
            if (response[i][side]>=0) close(response[i][side]);
        }
        if (pids[i]>0) waitpid(pids[i],NULL,0);
    }
    if (shared) munmap(shared,config->shared_size);
    if (rc) mm_process_level_free(result);
    return rc;
}

#else
int mm_run_process_level(const mm_config *config, unsigned processes, mm_process_level *result) {
    (void)config; (void)processes; (void)result;
    fprintf(stderr,"Process benchmark is currently implemented for Linux only\n");
    return -1;
}
void mm_process_level_free(mm_process_level *result) { free(result->children); result->children=NULL; }
#endif
