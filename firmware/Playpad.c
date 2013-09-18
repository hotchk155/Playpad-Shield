//////////////////////////////////////////////////////////////////////
// DUAL USB HOST FOR NOVATION LAUNCHPAD 
//////////////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//
#include "Playpad.h"
#include "USBHostGenericDrv.h"


//
// TYPE DEFS
//
typedef struct {
	VOS_HANDLE hUSBHOST;
	VOS_HANDLE hUSBHOSTGENERIC;
	unsigned char uchDeviceNumberBase;
	unsigned char uchDeviceNumber;
	unsigned char uchActivityLed;
	unsigned char uchChannel;
} HOST_PORT_DATA;
	
//
// VARIABLE DECL
//
vos_tcb_t *tcbSetup;
vos_tcb_t *tcbHostA;
vos_tcb_t *tcbHostB;
vos_tcb_t *tcbSPISlave;

vos_semaphore_t setupSem;

VOS_HANDLE hGpioA;
VOS_HANDLE hSPISlave;

unsigned char buf[512];
unsigned short pBuf = 0;


HOST_PORT_DATA PortA = {0};
HOST_PORT_DATA PortB = {0};
uint8 gpioAOutput = 0;

#define LED_SIGNAL 0x40	
#define LED_ACTIVITY 0x80
#define LED_USB_B 0x02
#define LED_USB_A 0x04

	
//
// FUNCTION PROTOTYPES
//
void Setup();
void RunHostPort(HOST_PORT_DATA *pHostData);
void RunSPISlave();

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
	//spislave_context_t spiSlaveContext;

	// Kernel initialisation
	vos_init(50, VOS_TICK_INTERVAL, VOS_NUMBER_DEVICES);
	vos_set_clock_frequency(VOS_48MHZ_CLOCK_FREQUENCY);
	vos_set_idle_thread_tcb_size(512);

	// Set up the io multiplexing
	iomux_setup();

	//spiSlaveContext.slavenumber = SPI_SLAVE_0;
	//spiSlaveContext.buffer_size = 64;
	//spislave_init(VOS_DEV_SPISLAVE, &spiSlaveContext);
	
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
	PortA.uchChannel = 0;
	PortA.uchDeviceNumberBase = VOS_DEV_USBHOST_1;
	PortA.uchDeviceNumber = VOS_DEV_USBHOSTGENERIC_1;
	
	PortB.uchActivityLed = LED_USB_B;
	PortB.uchChannel = 1;
	PortB.uchDeviceNumberBase = VOS_DEV_USBHOST_2;
	PortB.uchDeviceNumber = VOS_DEV_USBHOSTGENERIC_2;
	
	
	// Initializes our device with the device manager.
	
	tcbSetup = vos_create_thread_ex(10, 1024, Setup, "Setup", 0);
	tcbHostA = vos_create_thread_ex(20, 1024, RunHostPort, "RunHostPortA", sizeof(HOST_PORT_DATA*), &PortA);
	tcbHostB = vos_create_thread_ex(20, 1024, RunHostPort, "RunHostPortB", sizeof(HOST_PORT_DATA*), &PortB);
//	tcbSPISlave = vos_create_thread_ex(15, 1024, RunSPISlave, "RunSPISlave", 0);

	vos_init_semaphore(&setupSem,0);
	
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

	//hSPISlave = vos_dev_open(VOS_DEV_SPISLAVE);
	
	// Open up the base level drivers
	hGpioA  	= vos_dev_open(VOS_DEV_GPIO_A);
	

	gpio_iocb.ioctl_code = VOS_IOCTL_GPIO_SET_MASK;
	gpio_iocb.value = 0b11000110;
	vos_dev_ioctl(hGpioA, &gpio_iocb);
	setGpioA(0b11000110,0);
/*
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
	ss_iocb.set.param = SPI_SLAVE_MODE_UNMANAGED;
	vos_dev_ioctl(hSPISlave, &ss_iocb);


	ss_iocb.ioctl_code = VOS_IOCTL_COMMON_ENABLE_DMA;
	ss_iocb.set.param = DMA_ACQUIRE_AS_REQUIRED;
	vos_dev_ioctl(hSPISlave, &ss_iocb);

	*/
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

	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);

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

				hc_iocb.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_VID_PID;
				hc_iocb.handle.dif = ifDev;
				hc_iocb.get = &hc_iocb_vid_pid;
				
				// attach the Launchpad device to the USB host device
				pHostData->hUSBHOSTGENERIC = vos_dev_open(pHostData->uchDeviceNumber);
				genericAtt.hc_handle = pHostData->hUSBHOST;
				genericAtt.ifDev = ifDev;
				generic_iocb.ioctl_code = VOS_IOCTL_USBHOSTGENERIC_ATTACH;
				generic_iocb.set.att = &genericAtt;
				if (vos_dev_ioctl(pHostData->hUSBHOSTGENERIC, &generic_iocb) == USBHOSTGENERIC_OK)
				{
					// Turn on the LED for this port
					setGpioA(pHostData->uchActivityLed, pHostData->uchActivityLed);
					
					// now we loop until the launchpad is detached
					while(1)
					{
						int pos = 0;
						byte param = 1;
						
						// receiving data from the launchpad
						status = vos_dev_read(pHostData->hUSBHOSTGENERIC, buf, 64, &num_bytes);
						if(status != USBHOSTGENERIC_OK)
							break;
						vos_dev_write(pHostData->hUSBHOSTGENERIC, buf, num_bytes, &num_bytes);
						
/*							
						setGpioA(pHostData->uchActivityLed, 0);
						setGpioA(LED_SIGNAL,LED_SIGNAL);

						// prepare to pass the data to SPI slave
						msg[0] = pHostData->uchChannel;
						if(buf[0]&0x80)
						{
							msg[1] = buf[0];
							msg[2] = buf[1];
							msg[3] = buf[2];
						}
						else
						{
							msg[1] = 0;
							msg[2] = buf[0];
							msg[3] = buf[1];
						}							
						status = vos_dev_write(hSPISlave, msg, 4, &num_bytes);		
						setGpioA(LED_SIGNAL,0);
						
						setGpioA(pHostData->uchActivityLed, pHostData->uchActivityLed);
*/						
					}					
					// Turn off the LED
					setGpioA(pHostData->uchActivityLed, 0);
				}
				vos_dev_close(pHostData->hUSBHOSTGENERIC);
			}
		}
	}
}	

/*	
void SPISlave()
{
	unsigned short numRead;
	common_ioctl_cb_t spi_iocb;
	unsigned short dataAvail = 0;
	unsigned char testdata;

	vos_wait_semaphore(&setupSem);

	while(1)
	{
		// get bytes available...
		spi_iocb.ioctl_code = VOS_IOCTL_COMMON_GET_RX_QUEUE_STATUS;
		vos_dev_ioctl(hSPI_SLAVE_1, &spi_iocb);
		dataAvail = spi_iocb.get.queue_stat; // How much data to read?

		if (dataAvail)
		{
			if (dataAvail > (sizeof(buf) - pBuf))
				dataAvail = (sizeof(buf) - pBuf);

			vos_lock_mutex(&mBufAccess);
			vos_dev_read(hSPI_SLAVE_1, &buf[pBuf], dataAvail, &numRead);
			pBuf += numRead;
			vos_unlock_mutex(&mBufAccess);
			vos_signal_semaphore(&dataSem);
		}
	}
}
*/
	
////////////////////////////////////////////////////////////////////
//	
// THREAD TO RUN THE SPI INTERFACE
//
////////////////////////////////////////////////////////////////////
void RunSPISlave()
{
	uint8 buf[4];
	uint16 num_bytes;
	uint16 status;
	int iPutPos = 5;

	// wait for setup to complete
	vos_wait_semaphore(&setupSem);
	vos_signal_semaphore(&setupSem);
	

	// byte 1 is usb port number
	// byte 2 is midi command or 0 for none
	// byte 3 is midi param 1
	// byte 4 is midi param 2
	
	for(;;)
	{
		uint8 ch;
		status = vos_dev_read(hSPISlave, &ch, 1, &num_bytes);				
		if(0 == status)
		{
			if(ch == 0xff)
			{
				iPutPos = 0;
			}
			else if(iPutPos < 4)
			{
				buf[iPutPos++] = ch;
				if(iPutPos == 4)
				{
					setGpioA(LED_ACTIVITY, LED_ACTIVITY);
					if(buf[0] == 1)
					{
						if(buf[1])
							vos_dev_write(PortB.hUSBHOSTGENERIC, &buf[1], 3, &num_bytes);
						else
							vos_dev_write(PortB.hUSBHOSTGENERIC, &buf[2], 2, &num_bytes);
					}
					else 
					{
						if(buf[1])
							vos_dev_write(PortA.hUSBHOSTGENERIC, &buf[1], 3, &num_bytes);
						else
							vos_dev_write(PortA.hUSBHOSTGENERIC, &buf[2], 2, &num_bytes);
					}
					setGpioA(LED_ACTIVITY, 0);
				}
			}
		}
	}
}

	
