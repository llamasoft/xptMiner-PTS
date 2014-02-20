#include "global.h"
#include "ticker.h"
#include "metiscoinMiner.h"
#include <sstream>

// For copying lookup tables to OpenCL
#include "sph_metis.h"
#include "aes_helper.h"

#define MAX_NONCE 0x8000000
// #define MEASURE_TIME 1


MetiscoinOpenCL::MetiscoinOpenCL(int _device_num, uint32 _step_size) {
	this->device_num = _device_num;
    this->step_size  = (_step_size < MAX_NONCE ? _step_size : MAX_NONCE);

	printf("Initializing GPU %d\n", device_num);
	OpenCLMain &main = OpenCLMain::getInstance();
	OpenCLDevice* device = main.getDevice(device_num);
	this->max_wgs = device->getMaxWorkGroupSize();

    printf("======================================================================\n");
	printf("Device information for: %s\n", device->getName().c_str());
    device->dumpDeviceInfo(); // Makes troubleshooting easier
    printf("======================================================================\n");
    printf("\n");
	printf("Compiling OpenCL code... this may take 3-5 minutes\n");
	std::vector<std::string> file_list;
	file_list.push_back("opencl/keccak.cl");
	file_list.push_back("opencl/shavite.cl");
	file_list.push_back("opencl/metis.cl");
	file_list.push_back("opencl/miner.cl");

	std::stringstream params;
	params << " -I ./opencl/";
	params << " -D MAX_WGS=" << max_wgs;
	OpenCLProgram* program = device->getContext()->loadProgramFromFiles(file_list, params.str());

	// kernel_all = program->getKernel("metiscoin_process");
	kernel_keccak_noinit = program->getKernel("keccak_step_noinit");
	kernel_shavite = program->getKernel("shavite_step");
	kernel_metis = program->getKernel("metis_step");
    

	u = device->getContext()->createBuffer(25*sizeof(cl_ulong), CL_MEM_READ_WRITE, NULL);
	buff = device->getContext()->createBuffer(4, CL_MEM_READ_WRITE, NULL);

	hashes = device->getContext()->createBuffer(64 * step_size, CL_MEM_READ_WRITE, NULL);
	out = device->getContext()->createBuffer(sizeof(cl_uint) * 255, CL_MEM_READ_WRITE, NULL);
	out_count = device->getContext()->createBuffer(sizeof(cl_uint), CL_MEM_READ_WRITE, NULL);

    metis_mixtab0 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)mixtab0);
    metis_mixtab1 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)mixtab1);
    metis_mixtab2 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)mixtab2);
	metis_mixtab3 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)mixtab3);

    shavite_AES0 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)AES0);
    shavite_AES1 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)AES1);
    shavite_AES2 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)AES2);
    shavite_AES3 = device->getContext()->createBuffer(sizeof(cl_uint)*256, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, (void*)AES3);

	q = device->getContext()->createCommandQueue(device);
}


void MetiscoinOpenCL::metiscoin_process(minerMetiscoinBlock_t* block)
{

	block->nonce = 0;
	uint32 target = *(uint32*)(block->targetShare+28);
	OpenCLDevice* device = OpenCLMain::getInstance().getDevice(device_num);

	uint32 num_steps = MAX_NONCE / step_size;

	// measure time
	for (uint32 n = 0; n < num_steps; n++)
	{
#ifdef MEASURE_TIME
		uint32 begin = getTimeMilliseconds();
        uint32 end_keccak = begin;
        uint32 end_shavite = begin;
#endif
		if( block->height != monitorCurrentBlockHeight )
			break;

        cl_uint out_count_tmp = 0;
		sph_keccak512_context	 ctx_keccak;
		sph_keccak512_init(&ctx_keccak);
		sph_keccak512(&ctx_keccak, &block->version, 80);


		//keccak
		//kernel void keccak_step_noinit(constant const ulong* u, constant const char* buff, global ulong* out, uint begin_nonce)
		kernel_keccak_noinit->resetArgs();
		kernel_keccak_noinit->addGlobalArg(u);
		kernel_keccak_noinit->addGlobalArg(buff);
		kernel_keccak_noinit->addGlobalArg(hashes);
		kernel_keccak_noinit->addScalarUInt(n * step_size);

		q->enqueueWriteBuffer(u, ctx_keccak.u.wide, 25*sizeof(cl_ulong));
		q->enqueueWriteBuffer(buff, ctx_keccak.buf, 4);

		q->enqueueKernel1D(kernel_keccak_noinit, step_size, max_wgs);

#ifdef MEASURE_TIME
		q->finish();
		end_keccak = getTimeMilliseconds();
#endif

		// shavite
		kernel_shavite->resetArgs();
		kernel_shavite->addGlobalArg(hashes);
        kernel_shavite->addGlobalArg(shavite_AES0);
        kernel_shavite->addGlobalArg(shavite_AES1);
        kernel_shavite->addGlobalArg(shavite_AES2);
        kernel_shavite->addGlobalArg(shavite_AES3);

		q->enqueueKernel1D(kernel_shavite, step_size, max_wgs);

#ifdef MEASURE_TIME
		q->finish();
		end_shavite = getTimeMilliseconds();
#endif
		// metis
		kernel_metis->resetArgs();
		kernel_metis->addGlobalArg(hashes);
		kernel_metis->addGlobalArg(out);
		kernel_metis->addGlobalArg(out_count);
		kernel_metis->addScalarUInt(n * step_size);
		kernel_metis->addScalarUInt(target);

        kernel_metis->addGlobalArg(metis_mixtab0);
        kernel_metis->addGlobalArg(metis_mixtab1);
        kernel_metis->addGlobalArg(metis_mixtab2);
        kernel_metis->addGlobalArg(metis_mixtab3);

		q->enqueueWriteBuffer(out_count, &out_count_tmp, sizeof(cl_uint));

		q->enqueueKernel1D(kernel_metis, step_size, max_wgs);

        /*
        // Algorithm 2
        // Do all hashing in one pass
        } else if (this->algorithm == 2) {
            // Arguments
            kernel_all->resetArgs();
            kernel_all->addGlobalArg(u);
		    kernel_all->addGlobalArg(buff);
		    kernel_all->addGlobalArg(out);
		    kernel_all->addGlobalArg(out_count);
            kernel_all->addScalarUInt(n * step_size);
		    kernel_all->addScalarUInt(target);
            kernel_all->addGlobalArg(shavite_AES0);
            kernel_all->addGlobalArg(shavite_AES1);
            kernel_all->addGlobalArg(shavite_AES2);
            kernel_all->addGlobalArg(shavite_AES3);
            kernel_all->addGlobalArg(metis_mixtab0);
            kernel_all->addGlobalArg(metis_mixtab1);
            kernel_all->addGlobalArg(metis_mixtab2);
            kernel_all->addGlobalArg(metis_mixtab3);

            // Load up the required data
            q->enqueueWriteBuffer(u, ctx_keccak.u.wide, 25*sizeof(cl_ulong));
		    q->enqueueWriteBuffer(buff, ctx_keccak.buf, 4);
            q->enqueueWriteBuffer(out_count, &out_count_tmp, sizeof(cl_uint));

            // Run
            q->enqueueKernel1D(kernel_all, step_size, max_wgs);
        }
        */

		q->enqueueReadBuffer(out, out_tmp, sizeof(cl_uint) * 255);
		q->enqueueReadBuffer(out_count, &out_count_tmp, sizeof(cl_uint));
		q->finish();

		for (int i = 0; i < out_count_tmp; i++) {
			totalShareCount++;
			block->nonce = out_tmp[i];
			xptMiner_submitShare(block);
		}

		totalCollisionCount += 1;
#ifdef MEASURE_TIME
		uint32 end = getTimeMilliseconds();
		printf("Elapsed time: %d ms (k = %d, s = %d, m = %d)\n", (end-begin), (end_keccak-begin), (end_shavite-end_keccak), (end-end_shavite));
#endif
	}

}