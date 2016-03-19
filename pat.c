
// PIC18F1320 Configuration Bit Settings

// CONFIG1H
#pragma config OSC = INTIO2      // Oscillator Selection bits (INT oscillator, port functions)
#pragma config FSCM = ON        // Fail-Safe Clock Monitor Enable bit (Fail-Safe Clock Monitor enabled)
#pragma config IESO = ON        // Internal External Switchover bit (Internal External Switchover mode enabled)

// CONFIG2L
#pragma config PWRT = OFF       // Power-up Timer Enable bit (PWRT disabled)
#pragma config BOR = ON         // Brown-out Reset Enable bit (Brown-out Reset enabled)
// BORV = No Setting

// CONFIG2H
#pragma config WDT = ON        // Watchdog Timer Enable bit 
#pragma config WDTPS = 4096    // Watchdog Timer Postscale Select bits 

// CONFIG3H
#pragma config MCLRE = ON       // MCLR Pin Enable bit (MCLR pin enabled, RA5 input pin disabled)

// CONFIG4L
#pragma config STVR = ON        // Stack Full/Underflow Reset Enable bit (Stack full/underflow will cause Reset)
#pragma config LVP = OFF        // Low-Voltage ICSP Enable bit (Low-Voltage ICSP disabled)

// CONFIG5L
#pragma config CP0 = ON        // Code Protection bit (Block 0 (00200-000FFFh) not code-protected)
#pragma config CP1 = ON        // Code Protection bit (Block 1 (001000-001FFFh) not code-protected)

// CONFIG5H
#pragma config CPB = OFF        // Boot Block Code Protection bit (Boot Block (000000-0001FFh) not code-protected)
#pragma config CPD = OFF        // Data EEPROM Code Protection bit (Data EEPROM not code-protected)

// CONFIG6L
#pragma config WRT0 = OFF       // Write Protection bit (Block 0 (00200-000FFFh) not write-protected)
#pragma config WRT1 = OFF       // Write Protection bit (Block 1 (001000-001FFFh) not write-protected)

// CONFIG6H
#pragma config WRTC = OFF       // Configuration Register Write Protection bit (Configuration registers (300000-3000FFh) not write-protected)
#pragma config WRTB = OFF       // Boot Block Write Protection bit (Boot Block (000000-0001FFh) not write-protected)
#pragma config WRTD = OFF       // Data EEPROM Write Protection bit (Data EEPROM not write-protected)

// CONFIG7L
#pragma config EBTR0 = OFF      // Table Read Protection bit (Block 0 (00200-000FFFh) not protected from table reads executed in other blocks)
#pragma config EBTR1 = OFF      // Table Read Protection bit (Block 1 (001000-001FFFh) not protected from table reads executed in other blocks)

// CONFIG7H
#pragma config EBTRB = OFF      // Boot Block Table Read Protection bit (Boot Block (000000-0001FFh) not protected from table reads executed in other blocks)


/*
 * Driver for E220 fan sensor
 * std input 52hz at full speed 
 * Version
 * 0.1  detect pulses and flash fan failure lamps if RPM is out of spec
 *	for the ebmpapst 4606 ZH
 * 0.2 XC8 stripped version
 */

#include <xc.h>
#include <stdlib.h>
#include <stdio.h>
#include "pat.h"
#include "blinker.h"
#include <string.h>

int16_t sw_work(void);
void init_fanmon(void);
uint8_t init_fan_params(void);

volatile struct V_data V;
volatile union Obits2 LEDS;
uint8_t str[12];
uint16_t timer0_off = TIMEROFFSET, timer1_off = SAMPLEFREQ;

const uint8_t build_date[] = __DATE__, build_time[] = __TIME__;
const uint8_t
spacer0[] = " ",
	spacer1[] = "\r\n",
	status0[] = "\r\n OK ",
	status1[] = "\r\n Booting Dual Fan Monitor ",
	status2[] = "\r\n FANC waiting for signal ",
	status3[] = "\r\n FANC spinning normal ",
	status4[] = "\r\n FANC spinning low ",
	boot0[] = "\r\n Boot RCON ",
	boot1[] = "\r\n Boot STKPTR ";

#pragma warning disable 752  // disable compiler bug for 8 bit math

void interrupt high_priority tm_handler(void) // timer/serial functions are handled here
{
	static uint8_t led_cache = 0xff;
	static uint16_t total_spins = 0;

	RPMLED = LEDON;
	/* check for expected interrupts */
	V.valid = FALSE;

	if (INTCONbits.INT0IF) {
		V.valid = TRUE;
		INTCONbits.INT0IF = FALSE;
		V.spin_count1++;
	}

	if (INTCON3bits.INT2IF) {
		V.valid = TRUE;
		INTCON3bits.INT2IF = FALSE;
		V.spin_count2++;
	}

	if (PIR1bits.RCIF) { // is data from RS-232 port
		V.valid = TRUE;
		V.rx_data = RCREG;
		if (RCSTAbits.OERR) {
			RCSTAbits.CREN = 0; // clear overrun
			RCSTAbits.CREN = 1; // re-enable
		}
		V.comm = TRUE;
	}

	if (PIR1bits.TMR1IF) { //      Timer1 int handler PWM lamp effects
		V.valid = TRUE;

		PIR1bits.TMR1IF = FALSE; //      clear int flag
		WRITETIMER1(timer1_off);

		/* LED off at pwm value 0 and turn on at pwm_set */
		if (LEDS.out_bits.b1) {
			if (V.led_pwm[1]++ >= V.led_pwm_set[1])
				LED1 = DRIVEON;
			if (!V.led_pwm[1])
				LED1 = DRIVEOFF; // LED OFF
		} else {
			LED1 = DRIVEOFF; // LED OFF
		}

		if (LEDS.out_bits.b2) {
			if (V.led_pwm[2]++ >= V.led_pwm_set[2])
				LED2 = DRIVEON;
			if (!V.led_pwm[2])
				LED2 = DRIVEOFF; // LED OFF
		} else {
			LED2 = DRIVEOFF; // LED OFF
		}
		//		RPMLED = !RPMLED;
	}

	if (INTCONbits.TMR0IF) { //      check timer0 irq time timer
		RPMOUT = !RPMOUT;
		V.valid = TRUE;
		INTCONbits.TMR0IF = FALSE; //      clear interrupt flag
		WRITETIMER0(timer0_off);

		// check for fan motor movement
		total_spins = V.spin_count1 + V.spin_count2;
		V.fan1_spinning = (V.spin_count1 >= FAN1_PULSE) ? TRUE : FALSE;
		V.fan2_spinning = (V.spin_count2 >= FAN2_PULSE) ? TRUE : FALSE;

		if (total_spins >= RPM_COUNT) {
			V.spinning = TRUE;
			V.comm_state = 3;
			RELAY1 = DRIVEOFF;
		} else {
			V.spinning = FALSE;
			V.comm_state = 2;
			RELAY1 = DRIVEON;
		}
		V.spin_count1 = 0;
		V.spin_count2 = 0;

		/* Start Led Blink Code */
		if (V.blink_alt) {
			if (V.blink & 0b00000010) LEDS.out_bits.b1 = !LEDS.out_bits.b1;
			if (V.blink & 0b00000100) LEDS.out_bits.b2 = !LEDS.out_bits.b1;
			if (V.blink & 0b00001000) LEDS.out_bits.b3 = !LEDS.out_bits.b3;
		} else {
			if (V.blink & 0b00000010) LEDS.out_bits.b1 = !LEDS.out_bits.b1;
			if (V.blink & 0b00000100) LEDS.out_bits.b2 = !LEDS.out_bits.b2;
			if (V.blink & 0b00001000) LEDS.out_bits.b3 = !LEDS.out_bits.b3;
		}

		if (LEDS.out_byte != led_cache) {
			LED1 = LEDS.out_bits.b1 ? LEDON : LEDOFF;
			LED2 = LEDS.out_bits.b2 ? LEDON : LEDOFF;
			LED3 = LEDS.out_bits.b3 ? LEDON : LEDOFF;
			led_cache = LEDS.out_byte;
		}
		/* End Led Blink Code */
	}
	/*
	 * spurious interrupt handler
	 */
	if (!V.valid) {
		if (V.spurious_int++ > MAX_SPURIOUS)
			Reset();
	}
	RPMLED = LEDOFF;
}

/* main loop routine */
int16_t sw_work(void)
{

	if (V.valid)
		ClrWdt(); // reset watchdog

	if (V.spinning) {
		blink_led(1, OFF, OFF); // LED off
		blink_led(2, OFF, OFF); // LED off
	} else {
		blink_led(1, ON, ON); // LED blinks
		blink_led(2, ON, ON); // LED blinks
	}

	V.led_pwm_set[1]++; // testing with sweep modulation
	V.led_pwm_set[2]++;

	return 0;
}

void init_fanmon(void)
{
	/*
	 * check for a clean POR
	 */
	V.boot_code = FALSE;
	if (RCON != 0b0011100)
		V.boot_code = TRUE;

	if (STKPTRbits.STKFUL || STKPTRbits.STKUNF) {
		V.boot_code = TRUE;
		STKPTRbits.STKFUL = 0;
		STKPTRbits.STKUNF = 0;
	}

	OSCCON = 0x72;
	ADCON1 = 0x7F; // all digital, no ADC
	/* interrupt priority ON */
	RCONbits.IPEN = 1;
	/* define I/O ports */
	FANPORTA = FANPORT_IOA;
	FANPORTB = FANPORT_IOB;

	RPMOUT = LEDOFF; // preset all LEDS
	LED1 = LEDOFF;
	LED2 = LEDOFF;
	LED3 = LEDOFF;
	RELAY1 = DRIVEOFF;
	RPMLED = LEDON;
	Blink_Init();
	is_led_blinking(0); // kill warning
	is_led_on(0); // kill warning
	//T0CON = TIMER_INT_ON & T0_16BIT & T0_SOURCE_INT & T0_PS_1_64; // led blinker
	T0CON = 0b10000101;
	WRITETIMER0(timer0_off); //	start timer0 at ~1/2 second ticks
	//T1CON = TIMER_INT_ON & T1_16BIT_RW & T1_SOURCE_INT & T1_PS_1_8 & T1_OSC1EN_OFF & T1_SYNC_EXT_OFF; // PWM timer
	T1CON = 0b10110101;
	WRITETIMER1(timer1_off);

	/* Light-link data input */
	COMM_ENABLE = TRUE; // for PICDEM4 onboard RS-232, not used on custom board

	/*      work int thread setup */
	INTCONbits.TMR0IE = 1; // enable int
	INTCON2bits.TMR0IP = 1; // make it high level

	PIE1bits.TMR1IE = 1; // enable int
	IPR1bits.TMR1IP = 1; // make it high level

	INTCONbits.INT0IE = 1; // enable RPM sensor input fan1
	INTCON3bits.INT2IE = 1; // enable RPM sensor input fan2
	INTCON2bits.RBPU = 0; // enable weak pull-ups

	init_fan_params();

	/* Enable all high priority interrupts */
	INTCONbits.GIEH = 1;
}

uint8_t init_fan_params(void)
{
	V.spin_count1 = 0;
	V.spin_count2 = 0;
	V.spinning = FALSE;
	V.valid = TRUE;
	V.spurious_int = 0;
	V.comm = FALSE;
	V.comm_state = 0;
	V.led_pwm_set[1] = 128;
	V.led_pwm_set[2] = 64;
	return 0;
}

void main(void)
{
	init_fanmon();
	blink_led(3, ON, ON); // controller run indicator
	blink_led_alt(TRUE);

	/* Loop forever */
	while (TRUE) { // busy work
		sw_work(); // run housekeeping
		Nop();
		Nop();
		Nop();
		Nop();
	}
}
