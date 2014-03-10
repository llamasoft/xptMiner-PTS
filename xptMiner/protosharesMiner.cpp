#include"global.h"
#include "ticker.h"
#include "protoshareMiner.h"
#include <sstream>
#include <cmath>
#include <cstdlib>

#define MAX_MOMENTUM_NONCE		(1<<26)	// 67.108.864
#define SEARCH_SPACE_BITS		50
#define BIRTHDAYS_PER_HASH		8
#define BUCKET_THRESHOLD        20
// #define MEASURE_TIME
// #define VERIFY_RESULTS

#define SWAP64(n)               \
   (  ((n)               << 56) \
   | (((n) & 0xff00)     << 40) \
   | (((n) & 0xff0000)   << 24) \
   | (((n) & 0xff000000) <<  8) \
   | (((n) >>  8) & 0xff000000) \
   | (((n) >> 24) &   0xff0000) \
   | (((n) >> 40) &     0xff00) \
   |  ((n) >> 56)             )

extern commandlineInput_t commandlineInput;

double factorial(uint32_t n) {
    if (n == 0) { return 1; }
    double rtn = n;
    while (--n > 0) { rtn *= n; }

    return rtn;
}

double poisson_estimate(double buckets, double items, double bucket_size) {
    double total_drops = 0;
    double f = factorial(bucket_size);

    for (uint32_t i = bucket_size + 1; i < bucket_size + 10; i++) {
        f *= i;

        total_drops += (buckets * (i - bucket_size) * pow(items / buckets, (double)i))
                     / (exp(items / buckets) * f);
    }

    return (total_drops / items);
}


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


    printf("======================================================================\n");
	printf("Device information for: %s\n", device->getName().c_str());
    device->dumpDeviceInfo(); // Makes troubleshooting easier
    printf("======================================================================\n");
    printf("\n");

	// Sanitize input parameters
	if (commandlineInput.wgs == 0) {
		this->wgs = device->getMaxWorkGroupSize();
	} else {
		this->wgs = commandlineInput.wgs;
	}

	this->buckets_log2 = commandlineInput.buckets_log2;
    this->bucket_size = commandlineInput.bucket_size;
	this->target_mem = commandlineInput.target_mem;
    this->force_local = commandlineInput.force_local;

	printf("Using %d work group size\n", wgs);
	printf("Using 2^%d buckets\n", buckets_log2);

	// If bucket size unset and target memory unset, use maximum usable memory
	if (target_mem == 0 && bucket_size == 0) {
		target_mem = device->getGlobalMemSize() / 1024 / 1024;
	}

    // If set, convert target memory into a usable value for bucket_size
    if (target_mem > 0) {
        target_mem = target_mem * 1024 * 1024; // Convert to bytes
        target_mem = target_mem - 1; // Guarantee results are LESS THAN the specified amount

        // Determine the maximum usable bucket_size by solving the following for bucket_size:
        //   MEM = (sizeof(cl_ulong) * (1 << buckets_log2) * bucket_size)
        //       + (sizeof(cl_uint)  * (1 << buckets_log2))
        // We use sizeof(cl_ulong) = 8, sizeof(cl_uint) = 4.  This allows us to factor:
        //   MEM = (4 * (1 << buckets_log2)) * (2 * bucket_size + 1)
        bucket_size = ((target_mem / (4 * (1 << buckets_log2))) - 1) / 2;

        // Make sure the parameter configuration is sane:
        if (bucket_size < 1) {
            target_mem = (target_mem + 1) / 1024 / 1024; // Undo our butchering
            printf("ERROR: Memory target of %d MB cannot be attained with 2^%d buckets!\n", target_mem, buckets_log2);
            printf("       Please consider lowering the value of \"-b\".\n");
            exit(0);
        }
    }

    std::string limit_reason = "";

	// Make sure we can allocate hash_list (cannot violate CL_DEVICE_MAX_MEM_ALLOC_SIZE)
	if (sizeof(cl_ulong) * (1 << buckets_log2) * bucket_size > device->getMaxMemAllocSize()) {
		while (bucket_size > 0 && sizeof(cl_ulong) * (1 << buckets_log2) * bucket_size > device->getMaxMemAllocSize()) { bucket_size--; }
		if (bucket_size == 0) {
			printf("ERROR: Device %d cannot allocate hash list using 2^%d buckets!\n", device_num, buckets_log2);
			printf("       Please lower the value of \"-b\" or increase \"-m\".\n");
			exit(0);
		}

        limit_reason = "(limited by CL_DEVICE_MAX_MEM_ALLOC_SIZE)";
	}

    // Make sure we have enough local memory for sort/seek
    if (sizeof(cl_ulong) * wgs * bucket_size > device->getLocalMemSize()) {
        while (bucket_size > 0 && sizeof(cl_ulong) * wgs * bucket_size > device->getLocalMemSize()) { bucket_size--; }
		if (bucket_size == 0) {
			printf("ERROR: Device %d cannot allocate hash list using 2^%d buckets!\n", device_num, buckets_log2);
			printf("       Please lower the value of \"-b\" or increase \"-m\".\n");
			exit(0);
		}

        limit_reason = "(limited by CL_DEVICE_LOCAL_MEM_SIZE, consider increasing \"-b\" or lowering \"-w\")";
    }

    printf("Using %d elements per bucket %s\n", bucket_size, limit_reason.c_str());

	if (bucket_size < 2) {
		printf("ERROR: You must allocate at least 2 elements per bucket or you will find no collisions.\n");
		printf("       Consider lowering the value of \"-b\" or increasing \"-m\".\n");
		exit(0);
	}

	// Make sure the whole thing fits in memory
	// Because bucket_size is limited by CL_DEVICE_MAX_MEM_ALLOC_SIZE, I don't think
	//   it's possible to actually reach this.
	uint32 required_mem = sizeof(cl_ulong) * (1 << buckets_log2) * bucket_size;
	required_mem += sizeof(cl_uint) * (1 << buckets_log2);
	if (required_mem > device->getGlobalMemSize()) {
		printf("ERROR: Device %d cannot store 2^%d buckets of %d elements!\n", device_num, buckets_log2, bucket_size);
		printf("       You require %d MB of memory but only have %d MB available.\n",
			required_mem / 1024 / 1024,
			device->getGlobalMemSize() / 1024 / 1024);
		printf("       Consider setting a target memory usage with \"-m\".\n");
		exit(0);
	}
	printf("Using %d MB of memory\n", required_mem / 1024 / 1024);
	printf("Estimated drop percentage: %5.2f%%\n", 100 * poisson_estimate((1 << buckets_log2), MAX_MOMENTUM_NONCE, bucket_size));
	printf("\n");


	// Compile the OpenCL code
	printf("Compiling OpenCL code... this may take 3-5 minutes\n");
	std::vector<std::string> file_list;
	file_list.push_back("opencl/momentum.cl");

	std::stringstream params;
	params << " -I ./opencl/";
	params << " -D NUM_BUCKETS_LOG2=" << buckets_log2;
    params << " -D BUCKET_SIZE=" << bucket_size;
    params << " -D BUCKET_THRESHOLD=" << BUCKET_THRESHOLD;
    params << " -D FORCE_LOCAL=" << (force_local ? 1 : 0);
	params << " -D LOCAL_WGS=" << wgs;
	OpenCLProgram* program = device->getContext()->loadProgramFromFiles(file_list, params.str());

	kernel_hash  = program->getKernel("hash_nonce");
	kernel_reset = program->getKernel("reset_and_seek");

	mid_hash = device->getContext()->createBuffer(32 * sizeof(cl_uint), CL_MEM_READ_ONLY, NULL);

	hash_list  = device->getContext()->createBuffer((1 << buckets_log2) * bucket_size * sizeof(cl_ulong), CL_MEM_READ_WRITE, NULL);
	index_list = device->getContext()->createBuffer((1 << buckets_log2) * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

    nonce_a = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
    nonce_b = device->getContext()->createBuffer(256 * sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);
	nonce_qty = device->getContext()->createBuffer(sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

	q = device->getContext()->createCommandQueue(device);
}


void ProtoshareOpenCL::protoshare_process(minerProtosharesBlock_t* block)
{

	block->nonce = 0;
	uint32 target = *(uint32*)(block->targetShare+28);
	OpenCLDevice* device = OpenCLMain::getInstance().getDevice(device_num);

	uint32 midHash[8];
    // midHash = sha256(sha256(block))
	sha256_ctx c256;
	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)block, 80);
	sha256_final(&c256, (unsigned char*)midHash);

	sha256_init(&c256);
	sha256_update(&c256, (unsigned char*)midHash, 32);
	sha256_final(&c256, (unsigned char*)midHash);

    union { uint64 b64[16]; uint32 b32[32]; } hash_state;
    hash_state.b32[0] = 0; // Reserved for nonce
    hash_state.b32[1] = midHash[0];
    hash_state.b32[2] = midHash[1];
    hash_state.b32[3] = midHash[2];
    hash_state.b32[4] = midHash[3];
    hash_state.b32[5] = midHash[4];
    hash_state.b32[6] = midHash[5];
    hash_state.b32[7] = midHash[6];
    hash_state.b32[8] = midHash[7];
    hash_state.b32[9] = 0x80; // High 1 bit to mark end of input

    // Swap the non-zero b64's except the first (so we can mix in the nonce later)
    for (uint8 i = 1; i < 5; ++i) { hash_state.b64[i] = SWAP64(hash_state.b64[i]); }


#ifdef MEASURE_TIME
	// printf("Hashing...\n");
	uint32 begin = getTimeMilliseconds();
#endif


	cl_uint total_work = (MAX_MOMENTUM_NONCE / BIRTHDAYS_PER_HASH);

	// Calculate all hashes
	kernel_hash->resetArgs();

	kernel_hash->addGlobalArg(mid_hash);
	kernel_hash->addGlobalArg(hash_list);
	kernel_hash->addGlobalArg(index_list);


    q->enqueueWriteBuffer(mid_hash, hash_state.b32, 32 * sizeof(cl_uint));


	q->enqueueKernel1D(kernel_hash, total_work, wgs);

	q->finish();

#ifdef MEASURE_TIME
	uint32 hash_end = getTimeMilliseconds();
	// printf("Resetting...\n");
#endif

	// Reset index list and find collisions
    cl_uint result_qty = 0;
	cl_uint result_a[256];
	cl_uint result_b[256];

	kernel_reset->resetArgs();
    kernel_reset->addGlobalArg(hash_list);
    kernel_reset->addGlobalArg(index_list);
    if (force_local || bucket_size > BUCKET_THRESHOLD) {
        kernel_reset->addLocalArg(sizeof(cl_ulong) * wgs * bucket_size);
    }
	kernel_reset->addGlobalArg(nonce_a);
	kernel_reset->addGlobalArg(nonce_b);
	kernel_reset->addGlobalArg(nonce_qty);

    q->enqueueWriteBuffer(nonce_qty, &result_qty, sizeof(cl_uint));

	q->enqueueKernel1D(kernel_reset, (1 << buckets_log2), wgs);

	q->enqueueReadBuffer(nonce_a,   result_a,    sizeof(cl_uint) * 256);
	q->enqueueReadBuffer(nonce_b,   result_b,    sizeof(cl_uint) * 256);
	q->enqueueReadBuffer(nonce_qty, &result_qty, sizeof(cl_uint));
	q->finish();

	for (int i = 0; i < result_qty; i++) {
		protoshares_revalidateCollision(block, (uint8 *)midHash, result_a[i], result_b[i]);
	}

#ifdef MEASURE_TIME
	uint32 end = getTimeMilliseconds();
	printf("Found %d collisions\n", result_qty * 2, 100.00 * (double)overflow_res / (double)MAX_MOMENTUM_NONCE);
	printf("Elapsed time: %d ms (Hash: %d ms, Reset: %d ms)\n", (end-begin), (hash_end-begin), (end-hash_end));
#endif

	totalTableCount++;
}