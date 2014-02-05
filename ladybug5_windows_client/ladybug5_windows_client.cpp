#pragma once
#include "timing.h"
#include "ladybug5_windows_client.h"

void compressImages(ladybug5_network::pbMessage *message, unsigned char* arpBuffers, TJPF TJPF_BGRA, ladybug5_network::LadybugTimeStamp *msg_timestamp, int uiRawCols, int uiRawRows )
{
	for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
	{
		addImageToMessage(message, &arpBuffers[uiCamera], TJPF_BGRA, msg_timestamp, (ladybug5_network::ImageType) ( 1 << uiCamera), uiRawCols, uiRawRows );
	}
}

//
// This function reads an image from camera
//
LadybugError
	initCamera( LadybugContext context)
{    
	// Initialize camera
	printf( "Initializing camera...\n" );
	LadybugError error = LADYBUG_NOT_STARTED;
	
	error = ladybugInitializeFromIndex( context, 0 );
	int i = 0;
	while(error != LADYBUG_OK && i<10){
		++i;
		printf( "Initializing camera...try %i\n", i );
		Sleep(2500);
		error = ladybugInitializeFromIndex( context, 0 );
	}
	if (error != LADYBUG_OK)
	{
		return error;    
	}

	// Load config file
	printf( "Load configuration file...\n" );
	error = ladybugLoadConfig( context, NULL);
	i = 0;
	while(error != LADYBUG_OK && i<10){
		++i;
		printf( "Load configuration file...try %i\n", i );
		Sleep(500);
		error = ladybugLoadConfig( context, NULL);
	}
	if (error != LADYBUG_OK)
	{
		return error;    
	}
	return error;
}

LadybugError configureLadybugForPanoramic(LadybugContext context){
	
	// Set the panoramic view angle
	LadybugError error = ladybugSetPanoramicViewingAngle( context, LADYBUG_FRONT_0_POLE_5);
	if (error != LADYBUG_OK)
	{
		return error;    
	}

	// Make the rendering engine use the alpha mask
	error = ladybugSetAlphaMasking( context, true );
	if (error != LADYBUG_OK)
	{
		return error;    
	}

	// Set color processing method.
	error = ladybugSetColorProcessingMethod( context, COLOR_PROCESSING_METHOD );
	if (error != LADYBUG_OK)
	{
		return error;    
	}

	// Configure output images in Ladybug library
	printf( "Configure output images in Ladybug library...\n" );
	error = ladybugConfigureOutputImages( context, LADYBUG_PANORAMIC );
	if (error != LADYBUG_OK)
	{
		return error;    
	}

	error = ladybugSetOffScreenImageSize(
		context, 
		LADYBUG_PANORAMIC,  
		PANORAMIC_IMAGE_WIDTH, 
		PANORAMIC_IMAGE_HEIGHT );  
	if (error != LADYBUG_OK)
	{
		return error;    
	}
	return error;
}

LadybugError startLadybug(LadybugContext context){
	// Get camera info
	printf( "Getting camera info...\n" );
	
	LadybugCameraInfo caminfo;
	LadybugError error = ladybugGetCameraInfo( context, &caminfo );
	if (error != LADYBUG_OK)
	{
		return error;    
	}
	printf("Starting %s (%u)...\n", caminfo.pszModelName, caminfo.serialHead);

	switch( caminfo.deviceType )
	{
	case LADYBUG_DEVICE_COMPRESSOR:
	case LADYBUG_DEVICE_LADYBUG3:
	case LADYBUG_DEVICE_LADYBUG5:
		printf( "Starting Ladybug camera...\n" );
		error = ladybugStart(
			context,
			//LADYBUG_DATAFORMAT_JPEG8 
			//LADYBUG_DATAFORMAT_COLOR_SEP_JPEG8
			LADYBUG_DATAFORMAT_RAW8 
			);
		break;

	case LADYBUG_DEVICE_LADYBUG:
	default:
		printf( "Unsupported device.\n");
		error = LADYBUG_FAILED;
	}

	return error;
}


void main( int argc, char* argv[] ){
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	zmq::context_t context(1);
	zmq::socket_t grabber(context, ZMQ_ROUTER);
    //zmq_bind (grabber, "inproc://uncompressed");

    //  Socket to send messages to
    zmq::socket_t workers(context, ZMQ_DEALER);
    //zmq_bind (workers, "inproc://workers");
	
	//zmq::socket_t grabber(context, ZMQ_ROUTER);
	//unsigned int val = 12; //buffer size
	//grabber.setsockopt(ZMQ_RCVHWM, &val, sizeof(val));  //prevent buffer get overfilled
	//grabber.setsockopt(ZMQ_SNDHWM, &val, sizeof(val));  //prevent buffer get overfilled
	//grabber.bind("inproc://uncompressed");
 //
	//zmq::socket_t workers(context, ZMQ_DEALER);
	//workers.setsockopt(ZMQ_RCVHWM, &val, sizeof(val));  //prevent buffer get overfilled
	//grabber.setsockopt(ZMQ_SNDHWM, &val, sizeof(val));  //prevent buffer get overfilled
	//zmq_bind(workers, "inproc://workers");
	

//  Start a queue device zmq_device (ZMQ_QUEUE, frontend, backend)
	/*zmq::socket_t ladybugSocket (*context, ZMQ_ROUTER);
    ladybugSocket.bind("inproc://uncompressed");

    zmq::socket_t workers (*context, ZMQ_DEALER);
    workers.bind("inproc://worker");*/

	//boost::thread grab(ladybugThread, &context, "uncompressed");
	
	//zmq::proxy(ladybugSocket, workers, NULL);
	//std::string ros_master = "tcp://149.201.37.83:28882";
	std::string ros_master = "tcp://192.168.1.178:28882";
	//std::string connection = "tcp://149.201.37.61:28882"; //Kai
	//std::string connection = "tcp://192.168.0.5:28882"; //Kai

	boost::thread_group threads;
	threads.create_thread(std::bind(ladybugSimulator, &context)); //ladybug grabbing thread
	threads.create_thread(std::bind(sendingThread, &context, ros_master)); // sending thread
	
	for(int i=0; i< LADYBUG_NUM_CAMERAS; ++i){
		threads.create_thread(std::bind(compresseionThread, &context, i)); //worker thread (jpg-compression)
	}
	
	//zmq_proxy(grabber, workers, NULL); 

    //  Launch pool of worker threads
	while(true){
		Sleep(1000);
	}
	std::printf("<PRESS ANY KEY TO EXIT>");
	_getch();
}

ladybug5_network::pbMessage createMessage(std::string name, std::string camera){
	ladybug5_network::pbMessage message;
	message.set_id(1);
	message.set_name(name);
	message.set_camera(camera);
	return message;
}

void compressImageInMsg(ladybug5_network::pbMessage *message){
	//encode to jpg
	
	unsigned char* _compressedImage = 0;
	int JPEG_QUALITY = 85;
	TJPF color = TJPF_BGR;
	
	int img_width =  message->images(0).width();
	int img_hight =  message->images(0).hight();
	unsigned long img_Size = 0;
	ladybug5_network::ImageType img_type = message->images(0).type();

	unsigned char* pointer = (unsigned char*)message->mutable_images(0)->image().data();
	assert(message->images(0).image().size()!=0);
	assert(img_width!=0);
	assert(img_hight!=0);
	tjhandle _jpegCompressor = tjInitCompress();
	tjCompress2(_jpegCompressor, pointer, img_width, 0, img_hight, color,
				&_compressedImage, &img_Size, TJSAMP_420, JPEG_QUALITY,
				TJFLAG_FASTDCT);
	tjDestroy(_jpegCompressor);

	assert(img_Size!=0);

	ladybug5_network::LadybugTimeStamp* msg_timestamp = new ladybug5_network::LadybugTimeStamp(message->images(0).time());
	//append to message
	message->clear_images();
	ladybug5_network::pbImage* img_msg = 0;
	img_msg = message->add_images();
	img_msg->set_image(_compressedImage, img_Size);
	img_msg->set_size(img_Size);
	img_msg->set_type(img_type);
	img_msg->set_name(enumToString(img_msg->type()));
	img_msg->set_allocated_time(msg_timestamp);
	img_msg->set_hight(img_hight);
	img_msg->set_width(img_width);
	
	tjFree(_compressedImage);
}

void addImageToMessage(ladybug5_network::pbMessage *message,  unsigned char* uncompressedBGRImageBuffer, TJPF color, ladybug5_network::LadybugTimeStamp *timestamp, ladybug5_network::ImageType img_type, int _width, int _height){
	//encode to jpg
	//const int COLOR_COMPONENTS = 4;
	unsigned long imgSize = 0;
	unsigned char* _compressedImage = 0;
	int JPEG_QUALITY = 85;

	tjhandle _jpegCompressor = tjInitCompress();
	tjCompress2(_jpegCompressor, uncompressedBGRImageBuffer, _width, 0, _height, color,
				&_compressedImage, &imgSize, TJSAMP_420, JPEG_QUALITY,
				TJFLAG_FASTDCT);
	tjDestroy(_jpegCompressor);
	
	//append to message
	ladybug5_network::pbImage* image_msg = 0;
	image_msg = message->add_images();
	image_msg->set_image(_compressedImage, imgSize);
	image_msg->set_size(imgSize);
	image_msg->set_type(img_type);
	image_msg->set_name(enumToString(img_type));
	image_msg->set_allocated_time(new ladybug5_network::LadybugTimeStamp(*timestamp));
	tjFree(_compressedImage);
}



void jpegEncode(unsigned char* _compressedImage, unsigned long *_jpegSize, unsigned char* srcBuffer, int JPEG_QUALITY, int _width, int _height ){
	const int COLOR_COMPONENTS = 3;
		
	tjhandle _jpegCompressor = tjInitCompress();
	tjCompress2(_jpegCompressor, srcBuffer, _width, 0, _height, TJPF_BGR,
				&_compressedImage, _jpegSize, TJSAMP_420, JPEG_QUALITY,
				TJFLAG_FASTDCT);
	tjDestroy(_jpegCompressor);	
}


LadybugError saveImages(LadybugContext context, int i){

	LadybugError error;
	std::string status = "saving images";
	for(int j=0; j<LADYBUG_NUM_CAMERAS+1; j++)
	{
		// Stitch the images (inside the graphics card) and retrieve the output to the user's memory
		LadybugProcessedImage processedImage;
		char pszOutputName[256];
		sprintf( pszOutputName, "%03d_cam%d.jpg",i ,j);
		switch (j)
		{
		case 0:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM0, LADYBUG_BGR, &processedImage);
			break;
		case 1:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM1, LADYBUG_BGR, &processedImage);
			break;
		case 2:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM2, LADYBUG_BGR, &processedImage);
			break;
		case 3:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM3, LADYBUG_BGR, &processedImage);
			break;
		case 4:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM4, LADYBUG_BGR, &processedImage);
			break;
		case 5:
			error = ladybugRenderOffScreenImage(context, LADYBUG_RECTIFIED_CAM5, LADYBUG_BGR, &processedImage);
			break;
		case LADYBUG_NUM_CAMERAS:
			error = ladybugRenderOffScreenImage(context, LADYBUG_PANORAMIC, LADYBUG_BGR, &processedImage);
			sprintf( pszOutputName, "%03d_pano.jpg",i);
			break;
		}
		_HANDLE_ERROR

			printf( "Writing image %s...\n", pszOutputName);
		error = ladybugSaveImage( context, &processedImage, pszOutputName, LADYBUG_FILEFORMAT_JPG);
		_HANDLE_ERROR
	}


_EXIT:
	return error;
}

void initBuffers(unsigned char** arpBuffers, unsigned int number, unsigned int width, unsigned int height, unsigned int dimensions){
	//
	// Initialize the pointers to NULL 
	//
	std::cout << "Initialised arpBuffers with size: "<< width * height * dimensions << " width: " << width << " height: " << height << std::endl;
	for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
	{
		arpBuffers[ uiCamera ] = new unsigned char[ width * height * dimensions ];
	}
}


void initBuffersWitPicture(unsigned char** arpBuffers, long unsigned int* size){
	//
	// Initialize the pointers to NULL 
	//
	for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
	{
		char filename[24]; 
		std::sprintf(filename, "CAM%i.bmp", uiCamera);
		std::ifstream file (filename, std::ios::in|std::ios::binary|std::ios::ate);
		if (file.is_open())
		{
			size[uiCamera] = file.tellg();
			arpBuffers[ uiCamera ] = new unsigned char [size[uiCamera]];
			file.seekg (0, std::ios::beg);
			file.read( (char*)arpBuffers[ uiCamera ], size[uiCamera]);
			file.close();
			std::cout << "Initialised arpBuffers with size: "<< (double)size[uiCamera]/(1024*1024) << " MiB  "<< filename << std::endl;
		}else{
			std::cout << "Cant open file " << filename << std::endl;
		}
	}
}

