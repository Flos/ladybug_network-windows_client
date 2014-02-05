#pragma once
#include "timing.h"
#include "ladybug5_windows_client.h"

static bool pb_send(zmq::socket_t* socket, const ladybug5_network::pbMessage* pb_message){
	// serialize the request to a string
	std::string pb_serialized;
	pb_message->SerializeToString(&pb_serialized);
    
	// create and send the zmq message
	zmq::message_t request (pb_serialized.size());
	memcpy ((void *) request.data (), pb_serialized.c_str(), pb_serialized.size());
	return socket->send(request, 0);
}

static bool pb_recv(zmq::socket_t* socket, ladybug5_network::pbMessage* pb_message){
	// serialize the request to a string
	zmq::message_t zmq_msg;
	bool result = socket->recv(&zmq_msg);
	pb_message->ParseFromArray(zmq_msg.data(), zmq_msg.size());
	return result;
}

void ladybugThread(zmq::context_t* p_zmqcontext, std::string imageReciever)
{
    printf("Start ladybug5_windows_client.exe\nPano w: %i h: %i\n\n", PANORAMIC_IMAGE_WIDTH, 
		PANORAMIC_IMAGE_HEIGHT);
	std::string connection = "inproc://" + imageReciever;

_RESTART:
	zmq::socket_t socket(*p_zmqcontext, ZMQ_REQ);
	socket.connect("inproc://uncompressed");
	
	unsigned char* arpBuffers[ LADYBUG_NUM_CAMERAS ];
	
	for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
	{
		arpBuffers[ uiCamera ] = NULL;
	}
	
	double t_now = clock();	
	unsigned int uiRawCols = 0;
	unsigned int uiRawRows = 0;

	LadybugError error;

	int retry = 10;

	// create ladybug context
	std::string status = "Create ladybug context";
	LadybugContext context = NULL;
	error = ladybugCreateContext( &context );
	if ( error != LADYBUG_OK )
	{
		printf( "Failed creating ladybug context. Exit.\n" );
	}
	_HANDLE_ERROR
	_TIME

	status = "Initialize the camera";

	// Initialize  the camera
	error = initCamera(context);
	_HANDLE_ERROR
	_TIME	

	if(PANORAMIC){
		status = "configure for panoramic stitching";
		error = configureLadybugForPanoramic(context);
		_HANDLE_ERROR
		_TIME
	}else{
		printf("Skipped configuration for panoramic pictures\n");
		 
	}

	status = "start ladybug";
	error = startLadybug(context);
	_HANDLE_ERROR
	_TIME
	
	// Grab an image to inspect the image size
	status = "Grab an image to inspect the image size";
	error = LADYBUG_FAILED;
	LadybugImage image;
	while ( error != LADYBUG_OK && retry-- > 0)
	{
		error = ladybugGrabImage( context, &image ); 
	}
	_HANDLE_ERROR
	_TIME


	status = "inspect image size";
	// Set the size of the image to be processed
	if (COLOR_PROCESSING_METHOD == LADYBUG_DOWNSAMPLE4 || 
		COLOR_PROCESSING_METHOD == LADYBUG_MONO)
	{
		uiRawCols = image.uiCols / 2;
		uiRawRows = image.uiRows / 2;
	}
	else
	{
		uiRawCols = image.uiCols;
		uiRawRows = image.uiRows;
	}
	_TIME

	// Initialize alpha mask size - this can take a long time if the
	// masks are not present in the current directory.
	status = "Initializing alpha masks (this may take some time)...";
	error = ladybugInitializeAlphaMasks( context, uiRawCols, uiRawRows );
	_HANDLE_ERROR
	_TIME
	{
		initBuffers(arpBuffers, LADYBUG_NUM_CAMERAS, uiRawCols, uiRawRows, 4);
		ladybug5_network::LadybugTimeStamp msg_timestamp;
		ladybug5_network::pbMessage message;
		while(true)
		{
			try{
				status = "Grabbing loop...";
				double loopstart = t_now = clock();	
				double t_now = loopstart;	
				LadybugImage image;
				error = ladybugGrabImage(context, &image); 
				_HANDLE_ERROR
	
				msg_timestamp.set_ulcyclecount(image.timeStamp.ulCycleCount);
				msg_timestamp.set_ulcycleoffset(image.timeStamp.ulCycleOffset);
				msg_timestamp.set_ulcycleseconds(image.timeStamp.ulCycleSeconds);
				msg_timestamp.set_ulmicroseconds(image.timeStamp.ulMicroSeconds);
				msg_timestamp.set_ulseconds(image.timeStamp.ulSeconds);

				error = ladybugConvertImage(context, &image, arpBuffers);
				_HANDLE_ERROR
			

				for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
				{
				
					message.MergeFrom(createMessage("windows","ladybug5"));
					// send message to queue insted
					unsigned int image_size = uiRawCols*uiRawRows*4;
					ladybug5_network::pbImage* image_msg = 0;
					image_msg = message.add_images();
					image_msg->set_image(arpBuffers[uiCamera], image_size);
					image_msg->set_size(image_size);
					image_msg->set_type((ladybug5_network::ImageType) ( 1 << uiCamera));
					image_msg->set_name(enumToString(image_msg->type()));
					image_msg->set_allocated_time(new ladybug5_network::LadybugTimeStamp(msg_timestamp));
					image_msg->set_hight(uiRawCols);
					image_msg->set_width(uiRawRows);

					pb_send(&socket, &message);
					message.Clear();
				
					//std::cout << "send message with size: " << message.ByteSize() << std::endl;
				}
				_TIME
			}
			catch(std::exception e){
				printf("Exception, trying to recover\n");
				goto _EXIT; 
			};
		}
	
		printf("Done.\n");
	}
	
_EXIT:
	//
	// clean up
	//
	ladybugStop( context );
	ladybugDestroyContext( &context );
	for( int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ ){
		if ( arpBuffers[ uiCamera ] != NULL )
		{
			delete  [] arpBuffers[ uiCamera ];
		}
	}
	goto _RESTART;
}



void ladybugSimulator(zmq::context_t* p_zmqcontext ){
	std::string connection = "inproc://uncompressed";
	std::string status = "Simulator Thread: init";
	printf("%s connecting to: %s\n", status.c_str(), connection.c_str());
	////connection = "inproc://workers_";

	zmq::socket_t socket(*p_zmqcontext, ZMQ_REQ);
	socket.bind(connection.c_str());

	unsigned char* arpBuffers[ LADYBUG_NUM_CAMERAS ];
	for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
	{
		arpBuffers[ uiCamera ] = NULL;
	}
	
	double t_now = clock();	
	unsigned int uiRawCols = 2048;
	unsigned int uiRawRows = 2448;
	unsigned long size[LADYBUG_NUM_CAMERAS];

	initBuffersWitPicture(arpBuffers, size);
	ladybug5_network::LadybugTimeStamp msg_timestamp;
	ladybug5_network::pbMessage message;
	while(true)
	{
			status = "Grabbing loop...";
			double loopstart = t_now = clock();	
			double t_now = loopstart;	
			
			msg_timestamp.set_ulcyclecount(4);
			msg_timestamp.set_ulcycleoffset(2);
			msg_timestamp.set_ulcycleseconds(5);
			msg_timestamp.set_ulmicroseconds(3);
			msg_timestamp.set_ulseconds(7);

			for( unsigned int uiCamera = 0; uiCamera < LADYBUG_NUM_CAMERAS; uiCamera++ )
			{
				message.set_name("windows");
				message.set_camera("ladybug5");
				ladybug5_network::pbImage* image_msg = 0;
				image_msg = message.add_images();
				image_msg->set_image(arpBuffers[uiCamera],  size[uiCamera]);
				image_msg->set_size( size[uiCamera]);
				image_msg->set_type((ladybug5_network::ImageType) ( 1 << uiCamera));
				image_msg->set_name(enumToString(image_msg->type()));
				image_msg->set_hight(uiRawCols);
				image_msg->set_width(uiRawRows);
				image_msg->set_allocated_time(new ladybug5_network::LadybugTimeStamp(msg_timestamp));

				assert(image_msg->image().size()!=0);
				pb_send(&socket, &message);
				message.Clear();
				zmq::message_t ready;
				socket.recv(&ready);
				
			}
			Sleep(50);
			_TIME

		}
}


void compresseionThread(zmq::context_t* p_zmqcontext, int i)
{
	
	std::string connection_in = "inproc://uncompressed";
	std::string connection_out = "inproc://pb_message";

	printf("Compression Thread%i: in: %s out: $s\n", i, connection_in.c_str(), connection_out.c_str());

	zmq::socket_t socket_in(*p_zmqcontext, ZMQ_REP);
	socket_in.connect(connection_in.c_str());

	zmq::socket_t socket_out(*p_zmqcontext, ZMQ_REQ);
	socket_out.connect(connection_out.c_str());

	zmq::message_t zmq_ready;
	ladybug5_network::pbMessage pb_msg;

	while(true)
	{
		pb_recv(&socket_in, &pb_msg); /* Block until a message is available to be received from socket */
		socket_in.send(zmq_ready); /* send emtpy message, to sign that the other thread can continue*/

		assert(pb_msg.images(0).image().length()!=0);
		assert(pb_msg.images(0).image().size()!=0);
	
		compressImageInMsg(&pb_msg);

		pb_send(&socket_out, &pb_msg);
		pb_msg.Clear();
		zmq::message_t empty;
		socket_out.recv(&empty);		
	}
}

void sendingThread(zmq::context_t* p_zmqcontext, std::string rosmaster){
	printf("Sending Thread: init connecting to %s\n", rosmaster.c_str());
	
	std::string connection_in = "inproc://pb_message";
	zmq::socket_t socket_in(*p_zmqcontext, ZMQ_REP);
	socket_in.bind(connection_in.c_str());

	zmq::socket_t socket_out(*p_zmqcontext, ZMQ_PUB);
	socket_out.connect(rosmaster.c_str());

	zmq::message_t done;
	while(true){
		zmq::message_t in;
		socket_in.recv(&in);
		socket_in.send(done);
		socket_out.send(in);
	}
}
