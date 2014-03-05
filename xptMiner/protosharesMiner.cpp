#include"global.h"
#include "ticker.h"
#include "protoshareMiner.h"
#include <sstream>

#define MAX_MOMENTUM_NONCE		(1<<26)	// 67.108.864
#define SEARCH_SPACE_BITS		50
#define BIRTHDAYS_PER_HASH		8
//#define LOCAL_ALGO
//#define MEASURE_TIME
//#define VERIFY_RESULTS

extern commandlineInput_t commandlineInput;

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

#ifdef VERIFY_RESULTS
	printf("DEBUG:\n");
	printf(" Nonce A = %#010x; Hash A = %#018llx\n", indexA, birthdayA);
	printf(" Nonce B = %#010x; Hash B = %#018llx\n", indexB, birthdayB);
	printf("\n");
#endif

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

	if (commandlineInput.wgs == 0) {
		this->wgs = device->getMaxWorkGroupSize();
	} else {
		this->wgs = commandlineInput.wgs;
	}

	this->buckets_log2 = commandlineInput.buckets_log2;


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
	params << " -D NUM_BUCKETS_LOG2=" << buckets_log2;
	params << " -D LOCAL_WGS=" << wgs;
	OpenCLProgram* program = device->getContext()->loadProgramFromFiles(file_list, params.str());

	kernel_hash  = program->getKernel("hash_nonce");
	kernel_reset = program->getKernel("reset_indexes");

	mid_hash = device->getContext()->createBuffer(32 * sizeof(cl_uint), CL_MEM_READ_ONLY, NULL);

	hash_list  = device->getContext()->createBuffer(MAX_MOMENTUM_NONCE * sizeof(cl_ulong), CL_MEM_READ_WRITE, NULL);
#ifndef LOCAL_ALGO
	index_list = device->getContext()->createBuffer((1 << buckets_log2) * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
#endif

    nonce_a = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
    nonce_b = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
	nonce_qty = device->getContext()->createBuffer(sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

	overflow_qty = device->getContext()->createBuffer(sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

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
	printf("Hashing...\n");
	uint32 begin = getTimeMilliseconds();
#endif

	cl_uint result_qty = 0;
	cl_uint result_a[256];
	cl_uint result_b[256];
	cl_uint total_work = (MAX_MOMENTUM_NONCE / BIRTHDAYS_PER_HASH);

	// Calculate all hashes
	kernel_hash->resetArgs();

	kernel_hash->addGlobalArg(mid_hash);
	kernel_hash->addGlobalArg(hash_list);
#ifndef LOCAL_ALGO
	kernel_hash->addGlobalArg(index_list);
#else
	kernel_hash->addLocalArg(BIRTHDAYS_PER_HASH * wgs * sizeof(cl_ulong));
	kernel_hash->addLocalArg(BIRTHDAYS_PER_HASH * wgs * sizeof(cl_uint));
#endif
	kernel_hash->addGlobalArg(nonce_a);
	kernel_hash->addGlobalArg(nonce_b);
	kernel_hash->addGlobalArg(nonce_qty);

	q->enqueueWriteBuffer(mid_hash, midHash, 32 * sizeof(cl_uint));
	q->enqueueWriteBuffer(nonce_qty, &result_qty, sizeof(cl_uint));

	q->enqueueKernel1D(kernel_hash, total_work, wgs);

	q->enqueueReadBuffer(nonce_a,   result_a,    sizeof(cl_uint) * 256);
	q->enqueueReadBuffer(nonce_b,   result_b,    sizeof(cl_uint) * 256);
	q->enqueueReadBuffer(nonce_qty, &result_qty, sizeof(cl_uint));

	q->finish();

#ifdef MEASURE_TIME
	uint32 hash_end = getTimeMilliseconds();
	printf("Hashing complete...\n");
#endif

	for (int i = 0; i < result_qty; i++) {
		protoshares_revalidateCollision(block, midHash, result_a[i], result_b[i]);
	}


	// Reset index list, get overflow count
	cl_uint overflow_res = 0;
	kernel_reset->resetArgs();
#ifndef LOCAL_ALGO
    kernel_reset->addGlobalArg(index_list);
#else
	kernel_reset->addGlobalArg(hash_list);
#endif
    kernel_reset->addGlobalArg(overflow_qty);

	q->enqueueWriteBuffer(overflow_qty, &overflow_res, sizeof(cl_uint));

	q->enqueueKernel1D(kernel_reset, (1 << buckets_log2), wgs);

	q->enqueueReadBuffer(overflow_qty, &overflow_res, sizeof(cl_uint));
	q->finish();

#ifdef MEASURE_TIME
	uint32 end = getTimeMilliseconds();
	printf("Found %d hits, dropped %d items (%.2f%%)\n", result_qty, overflow_res, 100.00 * (double)overflow_res / (double)MAX_MOMENTUM_NONCE);
	printf("Elapsed time: %d ms\n", (end-begin));
#endif

	totalTableCount++;
	totalOverflowPct += (double)overflow_res / (double)MAX_MOMENTUM_NONCE;
}