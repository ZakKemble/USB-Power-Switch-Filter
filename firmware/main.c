/*
 * Project: USB Filter and MIC2545A Switch
 * Author: Zak Kemble, contact@zakkemble.co.uk
 * Copyright: (C) 2017 by Zak Kemble
 * License: 
 * Web: http://blog.zakkemble.co.uk/usb-power-switch-and-filter/
 */

#include <stdint.h>
#include <avr/io.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

#define LED_OFF		0
#define LED_RED		(_BV(PORTB0))
#define LED_GREEN	(_BV(PORTB1))
#define LED_MASK	(~(_BV(PORTB0)|_BV(PORTB1)))

#define PIN_BUTTON	(_BV(PINB2))
#define PIN_FLAG	(_BV(PINB4))

#define LEDSET(state)	(PORTB = (PORTB & LED_MASK) | state)

#define POWERON()	(PORTB &= ~_BV(PORTB3))
#define POWEROFF()	(PORTB |= _BV(PORTB3))

// B4 as output low (open-drain)
#define FLAGSET()	\
	do{ \
		PORTB &= ~_BV(PORTB4); \
		DDRB |= _BV(DDB4); \
	}while(0)

// B4 as input with pullup
#define FLAGCLEAR()	\
	do{ \
		DDRB &= ~_BV(DDB4); \
		PORTB |= _BV(PORTB4); \
	}while(0)

#define WDT_INT_RESET()		(WDTCR |= _BV(WDIE)|_BV(WDE)) // NOTE: Setting WDIE also enables global interrupts
#define WDT_TIMEDOUT()		(!(WDTCR & _BV(WDIE)))

void get_mcusr(void) __attribute__((naked)) __attribute__((section(".init3"))); // TODO init0?
void get_mcusr()
{
	MCUSR = 0;
	wdt_disable();
}

int main(void)
{
	clock_prescale_set(CPU_DIV);

	ACSR = _BV(ACD); // Power off analog comparator
	power_all_disable(); // Power off everything else
	
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);

	// B0 = Bi-LED 1 output
	// B1 = Bi-LED 2 output
	// B2 = Button input+pullup
	// B3 = EN output
	// B4 = /FLAG input+pullup (switches to output low - open drain)

	DDRB |= _BV(DDB0)|_BV(DDB1)|_BV(DDB3);
	PORTB |= _BV(PORTB2)|_BV(PORTB3)|_BV(PORTB4);

	// Power on flashy
	for(uint8_t i=0;i<4;i++)
	{
		_delay_ms(50);
		LEDSET(LED_RED);
		_delay_ms(50);
		LEDSET(LED_GREEN);
	}
	LEDSET(LED_OFF);

	// Setup WDT to 16ms and run interrupt on time out
	wdt_enable(WDTO_15MS);
	WDT_INT_RESET();

	PCMSK |= _BV(PCINT3); // Fault flag interrupt, we want to wakeup and turn power off as soon as possible
	GIMSK |= _BV(PCIE);

	uint8_t now = 0; // Increments every 16ms, overflows every 16*256 = 4.1 seconds
	uint8_t btnState = 0;
	uint8_t btnTime = 0;
	uint8_t flagTriggered = 0; // MIC2545A Fault flag was set
	uint8_t flagTrigTime = 0;
	uint8_t flagSet = 0;
	uint8_t powerOn = 0;
	uint8_t debounceOk = 1;

	sei();

	while(1)
	{
		// Timer stuff, mainly for button debouncing, increments every 16ms from the WDT
		if(WDT_TIMEDOUT())
		{
			WDT_INT_RESET();
			now++; // TODO this only does 4 seconds, that's probs enough though
		}

		// Get pin input states
		uint8_t pinb = PINB;
		uint8_t btnStateNow = !(pinb & PIN_BUTTON);
		uint8_t flagStateNow = !(pinb & PIN_FLAG);

		// Button processing, debouncing etc
		if(btnStateNow) // Button is currently pressed
		{
			if(!btnState) // Button has just been pressed (was released last time it was checked)
			{
				if(debounceOk)
				{
					debounceOk = 0;

					if(flagTriggered) // If fault flag triggered then just clear the flag, don't turn power back on yet
						flagTriggered = 0;
					else
						powerOn = !powerOn;
				}
			}

			btnTime = now;
		}
		else // Not pressed (or bouncing)
		{
			if((uint8_t)(now - btnTime) >= 3) // If last press was over 3*16ms ago then we're now ready for another press
				debounceOk = 1;
		}
		btnState = btnStateNow;

		if(flagStateNow)
		{
			if(!flagSet)
				flagTrigTime = now;

			// The fault flag has been set, turn power off
			flagTriggered = 1;
			powerOn = 0;

			// TODO set flag input pin to open drain for a few ms (need testing)
			FLAGSET();
			flagSet = 1;
		}
		
		if(flagTriggered && flagSet && (uint8_t)(now - flagTrigTime) >= 2) // 2 = 32ms
		{
			FLAGCLEAR();
			flagSet = 0;
		}

		// Do power and LEDs
		if(powerOn)
		{
			LEDSET(LED_GREEN);
			POWERON();
		}
		else
		{
			POWEROFF();
			if(flagTriggered)
				LEDSET(LED_RED);
			else
				LEDSET(LED_OFF);
		}

		// Sleep if nothing to do
		cli();
		if(!WDT_TIMEDOUT())
		{
			sleep_enable();
			//sleep_bod_disable();
			sei();
			sleep_cpu();
			sleep_disable();
		}
		sei();
	}
}

EMPTY_INTERRUPT(WDT_vect);
EMPTY_INTERRUPT(PCINT0_vect);
