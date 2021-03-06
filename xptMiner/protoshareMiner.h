#ifndef __PROTOSHARE_MINER_H__
#define __PROTOSHARE_MINER_H__
#include "global.h"


class ProtoshareOpenCL {
public:
    ProtoshareOpenCL(int device_num);
    void protoshare_process(minerProtosharesBlock_t* block);
    bool get_optimal_settings(uint32 *buckets_log2, uint32 *bucket_size, uint32 *mem_target);

private:
    int device_num;
    uint32 wgs;
    uint32 buckets_log2;
    uint32 bucket_size;
    uint32 target_mem;
    uint32 nonce_bits;
    uint32 vect_type;

    OpenCLDevice* device;

    OpenCLKernel* kernel_hash;
    OpenCLKernel* kernel_insert;
    OpenCLKernel* kernel_reset;

    OpenCLBuffer* mid_hash;
    OpenCLBuffer* hash_list;
    OpenCLBuffer* nonce_map;
    OpenCLBuffer* nonce_a;
    OpenCLBuffer* nonce_b;
    OpenCLBuffer* nonce_qty;

    OpenCLCommandQueue * q;
};

#endif
