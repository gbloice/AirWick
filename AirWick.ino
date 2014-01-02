/// @dir AirWick
/// Room sensor - JeeNode micro in an AirWick housing
/// Based on radioBlib2 by <jc@wippler.nl> http://opensource.org/licenses/mit-license.php
/// 2013-03-28 <github@bloice.co.uk>

#include <JeeLib.h>
#include <avr/sleep.h>
#include <dht.h>

#define BLIP_NODE 22  // wireless node ID to use for sending blips
#define BLIP_GRP  5   // wireless net group to use for sending blips
#define BLIP_ID   2   // set this to a unique ID to disambiguate multiple nodes
#define SEND_MODE 3   // set to 3 if fuses are e=06/h=DE/l=CE, else set to 2

#define PIR_PIN   7   // AIO2 of JeeNode Micro marked as PA3/TX. Port Pin 2 (also PCINT3)
#define DHT_PIN   8   // DIO2 of JeeNode Micro marked as RX/PA2. Port Pin 1
#define LED_PIN  10   // DIO1 of JeeNode Micro marked as PA0/DIO. Port Pin 4

#define LED_ON_TIME 2 // LED ON time in 10ths of mS
#define LED_OFF_TIME 3 // LED OFF time in 10ths of mS
#define LED_IDLE_TIME 600 // LED IDLE time before retrigger in 10ths of mS

enum LED_STATE { LED_IDLE, LED_ON1, LED_OFF1, LED_ON2, LED_OFF2 };
LED_STATE ledState = LED_IDLE;

enum TASKS { SEND_BLIP, FLASH_LED, TASK_LIMIT };
static word schedBuff[TASK_LIMIT];
Scheduler scheduler(schedBuff, TASK_LIMIT);

dht DHT;

struct {
  long ping;      // 32-bit counter
  byte id;        // identity, should be different for each node
  byte vcc1;      // VCC before transmit, 1.0V = 0 .. 6.0V = 250
  byte vcc2;      // battery voltage (BOOST=1), or VCC after transmit (BOOST=0)
  int  temp;      // Temp from dht
  int  humidity;  // humidity from dht
  byte status;    // status from dht;
  byte pirCount;  // count of PIR activations
} payload;

volatile bool ledFlashing = false;

volatile byte pirCount = 0;
// For the PIR, use a pin change interrupt to count triggers and enable the LED flasher task
ISR(PCINT0_vect) {
  if (digitalRead(PIR_PIN) == LOW) {
    if (pirCount < 255) {
      pirCount++;
    }
    if (!ledFlashing) {
      ledFlashing = true;
      scheduler.timer(FLASH_LED, 0);
    }
  }
}

volatile bool adcDone;
// for low-noise/-power ADC readouts, we'll use ADC completion interrupts
ISR(ADC_vect) { adcDone = true; }

// this must be defined since we're using the watchdog for low-power waiting
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

static byte vccRead (byte count =4) {
  set_sleep_mode(SLEEP_MODE_ADC);
  // use VCC as AREF and internal bandgap as input
#if defined(__AVR_ATtiny84__)
  ADMUX = 33;
#else
  ADMUX = bit(REFS0) | 14;
#endif
  bitSet(ADCSRA, ADIE);
  while (count-- > 0) {
    adcDone = false;
    while (!adcDone)
      sleep_mode();
  }
  bitClear(ADCSRA, ADIE);  
  // convert ADC readings to fit in one byte, i.e. 20 mV steps:
  //  1.0V = 0, 1.8V = 40, 3.3V = 115, 5.0V = 200, 6.0V = 250
  return (55U * 1024U) / (ADC + 1) - 50;
}

void setup() {
  cli();
  CLKPR = bit(CLKPCE);
#if defined(__AVR_ATtiny84__)
  CLKPR = 0; // div 1, i.e. speed up to 8 MHz
#else
  CLKPR = 1; // div 2, i.e. slow down to 8 MHz
#endif
  sei();

#if defined(__AVR_ATtiny84__)
    // power up the radio on JMv3
    bitSet(DDRB, 0);
    bitClear(PORTB, 0);
#endif

  rf12_initialize(BLIP_NODE, RF12_868MHZ, BLIP_GRP);
  // see http://tools.jeelabs.org/rfm12b
  rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
  rf12_sleep(RF12_SLEEP);

  payload.id = BLIP_ID;
  payload.pirCount = 0;
  
  // Set the PIR pin to input and the LED to output and off
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Enable the PIR interrupts
  PCMSK0 |= bit(PCINT3);
  GIMSK |= bit(PCIE0);
 
  // Set the LED state machine
  ledState = LED_IDLE;

  // Start the scheduler running
  scheduler.timer(SEND_BLIP, 0);
}

static byte sendPayload () {
  ++payload.ping;

  rf12_sleep(RF12_WAKEUP);
  rf12_sendNow(0, &payload, sizeof payload);
  rf12_sendWait(SEND_MODE);
  rf12_sleep(RF12_SLEEP);
}

// This code tries to implement a good survival strategy: when power is low,
// don't transmit - when power is even lower, don't read out the VCC level.
//
// With a 100 µF cap, normal packet sends can cause VCC to drop by some 0.6V,
// hence the choices below: sending at >= 2.7V should be ok most of the time.

#define VCC_OK    85  // >= 2.7V - enough power for normal 1-minute sends
#define VCC_LOW   80  // >= 2.6V - sleep for 1 minute, then try again
#define VCC_DOZE  75  // >= 2.5V - sleep for 5 minutes, then try again
                      //  < 2.5V - sleep for 60 minutes, then try again
#define VCC_SLEEP_MINS(x) ((x) >= VCC_LOW ? 1 : (x) >= VCC_DOZE ? 5 : 60)

// Reasoning is that when we're about to try sending and find out that VCC
// is far too low, then let's just send anyway, as one final sign of life.

#define VCC_FINAL 70  // <= 2.4V - send anyway, might be our last swan song

void loop() {
  
  switch(scheduler.pollWaiting()) {
    case SEND_BLIP: {
      // Make the required measurements and send the data
      byte vcc = payload.vcc1 = vccRead();
      if (vcc <= VCC_FINAL) { // hopeless, maybe we can get one last packet out
        sendPayload();
        vcc = 1; // don't even try reading VCC after this send
        payload.vcc2 = vcc;
      }
    
      if (vcc >= VCC_OK) { // enough energy for normal operation
        payload.status = DHT.read22(DHT_PIN);
        if (payload.status == DHTLIB_OK) {
          payload.temp = (int)(DHT.temperature * 10);
          payload.humidity = (int)(DHT.humidity * 10);
        }
        else {
          payload.temp = payload.humidity = 0;
        }
        payload.pirCount = pirCount;
        pirCount = 0;
        sendPayload();
        vcc = payload.vcc2 = vccRead(); // measure and remember the VCC drop
      }
      
      // Setup another blip
      scheduler.timer(SEND_BLIP, VCC_SLEEP_MINS(vcc) * 600);
      break;
    }
      
    case FLASH_LED:
      // Flash the LED twice for the PIR trigger then wait for a bit
      switch(ledState) {
        case LED_IDLE:
          // The LED flashing has started,set the LED on and reschedule
          digitalWrite(LED_PIN, LOW);
          ledState = LED_ON1;
          scheduler.timer(FLASH_LED, LED_ON_TIME);
        break;
        case LED_ON1:
          // Set the LED off and reschedule
          digitalWrite(LED_PIN, HIGH);
          ledState = LED_OFF1;
          scheduler.timer(FLASH_LED, LED_OFF_TIME);
        break;
        case LED_OFF1:
          // The LED has finished the first flash cycle, set it on again
          digitalWrite(LED_PIN, LOW);
          ledState = LED_ON2;
          scheduler.timer(FLASH_LED, LED_ON_TIME);
        break;
        case LED_ON2:
          // Set the LED off and reschedule
          digitalWrite(LED_PIN, HIGH);
          ledState = LED_OFF2;
          scheduler.timer(FLASH_LED, LED_IDLE_TIME);
        break;
        case LED_OFF2:
          // The LED has finished the second flash
          // Cancel the schedule, allow the LED to be triggered again
          ledState = LED_IDLE;
          scheduler.cancel(FLASH_LED);
          ledFlashing = false;
        break;
      }
      break;
  }
}
