#include "mm.h"
#include <math.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void) {
    size_t size;
    mm_system_info info;
    mm_config one;
    char *one_click[]={"memorymark"};
    char *score_json[]={"memorymark","--score","--json"};
    char *score_quick[]={"memorymark","--score-quick","--json"};
    CHECK(mm_parse(1,one_click,&one)==0 && one.score==1);
    CHECK(mm_parse(3,score_json,&one)==0 && one.score==1 && one.format==1);
    CHECK(mm_parse(3,score_quick,&one)==0 && one.score==1 && one.quick==1 && one.format==1);
    mm_collect_system_info(&info);
    CHECK(info.cpu_model[0] && info.kernel[0] && info.machine[0] && info.endianness[0]);
    CHECK(info.pointer_bits==(unsigned)(sizeof(void*)*8) && info.page_size>0);
    CHECK(!mm_size("64KiB",&size) && size==65536);
    CHECK(mm_size("-1",&size) && mm_size("0",&size) && mm_size("18446744073709551615G",&size));
    CHECK(mm_size("2MBjunk",&size));
    double values[]={4,1,3,2};
    mm_stats stats=mm_statistics(values,4);
    CHECK(stats.min==1 && stats.median==2.5 && stats.mean==2.5 && fabs(stats.stddev-sqrt(1.25))<1e-10);
    double rates[]={1,4,16};
    CHECK(fabs(mm_score_from_rates(rates,3)-4)<1e-12);
    double weights[]={1,2,1};
    CHECK(fabs(mm_score_weighted(rates,weights,3)-4)<1e-12);
    rates[1]=0; CHECK(mm_score_from_rates(rates,3)==0);
    uint64_t a=0,b=0;
    CHECK(mm_rng(&a)==UINT64_C(0xe220a8397b1dcdaf));
    for (unsigned i=0;i<1000;++i) CHECK(mm_bounded(&b,17)<17);
    for (size_t stride=8;stride<=4096;stride*=2) {
        mm_config c; mm_memory m,n;
        mm_defaults(&c); c.size=4096; c.stride=stride; c.test=LATENCY;
        CHECK(!mm_memory_init(&m,&c,0)); CHECK(mm_ring_valid(&m));
        CHECK(!mm_memory_init(&n,&c,0));
        CHECK(m.sequence_hash==n.sequence_hash);
        for (size_t i=0;i<m.count;++i) CHECK(m.data[i*m.stride_words]==n.data[i*n.stride_words]);
        if (m.count>1) { m.data[0]=0; CHECK(!mm_ring_valid(&m)); }
        mm_memory_destroy(&m); mm_memory_destroy(&n);
    }
    mm_config c; mm_memory m,n;
    mm_defaults(&c); c.size=4096; c.test=RANDOM_READ; c.accesses=1000;
    CHECK(!mm_memory_init(&m,&c,0)); CHECK(!mm_memory_init(&n,&c,0));
    for (size_t i=0;i<m.index_count;++i) CHECK(m.indices[i]==n.indices[i] && m.indices[i]<m.slots);
    mm_random(&m,&c); mm_random(&n,&c); CHECK(m.checksum==n.checksum);
    mm_memory_destroy(&m); mm_memory_destroy(&n);
    double t=mm_now(); CHECK(mm_now()>=t);
    puts("unit tests passed"); return 0;
}
