/**********
Copyright (c) 2018, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <CL/cl.h>
#include <vector>
#include "bitmap.h"
#include "xcl2.hpp"

#define X_SIZE 512
#define Y_SIZE 512


int main(int argc, char** argv)
{

	if (argc != 2)
	{
		printf("Usage: %s <image> \n", argv[0]) ;
		return -1 ;
	}
   
	FILE *input_file;               
	FILE *output_file;

	size_t vector_size_bytes = sizeof(unsigned short) * Y_SIZE*X_SIZE;
	std::vector<unsigned short,aligned_allocator<unsigned short>>
	input_image(Y_SIZE*X_SIZE);
	cl_int err;

	std::vector<unsigned short,aligned_allocator<unsigned short>> output_image(Y_SIZE*X_SIZE);

	// Read the bit map file into memory and allocate memory for the final image
	std::cout << "Reading input image...\n";
	
	// Load the input image
	const char *imageFilename = argv[1];
	input_file = fopen(imageFilename, "rb");     // Opens the image file given in argv[1]
	

	if (!input_file)
	{
		printf("Error: Unable to open input image file %s!\n",
		imageFilename);
		return 1;
	 }	
	
	printf("\n");
	printf("   Reading RAW Image\n");
	size_t items_read = fread(input_image.data(), vector_size_bytes,1,input_file);    // Reads the data from the given stream into 		array pointed to by pointer. Here Stream = input_file Array pointed to by ptr= input_image.data()
	printf("   Bytes read = %d\n\n", (int)(items_read* sizeof input_image));
	
	//The get_xil_devices will return vector of Xilinx Devices
	// get_xil_devices is a utility API which will find the xilinx platforms and will return the list
	//of devices connected to xilinx platform
	
	/*std::vector<cl::Device> devices = xcl::get_xil_devices();
	cl::Device device = devices[0];*/
	
	 // Filter for a 2.0 platform and set it as the default
    	std::vector<cl::Platform> platforms;
    	cl::Platform::get(&platforms);
    	cl::Platform plat;
    	for (auto &p : platforms) {
        std::string platver = p.getInfo<CL_PLATFORM_VERSION>();
        if (platver.find("OpenCL 2.") != std::string::npos) {
            plat = p;
        	}
    	}
    	if (plat() == 0)  {
        std::cout << "No OpenCL 2.0 platform found.";
        return -1;
    	}
    	cl::Platform newP = cl::Platform::setDefault(plat);
    	if (newP != plat) {
        std::cout << "Error setting default platform.";
        return -1;
    	}

	
	//Creating context and Command Queue for selected Device    [STEP 3  and STEP 4]
	OCL_CHECK(err, cl::Context context(device, NULL, NULL, NULL, &err));
  	OCL_CHECK(err, cl::CommandQueue q(context, device,CL_QUEUE_PROFILING_ENABLE, &err));
	OCL_CHECK(err, std::string device_name = device.getInfo<CL_DEVICE_NAME>(&err));
	
	//import_binary() command will find the OpenCL binary file created using the xocc compiler
	//load into OpenCL Binary and return as Binaries
	//OpenCL and it contain many functions which can be executed on the devices
	//find_binary_file() is a utility API which will search the xclbin file for
	// targeted mode(sw_emu/hw_emu/hw) and for targeted mode
	std::string binaryFile = xcl::find_binary_file(device_name,"krnl_affine");
	cl::Program::Binaries bins = xcl::import_binary_file(binaryFile);
	devices.resize(1);
	OCL_CHECK(err, cl::Program program(context, devices, bins, NULL, &err));
	
	//Dont know what this command is doing	
	OCL_CHECK(err, cl::Kernel krnl(program,"affine_kernel", &err));
	   
	//Separate Read/write Buffer vector is needed to migrate data between host/device  [STEP 5]
	// Allocate Buffers in Global Memory
        // Buffers are allocated using CL_MEM_USE_HOST_PTR for efficient memory and
        // Device-to-host communication
	std::vector<cl::Memory> inBufVec, outBufVec;
	OCL_CHECK(err, cl::Buffer imageToDevice(context,CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, vector_size_bytes, input_image.data(), 	&err));
	OCL_CHECK(err, cl::Buffer imageFromDevice(context,CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY,vector_size_bytes, output_image.data(), &err));

	inBufVec.push_back(imageToDevice);
	outBufVec.push_back(imageFromDevice);

	/* Copy input vectors to memory */ 
	// These commands will load the source_a and source_b vectors from the host        [STEP 6]
        // application and into the buffer_a and buffer_b cl::Buffer objects. The data
        // will be be transferred from system memory over PCIe to the FPGA on-board
        // DDR memory.
	OCL_CHECK(err, err = q.enqueueMigrateMemObjects(inBufVec,0/* 0 means from host*/));

	// Set the kernel arguments   [STEP 9]
	OCL_CHECK(err, err = krnl.setArg(0, imageToDevice));
	OCL_CHECK(err, err = krnl.setArg(1, imageFromDevice));

	// Launch the kernel  [STEP 11]
	OCL_CHECK(err, err = q.enqueueTask(krnl));

	// Read back the image from the kernel  [STEP 12]
	std::cout << "Reading output image and writing to file...\n";
	output_file = fopen("transformed_image.raw", "wb");
	if (!output_file)
	{
		printf("Error: Unable to open output image file!\n");
		return 1;
	}


	// The result of the previous kernel execution will need to be retrieved in
        // order to view the results. This call will write the data from the
        // buffer_result cl_mem object to the source_results vector
	OCL_CHECK(err, err = q.enqueueMigrateMemObjects(outBufVec,CL_MIGRATE_MEM_OBJECT_HOST));
	q.finish();

	printf("   Writing RAW Image\n");
	size_t items_written = fwrite(output_image.data(), vector_size_bytes, 1, output_file);
	printf("   Bytes written = %d\n\n", (int)(items_written * sizeof output_image));

	return 0 ;
}
