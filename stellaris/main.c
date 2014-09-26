#include <stdint.h>
#include <inc/hw_types.h>
#include <inc/hw_memmap.h>
#include <inc/hw_gpio.h>
#include <inc/hw_ints.h>
#include <inc/hw_timer.h>
#include "inc/hw_i2c.h"
#include <driverlib/sysctl.h>
#include <driverlib/timer.h>
#include <driverlib/pin_map.h>
#include <driverlib/gpio.h>
#include <driverlib/pin_map.h>
#include <driverlib/interrupt.h>
#include <driverlib/uart.h>
#include "driverlib/i2c.h"

//Buffers UART
unsigned char UART0_buffer_state = 0;
#define UART0_buffer_size (4*sizeof(int)+2)
unsigned char UART0_buffer[UART0_buffer_size];
unsigned char *UART0_buffer_pointer;

unsigned char watchdog_counter = 0;
unsigned char current_mode     = 0;  //0 => Alpine, 1 => PC

void UART0IntHandler(void) {
    unsigned long ulStatus;
    ulStatus = UARTIntStatus(UART0_BASE, true);
    UARTIntClear(UART0_BASE, ulStatus);

    while(UARTCharsAvail(UART0_BASE)) {
    	char c = UARTCharGet(UART0_BASE);
    	switch(UART0_buffer_state) {
    	case 1:
    		//On reçoit les données
			*UART0_buffer_pointer = c;
			UART0_buffer_pointer++;
			if (UART0_buffer_pointer - UART0_buffer >= 12) {
				UART0_buffer_state = 0;
				UART0_buffer_pointer = UART0_buffer;
			}
    		break;
    	default:
    		//On reçoit un int de start ou une commande simple
    		if ((c == 0xAB) && (UART0_buffer_pointer - UART0_buffer == 3)) {
    			//Pong
				UART0_buffer_pointer = UART0_buffer;
				watchdog_counter = 0;
				current_mode = 1;
			}
    		else if (c != 0xAA) {
    			//Truc pas normal, on reset la stack
    			UART0_buffer_pointer = UART0_buffer;
    		}
    		else {
    			//Octet de start
        		*UART0_buffer_pointer = c;
        		UART0_buffer_pointer++;
        		if (UART0_buffer_pointer - UART0_buffer >= 4) {
        			//Passage en réception des données
        			UART0_buffer_state = 1;
        			UART0_buffer_pointer = UART0_buffer;
        		}
    		}
    	}
    }
}

void WTimer1AIntHandler(void) {
	TimerIntClear(WTIMER1_BASE, TIMER_TIMA_TIMEOUT);
	if (watchdog_counter++ >= 10) {
		current_mode = 0;
		watchdog_counter = 10; //On ne vas pas risquer un char overflow
	}
}

//Les données à envoyer dans l'autoradio, format propriétaire Alpine j'imagine
unsigned char  data_vol_plus[]   = {0x1A, 0xFB, 0x75, 0x7B, 0x7A, 0xDA, 0xA0};
unsigned char  data_vol_minus[]  = {0x1A, 0xFB, 0x75, 0x6D, 0xBE, 0xDA, 0xA0};
unsigned char  data_bottom[]     = {0x1A, 0xFB, 0x75, 0x77, 0x7B, 0x5A, 0xA0};
unsigned char  data_top_1[]      = {0x1A, 0xFB, 0x75, 0x76, 0xFB, 0x6A, 0xA0};
unsigned char  data_top_2[]      = {0x1A, 0xFB, 0x75, 0x6D, 0x7E, 0xEA, 0xA0};
unsigned char  data_wheel_down[] = {0x1A, 0xFB, 0x75, 0x75, 0x7D, 0xEA, 0xA0};
unsigned char  data_wheel_up[]   = {0x1A, 0xFB, 0x75, 0x6A, 0xBF, 0xEA, 0xA0};
unsigned char  data_mute[]       = {0x1A, 0xFB, 0x75, 0x75, 0xBD, 0xDA, 0xA0};

unsigned char  *data = data_vol_plus;
unsigned char  bits_sent = 0;
unsigned char  state = 0xFF;
unsigned char  has_data = 0;
unsigned short count = 0;

void Timer0AIntHandler(void) {
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

	switch(state) {
	case 0:
		if(count++ >= 1000) {
			state = 1;
			GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, 0);
			count = 0;
		}
		break;
	case 1:
		if (count >= 222-13)
			GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, GPIO_PIN_0);
		if (count++ >= 222) {
			count = 0;
			bits_sent = 0;
			state = 2;
		}
		break;
	case 2:
		if (count++ >= 13) {
			unsigned char current_bit = (data[(bits_sent / 8)] >> (7 - (bits_sent % 8))) & 0x1;
			GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, current_bit ? 0 : GPIO_PIN_0);
			state = 3;
			count = 0;
		}
		break;
	case 3:
		if (count++ >= 13) {
			GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, GPIO_PIN_0);
			count = 0;
			if (bits_sent++ >= 50) {
				state = 0xFF;
				has_data = 0;
			}
			else {
				state = 2;
			}
		}
	}
}

#define BUTTON_VOL_PLUS  0
#define BUTTON_VOL_MINUS 1
#define BUTTON_TOP_1     2
#define BUTTON_TOP_2     3
#define BUTTON_BOTTOM    4
#define BUTTON_WHEEL_1   5
#define BUTTON_WHEEL_2   6
#define BUTTON_WHEEL_3   7
unsigned char  button_states[8]          = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
unsigned char  button_states_previous[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
unsigned char  last_wheel_state          = 0;

#define SEND_DATA_ALPINE(DATA)  \
if(1) {                         \
	while(has_data);            \
	has_data = 1;               \
	data = DATA;                \
	state = 0;                  \
}

#define BUTTON_CMD_VOL_PLUS       0
#define BUTTON_CMD_VOL_MINUS      1
#define BUTTON_CMD_TOP_1          2
#define BUTTON_CMD_TOP_2          3
#define BUTTON_CMD_BOTTOM         4
#define BUTTON_CMD_WHEEL_DOWN     5
#define BUTTON_CMD_WHEEL_UP       6
#define BUTTON_CMD_VOL_PLUSMINUS  8

#define SEND_DATA_PC(DATA)          \
if (1) {                            \
	UARTCharPut(UART0_BASE, 0xAA);  \
	UARTCharPut(UART0_BASE, 0xAA);  \
	UARTCharPut(UART0_BASE, 0xAA);  \
	UARTCharPut(UART0_BASE, 0xAA);  \
	UARTCharPut(UART0_BASE, DATA);  \
}

unsigned char i2c_data[16];
unsigned char i2c_sent_bytes = 0;
unsigned char i2c_packet_length = 1;

#define I2C_SLAVE_ADDRESS 0x23
void WTimer2AIntHandler(void) {
	TimerIntClear(WTIMER2_BASE, TIMER_TIMA_TIMEOUT);
	i2c_data[0] = 0x11;
	i2c_packet_length = 1;
	i2c_sent_bytes = 0;
	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_4, 0);
	I2CMasterSlaveAddrSet(I2C0_MASTER_BASE, I2C_SLAVE_ADDRESS, false);
	I2CMasterDataPut(I2C0_MASTER_BASE, *((unsigned char *)&i2c_data));
	I2CMasterControl(I2C0_MASTER_BASE, I2C_MASTER_CMD_BURST_SEND_START);
}

void i2c0_master_interrupt(void) {
	unsigned long err = 0x00;
	unsigned long status = I2CMasterIntStatusEx(I2C0_MASTER_BASE, false);
	if (status & I2C_MASTER_INT_TIMEOUT) {
		I2CMasterIntClearEx(I2C0_MASTER_BASE, I2C_MASTER_INT_TIMEOUT);
	}
	if (status & I2C_MASTER_INT_DATA) {
		err = I2CMasterErr(I2C0_MASTER_BASE);
		if (err != I2C_MASTER_ERR_NONE) {
			I2CMasterIntClearEx(I2C0_MASTER_BASE, I2C_MASTER_INT_DATA);
			GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_PIN_4);
			return;
		}
		i2c_sent_bytes++;
		if (i2c_sent_bytes < i2c_packet_length-1) {
			//Continuing
			 I2CMasterDataPut(I2C3_MASTER_BASE, ((unsigned char *)&i2c_data)[i2c_sent_bytes]);
			 I2CMasterControl(I2C3_MASTER_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);
		}
		else if (i2c_sent_bytes == i2c_packet_length-1) {
			//Last byte remaining
			 I2CMasterDataPut(I2C0_MASTER_BASE, ((unsigned char *)&i2c_data)[i2c_sent_bytes]);
			 I2CMasterControl(I2C0_MASTER_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
			 GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_PIN_4);
		}

		I2CMasterIntClearEx(I2C0_MASTER_BASE, I2C_MASTER_INT_DATA);
	}
}

void main(void) {
	unsigned char i;

    SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
						 (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
						  UART_CONFIG_PAR_NONE));
	UARTFIFODisable(UART0_BASE);
	IntEnable(INT_UART0);
	UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);

	//Configuration du timer pour trame données alpine
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_A_PERIODIC);
	TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet()/25250);
	IntEnable(INT_TIMER0A);
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(TIMER0_BASE, TIMER_A);

	//Configuration du timer watchdog de communication PC
	SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER1);
	TimerConfigure(WTIMER1_BASE, TIMER_CFG_A_PERIODIC);
	TimerLoadSet(WTIMER1_BASE, TIMER_A, SysCtlClockGet());
	IntEnable(INT_WTIMER1A);
	TimerIntEnable(WTIMER1_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(WTIMER1_BASE, TIMER_A);

	//Configuration du timer pour l'I2C
	/*SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER2);
	TimerConfigure(WTIMER2_BASE, TIMER_CFG_A_PERIODIC);
	TimerLoadSet(WTIMER2_BASE, TIMER_A, SysCtlClockGet());
	IntEnable(INT_WTIMER2A);
	TimerIntEnable(WTIMER2_BASE, TIMER_TIMA_TIMEOUT);
	TimerEnable(WTIMER2_BASE, TIMER_A);

	//Configuration de l'I2C
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_4);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_PIN_4);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    I2CMasterInitExpClk(I2C0_MASTER_BASE, SysCtlClockGet()/40, false);
    SysCtlDelay(10000);
    I2CMasterTimeoutSet(I2C0_MASTER_BASE, 0x7d);
    I2CMasterSlaveAddrSet(I2C0_MASTER_BASE, I2C_SLAVE_ADDRESS, false);
	I2CMasterIntEnableEx(I2C0_MASTER_BASE, I2C_MASTER_INT_TIMEOUT|I2C_MASTER_INT_DATA);
	IntEnable(INT_I2C0);*/

    //Vers adaptateur Alpine
	//Ne pas oublier que la conversion 3.3->5 est effectué avec un inverter
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE, GPIO_PIN_0);
    GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, GPIO_PIN_0);

    //Entrée matrice d'interrupteurs
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    GPIOPinTypeGPIOOutput(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_5);
    GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE, GPIO_PIN_4);
    GPIOPinTypeGPIOInput(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);

    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    //GPIOPinTypeGPIOInput(GPIO_PORTF_BASE, GPIO_PIN_4);
    //GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    /*
     * PE4 Jaune
     * PE5 Bleu
     * PB4 Vert
     * PA5 Marron
     * PA6 Gris
     * PA7 Rouge
     */
#if DEBUG
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
    unsigned char lol = 0;
#endif

#if 0
    while(1) {
    	while(has_data);
    	has_data = 1;
    	data = data_vol_plus;
    	state = 0;
    	SysCtlDelay(SysCtlClockGet());

    	while(has_data);
    	has_data = 1;
    	data = data_vol_minus;
    	state = 0;
    	SysCtlDelay(SysCtlClockGet());
    }
#endif

	while(1) {
#if DEBUG
		SysCtlDelay(SysCtlClockGet()/10);

		lol = lol ? 0 : 1;
		GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, lol ? GPIO_PIN_1 : 0);
#else
		SysCtlDelay(SysCtlClockGet()/100);
#endif

		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_5, GPIO_PIN_5);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_1]  = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_VOL_PLUS] = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_5, 0);

		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_4, GPIO_PIN_4);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_2]   = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_VOL_MINUS] = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		button_states[BUTTON_TOP_1]     = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_4, 0);

		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_PIN_4);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_3]   = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_BOTTOM]    = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		button_states[BUTTON_TOP_2]     = GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_4, 0);

		SysCtlDelay(SysCtlClockGet()/100);

		//Seconde passe pour éviter les rebonds
		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_5, GPIO_PIN_5);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_1]  &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_VOL_PLUS] &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_5, 0);

		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_4, GPIO_PIN_4);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_2]   &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_VOL_MINUS] &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		button_states[BUTTON_TOP_1]     &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTE_BASE, GPIO_PIN_4, 0);

		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_PIN_4);
		SysCtlDelay(SysCtlClockGet()/100000);
		button_states[BUTTON_WHEEL_3]   &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_5) ? 1 : 0;
		button_states[BUTTON_BOTTOM]    &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7) ? 1 : 0;
		button_states[BUTTON_TOP_2]     &= GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6) ? 1 : 0;
		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_4, 0);

		if (
			   (button_states_previous[BUTTON_VOL_PLUS] != button_states[BUTTON_VOL_PLUS])
			&& (button_states_previous[BUTTON_VOL_MINUS] != button_states[BUTTON_VOL_MINUS])
			&& button_states[BUTTON_VOL_PLUS]
			&& button_states[BUTTON_VOL_MINUS]
		) {
			SEND_DATA_ALPINE(data_mute);
		}
		else {
			if (button_states_previous[BUTTON_VOL_PLUS] != button_states[BUTTON_VOL_PLUS]) {
				if (button_states[BUTTON_VOL_PLUS]) {
					SEND_DATA_ALPINE(data_vol_plus);
				}
			}

			if (button_states_previous[BUTTON_VOL_MINUS] != button_states[BUTTON_VOL_MINUS]) {
				if (button_states[BUTTON_VOL_MINUS]) {
					SEND_DATA_ALPINE(data_vol_minus);
				}
			}
		}

		if (button_states_previous[BUTTON_TOP_1] != button_states[BUTTON_TOP_1]) {
			if (button_states[BUTTON_TOP_1]) {
				if (current_mode == 0) {
					SEND_DATA_ALPINE(data_top_1);
				}
				else {
					SEND_DATA_PC(BUTTON_CMD_TOP_1);
				}
			}
		}

		if (button_states_previous[BUTTON_TOP_2] != button_states[BUTTON_TOP_2]) {
			if (button_states[BUTTON_TOP_2]) {
				if (current_mode == 0) {
					SEND_DATA_ALPINE(data_top_2);
				}
				else {
					SEND_DATA_PC(BUTTON_CMD_TOP_2);
				}
			}
		}

		if (button_states_previous[BUTTON_BOTTOM] != button_states[BUTTON_BOTTOM]) {
			if (button_states[BUTTON_BOTTOM]) {
				if (current_mode == 0) {
					SEND_DATA_ALPINE(data_bottom);
				}
				else {
					SEND_DATA_PC(BUTTON_CMD_BOTTOM);
				}
			}
		}

		if (!last_wheel_state) {
			//Position initiale molette
			if (button_states[BUTTON_WHEEL_1]) {
				last_wheel_state = 1;
			}
			else if (button_states[BUTTON_WHEEL_2]) {
				last_wheel_state = 2;
			}
			else if (button_states[BUTTON_WHEEL_3]) {
				last_wheel_state = 3;
			}
		}
		else if (button_states_previous[BUTTON_WHEEL_1] != button_states[BUTTON_WHEEL_1]) {
			if (button_states[BUTTON_WHEEL_1]) {
				if (last_wheel_state == 2) {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_up);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_UP);
					}
				}
				else {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_down);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_DOWN);
					}
				}
				last_wheel_state = 1;
			}
		}
		else if (button_states_previous[BUTTON_WHEEL_2] != button_states[BUTTON_WHEEL_2]) {
			if (button_states[BUTTON_WHEEL_2]) {
				if (last_wheel_state == 1) {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_down);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_DOWN);
					}
				}
				else {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_up);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_UP);
					}
				}
				last_wheel_state = 2;
			}
		}
		else if (button_states_previous[BUTTON_WHEEL_3] != button_states[BUTTON_WHEEL_3]) {
			if (button_states[BUTTON_WHEEL_3]) {
				if (last_wheel_state == 2) {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_down);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_DOWN);
					}
				}
				else {
					if (current_mode == 0) {
						SEND_DATA_ALPINE(data_wheel_up);
					}
					else {
						SEND_DATA_PC(BUTTON_CMD_WHEEL_UP);
					}
				}
				last_wheel_state = 3;
			}
		}

		for (i=0; i<8; i++) {
			button_states_previous[i] = button_states[i];
		}
	}
}
