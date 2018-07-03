// *****************************************************************************
//
//      Copyright (c) 2013, Pleora Technologies Inc., All rights reserved.
//
// *****************************************************************************

//
// Shows how to use a PvStream object to acquire images from a GigE Vision or
// USB3 Vision device.
//

#include <opencv\cv.hpp> //BIBLIOTECA DO OPENCV
#include <PvSampleUtils.h>
#include <PvDevice.h>
#include <PvDeviceGEV.h>
#include <PvDeviceU3V.h>
#include <PvStream.h>
#include <PvStreamGEV.h>
#include <PvStreamU3V.h>
#include <PvBuffer.h>
#include <typeinfo>

#include <list>
typedef std::list<PvBuffer *> BufferList;

PV_INIT_SIGNAL_HANDLER();

#define BUFFER_COUNT ( 16 )

///
/// Function Prototypes
///
PvDevice *ConnectToDevice( const PvString &aConnectionID );
PvStream *OpenStream( const PvString &aConnectionID );
void ConfigureStream( PvDevice *aDevice, PvStream *aStream );
void CreateStreamBuffers( PvDevice *aDevice, PvStream *aStream, BufferList *aBufferList );
void AcquireImages( PvDevice *aDevice, PvStream *aStream );
void FreeStreamBuffers( BufferList *aBufferList );

// VARI�VEIS CRIADAS PARA TESTE
cv::Mat joinedFrame; //CARCA�A DE ARRAY PARA CONCATENAR
unsigned int totalFrames = 100; //N�MERO DE VEZES QUE A C�MERA ADQUIRE ANTE DE SOLTAR A IMAGEM
unsigned int currentFrame = 0; //VARI�VEL INCREMENTAL PARA CONTAGEM
bool imageTaken = false; //BOOLEAN PARA ENCERRAMENTO DO C�DIGO

#include <ctime>
#include <string>

//
// Main function
//
int main()
{

    PvDevice *lDevice = NULL;
    PvStream *lStream = NULL;
    BufferList lBufferList;

    PV_SAMPLE_INIT();

    cout << "PvStreamSample:" << endl << endl;

    PvString lConnectionID;
    if ( PvSelectDevice( &lConnectionID ) )
    {
        lDevice = ConnectToDevice( lConnectionID );
        if ( NULL != lDevice )
        {
            lStream = OpenStream( lConnectionID );
            if ( NULL != lStream )
            {
                ConfigureStream( lDevice, lStream );
                CreateStreamBuffers( lDevice, lStream, &lBufferList );
                AcquireImages( lDevice, lStream );
                FreeStreamBuffers( &lBufferList );
                
                // Close the stream
                cout << "Closing stream" << endl;
                lStream->Close();
                PvStream::Free( lStream );    
            }

            // Disconnect the device
            cout << "Disconnecting device" << endl;
            lDevice->Disconnect();
            PvDevice::Free( lDevice );
        }
    }

    cout << endl;
    cout << "<press a key to exit>" << endl;
    PvWaitForKeyPress();

    PV_SAMPLE_TERMINATE();

    return 0;
}

PvDevice *ConnectToDevice( const PvString &aConnectionID )
{
    PvDevice *lDevice;
    PvResult lResult;

    // Connect to the GigE Vision or USB3 Vision device
    cout << "Connecting to device." << endl;
    lDevice = PvDevice::CreateAndConnect( aConnectionID, &lResult );
    if ( lDevice == NULL )
    {
        cout << "Unable to connect to device." << endl;
    }

    return lDevice;
}

PvStream *OpenStream( const PvString &aConnectionID )
{
    PvStream *lStream;
    PvResult lResult;

    // Open stream to the GigE Vision or USB3 Vision device
    cout << "Opening stream from device." << endl;
    lStream = PvStream::CreateAndOpen( aConnectionID, &lResult );
    if ( lStream == NULL )
    {
        cout << "Unable to stream from device." << endl;
    }

    return lStream;
}

void ConfigureStream( PvDevice *aDevice, PvStream *aStream )
{
    // If this is a GigE Vision device, configure GigE Vision specific streaming parameters
    PvDeviceGEV* lDeviceGEV = dynamic_cast<PvDeviceGEV *>( aDevice );
    if ( lDeviceGEV != NULL )
    {
        PvStreamGEV *lStreamGEV = static_cast<PvStreamGEV *>( aStream );

        // Negotiate packet size
        lDeviceGEV->NegotiatePacketSize();

        // Configure device streaming destination
        lDeviceGEV->SetStreamDestination( lStreamGEV->GetLocalIPAddress(), lStreamGEV->GetLocalPort() );
    }
}

void CreateStreamBuffers( PvDevice *aDevice, PvStream *aStream, BufferList *aBufferList )
{
    // Reading payload size from device
    uint32_t lSize = aDevice->GetPayloadSize();

    // Use BUFFER_COUNT or the maximum number of buffers, whichever is smaller
    uint32_t lBufferCount = ( aStream->GetQueuedBufferMaximum() < BUFFER_COUNT ) ? 
        aStream->GetQueuedBufferMaximum() :
        BUFFER_COUNT;

    // Allocate buffers
    for ( uint32_t i = 0; i < lBufferCount; i++ )
    {
        // Create new buffer object
        PvBuffer *lBuffer = new PvBuffer;

        // Have the new buffer object allocate payload memory
        lBuffer->Alloc( static_cast<uint32_t>( lSize ) );
        
        // Add to external list - used to eventually release the buffers
        aBufferList->push_back( lBuffer );
    }
    
    // Queue all buffers in the stream
    BufferList::iterator lIt = aBufferList->begin();
    while ( lIt != aBufferList->end() )
    {
        aStream->QueueBuffer( *lIt );
        lIt++;
    }
}

void AcquireImages( PvDevice *aDevice, PvStream *aStream )
{
    // Get device parameters need to control streaming
    PvGenParameterArray *lDeviceParams = aDevice->GetParameters();

    // Map the GenICam AcquisitionStart and AcquisitionStop commands
    PvGenCommand *lStart = dynamic_cast<PvGenCommand *>( lDeviceParams->Get( "AcquisitionStart" ) );
    PvGenCommand *lStop = dynamic_cast<PvGenCommand *>( lDeviceParams->Get( "AcquisitionStop" ) );

    // Get stream parameters
    PvGenParameterArray *lStreamParams = aStream->GetParameters();

    // Map a few GenICam stream stats counters
    PvGenFloat *lFrameRate = dynamic_cast<PvGenFloat *>( lStreamParams->Get( "AcquisitionRate" ) );
    PvGenFloat *lBandwidth = dynamic_cast<PvGenFloat *>( lStreamParams->Get( "Bandwidth" ) );

    // Enable streaming and send the AcquisitionStart command
    cout << "Enabling streaming and sending AcquisitionStart command." << endl;
    aDevice->StreamEnable();
    lStart->Execute();

    char lDoodle[] = "|\\-|-/";
    int lDoodleIndex = 0;
    double lFrameRateVal = 0.0;
    double lBandwidthVal = 0.0;

    // Acquire images until the user instructs us to stop.
    cout << endl << "<press a key to stop streaming>" << endl;
	while (!PvKbHit() && imageTaken == false)   //INTERROMPE O PROGRAMA AO PRESSIONAR TECLAS OU TIRAR A FOTO
    {
        PvBuffer *lBuffer = NULL;
        PvResult lOperationResult;

        // Retrieve next buffer
        PvResult lResult = aStream->RetrieveBuffer( &lBuffer, &lOperationResult, 1000 );

        if ( lResult.IsOK() )
        {
            if ( lOperationResult.IsOK() )
            {
                PvPayloadType lType;

                //
                // We now have a valid buffer. This is where you would typically process the buffer.
                // -----------------------------------------------------------------------------------------
                // ...

                lFrameRate->GetValue( lFrameRateVal );
                lBandwidth->GetValue( lBandwidthVal );

                // If the buffer contains an image, display width and height.
                uint32_t lWidth = 0, lHeight = 0;
                lType = lBuffer->GetPayloadType();

                //cout << fixed << setprecision( 1 );
                //cout << lDoodle[ lDoodleIndex ];
                //cout << " BlockID: " << uppercase << hex << setfill( '0' ) << setw( 16 ) << lBuffer->GetBlockID();
                if ( lType == PvPayloadTypeImage )
                {
                    // Get image specific buffer interface.
                    PvImage *lImage = lBuffer->GetImage();
					
                    // Read width, height.
                    lWidth = lImage->GetWidth();
                    lHeight = lImage->GetHeight();

					unsigned char * data = lBuffer->GetImage()->GetDataPointer();

					cv::Mat workImage;

					lImage->Alloc(lWidth, lHeight, PvPixelRGB8);
					// Get image data pointer so we can pass it to CV::MAT container
					unsigned char *img = lImage->GetDataPointer();
					// Copy/convert Pleora Vision image pointer to cv::Mat container
					
					//-----------------------------------------------------------------------------------//
					// https://stackoverflow.com/questions/34218482/pvbuffer-pleora-sdk-to-opencv-buffer //
					cv::Mat lframe(lHeight, lWidth, CV_8UC1, img, cv::Mat::AUTO_STEP); //CONVERTE O BUFFER EM UM ARRAY
  					lframe.copyTo(workImage); //CONVERTE O BUFFER EM UM ARRAY
					//-----------------------------------------------------------------------------------//
			
					if (cv::countNonZero(workImage) < 1) { //TESTA QUANTIDADE DE N�O-ZEROS (PIXELS PRETOS)

						// IGNORA MATRIZES TODAS EM PRETO AT� O STREAM COME�AR (POR ALGUM MOTIVO ELE CAPTURAVA ALGUMAS LINHAS TODAS EM PRETO ANTES DE AQUECER O SISTEMA)
					}
					else if ( currentFrame == 0 ) //SE O FRAME ATUAL � O PRIMEIRO (N�MERO 0)
					{
						currentFrame++; // INCREMENTA
						joinedFrame = workImage; // COLOCA O ARRAY PEGO NA VARI�VEL JOINEDFRAME
						cout << "Frame: " << currentFrame << " de " << totalFrames << endl; // MOSTRA O PROGRESSO
					}
					else if ( currentFrame++ < totalFrames ) // SE O FRAME ATUAL � MAIOR QUE ZERO E MENOR QUE O M�XIMO
					{
						cout << "Frame: " << currentFrame << " de " << totalFrames << endl; // MOSTRA O PROGRESSO 
						cv::vconcat(joinedFrame, workImage, joinedFrame); // CONCATENA O FRAME ANTERIOR COM O ATUAL
					}
					else
					{
						currentFrame = 0; // QUANDO ROMPE, ZERA A VARI�VEL
						//cv::imshow("Display window", joinedFrame);
						//cv::waitKey(1);


						// PEGA A HORA PARA SALVAR A IMAGEM COM O NOME
						time_t now = time(0);
						tm *ltm = localtime(&now);

						stringstream ss;
						ss << 1 + ltm->tm_hour << "_" << 1 + ltm->tm_min << 1 + ltm->tm_sec<< ".png";
						// PEGA A HORA PARA SALVAR A IMAGEM COM O NOME

						cv::imwrite(ss.str(), joinedFrame); // SALVA A IMAGEM 
						imageTaken = true; // SETA O BOOLEAN PARA ENCERRAR O PROGRAMA
						
					}

                    //cout << "  W: " << dec << lWidth << " H: " << lHeight;
                }
                else 
                {
                    //cout << " (buffer does not contain image)";
                }
                //cout << "  " << lFrameRateVal << " FPS  " << ( lBandwidthVal / 1000000.0 ) << " Mb/s   \r";

            }
            else
            {
                // Non OK operational result
                cout << lDoodle[ lDoodleIndex ] << " " << lOperationResult.GetCodeString().GetAscii() << "\r";
            }

            // Re-queue the buffer in the stream object
            aStream->QueueBuffer( lBuffer );
        }
        else
        {
            // Retrieve buffer failure
            cout << lDoodle[ lDoodleIndex ] << " " << lResult.GetCodeString().GetAscii() << "\r";
        }

        ++lDoodleIndex %= 6;
    }

    PvGetChar(); // Flush key buffer for next stop.
    cout << endl << endl;

    // Tell the device to stop sending images.
    cout << "Sending AcquisitionStop command to the device" << endl;
    lStop->Execute();

    // Disable streaming on the device
    cout << "Disable streaming on the controller." << endl;
    aDevice->StreamDisable();

    // Abort all buffers from the stream and dequeue
    cout << "Aborting buffers still in stream" << endl;
    aStream->AbortQueuedBuffers();
    while ( aStream->GetQueuedBufferCount() > 0 )
    {
        PvBuffer *lBuffer = NULL;
        PvResult lOperationResult;

        aStream->RetrieveBuffer( &lBuffer, &lOperationResult );
    }
}

void FreeStreamBuffers( BufferList *aBufferList )
{
    // Go through the buffer list
    BufferList::iterator lIt = aBufferList->begin();
    while ( lIt != aBufferList->end() )
    {
        delete *lIt;
        lIt++;
    }

    // Clear the buffer list 
    aBufferList->clear();
}


