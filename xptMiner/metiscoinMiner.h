#ifndef __METISCOIN_MINER_H__
#define __METISCOIN_MINER_H__
#include "global.h"


class MetiscoinOpenCL {
public:

	MetiscoinOpenCL(int device_num, uint32 step_size);
	void metiscoin_process(minerMetiscoinBlock_t* block);
private:

	int device_num;
    uint32 step_size;
	uint32 max_wgs;

	OpenCLKernel* kernel_all;
	OpenCLKernel* kernel_keccak_noinit;
	OpenCLKernel* kernel_shavite;
	OpenCLKernel* kernel_metis;
	#ifdef VALIDATE_ALGORITHMS
	OpenCLKernel* kernel_validate;
	#endif
	OpenCLBuffer* u;
	OpenCLBuffer* buff;
	OpenCLBuffer* hashes;
	OpenCLBuffer* out;
	OpenCLBuffer* out_count;

    OpenCLBuffer* metis_mixtab0;
	OpenCLBuffer* metis_mixtab1;
    OpenCLBuffer* metis_mixtab2;
    OpenCLBuffer* metis_mixtab3;

    OpenCLBuffer* shavite_AES0;
    OpenCLBuffer* shavite_AES1;
    OpenCLBuffer* shavite_AES2;
    OpenCLBuffer* shavite_AES3;

    OpenCLBuffer* sbox;

    OpenCLCommandQueue * q;
	uint32_t out_tmp[255];
};

#endif
