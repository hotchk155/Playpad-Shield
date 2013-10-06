//////////////////////////////////////////////////////////////////////
// DUAL USB HOST FOR NOVATION LAUNCHPAD 
//////////////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//
#include "Playpad.h"
#include "USBHostGenericDrv.h"

//
// VARIABLE DECL
//
vos_tcb_t *tcbSetup;
vos_tcb_t *tcbHostA;
vos_tcb_t *tcbHostB;
vos_tcb_t *tcbRunSPISend;
vos_tcb_t *tcbRunSPIReceive;
vos_tcb_t *tcbRunUSBSend;


vos_semaphore_t setupSem;


//
// TYPE DEFS
//
typedef struct {
	VOS_HANDLE hUSBHOST;
	VOS_HANDLE hUSBHOSTGENERIC;
	unsigned char uchDeviceNumberBase;
	unsigned char uchDeviceNumber;
	unsigned char uchActivityLed;
	unsigned char uchMsg;
} HOST_PORT_DATA;
	
VOS_HANDLE hGpioA;
VOS_HANDLE hSPISlave;
HOST_PORT_DATA PortA = {0};
HOST_PORT_DATA PortB = {0};
uint8 gpioAOutput = 0;

#define LED_SIGNAL 0x40	
#define LED_ACTIVITY 0x80
#define LED_USB_B 0x02
#define LED_USB_A 0x04

void setGpioA(uint8 mask, uint8 data);
	
////////////////////////////////////////////////////////////////////////////////
//
// SYNCRONISED CIRCULAR FIFO BUFFER OF 32-BIT MESSAGES
//
////////////////////////////////////////////////////////////////////////////////

#define SZ_FIFO 50	
typedef struct {
	uint32 data[SZ_FIFO];
	uint8 head;
	uint8 tail;
	vos_semaphore_t semRead;
	vos_semaphore_t semWrite;
} FIFO_TYPE;
	
#define FIFO_INC(d) ((d)<SZ_FIFO-1)?(d+1):0
	
////////////////////////////////////////////////////////////////////////////////
void fifo_init(FIFO_TYPE *pfifo)
{
	memset(pfifo, 0, sizeof(FIFO_TYPE));
	vos_init_semaphore(&pfifo->semRead, 0);
	vos_init_semaphore(&pfifo->semWrite, SZ_FIFO);
}
	
////////////////////////////////////////////////////////////////////////////////
void fifo_write(FIFO_TYPE *pfifo, uint8 *pmsg)
{	
	vos_wait_semaphore(&pfifo->semWrite);	// wait until we have space for the write
	VOS_ENTER_CRITICAL_SECTION;				
	pfifo->data[pfifo->head] = *((uint32*)pmsg); // write the message to FIFO to head pos
	pfifo->head = FIFO_INC(pfifo->head);	// advance head position
	VOS_EXIT_CRITICAL_SECTION;
	vos_signal_semaphore(&pfifo->semRead);	// signal a message available
}

////////////////////////////////////////////////////////////////////////////////
uint32 fifo_read(FIFO_TYPE *pfifo, uint8 *pmsg)
{
	uint32 result;
	vos_wait_semaphore(&pfifo->semRead);	// wait until we have data to read
	VOS_ENTER_CRITICAL_SECTION;
	*((uint32*)pmsg) = pfifo->data[pfifo->tail];		// read value from tail position
	pfifo->tail = FIFO_INC(pfifo->tail);	// advance tail position
	VOS_EXIT_CRITICAL_SECTION;
	vos_signal_semaphore(&pfifo->semWrite);	// signal space available to queue a message
	return result;
}

#define MSG_PORTA 0x40
#define MSG_PORTB 0x80
	
//
// FUNCTION PROTOTYPES
//
void Setup();
void RunHostPort(HOST_PORT_DATA *pHostData);
void RunSPISend();
void RunSPIReceive();
void RunUSBSend();
	
FIFO_TYPE stSPIReadFIFO;
FIFO_TYPE stSPIWriteFIFO;
	
//////////////////////////////////////////////////////////////////////
//
// IOMUX SETUP
// ALWAYS VNC2 32PIN PACKAGE
//
//////////////////////////////////////////////////////////////////////
void iomux_setup()
{
	vos_iomux_define_bidi( 199, 	IOMUX_IN_DEBUGGER, IOMUX_OUT_DEBUGGER);
	
	// GPIOS
	vos_iomux_define_output(12, 	IOMUX_OUT_GPIO_PORT_A_1);
	vos_iomux_define_output(14, 	IOMUX_OUT_GPIO_PORT_A_2);
	vos_iomux_define_input(	15, 	IOMUX_IN_GPIO_PORT_A_3);
	vos_iomux_define_output(25, 	IOMUX_OUT_GPIO_PORT_A_6);
	vos_iomux_define_output(26, 	IOMUX_OUT_GPIO_PORT_A_7);
	
	// UART
	vos_iomux_define_output(23, 	IOMUX_OUT_UART_TXD);
	vos_iomux_define_input(	24, 	IOMUX_IN_UART_RXD);
	
	// SPI SLAVE 0
	vos_iomux_define_input(	29, 	IOMUX_IN_SPI_SLAVE_0_CLK);
	vos_iomux_define_input(	30, 	IOMUX_IN_SPI_SLAVE_0_MOSI);
	vos_iomux_define_output(31, 	IOMUX_OUT_SPI_SLAVE_0_MISO);
	vos_iomux_define_input(	32, 	IOMUX_IN_SPI_SLAVE_0_CS);
}

////////////////////////////////////////////////////////////////////////////////
// SET GPIO A
////////////////////////////////////////////////////////////////////////////////
void setGpioA(uint8 mask, uint8 data)
{
	gpioAOutput &= ~mask;
	gpioAOutput |= data;
	vos_dev_write(hGpioA,&gpioAOutput,1,NULL);
}
//////////////////////////////////////////////////////////////////////
//
// MAIN
//
//////////////////////////////////////////////////////////////////////
void main(void)
{
	usbhost_context_t usbhostContext;
	gpio_context_t gpioCtx;
	spislave_context_t spiSlaveContext;

	// Kernel initialisation
	vos_init(50, VOS_TICK_INTERVAL, VOS_NUMBER_DEVICES);
	vos_set_clock_frequency(VOS_48MHZ_CLOCK_FREQUENCY);
	vos_set_idle_thread_tcb_size(512);

	// Set up the io multiplexing
	iomux_setup();

	spiSlaveContext.slavenumber = SPI_SLAVE_0;
	spiSlaveContext.buffer_size = 64;
	spislave_init(VOS_DEV_SPISLAVE, &spiSlaveContext);
	
	// Initialise GPIO port A
	gpioCtx.port_identifier = GPIO_PORT_A;
	gpio_init(VOS_DEV_GPIO_A,&gpioCtx); 
	
	// Initialise USB Host devices
	usbhostContext.if_count = 8;
	usbhostContext.ep_count = 16;
	usbhostContext.xfer_count = 2;
	usbhostContext.iso_xfer_count = 2;
	usbhost_init(VOS_DEV_USBHOST_1, VOS_DEV_USBHOST_2, &usbhostContext);
	

	// Initialise the USB function device
	usbhostGeneric_init(VOS_DEV_USBHOSTGENERIC_1);
	usbhostGeneric_init(VOS_DEV_USBHOSTGENERIC_2);

	PortA.uchActivityLed = LED_USB_A;
	PortA.uchMsg = MSG_PORTA;
	PortA.uchDeviceNumberBase = VOS_DEV_USBHOST_1;
	PortA.uchDeviceNumber = VOS_DEV_USBHOSTGENERIC_1;
	
	PortB.uchActivityLed = LED_USB_B;
	PortB.uchMsg = MSG_PORTB;
	PortB.uchDeviceNumberBase = VOS_DEV_USBHOST_2;
	PortB.uchDeviceNumber = VOS_DEV_USBHOSTGENERIC_2;
	

	vos_init_semaphore(&setupSem,0);
	fifo_init(&stSPIReadFIFO);
	fifo_init(&stSPIWriteFIFO);
	
	
	tcbSetup = vos_create_thread_ex(10, 1024, Setup, "Setup", 0);
	tcbHostA = vos_create_thread_ex(20, 1024, RunHostPort, "RunHostPortA", sizeof(HOST_PORT_DATA*), &PortA);
	tcbHostB = vos_create_thread_ex(20, 1024, RunHostPort, "RunHostPortB", sizeof(HOST_PORT_DATA*), &PortB);
	tcbRunSPISend = vos_create_thread_ex(20, 1024, RunSPISend, "RunSPISend", 0);
	tcbRunSPIReceive = vos_create_thread_ex(20, 1024, RunSPIReceive, "RunSPIReceive", 0);	
	tcbRunUSBSend = vos_create_thread_ex(20, 1024, RunUSBSend, "RunUSBSend", 0);

	
	// And start the thread
	vos_start_scheduler();

main_loop:
	goto main_loop;
}
	
//////////////////////////////////////////////////////////////////////
//
// APPLICATION SETUP THREAD FUNCTION
//
//////////////////////////////////////////////////////////////////////
void Setup()
{
	common_ioctl_cb_t spis_iocb;
	usbhostGeneric_ioctl_t generic_iocb;
	gpio_ioctl_cb_t gpio_iocb;
	common_ioctl_cb_t uart_iocb;
	unsigned char uchLeds;
	common_ioctl_cb_t ss_iocb;

	hSPISlave = vos_dev_open(VOS_DEV_SPISLAVE);
	
	// Open up the base level drivers
	hGpioA  	= vos_dev_open(VOS_DEV_GPIO_A);
	

	gpio_iocb.ioctl_code = VOS_IOCTL_GPIO_SET_MASK;
	gpio_iocb.value = 0b11000110;
	vos_dev_ioctl(hGpioA, &gpio_iocb);
	setGpioA(0b11000110,0);

	ss_iocb.ioctl_code = VOS_IOCTL_SPI_SLAVE_SCK_CPHA;
	ss_iocb.set.param = SPI_SLAVE_SCK_CPHA_0;
	vos_dev_ioctl(hSPISlave, &ss_iocb);

	ss_iocb.ioctl_code = VOS_IOCTL_SPI_SLAVE_SCK_CPOL;
	ss_iocb.set.param = SPI_SLAVE_SCK_CPOL_0;
	vos_dev_ioctl(hSPISlave, &ss_iocb);
	
	ss_iocb.ioctl_code = VOS_IOCTL_SPI_SLAVE_DATA_ORDER;
	ss_iocb.set.param = SPI_SLAVE_DATA_ORDER_MSB;
	vos_dev_ioctl(hSPISlave, &ss_iocb);
	
	ss_iocb.ioctl_code = VOS_IOCTL_SPI_SLAVE_SET_MODE;
	ss_iocb.set.param = SPI_SLAVE_MODE_FULL_DUPLEX;
	vos_dev_ioctl(hSPISlave, &ss_iocb);

	ss_iocb.ioctl_code = VOS_IOCTL_SPI_SLAVE_SET_ADDRESS;
	ss_iocb.set.param = 0;
	vos_dev_ioctl(hSPISlave, &ss_iocb);

	ss_iocb.ioctl_code = VOS_IOCTL_COMMON_ENABLE_DMA;
	ss_iocb.set.param = DMA_ACQUIRE_AND_RETAIN;
	vos_dev_ioctl(hSPISlave, &ss_iocb);
	
	// Release other application threads
	vos_signal_semaphore(&setupSem);
}

//////////////////////////////////////////////////////////////////////
//
// GET GPIO
//
//////////////////////////////////////////////////////////////////////
byte getGPIO()
{
	unsigned char gpio;
	vos_dev_read(hGpioA,&gpio,1,NULL);
	return gpio;
}
	
//////////////////////////////////////////////////////////////////////
//
// Get connect state
//
//////////////////////////////////////////////////////////////////////
unsigned char usbhost_connect_state(VOS_HANDLE hUSB)
{
	unsigned char connectstate = PORT_STATE_DISCONNECTED;
	usbhost_ioctl_cb_t hc_iocb;

	if (hUSB)
	{
		hc_iocb.ioctl_code = VOS_IOCTL_USBHOST_GET_CONNECT_STATE;
		hc_iocb.get = &connectstate;
		vos_dev_ioctl(hUSB, &hc_iocb);
	}

	return connectstate;
}


//////////////////////////////////////////////////////////////////////
//
// RUN USB HOST PORT
//
//////////////////////////////////////////////////////////////////////
void RunHostPort(HOST_PORT_DATA *pHostData)
{
	int i;
	int midiParam;
	unsigned char status;
	unsigned char buf[64];	
	unsigned char msg[4];
	unsigned short num_bytes;
	unsigned int handle;
	usbhostGeneric_ioctl_t generic_iocb;
	usbhostGeneric_ioctl_cb_attach_t genericAtt;
	usbhost_device_handle_ex ifDev;
	usbhost_ioctl_cb_t hc_iocb;
	usbhost_ioctl_cb_vid_pid_t hc_iocb_vid_pid;
	gpio_ioctl_cb_t gpio_iocb;
	VOS_HANDLE hUSB;

	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);
	
	// Open the base USB Host driver
	pHostData->hUSBHOST = vos_dev_open(pHostData->uchDeviceNumberBase);
	
	// loop forever
	while(1)
	{
		// is the device enumerated on this port?
		if (usbhost_connect_state(pHostData->hUSBHOST) == PORT_STATE_ENUMERATED)
		{
			// user ioctl to find first hub device
			hc_iocb.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_NEXT_HANDLE;
			hc_iocb.handle.dif = NULL;
			hc_iocb.set = NULL;
			hc_iocb.get = &ifDev;
			if (vos_dev_ioctl(pHostData->hUSBHOST, &hc_iocb) == USBHOST_OK)
			{
				// query the device VID/PID
				hc_iocb.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_VID_PID;
				hc_iocb.handle.dif = ifDev;
				hc_iocb.get = &hc_iocb_vid_pid;
				
				// Load the function driver
				hUSB = vos_dev_open(pHostData->uchDeviceNumber);
				
				// Attach the function driver to the base driver
				genericAtt.hc_handle = pHostData->hUSBHOST;
				genericAtt.ifDev = ifDev;
				generic_iocb.ioctl_code = VOS_IOCTL_USBHOSTGENERIC_ATTACH;
				generic_iocb.set.att = &genericAtt;
				if (vos_dev_ioctl(hUSB, &generic_iocb) == USBHOSTGENERIC_OK)
				{					
					// prepare the message header
					msg[0] = pHostData->uchMsg;
					msg[1] = 0;
					midiParam = 0;
					
					// Turn on the LED for this port
					setGpioA(pHostData->uchActivityLed, pHostData->uchActivityLed);

					// flag that the port is attached
					VOS_ENTER_CRITICAL_SECTION;
					pHostData->hUSBHOSTGENERIC = hUSB;
					VOS_EXIT_CRITICAL_SECTION;
										
					// now we loop until the launchpad is detached
					while(1)
					{
						// listen for data from launchpad
						uint16 result = vos_dev_read(hUSB, buf, 64, &num_bytes);
						if(0 != result)
							break; // break when the launchpad is detached
							
						// convert the MIDI messages into 4 byte packets to be sent
						// over SPI to the Arduino
						for(i=0;i<num_bytes;++i)
						{
							if(buf[i]&0x80)	// MIDI status
							{
								msg[1] = buf[i];	// store MIDI status
								midiParam = 0;		// get ready to read first MIDI param
							}
							else if(midiParam == 0)
							{
								msg[2] = buf[i];	// read first MIDI param
								midiParam = 1;		// prepare for the second
							}
							else
							{
								msg[3] = buf[i];	// read second MIDI param
								midiParam = 0;		// prepare to roll back to first
								if(msg[1])			// if we have a status byte...
								{
									fifo_write(&stSPIWriteFIFO, msg); // queue to SPI
								}
							}
						}
					}					
					
					// flag that the port is no longer attached
					VOS_ENTER_CRITICAL_SECTION;
					pHostData->hUSBHOSTGENERIC = NULL;
					VOS_EXIT_CRITICAL_SECTION;
					
					// turn off the activity LED
					setGpioA(pHostData->uchActivityLed, 0);
				}
				
				// close the function driver
				vos_dev_close(hUSB);
			}
		}
	}
}	

////////////////////////////////////////////////////////////////////
//	
// RECEIVE DATA FROM SPI
//
////////////////////////////////////////////////////////////////////
void RunSPIReceive()
{
	unsigned short bytes_read;
	uint8 msg[4];
	
	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);

	while(1)
	{
		// wait for a 4 byte message and place it in queue to USB 
		if(0==vos_dev_read(hSPISlave, (char*)msg, 4, &bytes_read) && (4==bytes_read))
			fifo_write(&stSPIReadFIFO, msg);
	}
}

////////////////////////////////////////////////////////////////////
//	
// SEND DATA TO SPI
//
////////////////////////////////////////////////////////////////////
void RunSPISend()
{
	unsigned short bytes_written;
	uint8 msg[4];
	
	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);

	while(1)
	{
		// wait for a message to be available from USB
		fifo_read(&stSPIWriteFIFO, msg);
		
		// pass it to SPI
		vos_dev_write(hSPISlave, (char*)msg, 4, &bytes_written);		
		setGpioA(LED_SIGNAL,0); 
		setGpioA(LED_SIGNAL,LED_SIGNAL|LED_ACTIVITY); 		
		
	}
}
	
////////////////////////////////////////////////////////////////////
//	
// SEND DATA TO USB
//
////////////////////////////////////////////////////////////////////
void RunUSBSend()
{
	unsigned char attached;
	uint8 msg[4];
	unsigned short bytes_written;
	VOS_HANDLE hUSB;
	
	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);

	while(1)
	{
		// wait for a message to be received from SPI
		fifo_read(&stSPIReadFIFO, msg);
		
		// get the port driver handle
		switch(msg[0]) 
		{
			case MSG_PORTA:
				VOS_ENTER_CRITICAL_SECTION;
				hUSB = PortA.hUSBHOSTGENERIC;
				VOS_EXIT_CRITICAL_SECTION;
				break;
			case MSG_PORTB:
				VOS_ENTER_CRITICAL_SECTION;
				hUSB = PortB.hUSBHOSTGENERIC;
				VOS_EXIT_CRITICAL_SECTION;
				break;
			default:
				hUSB = NULL;
				break;
		}		
		if(hUSB) // will be NULL unless the device is attached
			vos_dev_write(hUSB, &msg[1], 3, &bytes_written);
	}
}
