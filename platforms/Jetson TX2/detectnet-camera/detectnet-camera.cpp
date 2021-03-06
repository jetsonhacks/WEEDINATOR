/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <unistd.h>			//Needed for I2C port
#include <fcntl.h>			//Needed for I2C port
#include <sys/ioctl.h>			//Needed for I2C port
#include <linux/i2c-dev.h>		//Needed for I2C port
#include <linux/types.h>

#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include <cmath>
#include <cfenv>
#include <climits>

#include "gstCamera.h"

#include "glDisplay.h"
#include "glTexture.h"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "cudaMappedMemory.h"
#include "cudaNormalize.h"
#include "cudaFont.h"

#include "detectNet.h"
#include "imageNet.h"

#pragma STDC FENV_ACCESS ON
#define PADDYADDRESS 0x70
#define DEFAULT_CAMERA -1	// -1 for onboard camera, or change to index of /dev/video V4L2 camera (>=0)


int numClasses;
float obj_conf;
int nc;                         // Class number eg 0 = dog.
int myBoxNumber;
int myNumberOfBoxes;
int myArray[4][4]; 
int intBB[4];
int top;
int writeValue;	
char writeChar;	
char g (122);                // z
int kI2CFileDescriptor;
int length;
unsigned char buffer[60] = {0};
bool signal_recieved = false;

void sig_handler(int signo)
{
	if( signo == SIGINT )
	{
		printf("received SIGINT\n");
		signal_recieved = true;
	}
}
////////////////////////////////////////////////////////////////////////////////////////
int i2cwrite(int writeValue) 
{
  int toReturn = i2c_smbus_write_byte(kI2CFileDescriptor, writeValue);
  if (toReturn < 0) 
  {
    printf(" ************ Write error ************* \n") ;
    toReturn = -1 ;
  }
  return toReturn ;
}
////////////////////////////////////////////////////////////////////////////////////////
void paddyOpenI2C()
{
  int length;
  unsigned char buffer[60] = {0};
  //----- OPEN THE I2C BUS -----
  char *filename = (char*)"/dev/i2c-1";
  if ((kI2CFileDescriptor = open(filename, O_RDWR)) < 0)
  {
	//ERROR HANDLING: you can check errno to see what went wrong
    printf("*************** Failed to open the i2c bus ******************\n");
		//return;
  }
  if( ioctl( kI2CFileDescriptor, I2C_SLAVE, PADDYADDRESS ) < 0 )
  {
    fprintf( stderr, "Failed to set slave address: %m\n" );
                //return 2;
  }
}
/////////////////////////////////////////////////////////////////////////////////////////
void I2CDataHandler()
{
  printf(" My box number  = %i \n",myBoxNumber); 
  int conf = (std::lround(obj_conf*10)+10);                   // Inference confidence % rounded down and mapped to range (10 to 19).
  i2cwrite(conf);
  //printf("My confidence one: %f \n", obj_conf*100);
  //printf("My confidence two: %i \n", conf);

  printf("My image class: %i \n", nc);
  if((nc>=0) && (nc<=79))
  {i2cwrite(nc+20);}                                          // Image class mapped to 20 to 99;

  printf("My image classes: %i \n", numClasses);
  if((numClasses>=0) && (numClasses<=9))
  {i2cwrite(numClasses+100);}                                 // Number of image classes mapped to 100 to 109;


  for( int j=0; j < 4; j++ )
  {
    if(j==0){i2cwrite(200+myNumberOfBoxes);  }                 // Total number of bounding boxes.
    if(j==0){i2cwrite(140+myBoxNumber);  }                     // Designates bounding box number.
    i2cwrite(120+j);                                           // Designates box corner number
    printf(" intBB[j]   = %i \n",intBB[j]);

    top = intBB[j];                        
    myArray[j][0] = static_cast<int>(top/1000);
    printf(" myArray[j][0]  = %i \n",myArray[j][0]);
    i2cwrite(myArray[j][0]);

    top = (top - myArray[j][0]*1000);
    myArray[j][1] = static_cast<int>(top/100);
    printf(" myArray[j][1]  = %i \n",myArray[j][1]);
    i2cwrite(myArray[j][1]);

    top = (top - myArray[j][1]*100);
    myArray[j][2] = static_cast<int>(top/10);
    printf(" myArray[j][2]  = %i \n",myArray[j][2]);
    i2cwrite(myArray[j][2]);

    top = (top - myArray[j][2]*10);
    myArray[j][3] = static_cast<int>(top);
    printf(" myArray[j][3]  = %i \n",myArray[j][3]); 

    i2cwrite(myArray[j][3]);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////
int main( int argc, char** argv )
{
/////////////////////////////////////////////////////////////////////////////////////////
        paddyOpenI2C();
/////////////////////////////////////////////////////////////////////////////////////////
	printf("detectnet-camera\n  args (%i):  ", argc);

	for( int i=0; i < argc; i++ )
		printf("%i [%s]  ", i, argv[i]);
		
	printf("\n\n");
	

	/*
	 * parse network type from CLI arguments
	 */
	/*detectNet::NetworkType networkType = detectNet::PEDNET_MULTI;

	if( argc > 1 )
	{
		if( strcmp(argv[1], "multiped") == 0 || strcmp(argv[1], "pednet") == 0 || strcmp(argv[1], "multiped-500") == 0 )
			networkType = detectNet::PEDNET_MULTI;
		else if( strcmp(argv[1], "ped-100") == 0 )
			networkType = detectNet::PEDNET;
		else if( strcmp(argv[1], "facenet") == 0 || strcmp(argv[1], "facenet-120") == 0 || strcmp(argv[1], "face-120") == 0 )
			networkType = detectNet::FACENET;
	}*/
	
	if( signal(SIGINT, sig_handler) == SIG_ERR )
		printf("\ncan't catch SIGINT\n");


	/*
	 * create the camera device
	 */
	gstCamera* camera = gstCamera::Create(DEFAULT_CAMERA);
	
	if( !camera )
	{
		printf("\ndetectnet-camera:  failed to initialize video device\n");
		return 0;
	}
	
	printf("\ndetectnet-camera:  successfully initialized video device\n");
	printf("    width:  %u\n", camera->GetWidth());
	printf("   height:  %u\n", camera->GetHeight());
	printf("    depth:  %u (bpp)\n\n", camera->GetPixelDepth());

	/*
	 * create imageNet
	 
	imageNet* net = imageNet::Create(argc, argv);
	
	if( !net )
	{
		printf("imagenet-console:   failed to initialize imageNet\n");
		return 0;
	}
        */
	/*
	 * create detectNet
	 */
	detectNet* net = detectNet::Create(argc, argv);
	
	if( !net )
	{
		printf("detectnet-camera:   failed to initialize imageNet\n");
		return 0;
	}


	/*
	 * allocate memory for output bounding boxes and class confidence
	 */
	const uint32_t maxBoxes = net->GetMaxBoundingBoxes();		printf("maximum bounding boxes:  %u\n", maxBoxes);
	const uint32_t classes  = net->GetNumClasses();
	
	float* bbCPU    = NULL;
	float* bbCUDA   = NULL;
	float* confCPU  = NULL;
	float* confCUDA = NULL;
	
	if( !cudaAllocMapped((void**)&bbCPU, (void**)&bbCUDA, maxBoxes * sizeof(float4)) ||
	    !cudaAllocMapped((void**)&confCPU, (void**)&confCUDA, maxBoxes * classes * sizeof(float)) )
	{
		printf("detectnet-console:  failed to alloc output memory\n");
		return 0;
	}
	

	/*
	 * create openGL window
	 */
	glDisplay* display = glDisplay::Create();
	glTexture* texture = NULL;
	
	if( !display ) {
		printf("\ndetectnet-camera:  failed to create openGL display\n");
	}
	else
	{
		texture = glTexture::Create(camera->GetWidth(), camera->GetHeight(), GL_RGBA32F_ARB/*GL_RGBA8*/);

		if( !texture )
			printf("detectnet-camera:  failed to create openGL texture\n");
	}
	
	
	/*
	 * create font
	 */
	cudaFont* font = cudaFont::Create();
	

	/*
	 * start streaming
	 */
	if( !camera->Open() )
	{
		printf("\ndetectnet-camera:  failed to open camera for streaming\n");
		return 0;
	}
	
	printf("\ndetectnet-camera:  camera open for streaming\n");
	
	
	/*
	 * processing loop
	 */
	float confidence = 0.0f;
	
	while( !signal_recieved )
	{
		void* imgCPU  = NULL;
		void* imgCUDA = NULL;
		
		// get the latest frame
		if( !camera->Capture(&imgCPU, &imgCUDA, 1000) )
			printf("\ndetectnet-camera:  failed to capture frame\n");

		// convert from YUV to RGBA
		void* imgRGBA = NULL;
		
		if( !camera->ConvertRGBA(imgCUDA, &imgRGBA) )
			printf("detectnet-camera:  failed to convert from NV12 to RGBA\n");

		// classify image with detectNet
		int numBoundingBoxes = maxBoxes;
                numClasses = classes;
                printf("Number of classes: %i \n", numClasses);

                //printf("Image class: %s", net->GetClassDesc(img_class))
	
		if( net->Detect((float*)imgRGBA, camera->GetWidth(), camera->GetHeight(), bbCPU, &numBoundingBoxes, confCPU))
		{
			printf("%i bouncing boxes detected\n", numBoundingBoxes);

			int lastClass = 0;
			int lastStart = 0;
			
			for( int n=0; n < numBoundingBoxes; n++ )
			{
                                obj_conf = confCPU[n*2];     // Confidence
				nc = confCPU[n*2+1];         // Image class eg 0 = dog
				float* bb = bbCPU + (n * 4);
			        printf("Object class: %i \n", nc);
                                printf("Object confidence: %f \n", obj_conf*100);
                                
				printf("bounding box %i   (%f, %f)  (%f, %f)  w=%f  h=%f\n", n, bb[0], bb[1], bb[2], bb[3], bb[2] - bb[0], bb[3] - bb[1]); 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                                intBB[0] = static_cast<int>(bb[0])+200;     // +200 to avoid minus values.
                                intBB[1] = static_cast<int>(bb[1])+200;
                                intBB[2] = static_cast<int>(bb[2])+200;
                                intBB[3] = static_cast<int>(bb[3])+200;
                                myBoxNumber = n;
                                myNumberOfBoxes = numBoundingBoxes;
                                I2CDataHandler();
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				if( nc != lastClass || n == (numBoundingBoxes - 1) )
				{
					if( !net->DrawBoxes((float*)imgRGBA, (float*)imgRGBA, camera->GetWidth(), camera->GetHeight(), 
						                        bbCUDA + (lastStart * 4), (n - lastStart) + 1, lastClass) )
						printf("detectnet-console:  failed to draw boxes\n");
						
					lastClass = nc;
					lastStart = n;

					CUDA(cudaDeviceSynchronize());
				}
			}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// classify image
	        /*

	 const int img_class = net->Classify((float*)imgRGBA, camera->GetWidth(), camera->GetHeight(), &confidence);
	
		if( img_class >= 0 )
		{
			printf("imagenet-camera:  %2.5f%% class #%i (%s)\n", confidence * 100.0f, img_class, net->GetClassDesc(img_class));	

			if( font != NULL )
			{
				char str[256];
				sprintf(str, "%05.2f%% %s", confidence * 100.0f, net->GetClassDesc(img_class));
	
				font->RenderOverlay((float4*)imgRGBA, (float4*)imgRGBA, camera->GetWidth(), camera->GetHeight(),
								    str, 0, 0, make_float4(255.0f, 255.0f, 255.0f, 255.0f));
			}
                */
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			if( display != NULL )
			{
				char str[256];
				sprintf(str, "TensorRT build %i.%i.%i | %s | %04.1f FPS", NV_TENSORRT_MAJOR, NV_TENSORRT_MINOR, NV_TENSORRT_PATCH, net->HasFP16() ? "FP16" : "FP32", display->GetFPS());
				display->SetTitle(str);	
			}	
		}	
		// update display
		if( display != NULL )
		{
			display->UserEvents();
			display->BeginRender();

			if( texture != NULL )
			{
				// rescale image pixel intensities for display
				CUDA(cudaNormalizeRGBA((float4*)imgRGBA, make_float2(0.0f, 255.0f), 
								   (float4*)imgRGBA, make_float2(0.0f, 1.0f), 
		 						   camera->GetWidth(), camera->GetHeight()));

				// map from CUDA to openGL using GL interop
				void* tex_map = texture->MapCUDA();

				if( tex_map != NULL )
				{
					cudaMemcpy(tex_map, imgRGBA, texture->GetSize(), cudaMemcpyDeviceToDevice);
					texture->Unmap();
				}

				// draw the texture
				texture->Render(100,100);		
			}

			display->EndRender();
		}
	}
	
	printf("\ndetectnet-camera:  un-initializing video device\n");
	
	
	/*
	 * shutdown the camera device
	 */
	if( camera != NULL )
	{
		delete camera;
		camera = NULL;
	}

	if( display != NULL )
	{
		delete display;
		display = NULL;
	}
	
	printf("detectnet-camera:  video device has been un-initialized.\n");
	printf("detectnet-camera:  this concludes the test of the video device.\n");
	return 0;
}

