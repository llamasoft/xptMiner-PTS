#include"global.h"
#include "ticker.h"
#include "protoshareMiner.h"
#include <sstream>
#include <cmath>
#include <cstdlib>

#define MAX_MOMENTUM_NONCE		(1<<26)	// 67.108.864
#define SEARCH_SPACE_BITS		50
#define BIRTHDAYS_PER_HASH		8
// #define MEASURE_TIME
// #define VERIFY_RESULTS

uint32 totalHashTime   = 0;
uint32 totalInsertTime = 0;
uint32 totalResetTime  = 0;

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
    uint32 trials = (items / buckets) * 2;

    for (uint32_t i = bucket_size + 1; i < bucket_size + trials; i++) {
        f *= i;

        total_drops += (buckets * (i - bucket_size) * pow(items / buckets, (double)i))
            / (exp(items / buckets) * f);
    }

    return (total_drops / items);
}


size_t calc_hash_mem_usage() { return sizeof(cl_ulong) * MAX_MOMENTUM_NONCE; }
size_t calc_map_mem_usage(uint32 buckets_log2, uint32 bucket_size) { return sizeof(cl_uint) * (1 << buckets_log2) * bucket_size; }

size_t calc_total_mem_usage(uint32 buckets_log2, uint32 bucket_size) {
    return calc_hash_mem_usage() + calc_map_mem_usage(buckets_log2, bucket_size);
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
        curShareCount++;
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
        curShareCount++;
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
    this->nonce_bits = commandlineInput.nonce_bits;

    printf("Using %d work group size\n", wgs);
    printf("Using 2^%d buckets\n", buckets_log2);

    // If bucket size unset and target memory unset, use maximum usable memory
    if (target_mem == 0 && bucket_size == 0) {
        target_mem = device->getGlobalMemSize() / 1024 / 1024;
    }

    // If set, convert target memory into a usable value for bucket_size
    if (target_mem > 0) {
        // Convert target to bytes, subtract 1 to guarantee results LESS THAN target
        uint32 target_mem_temp = (target_mem * 1024 * 1024);

        // Lazy calculation, assume large bucket_size, scale back from there
        bucket_size = 1024;
        while (bucket_size > 0 && calc_total_mem_usage(buckets_log2, bucket_size) > target_mem_temp) { bucket_size--; }

        // Make sure the parameter configuration is sane:
        if (bucket_size < 1) {
            printf("ERROR: Memory target of %d MB cannot be attained with 2^%d buckets!\n", target_mem, buckets_log2);
            printf("       Please consider lowering the value of \"-b\".\n");
            exit(0);
        }
    }

    std::string limit_reason = "";

    // Make sure we can allocate hash_list (cannot violate CL_DEVICE_MAX_MEM_ALLOC_SIZE)
    if (std::max(calc_hash_mem_usage(), calc_map_mem_usage(buckets_log2, bucket_size)) > device->getMaxMemAllocSize()) {
        while (bucket_size > 0 && std::max(calc_hash_mem_usage(), calc_map_mem_usage(buckets_log2, bucket_size)) > device->getMaxMemAllocSize()) { bucket_size--; }
        if (bucket_size < 1) {
            printf("ERROR: Device %d cannot allocate hash list using 2^%d buckets!\n", device_num, buckets_log2);
            printf("       Please lower the value of \"-b\" or increase \"-m\".\n");
            exit(0);
        }

        limit_reason = "(limited by CL_DEVICE_MAX_MEM_ALLOC_SIZE)";
    }

    printf("Using %d elements per bucket %s\n", bucket_size, limit_reason.c_str());
    printf("Using %d bits per nonce\n", nonce_bits);


    // Make sure the whole thing fits in memory
    // Because bucket_size is limited by CL_DEVICE_MAX_MEM_ALLOC_SIZE, I don't think
    //   it's possible to actually reach this.
    uint32 required_mem = calc_total_mem_usage(buckets_log2, bucket_size);
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

    bool isAMD = (device->getVendor().find("Advanced Micro Devices") != std::string::npos);
    bool isGPU = device->isGPU();

    std::stringstream params;
    params << " -I ./opencl/";
    params << " -D DEVICE_AMD_GPU=" << (isGPU && isAMD ? 1 : 0);
    params << " -D NONCE_BITS=" << nonce_bits;
    params << " -D NUM_BUCKETS_LOG2=" << buckets_log2;
    params << " -D BUCKET_SIZE=" << bucket_size;
    OpenCLProgram* program = device->getContext()->loadProgramFromFiles(file_list, params.str());

    kernel_hash   = program->getKernel("hash_step");
    kernel_insert = program->getKernel("insert_step");
    kernel_reset  = program->getKernel("reset_step");

    mid_hash = device->getContext()->createBuffer(32 * sizeof(cl_uint), CL_MEM_READ_ONLY, NULL);

    hash_list = device->getContext()->createBuffer(calc_hash_mem_usage(), CL_MEM_READ_WRITE, NULL);
    nonce_map = device->getContext()->createBuffer(calc_map_mem_usage(buckets_log2, bucket_size), CL_MEM_READ_WRITE, NULL);

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


    // Calculate all hashes
    kernel_hash->resetArgs();

    kernel_hash->addGlobalArg(mid_hash);
    kernel_hash->addGlobalArg(hash_list);

    q->enqueueWriteBuffer(mid_hash, hash_state.b32, 32 * sizeof(cl_uint));

    q->enqueueKernel1D(kernel_hash, MAX_MOMENTUM_NONCE / BIRTHDAYS_PER_HASH, wgs);

#ifdef MEASURE_TIME
    q->finish();
    // printf("Inserting...\n");
    uint32 hash_end = getTimeMilliseconds();
#endif

    // Reset index list and find collisions
    cl_uint result_qty = 0;
    cl_uint result_a[256];
    cl_uint result_b[256];

    kernel_insert->resetArgs();
    kernel_insert->addGlobalArg(hash_list);
    kernel_insert->addGlobalArg(nonce_map);
    kernel_insert->addGlobalArg(nonce_a);
    kernel_insert->addGlobalArg(nonce_b);
    kernel_insert->addGlobalArg(nonce_qty);

    q->enqueueWriteBuffer(nonce_qty, &result_qty, sizeof(cl_uint));

    q->enqueueKernel1D(kernel_insert, MAX_MOMENTUM_NONCE, wgs);

    q->enqueueReadBuffer(nonce_a,   result_a,    sizeof(cl_uint) * 256);
    q->enqueueReadBuffer(nonce_b,   result_b,    sizeof(cl_uint) * 256);
    q->enqueueReadBuffer(nonce_qty, &result_qty, sizeof(cl_uint));
    q->finish();

    for (int i = 0; i < result_qty; i++) {
        protoshares_revalidateCollision(block, (uint8 *)midHash, result_a[i], result_b[i]);
    }

#ifdef MEASURE_TIME
    q->finish();
    //printf("Resetting...\n");
    uint32 insert_end = getTimeMilliseconds();
#endif
    
    kernel_reset->resetArgs();
    kernel_reset->addGlobalArg(nonce_map);
    q->enqueueKernel1D(kernel_reset, (1 << buckets_log2) * bucket_size / 16, wgs);
    q->finish();

#ifdef MEASURE_TIME
    uint32 end = getTimeMilliseconds();
    totalHashTime += (hash_end-begin);
    totalInsertTime += (insert_end-hash_end);
    totalResetTime += (end-insert_end);
    printf("Found %2d collisions in %4d ms (Hash: %4d ms, Insert: %4d ms, Reset: %4d ms)\n", result_qty * 2, (end-begin), (hash_end-begin), (insert_end-hash_end), (end-insert_end));

    if (totalTableCount > 0 && totalTableCount % 10 == 0) {
        printf("AVG TIMES - Total: %4d, Hash: %4d, Insert: %4d, Reset: %4d\n",
            (totalHashTime + totalInsertTime + totalResetTime) / totalTableCount,
            totalHashTime / totalTableCount,
            totalInsertTime / totalTableCount,
            totalResetTime / totalTableCount);
    }
#endif

    totalTableCount++;
}