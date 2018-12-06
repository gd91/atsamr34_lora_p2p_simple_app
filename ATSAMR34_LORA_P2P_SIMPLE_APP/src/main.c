/** m16946 - ATSAMR34 LoRa P2P Simple Application -
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * This is a bare minimum user application template.
 *
 * For documentation of the board, go \ref group_common_boards "here" for a link
 * to the board-specific documentation.
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# Minimal main function that starts with a call to system_init()
 * -# Basic usage of on-board LED and button
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
 */
#include <asf.h>
#include "sio2host.h"
#include "stdio_serial.h"
#include "radio_interface.h"
#include "radio_driver_hal.h"
#include "aes_engine.h"
#include "sw_timer.h"
#include "system_init.h"
#include "lorawan.h"

/************************** macro definition ***********************************/
#define APP_DEBOUNCE_TIME   50	// button debounce time in ms
#define BUFFER_SIZE			50

/************************** Global variables ***********************************/
uint8_t buttonPressed = 0 ;
uint8_t buttonCounter = 0 ;
uint8_t buffer[BUFFER_SIZE] ;

/****************************** PROTOTYPES *************************************/
static void init(void) ;
static void configure_led(void) ;
static void configure_extint(void) ;
static void configure_radio(void) ;
static void configure_eic_callback(void) ;
static void extint_callback(void) ;
void print_menu(void) ;
void serial_data_handler(void) ;
void radio_transmit_uplink(uint8_t *data, uint16_t len) ;
void radio_enter_receive_mode(void) ;
void radio_exit_receive_mode(void) ;
SYSTEM_TaskStatus_t APP_TaskHandler(void) ;
void appData_callback(void *appHandle, appCbParams_t *appdata) ;
void print_array(uint8_t *array, uint8_t length) ;

/****************************** FUNCTIONS *************************************/
static void init(void)
{
	system_init() ;
	delay_init() ;
	board_init() ;
	configure_led() ;
	sio2host_init() ;
	INTERRUPT_GlobalInterruptEnable() ;
	// LoRaWAN Stack driver init
	HAL_RadioInit() ;
	AESInit() ;
	SystemTimerInit() ;
	Stack_Init() ;
	LORAWAN_Init(appData_callback, NULL) ;
}

void print_menu(void)
{
	printf("\r\n-- ATSAMR34 LoRa P2P Simple Application --\r\n") ;	
	printf("- Press SW0 button to transmit counter value [%d]\r\n", buttonCounter) ;
	printf("- Type any character to transmit over LoRa Radio\r\n") ;
}

int main (void)
{
	init() ;	
	configure_led() ;
	configure_extint() ;
	configure_eic_callback() ;
	
	configure_radio() ;
	radio_enter_receive_mode() ;
	
	print_menu() ;
	while (1) 
	{
		serial_data_handler() ;
		SYSTEM_RunTasks() ;
	}
}

static void configure_led(void)
{
	struct port_config pin_conf ;
	port_get_config_defaults(&pin_conf) ;
	pin_conf.direction  = PORT_PIN_DIR_OUTPUT ;
	port_pin_set_config(LED_0_PIN, &pin_conf) ;
	port_pin_set_output_level(LED_0_PIN, LED_0_INACTIVE) ;
}

static void configure_extint(void)
{
	struct extint_chan_conf eint_chan_conf;
	extint_chan_get_config_defaults(&eint_chan_conf);
	eint_chan_conf.gpio_pin           = BUTTON_0_EIC_PIN;
	eint_chan_conf.gpio_pin_mux       = BUTTON_0_EIC_MUX;
	eint_chan_conf.detection_criteria = EXTINT_DETECT_FALLING;
	eint_chan_conf.filter_input_signal = true;
	extint_chan_set_config(BUTTON_0_EIC_LINE, &eint_chan_conf);
}

static void configure_eic_callback(void)
{
	extint_register_callback(
		extint_callback,
		BUTTON_0_EIC_LINE,
		EXTINT_CALLBACK_TYPE_DETECT
	);
	extint_chan_enable_callback(BUTTON_0_EIC_LINE, EXTINT_CALLBACK_TYPE_DETECT);
}

static void extint_callback(void)
{
	/* Read the button level */
	if (port_pin_get_input_level(BUTTON_0_PIN) == BUTTON_0_ACTIVE)
	{
		/* Wait for button debounce time */
		delay_ms(APP_DEBOUNCE_TIME);
		/* Check whether button is in default state */
		while(port_pin_get_input_level(BUTTON_0_PIN) == BUTTON_0_ACTIVE)
		{
			delay_ms(500);
		}
		buttonPressed = true;
		/* Post task to application handler on button press */
		SYSTEM_PostTask(APP_TASK_ID);
	}
}

SYSTEM_TaskStatus_t APP_TaskHandler(void)
{
	if (buttonPressed == true)
	{
		buttonPressed = false ;
		buttonCounter++ ;
		if (buttonCounter > 255) buttonCounter = 0 ;
		printf("Button pressed %d times\r\n", buttonCounter) ;

		// exit receive mode
		radio_exit_receive_mode() ;
		
		// prepare and transmit buttonCounter
		buffer[0] = buttonCounter ;
		radio_transmit_uplink(buffer, 1) ;
	}
	return SYSTEM_TASK_SUCCESS;
}

void serial_data_handler(void)
{
	int rxChar ;
	char serialData ;
	/* verify if there was any character received*/
	if((-1) != (rxChar = sio2host_getchar_nowait()))
	{
		serialData = (char)rxChar;
		if((serialData != '\r') && (serialData != '\n') && (serialData != '\b'))
		{
			printf("\r\n") ;
			// exit receive mode
			radio_exit_receive_mode() ;
			// prepare and transmit character received
			buffer[0] = serialData ;
			radio_transmit_uplink(buffer, 1) ;
		}
	}
}

/* Configure LoRa Radio for P2P */
static void configure_radio(void)
{
	// mac reset 868 - reset the LoRaWAN stack and initialize it with the parameters of the selected ISM band
	LORAWAN_Reset(ISM_EU868) ;	// see stack_common.h for the definition of IsmBand_t

	// mac pause
	uint32_t time_ms = LORAWAN_Pause() ;
	printf("MAC Pause %ld\r\n", time_ms) ;

	// Set some radio parameters manually, the others parameters are following the selected ISM band
	
	// radio set pwr 15
	int16_t outputPwr = 15 ;
	RADIO_SetAttr(OUTPUT_POWER,(void *)&outputPwr) ;
	printf("Configuring Radio Output Power %d\r\n", outputPwr) ;

	// radio set sf sf12
	int16_t sf = SF_12 ;
	RADIO_SetAttr(OUTPUT_POWER,(void *)&sf) ;
	printf("Configuring Radio SF %d\r\n", sf) ;

	// radio set wdt 60000
	uint32_t wdt = 60000 ;
	RADIO_SetAttr(WATCHDOG_TIMEOUT,(void *)&wdt) ;
	printf("Configuring Radio Watch Dog Timeout %ld\r\n", wdt) ;
}

/* Transmit LoRa Radio Uplink */
void radio_transmit_uplink(uint8_t *data, uint16_t len)
{
	printf("[Transmit Uplink] ") ;
	print_array(data, len) ;
	RadioError_t radioStatus ;
	RadioTransmitParam_t RadioTransmitParam ;
	RadioTransmitParam.bufferLen = len ;
	RadioTransmitParam.bufferPtr = data ;
	radioStatus = RADIO_Transmit(&RadioTransmitParam) ;
	switch(radioStatus)
	{
		case ERR_NONE:
			printf("Radio Transmit Success \r\n") ;
			print_menu() ;
			break;
		case ERR_DATA_SIZE:
			//do nothing, status already set to invalid
			break;
		default:
			printf("Radio busy \r\n") ;
	}
}

/* LoRa Radio enter into Receive Mode */
void radio_enter_receive_mode(void)
{
	RadioReceiveParam_t radioReceiveParam ;
	uint32_t rxTimeout = 0 ;	// forever
	radioReceiveParam.action = RECEIVE_START ;
	radioReceiveParam.rxWindowSize = rxTimeout ;
	if (RADIO_Receive(&radioReceiveParam) == 0)
	{
		printf("Radio in Receive mode\r\n") ;
	}
}

/* LoRa Radio exit from Receive Mode */
void radio_exit_receive_mode(void)
{
	RadioReceiveParam_t radioReceiveParam ;
	radioReceiveParam.action = RECEIVE_STOP ;
	if (RADIO_Receive(&radioReceiveParam) == 0)
	{
		printf("Radio Exit Receive mode\r\n") ;
	}	
}

/* Uplink/Downlink Callback */
void appData_callback(void *appHandle, appCbParams_t *appdata)
{
	StackRetStatus_t status = LORAWAN_INVALID_REQUEST ;
	
	if (appdata->evt == LORAWAN_EVT_RX_DATA_AVAILABLE)
	{
		// Downlink Event
		status = appdata->param.rxData.status ;
		switch(status)
		{
			case LORAWAN_RADIO_SUCCESS:
			{
				uint8_t dataLength = appdata->param.rxData.dataLength ;
				uint8_t *pData = appdata->param.rxData.pData ;
				if((dataLength > 0U) && (NULL != pData))
				{
					int8_t rssi_value, snr_value ;
					RADIO_GetAttr(PACKET_RSSI_VALUE, &rssi_value) ;
					RADIO_GetAttr(PACKET_SNR, &snr_value) ;
					
					printf(">> Payload received: ") ;
					print_array(pData, dataLength) ;
					printf("RSSI Value: %d\r\n", rssi_value) ;
					printf("SNR Value: %d", snr_value) ;
					printf("\r\n*******************\r\n") ;
					
					LED_On(LED_0_PIN) ;
					delay_ms(50) ;
					LED_Off(LED_0_PIN) ;

					radio_enter_receive_mode() ;
					print_menu() ;
				}
			}
			break ;
			case LORAWAN_RADIO_NO_DATA:
			{
				printf("\n\rRADIO_NO_DATA \n\r");
			}
			break;
			case LORAWAN_RADIO_DATA_SIZE:
				printf("\n\rRADIO_DATA_SIZE \n\r");
			break;
			case LORAWAN_RADIO_INVALID_REQ:
				printf("\n\rRADIO_INVALID_REQ \n\r");
			break;
			case LORAWAN_RADIO_BUSY:
				printf("\n\rRADIO_BUSY \n\r");
			break;
			case LORAWAN_RADIO_OUT_OF_RANGE:
				printf("\n\rRADIO_OUT_OF_RANGE \n\r");
			break;
			case LORAWAN_RADIO_UNSUPPORTED_ATTR:
				printf("\n\rRADIO_UNSUPPORTED_ATTR \n\r");
			break;
			case LORAWAN_RADIO_CHANNEL_BUSY:
				printf("\n\rRADIO_CHANNEL_BUSY \n\r");
			break;
			case LORAWAN_INVALID_PARAMETER:
				printf("\n\rINVALID_PARAMETER \n\r");
			break;
			default:
				printf("UNKNOWN ERROR %d\r\n", status) ;
			break ;
		}
	}
	else if(appdata->evt == LORAWAN_EVT_TRANSACTION_COMPLETE)
	{
		// Uplink Event
		switch(status = appdata->param.transCmpl.status)
		{
			case LORAWAN_SUCCESS:
			case LORAWAN_RADIO_SUCCESS:
				printf("Transmission success\r\n") ;
				radio_enter_receive_mode() ;
				break ;
			case LORAWAN_RADIO_NO_DATA:
				printf("\r\nRADIO_NO_DATA\r\n") ;
				break ;
			case LORAWAN_RADIO_BUSY:
				printf("\r\nRADIO_BUSY\r\n") ;
				break ;
			default:
				break ;
		}
	}
}

/*********************************************************************//*
 \brief      Function to Print array of characters
 \param[in]  *array  - Pointer of the array to be printed
 \param[in]   length - Length of the array
 ************************************************************************/
void print_array(uint8_t *array, uint8_t length)
{
	printf("0x") ;
	for (uint8_t i = 0; i < length; i++)
	{
		printf("%02x", *array) ;
		array++ ;
	}
	printf("\n\r") ;
}
