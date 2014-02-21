#ifndef __PROTOSHARE_MINER_H__
#define __PROTOSHARE_MINER_H__
#include "global.h"


class ProtoshareOpenCL {
public
	ProtoshareOpenCL(int device_num, uint32 step_size);
	void protoshare_process(minerProtosharesBlock_t* block);

private:
	int device_num;
	uint32 max_wgs;
    uint32 sort_wgs;
    uint32 local_mem;

	OpenCLKernel* kernel_hash;
	OpenCLKernel* kernel_sort;
	OpenCLKernel* kernel_seek;
	
	OpenCLBuffer* mid_hash;
    OpenCLBuffer* hashes;
    OpenCLBuffer* nonces;
    OpenCLBuffer* hash_temp;
    OpenCLBuffer* nonce_temp;

    OpenCLBuffer* nonce_a;
    OpenCLBuffer* nonce_b;
    OpenCLBuffer* nonce_qty;

    OpenCLCommandQueue * q;
};

#endif
