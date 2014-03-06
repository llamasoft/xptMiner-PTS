#ifndef __PROTOSHARE_MINER_H__
#define __PROTOSHARE_MINER_H__
#include "global.h"


class ProtoshareOpenCL {
public:
	ProtoshareOpenCL(int device_num);
	void protoshare_process(minerProtosharesBlock_t* block);

private:
	int device_num;
	uint32 wgs;
	uint32 buckets_log2;
    uint32 bucket_size;

	OpenCLKernel* kernel_hash;
	OpenCLKernel* kernel_reset;
	
	OpenCLBuffer* mid_hash;
    OpenCLBuffer* hash_list;
	OpenCLBuffer* index_list;

    OpenCLBuffer* nonce_a;
    OpenCLBuffer* nonce_b;
    OpenCLBuffer* nonce_qty;
	OpenCLBuffer* overflow_ct;

    OpenCLCommandQueue * q;
};

#endif
