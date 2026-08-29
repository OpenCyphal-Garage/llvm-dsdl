#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dsdl_runtime.h"
int8_t ir_dsdl_runtime_set_f16(uint8_t*, size_t, size_t, float);
int8_t ir_dsdl_runtime_set_f32(uint8_t*, size_t, size_t, float);
int8_t ir_dsdl_runtime_set_f64(uint8_t*, size_t, size_t, double);
float  ir_dsdl_runtime_get_f16(const uint8_t*, size_t, size_t);
float  ir_dsdl_runtime_get_f32(const uint8_t*, size_t, size_t);
double ir_dsdl_runtime_get_f64(const uint8_t*, size_t, size_t);

static unsigned long long s = 0x243F6A8885A308D3ull;
static unsigned rnd(unsigned n){ s^=s<<13; s^=s>>7; s^=s<<17; return (unsigned)(s % n); }
static float  bitsf(uint32_t u){ float f; memcpy(&f,&u,4); return f; }
static double bitsd(uint64_t u){ double d; memcpy(&d,&u,8); return d; }

int main(void)
{
    int failures = 0;
    const float specials[] = {0.0f,-0.0f,1.0f,-1.0f,65504.0f,-65504.0f,65520.0f,131008.0f,
                              6.1e-5f,5.96e-8f,1e-8f,INFINITY,-INFINITY,NAN,-NAN,3.14159f,-2.5e30f};
    for (int t = 0; t < 60000; ++t) {
        uint8_t a[24], b[24], buf[24];
        for (size_t i=0;i<sizeof buf;++i){ buf[i]=(uint8_t)rnd(256); a[i]=b[i]=(uint8_t)rnd(256); }
        const size_t size = 1 + rnd(sizeof buf);
        const size_t off  = rnd(size*8 + 8);
        float f = (t < (int)(sizeof specials/sizeof specials[0])) ? specials[t]
                                                                 : bitsf(((uint32_t)rnd(0xFFFFu)<<16)|rnd(0xFFFFu));
        double d = bitsd(((uint64_t)rnd(0xFFFFFFFFu)<<32)|rnd(0xFFFFFFFFu));

        int8_t rc, ri;
        rc = dsdl_runtime_set_f16(a,size,off,f); ri = ir_dsdl_runtime_set_f16(b,size,off,f);
        if (rc!=ri || memcmp(a,b,sizeof a)) { if(++failures<=3) printf("set_f16 differs: size=%zu off=%zu f=%a\n",size,off,(double)f); }
        memcpy(b,a,sizeof a);
        rc = dsdl_runtime_set_f32(a,size,off,f); ri = ir_dsdl_runtime_set_f32(b,size,off,f);
        if (rc!=ri || memcmp(a,b,sizeof a)) { if(++failures<=3) printf("set_f32 differs: size=%zu off=%zu f=%a\n",size,off,(double)f); }
        memcpy(b,a,sizeof a);
        rc = dsdl_runtime_set_f64(a,size,off,d); ri = ir_dsdl_runtime_set_f64(b,size,off,d);
        if (rc!=ri || memcmp(a,b,sizeof a)) { if(++failures<=3) printf("set_f64 differs: size=%zu off=%zu d=%a\n",size,off,d); }

        const float  c16 = dsdl_runtime_get_f16(buf,size,off), i16v = ir_dsdl_runtime_get_f16(buf,size,off);
        const float  c32 = dsdl_runtime_get_f32(buf,size,off), i32v = ir_dsdl_runtime_get_f32(buf,size,off);
        const double c64 = dsdl_runtime_get_f64(buf,size,off), i64v = ir_dsdl_runtime_get_f64(buf,size,off);
        if (memcmp(&c16,&i16v,4)) { if(++failures<=3) printf("get_f16 differs: size=%zu off=%zu C=%a IR=%a\n",size,off,(double)c16,(double)i16v); }
        if (memcmp(&c32,&i32v,4)) { if(++failures<=3) printf("get_f32 differs: size=%zu off=%zu C=%a IR=%a\n",size,off,(double)c32,(double)i32v); }
        if (memcmp(&c64,&i64v,8)) { if(++failures<=3) printf("get_f64 differs: size=%zu off=%zu C=%a IR=%a\n",size,off,c64,i64v); }
    }
    if (failures) { printf("%d differences\n", failures); return 1; }
    printf("float primitives: 60000 trials x 6 primitives agree\n");
    return 0;
}
