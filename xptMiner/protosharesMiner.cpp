#include"global.h"
#include "ticker.h"
#include "protoshareMiner.h"
#include <sstream>

#define MAX_MOMENTUM_NONCE		(1<<26)	// 67.108.864
#define SEARCH_SPACE_BITS		50
#define BIRTHDAYS_PER_HASH		8
#define MEASURE_TIME

bool protoshares_revalidateCollision(minerProtosharesBlock_t* block, uint8* midHash, uint32 indexA, uint32 indexB)
{
	uint8 tempHash[32+4];
	uint64 resultHash[8];
	memcpy(tempHash+4, midHash, 32);
	// get birthday A
	*(uint32*)tempHash = indexA&~7; // indexA & ~7 == indexA - (indexA % BIRTHDAYS_PER_HASH)
	sha512_ctx c512;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayA = resultHash[indexA&7] >> (64ULL-SEARCH_SPACE_BITS);

	// get birthday B
	*(uint32*)tempHash = indexB&~7;
	sha512_init(&c512);
	sha512_update(&c512, tempHash, 32+4);
	sha512_final(&c512, (unsigned char*)resultHash);
	uint64 birthdayB = resultHash[indexB&7] >> (64ULL-SEARCH_SPACE_BITS);
	if( birthdayA != birthdayB )
	{
		return false; // invalid collision
	}

	// birthday collision found
	totalCollisionCount += 2; // we can use every collision twice -> A B and B A
	//printf("Collision found %8d = %8d | num: %d\n", indexA, indexB, totalCollisionCount);
	// get full block hash (for A B)
	block->birthdayA = indexA;
	block->birthdayB = indexB;
	uint8 proofOfWorkHash[32];
	sha256_ctx c256;
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80+8);
	sha256_final(&c256, proofOfWorkHash);
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)proofOfWorkHash, 32);
	sha256_final(&c256, proofOfWorkHash);
	bool hashMeetsTarget = true;
	uint32* generatedHash32 = (uint32*)proofOfWorkHash;
	uint32* targetHash32 = (uint32*)block->targetShare;
	for(sint32 hc=7; hc>=0; hc--)
	{
		if( generatedHash32[hc] < targetHash32[hc] )
		{
			hashMeetsTarget = true;
			break;
		}
		else if( generatedHash32[hc] > targetHash32[hc] )
		{
			hashMeetsTarget = false;
			break;
		}
	}
	if( hashMeetsTarget )
	{
		//printf("[DEBUG] Submit Protoshares share\n");
		totalShareCount++;
		xptMiner_submitShare(block);
	}
	// get full block hash (for B A)
	block->birthdayA = indexB;
	block->birthdayB = indexA;
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80+8);
	sha256_final(&c256, proofOfWorkHash);
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)proofOfWorkHash, 32);
	sha256_final(&c256, proofOfWorkHash);
	hashMeetsTarget = true;
	generatedHash32 = (uint32*)proofOfWorkHash;
	targetHash32 = (uint32*)block->targetShare;
	for(sint32 hc=7; hc>=0; hc--)
	{
		if( generatedHash32[hc] < targetHash32[hc] )
		{
			hashMeetsTarget = true;
			break;
		}
		else if( generatedHash32[hc] > targetHash32[hc] )
		{
			hashMeetsTarget = false;
			break;
		}
	}
	if( hashMeetsTarget )
	{
		// printf("[DEBUG] Submit Protoshares share\n");
		totalShareCount++;
		xptMiner_submitShare(block);
	}
	return true;
}


ProtoshareOpenCL::ProtoshareOpenCL(int _device_num) {
	this->device_num = _device_num;

	printf("Initializing GPU %d\n", device_num);
	OpenCLMain &main = OpenCLMain::getInstance();
	OpenCLDevice* device = main.getDevice(device_num);

	this->max_wgs   = device->getMaxWorkGroupSize();
    this->local_mem = device->getLocalMemSize();
    this->sort_wgs  = 1;

    // Determine maximum bitonic sort work group size (limited by max work group size and local memory)
    while (sort_wgs <= max_wgs && sort_wgs * (sizeof(cl_ulong) + sizeof(cl_uint)) < local_mem) { sort_wgs <<= 1; }
    sort_wgs >>= 1;
    if (sort_wgs > 64) { sort_wgs = 64; }

    printf("======================================================================\n");
	printf("Device information for: %s\n", device->getName().c_str());
    device->dumpDeviceInfo(); // Makes troubleshooting easier
    printf("======================================================================\n");
    printf("\n");
	printf("Compiling OpenCL code... this may take 3-5 minutes\n");
	std::vector<std::string> file_list;
	file_list.push_back("opencl/momentum.cl");

	std::stringstream params;
	params << " -I ./opencl/";
	params << " -D MAX_WGS=" << max_wgs;
	OpenCLProgram* program = device->getContext()->loadProgramFromFiles(file_list, params.str());

	kernel_hash = program->getKernel("hash_nonce");
	kernel_sort = program->getKernel("bitonicSort");
	kernel_seek = program->getKernel("seek_hits");


	hashes = device->getContext()->createBuffer(MAX_MOMENTUM_NONCE * sizeof(cl_ulong), CL_MEM_READ_WRITE, NULL);
	nonces = device->getContext()->createBuffer(MAX_MOMENTUM_NONCE * sizeof(cl_uint) , CL_MEM_READ_WRITE, NULL);

	hash_temp  = device->getContext()->createBuffer(sort_wgs * sizeof(cl_ulong), CL_MEM_READ_WRITE, NULL);
    nonce_temp = device->getContext()->createBuffer(sort_wgs * sizeof(cl_uint) , CL_MEM_READ_WRITE, NULL);
    
    nonce_a = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
    nonce_b = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

	mid_hash = device->getContext()->createBuffer(32 * sizeof(cl_uint), CL_MEM_READ_ONLY, NULL);

	q = device->getContext()->createCommandQueue(device);
}


void ProtoshareOpenCL::protoshare_process(minerProtosharesBlock_t* block)
{

	block->nonce = 0;
	uint32 target = *(uint32*)(block->targetShare+28);
	OpenCLDevice* device = OpenCLMain::getInstance().getDevice(device_num);

	uint8 midHash[32];
    // midHash = sha256(sha256(block))
	sha256_ctx c256;
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80);
	sha256_final(&c256, midHash);

	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)midHash, 32);
	sha256_final(&c256, midHash);

#ifdef MEASURE_TIME
	printf("Starting...\n");
	uint32 begin = getTimeMilliseconds();
    uint32 end_hashing, end_sorting, end_seeking;
#endif

    // Calculate all hashes
    kernel_hash->resetArgs();
    kernel_hash->addGlobalArg(mid_hash);
    kernel_hash->addGlobalArg(hashes);
    kernel_hash->addGlobalArg(nonces);

	q->enqueueWriteBuffer(mid_hash, midHash, 32*sizeof(cl_uint));

	q->enqueueKernel1D(kernel_hash, MAX_MOMENTUM_NONCE, max_wgs);

#ifdef MEASURE_TIME
	q->finish();
	printf("Survived hashing...\n");
	end_hashing = getTimeMilliseconds();
#endif

    // Sort all hashes
    kernel_sort->resetArgs();
    kernel_sort->addGlobalArg(hashes);
    kernel_sort->addGlobalArg(nonces);
    kernel_sort->addGlobalArg(hash_temp);
    kernel_sort->addGlobalArg(nonce_temp);

    q->enqueueKernel1D(kernel_sort, MAX_MOMENTUM_NONCE, sort_wgs);

#ifdef MEASURE_TIME
	q->finish();
	printf("Survived sorting...\n");
	end_sorting = getTimeMilliseconds();
#endif

	// Extract the results
    uint32 result_qty = 0;
    uint32 result_a[256];
    uint32 result_b[256];

	kernel_seek->resetArgs();
    kernel_seek->addGlobalArg(hashes);
    kernel_seek->addGlobalArg(nonces);
    kernel_seek->addGlobalArg(nonce_a);
    kernel_seek->addGlobalArg(nonce_b);
    kernel_seek->addGlobalArg(nonce_qty);

	q->enqueueWriteBuffer(nonce_qty, &result_qty, sizeof(cl_uint));

	q->enqueueKernel1D(kernel_seek, MAX_MOMENTUM_NONCE, max_wgs);

	q->enqueueReadBuffer(nonce_a,   result_a,    sizeof(cl_uint) * 256);
    q->enqueueReadBuffer(nonce_b,   result_b,    sizeof(cl_uint) * 256);
	q->enqueueReadBuffer(nonce_qty, &result_qty, sizeof(cl_uint));
	q->finish();

	for (int i = 0; i < result_qty; i++) {
		protoshares_revalidateCollision(block, midHash, result_a[i], result_a[i]);
	}

#ifdef MEASURE_TIME
	uint32 end = getTimeMilliseconds();
    uint32 total_time = (end - begin);
    uint32 hash_time = (end_hashing - begin);
    uint32 sort_time = (end_sorting - end_hashing);

	printf("Elapsed time: %d ms (Hash = %d, Sort = %d, Seek = %d)\n", (end-begin), (end_hashing-begin), (end_sorting-end_hashing), (end-end_sorting));
#endif

}