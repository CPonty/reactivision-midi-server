/*  reacTIVision fiducial tracking framework
	Copyright (C) 2005-2008 Martin Kaltenbrunner <mkalten@iua.upf.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "PortVideoSDL.h"
#include <iterator>

#define MIDI_STATUS_CONTROLLER 0xB0
#define MIDI_CHANNEL_0 0x0

#define MIDI_CONTROL_SLIDEID(byte) (byte-15)&0x7F
#define MIDI_CONTROL_BUTTONID(byte) (byte-79)&0x7F

#define MIDI_CONTROL_SLIDER(sliderID) ((15+sliderID)&0x7F)
#define MIDI_CONTROL_BUTTON(buttonID) ((79+buttonID)&0x7F)

#define MIDI_CONTROL_NSLIDERS 3
#define MIDI_CONTROL_NBUTTONS 1

#define MIDI_VALUE_SLIDER(byte) (byte&0x7F)
#define MIDI_VALUE_BUTTON(byte) (byte&0x7F>=64?1:0)

#define uint8_t unsigned __int8

// helper function to process MIDI messages
void midiProcessor(uint8_t byte0, uint8_t byte1, uint8_t byte2, PortVideoSDL *engine) {
	using namespace std;
	/*
	reactivision's MIDI controls are on channel 0.

	Slider 1 = camera brightness
	Slider 2 = camera contrast
	Slider 3 = camera sharpness

	Button 1 = reset to defaults
	*/

	//validate first byte: ch0 controller
	if (byte0!=MIDI_STATUS_CONTROLLER|MIDI_CHANNEL_0) return;
	//validate second byte: button1 press or slider1-4 set
	if (byte1>=MIDI_CONTROL_BUTTON(1) && byte1<=MIDI_CONTROL_BUTTON(MIDI_CONTROL_NBUTTONS)) {
		if (MIDI_VALUE_BUTTON(byte2)==1) {
			// reset
			engine->camera_->resetSettings();
			cout<<"<MIDI: Reset>"<<endl; cout.flush();
			return;
		}
	}
	if (byte1>=MIDI_CONTROL_SLIDER(1) && byte1<=MIDI_CONTROL_SLIDER(MIDI_CONTROL_NSLIDERS)) {
		// slider value
		switch (MIDI_CONTROL_SLIDEID(byte1)) {
		case 1:
			cout<<"<MIDI: brightness 0x"<<MIDI_VALUE_SLIDER(byte2)<<">"<<endl; cout.flush();
			engine->camera_->applyCameraSettingByte(CameraEngine::BRIGHTNESS, MIDI_VALUE_SLIDER(byte2));
			return;
		case 2:
			cout<<"<MIDI: contrast 0x"<<MIDI_VALUE_SLIDER(byte2)<<">"<<endl; cout.flush();
			engine->camera_->applyCameraSettingByte(CameraEngine::CONTRAST, MIDI_VALUE_SLIDER(byte2));
			return;
		case 3:
			cout<<"<MIDI: sharpness 0x"<<MIDI_VALUE_SLIDER(byte2)<<">"<<endl; cout.flush();
			engine->camera_->applyCameraSettingByte(CameraEngine::SHARPNESS, MIDI_VALUE_SLIDER(byte2));
			return;
		}
	}
	cout<<"<WARN: Midi message not supported (unused control)>"<<endl; cout.flush();
}

// the thread function which listens for TCP socket connections and spawns new threads
int runServer(void *obj) {
	using namespace std;

	PortVideoSDL *engine = (PortVideoSDL *)obj;

#ifdef WIN32

	SOCKET Server, Client;

	//init winsock
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2,0), &WsaData) != 0) {
		cout<<"<ERROR: WSAStartup failed>"<<endl; cout.flush();
		WSACleanup();
		return 1;
	}

	//server loop
	while(engine->running_) {

		cout<<"<Starting server>"<<endl; cout.flush();
		
		//connection process
		Server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Server == SOCKET_ERROR || Server == INVALID_SOCKET) {
			cout<<"<ERROR: Socket failed to load>"<<endl; cout.flush();
			WSACleanup();
			return 2;
		}

		SOCKADDR_IN sin;
		memset(&sin, 0, sizeof(sin));//
		sin.sin_port=htons(5005);
		sin.sin_family=AF_INET;
		sin.sin_addr.s_addr = INADDR_ANY;

		if (bind(Server, (SOCKADDR *)(&sin), sizeof(sin)) == SOCKET_ERROR) {
			cout<<"<ERROR: Binding socket failed>"<<endl; cout.flush();
			WSACleanup();
			return 3;
		}

		//or while(listen(server, 5)==SOCKET_ERROR);
		if (listen(Server, 5) == SOCKET_ERROR) {
			cout<<"<ERROR: Listening to socket failed>"<<endl; cout.flush();
			WSACleanup();
			return 4;
		}

		int length;
		length = sizeof(sin);
		Client = accept(Server, (SOCKADDR *)(&sin), &length);

		if (Client == INVALID_SOCKET) {
			cout<<"<ERROR: Accepted bad socket>"<<endl; cout.flush();
			WSACleanup();
			return 5;
		}

		std::cout<<"<Client connect>"<<endl; cout.flush();

		//rx/tx
		vector<unsigned __int8> buffer;
		unsigned __int8 rxbuf[32];
		int rxlen;

		buffer.reserve(128);
		while(1) {
			
			//get data
			rxlen = recv(Client, (char *)rxbuf, 32, 0);
			unsigned __int8 val;
			if (rxlen==0) {
				std::cout<<"<Client socket close>"<<endl; cout.flush(); break;
			} else if (rxlen==-1) {
				std::cout<<"<Client socket force-disconnect>"<<endl; cout.flush(); break;
			}
			std::cout<<"<Data ("<<rxlen<<"): ";
			for (int i=0; i<rxlen; i++) {
				val = (unsigned __int8)(rxbuf[i]);
				std::cout<<"0x"<<hex<<int(val)<<" "; 
			}
			std::cout<<">"<<endl; cout.flush();

			//process MIDI messages
			int deleteTo=0;
			std::copy(rxbuf, rxbuf+rxlen, std::back_inserter(buffer));
			for (int i=0; i<buffer.size(); i++) {
				//look for the first character of a valid message (status byte)
				if (buffer[i]==MIDI_STATUS_CONTROLLER|MIDI_CHANNEL_0) {
					//process the succeeding bytes
					if (buffer.size()>i+2) {
						midiProcessor(buffer[i], buffer[i+1], buffer[i+2], engine);
						deleteTo=i+2;
					}
				}
			}
			if (deleteTo>0) buffer.erase(buffer.begin(), buffer.begin()+deleteTo);

			//make sure the client is really sending MIDI data
			if (buffer.size()>8) {
				std::cout<<"<WARN: Receiving non-MIDI data>"<<endl; cout.flush();
			}
			if (buffer.size()>128) {
				std::cout<<"<WARN: Inbound buffer full, non-MIDI data. Closing connection>"<<endl; cout.flush();
				break;
			}
		}

		//close sockets for next round
		closesocket(Client);
		closesocket(Server);

		//---------------------------------------------------------------------
		//SDL_Delay(100);
	}

	//memory cleanup
	WSACleanup();

#else
	while(engine->running_) { cout<<"servering"; SDL_Delay(100); }
#endif

	return (0);
}

// the thread function which constantly scans and processes console input
int getConsoleInput(void *obj) {
	using namespace std;

	PortVideoSDL *engine = (PortVideoSDL *)obj;

	string consoleBuffer;
	boolean badInputFlag;
	char propertyChar;
	int propertyVal;

	/*
	std::cout<<engine->camera_->getMinCameraSetting(CameraEngine::CONTRAST)<<endl;
	std::cout<<engine->camera_->getMaxCameraSetting(CameraEngine::CONTRAST)<<endl;
	std::cout<<engine->camera_->getMinCameraSetting(CameraEngine::BRIGHTNESS)<<endl;
	std::cout<<engine->camera_->getMaxCameraSetting(CameraEngine::BRIGHTNESS)<<endl;
	std::cout<<engine->camera_->getMinCameraSetting(CameraEngine::SHARPNESS)<<endl;
	std::cout<<engine->camera_->getMaxCameraSetting(CameraEngine::SHARPNESS)<<endl;
	*/

	while(engine->running_) {
		if(!engine->pause_) {
			//std::cout << "Thread running" << std::endl;	SDL_Delay(500);

			getline(cin, consoleBuffer);
			badInputFlag=false;

			/* Reset all
			*/
			if (consoleBuffer.compare("reset")==0) {
				engine->camera_->resetSettings();
				continue;
			}

			/* Bad input conditions:
				- nSpaces <> 1
				- spacePos <> 2nd char
				- 1stchar <> "bcs"
				- 3rdchar.. <> "0123456789"
				- property value not 0..127
			*/
			if (count(consoleBuffer.begin(), consoleBuffer.end(), ' ') != 1) badInputFlag=true;
			if (consoleBuffer.find(' ') != 1) badInputFlag=true;
			if (consoleBuffer.find_first_of("bcs") != 0) badInputFlag=true;
			if (consoleBuffer.find_first_not_of("0123456789", 2) != string::npos) badInputFlag=true;
			if (consoleBuffer.length() < 3) badInputFlag=true;

			if (badInputFlag) {
					std::cout << "Invalid command! Correct format: <propertyID> <value>"<<endl; cout.flush();
			} else {
				sscanf(consoleBuffer.c_str(), "%c %d", &propertyChar, &propertyVal);
				
				if (!(propertyVal>=0 && propertyVal<=127)) {
					std::cout << "Invalid property value! Use percentages from 0 to 127"<<endl; cout.flush();
				} else {

					/* Set the camera property */
					if (propertyChar=='b') 
						engine->camera_->applyCameraSettingByte(CameraEngine::BRIGHTNESS, propertyVal);
					if (propertyChar=='c') 
						engine->camera_->applyCameraSettingByte(CameraEngine::CONTRAST, propertyVal); //! contrast was not implemented, I added it...
					if (propertyChar=='s') 
						engine->camera_->applyCameraSettingByte(CameraEngine::SHARPNESS, propertyVal);
				}
			}

			/*
			//cool blinking cursor
			int counter = 0;//MOVE TO TOP
			if (counter++ & 1) {
				std::cout << "\r_";
			} else {
				std::cout << "\r ";
			} SDL_Delay(500);
			*/

			SDL_Delay(1);
		} else SDL_Delay(5);
	}
	return 0;
}

// the thread function which contantly retrieves the latest frame
int getFrameFromCamera(void *obj) {

		PortVideoSDL *engine = (PortVideoSDL *)obj;
		
		unsigned char *cameraBuffer = NULL;
		unsigned char *cameraWriteBuffer = NULL;
		
		while(engine->running_) {
			if(!engine->pause_) {
				//long start_time = PortVideoSDL::currentTime();
				cameraBuffer = engine->camera_->getFrame();
				if (cameraBuffer!=NULL) {
					cameraWriteBuffer = engine->ringBuffer->getNextBufferToWrite();
					if (cameraWriteBuffer!=NULL) {
						memcpy(cameraWriteBuffer,cameraBuffer,engine->ringBuffer->size());
						engine->framenumber_++;
						engine->ringBuffer->writeFinished();
						//long driver_time = PortVideoSDL::currentTime() - start_time;
						//std::cout << "camera latency: " << (driver_time/100)/10.0f << "ms" << std::endl;
					}
					SDL_Delay(1);
				} else {
					if (!engine->camera_->stillRunning()) {
						engine->running_=false;
						engine->error_=true;
					} else SDL_Delay(1);
				}
			} else SDL_Delay(5);
		}
		return(0);
}

#ifndef NDEBUG
void PortVideoSDL::saveBuffer(unsigned char* buffer) {

	int zerosize = 16-(int)floor(log10((float)framenumber_));
	if (zerosize<0) zerosize = 0;
	char zero[zerosize+1];
	zero[zerosize]=0;
	for (int i=0;i<(zerosize);i++) zero[i]=48;

	char fileName[256];
#ifdef WIN32
	sprintf(fileName,".\recording\%s%ld.pgm",zero,framenumber_);
#else
	sprintf(fileName,"./recording/%s%ld.pgm",zero,framenumber_);
#endif
	FILE*  imagefile=fopen(fileName, "w");
	fprintf(imagefile,"P5\n%u %u 255\n", width_, height_);
	fwrite((const char *)buffer, 1,  width_*height_, imagefile);
	fclose(imagefile);
}
#endif

void PortVideoSDL::stop() {
	running_=false;
	error_=false;
}

// the principal program sequence
void PortVideoSDL::run() {

	if( !setupCamera() ) {
		if( !setupWindow() ) return;
		showError("No camera found!");
		teardownWindow();
		return;
	}

	if( !setupWindow() ) return;

	allocateBuffers();
	initFrameProcessors();

	bool success = camera_->startCamera();

	if( success ){
		SDL_FillRect(window_,0,0);
		SDL_Flip(window_);
	
		// add the help message from all FrameProcessors
		for (frame = processorList.begin(); frame!=processorList.end(); frame++) {
                        std::vector<std::string> processor_text = (*frame)->getOptions();
			if (processor_text.size()>0) help_text.push_back("");
			for(std::vector<std::string>::iterator processor_line = processor_text.begin(); processor_line!=processor_text.end(); processor_line++) {
				help_text.push_back(*processor_line);
			} 
		}
		
		//print the help message
		for(std::vector<std::string>::iterator help_line = help_text.begin(); help_line!=help_text.end(); help_line++) {
			std::cout << *help_line << std::endl;
		} std::cout << std::endl;

		running_=true;
		cameraThread = SDL_CreateThread(getFrameFromCamera,this);
		consoleThread = SDL_CreateThread(getConsoleInput,this);
		serverThread = SDL_CreateThread(runServer,this);
		mainLoop();
		
		SDL_KillThread(serverThread);
		SDL_KillThread(cameraThread);
		SDL_KillThread(consoleThread);
		teardownCamera();
	} else {
		showError("Could not start camera!");
	}	

	teardownWindow();
	freeBuffers();
	
}

void PortVideoSDL::showError(const char* error)
{
	SDL_FillRect(window_,0,0);
	SDL_Rect *image_rect = new SDL_Rect();
	image_rect->x = (width_-camera_rect.w)/2-47;
	image_rect->y = (height_-camera_rect.h)/2-39;
	image_rect->w = camera_rect.w;
	image_rect->h = camera_rect.h;
	SDL_BlitSurface(getCamera(), &camera_rect, window_, image_rect);
	delete image_rect;
	
	std::string error_message = "Press any key to exit "+app_name_+" ...";
	FontTool::drawText((width_- FontTool::getTextWidth(error))/2,height_/2+60,error,window_);
	FontTool::drawText((width_- FontTool::getTextWidth(error_message.c_str()))/2,height_/2+80,error_message.c_str(),window_);
	SDL_Flip(window_);

	error_=true;
	while(error_) {
		process_events();
		SDL_Delay(1);
	}

}

void PortVideoSDL::drawHelp()
{
	int y = FontTool::getFontHeight()-3;

	for(std::vector<std::string>::iterator help_line = help_text.begin(); help_line!=help_text.end(); help_line++) {
		if (strcmp(help_line->c_str(), "") == 0) y+=5;
		else {
			FontTool::drawText(FontTool::getFontHeight(),y,help_line->c_str(),displayImage_);
			y+=FontTool::getFontHeight()-3;
		}
	}
}

// does what its name suggests
void PortVideoSDL::mainLoop()
{
	unsigned char* cameraReadBuffer = NULL;

	while(running_) {
		
		// do nothing if paused
		if (pause_){
			process_events();
			SDL_Delay(50);
			continue;
		}


		long start_time = currentTime();
		cameraReadBuffer = ringBuffer->getNextBufferToRead();
		// loop until we get access to a frame
		while (cameraReadBuffer==NULL) {
			cameraReadBuffer = ringBuffer->getNextBufferToRead();
			//if (cameraReadBuffer!=NULL) break;
			SDL_Delay(1);
			process_events();
			if(!running_) { 
				endLoop(); 
				return;
			}
		}
		long camera_time = currentTime();

		
		//memcpy(sourceBuffer_,cameraReadBuffer,ringBuffer->size());
		//ringBuffer->readFinished();
		
		// try again if we can get a more recent frame
		/*do {
			memcpy(sourceBuffer_,cameraReadBuffer,ringBuffer->size());
			ringBuffer->readFinished();
			
			cameraReadBuffer = ringBuffer->getNextBufferToRead();
		} while( cameraReadBuffer != NULL );*/
		
		// do the actual image processing job
		for (frame = processorList.begin(); frame!=processorList.end(); frame++)
			(*frame)->process(cameraReadBuffer,destBuffer_,displayImage_);
		long processing_time = currentTime();
		
		// update display
		switch( displayMode_ ) {
			case NO_DISPLAY:
				break;
			case SOURCE_DISPLAY: {
				memcpy(sourceBuffer_,cameraReadBuffer,ringBuffer->size());
				SDL_BlitSurface(sourceImage_, NULL, window_, NULL);
				if (help_) drawHelp();
				camera_->drawGUI(displayImage_);
				SDL_BlitSurface(displayImage_, NULL, window_, NULL);
				SDL_FillRect(displayImage_, NULL, 0 );
				SDL_Flip(window_);
				break;
			}			
			case DEST_DISPLAY: {
				SDL_BlitSurface(destImage_, NULL, window_, NULL);
				if (help_) drawHelp();
				camera_->drawGUI(displayImage_);
				SDL_BlitSurface(displayImage_, NULL, window_, NULL);
				SDL_FillRect(displayImage_, NULL, 0 );
				SDL_Flip(window_);
				break;
			}
		}

#ifndef NDEBUG
		if (recording_) {
			if (displayMode_!=SOURCE_DISPLAY) memcpy(sourceBuffer_,cameraReadBuffer,ringBuffer->size());
			saveBuffer(sourceBuffer_);
		}
#endif
		
		ringBuffer->readFinished();
		if (!recording_) frameStatistics(camera_time-start_time,processing_time-camera_time, currentTime()-start_time);
		process_events();
	}
	
	endLoop(); 
}

void PortVideoSDL::endLoop() {
	// finish all FrameProcessors
	for (frame = processorList.begin(); frame!=processorList.end(); frame++)
		(*frame)->finish();

	if(error_) showError("Camera disconnected!");
}

void PortVideoSDL::frameStatistics(long cameraTime, long processingTime, long totalTime) {

	frames_++;
	cameraTime_+=cameraTime;
	processingTime_+=processingTime;
	totalTime_+=totalTime;

	time_t currentTime;
	time(&currentTime);
	long diffTime = (long)( currentTime - lastTime_ );
	
	if (diffTime >= 1) {
		current_fps = (int)floor( (frames_ / diffTime) + 0.5f );
		char caption[24] = "";
		sprintf(caption,"%s - %d FPS",app_name_.c_str(),current_fps);
		if (!calibrate_) SDL_WM_SetCaption(caption, NULL );
		/*std::cout 	<< floor((cameraTime_/frames_)/1000.0f) << " " 
				<< floor((processingTime_/frames_)/1000.0f) << " " 
				<< floor((totalTime_/frames_)/1000.0f) << std::endl;*/
		//std::cout << "frame latency: " << ((processingTime_/frames_)/100)/10.0f << " ms" << std::endl;

		frames_ = 0;
		cameraTime_ = processingTime_ = totalTime_ = 0.0f;
		//std::cout << current_fps << std::endl;
		lastTime_ = (long)currentTime;

		if (!calibrated_) {
			calibrated_ = true;
			for (frame = processorList.begin(); frame!=processorList.end(); frame++)
                 		(*frame)->toggleFlag(' ');
		}
    	}
}

bool PortVideoSDL::setupWindow() {

	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		printf("SDL could not be initialized: %s\n", SDL_GetError());
		return false;
	}
	
	window_ = SDL_SetVideoMode(width_, height_, 32, SDL_HWSURFACE);
	if ( window_ == NULL ) {
		printf("Could not open window: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}

	iconImage_ = getIcon();
	#ifndef __APPLE__
	SDL_WM_SetIcon(iconImage_, getMask());
	#endif

	FontTool::init();
	SDL_EnableKeyRepeat(200, 10);
	SDL_WM_SetCaption(app_name_.c_str(), NULL);

	return true;
}

void PortVideoSDL::teardownWindow()
{
	FontTool::close();
	SDL_Quit();
}

void PortVideoSDL::process_events()
{
    SDL_Event event;
    while( SDL_PollEvent( &event ) ) {

        switch( event.type ) {
		case SDL_KEYDOWN:
			if (error_) { error_ = false; return; }
			//printf("%d\n",event.key.keysym.sym);
			if( event.key.keysym.sym == SDLK_n ){
				displayMode_ = NO_DISPLAY;
				// turn the display black
				SDL_FillRect(window_,0,0);
				SDL_Flip(window_);
			} else if( event.key.keysym.sym == SDLK_s ){
				SDL_FillRect(displayImage_, NULL, 0 );
				displayMode_ = SOURCE_DISPLAY;
			} else if( event.key.keysym.sym == SDLK_t ){
				SDL_FillRect(displayImage_, NULL, 0 );
				displayMode_ = DEST_DISPLAY;
			} else if( event.key.keysym.sym == SDLK_o ){
				//!TEST
				//camera_->resetSettings();
				camera_->showSettingsDialog(); 
			} else if( event.key.keysym.sym == SDLK_v ){
				if (verbose_) {
					verbose_=false;
				} else {
					help_ = false;
					verbose_=true;
				}
			} 
#ifndef NDEBUG
			  else if( event.key.keysym.sym == SDLK_m ){
					if (recording_) {
						recording_ = false;
						char caption[24] = "";
						sprintf(caption,"%s - %d FPS",app_name_.c_str(),current_fps);
						SDL_WM_SetCaption( caption, NULL );
					} else {
						struct stat info;
#ifdef WIN32			
						if (stat(".\recording",&info)!=0) {
							std::string dir(".\recording");
							LPSECURITY_ATTRIBUTES attr = NULL;
							CreateDirectory(dir.c_str(),attr);
						}
#else
						if (stat("./recording",&info)!=0) mkdir("./recording",0777);
#endif
						recording_ = true;
						std::string caption = app_name_ + " - recording";
						SDL_WM_SetCaption( caption.c_str(), NULL );
					}
			} else if( event.key.keysym.sym == SDLK_b ){
				struct stat info;
#ifdef WIN32			
				if (stat(".\recording",&info)!=0) {
					std::string dir(".\recording");
					LPSECURITY_ATTRIBUTES attr = NULL;
					CreateDirectory(dir.c_str(),attr);
				}
#else
				if (stat("./recording",&info)!=0) mkdir("./recording",0777);
#endif
				if (displayMode_==DEST_DISPLAY) saveBuffer(destBuffer_);
				else saveBuffer(sourceBuffer_);
			} 
#endif
			else if( event.key.keysym.sym == SDLK_p ) {
				if (pause_) {
					pause_=false;
					char caption[24] = "";
					sprintf(caption,"%s - %d FPS",app_name_.c_str(),current_fps);
					SDL_WM_SetCaption( caption, NULL );
					
				} else {
					pause_=true;
					std::string caption = app_name_ + " - paused";
					SDL_WM_SetCaption( caption.c_str(), NULL );
					// turn the display black
					SDL_FillRect(window_,0,0);
					SDL_Flip(window_);
				}				
			} else if( event.key.keysym.sym == SDLK_c ) {
				if (calibrate_) {
					calibrate_=false;
					char caption[24] = "";
					sprintf(caption,"%s - %d FPS",app_name_.c_str(),current_fps);
					SDL_WM_SetCaption( caption, NULL );
					
				} else {
					calibrate_=true;
					std::string caption = app_name_ + " - calibration";
					SDL_WM_SetCaption( caption.c_str(), NULL );
				}				
			} else if( event.key.keysym.sym == SDLK_h ){
				help_=!help_;
				verbose_=false;
			} else if( event.key.keysym.sym == SDLK_ESCAPE ){
				running_=false;
				return;
			}

			for (frame = processorList.begin(); frame!=processorList.end(); frame++)
				(*frame)->toggleFlag(event.key.keysym.sym);
			camera_->control(event.key.keysym.sym);

			break;
		case SDL_QUIT:
			running_ = false;
			error_ = false;
			break;
        }
    }
}

bool PortVideoSDL::setupCamera() {

	camera_ = CameraTool::findCamera(camera_config);	
	if (camera_ == NULL) return false;
	
	bool success = camera_->initCamera();
	
	if(success) {
		width_ = camera_->getWidth();
		height_ = camera_->getHeight();
		fps_ = camera_->getFps();
					
		printf("camera: %s\n",camera_->getName());
		if (fps_>0) printf("format: %dx%d, %dfps\n\n",width_,height_,fps_);
		else printf("format: %dx%d\n\n",width_,height_);
	
		return true;
	} else {
		printf("could not initialize camera\n");
		camera_->closeCamera();
		delete camera_;
		return false;
	}
}

void PortVideoSDL::teardownCamera()
{
	camera_->stopCamera();
	camera_->closeCamera();
	delete camera_;
}

void PortVideoSDL::allocateBuffers()
{
	bytesPerSourcePixel_ = sourceDepth_/8;	
	bytesPerDestPixel_ = destDepth_/8;
	sourceBuffer_ = new unsigned char[width_*height_*bytesPerSourcePixel_];
	destBuffer_ = new unsigned char[width_*height_*bytesPerDestPixel_];
	displayBuffer_ = new unsigned char[width_*height_*3];
	cameraBuffer_ = NULL;
		
	sourceImage_ = SDL_CreateRGBSurfaceFrom(sourceBuffer_, width_, height_, sourceDepth_, width_*bytesPerSourcePixel_, 0 , 0, 0, 0);
	if (sourceDepth_==8)
		SDL_SetPalette(sourceImage_, SDL_LOGPAL|SDL_PHYSPAL, palette_, 0, 256 );
	//SDL_DisplayFormat(sourceImage_);

	destImage_ = SDL_CreateRGBSurfaceFrom(destBuffer_, width_, height_, destDepth_, width_*bytesPerDestPixel_, 0 , 0, 0, 0);
	if (destDepth_==8)
		SDL_SetPalette(destImage_, SDL_LOGPAL|SDL_PHYSPAL, palette_, 0, 256 );
	//SDL_DisplayFormat(destImage_);

	displayImage_ = SDL_CreateRGBSurfaceFrom(displayBuffer_, width_, height_, 24, width_*3, 0, 0, 0, 0);
	SDL_SetColorKey(displayImage_, SDL_SRCCOLORKEY, SDL_MapRGB(displayImage_->format, 0, 0, 0));
	//SDL_DisplayFormat(displayImage_);
	
	ringBuffer = new RingBuffer(width_*height_*bytesPerSourcePixel_);
}

void PortVideoSDL::freeBuffers()
{
	SDL_FreeSurface(sourceImage_);
	SDL_FreeSurface(destImage_);
	SDL_FreeSurface(displayImage_);
	SDL_FreeSurface(iconImage_);
	
	delete [] sourceBuffer_;
	delete [] destBuffer_;
	delete [] displayBuffer_;	
	delete ringBuffer;
}

void PortVideoSDL::addFrameProcessor(FrameProcessor *fp) {

	processorList.push_back(fp);
	fp->addMessageListener(this);
}


void PortVideoSDL::removeFrameProcessor(FrameProcessor *fp) {
	frame = std::find( processorList.begin(), processorList.end(), fp );
	if( frame != processorList.end() ) {
		processorList.erase( frame );
		fp->removeMessageListener(this);
	}
}

void PortVideoSDL::setMessage(std::string message)
{
	if (verbose_) {
		std::cout << message << std::endl;
		std::cout.flush();
	}
}

void PortVideoSDL::displayMessage(const char *message)
{
	SDL_FillRect(window_,0,0);
	FontTool::drawText((width_- FontTool::getTextWidth(message))/2,height_/2,message,window_);
	SDL_Flip(window_);
}

void PortVideoSDL::setDisplayMode(DisplayMode mode) {
	if ((window_!=NULL) && (mode==NO_DISPLAY)) {
		SDL_FillRect(window_,0,0);
		SDL_Flip(window_);
	}
	displayMode_ = mode;
}

void PortVideoSDL::initFrameProcessors() {
	for (frame = processorList.begin(); frame!=processorList.end(); ) {
		bool success = (*frame)->init(width_ , height_, bytesPerSourcePixel_, bytesPerDestPixel_);
		if(!success) {	
			processorList.erase( frame );
			printf("removed frame processor\n");
		} else frame++;
	}
}

unsigned int PortVideoSDL::current_fps = 0;
bool PortVideoSDL::display_lock = false;

PortVideoSDL::PortVideoSDL(const char* name, bool background, const char* cfg)
	: error_( false )
	, pause_( false )
	, calibrate_( false )
	, help_( false )
	, framenumber_( 0 )
	, frames_( 0 )
	, recording_( false )
	, width_( WIDTH )
	, height_( HEIGHT )
	, displayMode_( DEST_DISPLAY )
{
	cameraTime_ = processingTime_ = totalTime_ = 0.0f;
	calibrated_ = !background;
	camera_config = cfg;
	
	time_t start_time;
	time(&start_time);
	lastTime_ = (long)start_time;

	app_name_ = std::string(name);
	sourceDepth_ = 8;
	destDepth_   = 8;

	window_ = NULL;
	sourceBuffer_ = NULL;
	destBuffer_ = NULL;

	for(int i=0;i<256;i++){
		palette_[i].r=i;
		palette_[i].g=i;
		palette_[i].b=i;
	}
		
	help_text.push_back("display:");
 	help_text.push_back("   n - no image");
	help_text.push_back("   s - source image");
	help_text.push_back("   t - target image");	
	help_text.push_back("   h - toggle help text");
	help_text.push_back("");
	help_text.push_back("commands:");
	help_text.push_back("   ESC - quit " + app_name_);
	help_text.push_back("   v - verbose output");
	help_text.push_back("   o - camera options");
	help_text.push_back("   p - pause processing");
	help_text.push_back("");
	#ifndef NDEBUG
	help_text.push_back("debug options:");
	help_text.push_back("   b - save buffer as PGM");
	help_text.push_back("   m - save buffer sequence");
	#endif

}