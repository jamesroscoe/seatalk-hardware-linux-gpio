#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/gpio.h>
#include "../seatalk/seatalk_hardware_layer.h"
#include "../seatalk/seatalk_transport_layer.h"

// The Seatalk bus has one single wire pulled to High (+12V) when no value is being asserted.
// A level translater must be built to convert that +12V high signal to something compatible with
// your hardware. Thomas Knauf has an example schematic here:
//
// http://www.thomasknauf.de/rap/seatalk3.htm#Uni
//
// This can't be stressed enough: USE AT YOUR OWN RISK! No warrantee is expressed or implied. If you fry your hardware it's on you.
//
// The hardware schematic linked above splits the signal into an input line and an output line.
//
// The pin to read input signals from
#define GPIO_RXD_PIN 23
#define GPIO_RXD_DESC "Seatalk RxD pin"

// The pin to write output signals to
#define GPIO_TXD_PIN 24
#define GPIO_TXD_DESC "Seatalk TxD pin"

// Device name to show in Linux system status
#define GPIO_DEVICE_DESC "Seatalk communications driver"

// The level translator in this example expects a 0 to be sent as a High value and 1 as a Low
// so to detect the start bit (0 to 1 transition) we need to be informed of a falling edge
#define START_BIT_DIRECTION IRQF_TRIGGER_RISING

// RX and TX logic levels are stored as separate constants here in case the hardware
// reverses one but not the other for some reason
#define GPIO_RX_LOW_VALUE 1
#define GPIO_RX_HIGH_VALUE 0
#define GPIO_TX_LOW_VALUE 1
#define GPIO_TX_HIGH_VALUE 0

// All communications will be through a single port so hard-code to port zero
#define SEATALK_PORT 0

// transmit and receive
// bit timer period; 1000000000 ns/s / 4800 bits/s = 208333 ns/bit
#define BIT_INTERVAL 208333
// start receive timer 1/4 bit after triggering edge of start bit
// this gives some time for the signal level to settle
#define START_BIT_DELAY (BIT_INTERVAL / 4)
#define DEBOUNCE_NANOS 60000

// receive data state

// Linux High Resolution timer will be fired every BIT_INTERVAL nanoseconds while we are actively receiving a byte
// The timer is started by the receive data interrupt request handler (rxd_irq_handler) function and is restarted after each firing until all bits in the data byte have been received.
// When the final bit is received the timer will not be restarted.
static struct hrtimer hrtimer_rxd;

// prototype for the timer function
// returns enum indicating whether to fire the timer again (restart) or to go idle
static enum hrtimer_restart receive_bit(struct hrtimer *timer);

// are we currently debouncing a signal state transition?
int debouncing = 0;

// transmit data state

// Linux High Resolution timer will be fired every BIT_INTERVAL nanoseconds while we are actively sending a byte
// The timer is started by initiate_seatalk_hardware_transmitter() and is restarted after each firing until all bits in the data byte have been sent.
// When the final bit is received the transport layer checks for new data in the transmit queue and restarts only if a queued datagram is present.
static struct hrtimer hrtimer_txd;

// prototype for the timer function
// returns enum indicating whether to fire the timer again (restart) or to go idle
static enum hrtimer_restart transmit_bit(struct hrtimer *timer);

// interrupt requset handler triggered when the input signal line transitions from 0 to 1 (Logical Low to High)
// When the bus is idle this indicates the start of a new data byte. When the bus is in some other state then this signal should be ignored.
static irqreturn_t rxd_irq_handler(int irq, void *dev_id, struct pt_regs *regs) {
  unsigned long flags;

  // disable hardware interrupts to prevent re-entry into this IRQ handler
  local_irq_save(flags);
  // debounce the state transition by ignoring IRQs for DEBOUNCE_NANOS nanoseconds after each "real" one
  if (debouncing) {
    pr_info("debouncing\n");
  } else {
    // seatalk_transport_layer.c manages the state logic around sending and receiving data so call into it
    // seatalk_initiate_receive_character returns truthy if we are starting a new byte
    if (seatalk_initiate_receive_character(SEATALK_PORT)) {
      // This 0 to 1 transition was a start bit so we schedule the receive event for first bit.
      // Wait 1 bit timing plus a bit extra (START_BIT_DELAY) so that we sample the logic value after a debouncing period in order to account for slow logic level transitions
      hrtimer_start(&hrtimer_rxd, ktime_set(0, BIT_INTERVAL + START_BIT_DELAY), HRTIMER_MODE_REL);
    }
  }
  // restore hardware interrupts
  local_irq_restore(flags);
  // let OS know this IRQ has been handled successfully
  return IRQ_HANDLED;
}

// read the logic level from the input pin
int seatalk_get_hardware_bit_value(int seatalk_port) {
  return gpio_get_value(GPIO_RXD_PIN) ? GPIO_RX_HIGH_VALUE : GPIO_RX_LOW_VALUE; // normal sense
}

// write the desired logic level to the output pin
void seatalk_set_hardware_bit_value(int seatalk_port, int bit_value) {
  gpio_set_value(GPIO_TXD_PIN, (bit_value == GPIO_TX_HIGH_VALUE) ? 1 : 0); // normal sense
//#ifdef DEBUG
//  pr_info("set GPIO_TXD_PIN to %d\n", bit_value);
//#endif
}

// called by hrtimer_rxd when it expires
// This function passes the receive data logic off to seatalk_transport_layer.c
static enum hrtimer_restart receive_bit(struct hrtimer *timer) {
  // after a character has been received there is a rising-edge stop bit with a lot
  // of signal bounce. Wait DEBOUNCE_NANOS after the stop bit timing to ignore bounces
  if (debouncing) {
    // The stop bit debounce period has expired so clear the debouncing flag to allow the interrupt handler to stop ignoring level transitions.
    debouncing = 0;
    // debouncing in this way only happens on stop bit so we do not restart the timer so we know we are at the end of the byte and can safely idle the receiver until the next start bit detected.
    return HRTIMER_NORESTART;
  } else {
    // calculate the wake-up time for the next bit now in case the receive bit logic runs a long time. Pretty much unnecessary except on the slowest of hardware but better safe than sorry.
    hrtimer_forward_now(&hrtimer_rxd, ktime_set(0, BIT_INTERVAL));
    // Dispatch seatalk_transport_layer.c logic to receive a single bit. A truthy return value indicates more bits are expected
    if (seatalk_receive_bit(SEATALK_PORT)) {
      // more bits are expected. Restart the timer for one BIT_INTERVAL from now
      return HRTIMER_RESTART;
    } else {
      // no more bits are expected. Restart the timer for DEBOUNCE_NANOS to force stop bit wobbles to be ignored by 0 to 1 logic level transition interrupt handler.
      hrtimer_forward_now(&hrtimer_rxd, ktime_set(0, DEBOUNCE_NANOS));
      // Tell interrupt handler to ignore transitions
      debouncing = 1;
      return HRTIMER_RESTART;
    }
  }
}

// called by hrtimer_txd when it expires
// This function passes the transmit data logic off to seatalk_transport_layer.c
static enum hrtimer_restart transmit_bit(struct hrtimer *timer) {
  // calculate the wake-up time for the next bit (if any)
  // (done now to limit time lag on very slow machines)
  hrtimer_forward_now(&hrtimer_txd, ktime_set(0, BIT_INTERVAL));
  // Dispatch seatalk_transport_layer.c logic to send a single bit. A truthy return value indicates there are more bits to send
  if (seatalk_transmit_bit(SEATALK_PORT)) {
    // more bits to send. Restart the timer for one BIT_INTERVAL from now
    return HRTIMER_RESTART;
  } else {
    // no more bits to send. Allow timer to idle. Transmission will need to be awakened with call to seatalk_initiate_hardware_transmitter() function (that call made by seatalk_transport_layer.c)
    return HRTIMER_NORESTART;
  }
}

// called from seatalk_transport_layer.c to start hrtimer_txd to begin sending a new data byte
void seatalk_initiate_hardware_transmitter(int seatalk_port, int bit_delay) {
  // Reawaken the transmittter. Wait bit_delay BIT_INTERVALS as guard time after the last byte (from any device) on the bus
  // First step: stop pending timer (if any)
  hrtimer_cancel(&hrtimer_txd);
  // schedule new timer after delay period
  hrtimer_start(&hrtimer_txd, ktime_set(0, BIT_INTERVAL * bit_delay), HRTIMER_MODE_REL);
}

// initialize the GPIO pins
int seatalk_init_hardware_signal(void) {
  // initialize rx
  // reserve GPIO_RXD_PIN (default GPIO 23)
  if (gpio_request(GPIO_RXD_PIN, GPIO_RXD_DESC)) {
    pr_info("Unable to request GPIO RxD pin %d", GPIO_RXD_PIN);
    goto cleanup;
  }
  // set pin direction to input
  gpio_direction_input(GPIO_RXD_PIN);
  // initialize the receive timer but don't start it
  hrtimer_init(&hrtimer_rxd, CLOCK_REALTIME, HRTIMER_MODE_REL);
  hrtimer_rxd.function = receive_bit;

  // initialize tx
  // reserve GPIO_TXD_PIN (default GPIO 24)
  if (gpio_request(GPIO_TXD_PIN, GPIO_TXD_DESC)) {
    pr_info("Unable to request GPIO TxD pin %d", GPIO_TXD_PIN);
    goto cleanup_rx;
  }
  // set pin direction to output
  gpio_direction_output(GPIO_TXD_PIN, 1);
  // set at-rest pin value to high
  seatalk_set_hardware_bit_value(SEATALK_PORT, 1);
  // initialize the transmit timer bit don't start it
  hrtimer_init(&hrtimer_txd, CLOCK_REALTIME, HRTIMER_MODE_REL);
  hrtimer_txd.function = transmit_bit;

  return 0;

cleanup_rx:
  gpio_free(GPIO_RXD_PIN);
cleanup:
  return -1;
}

// variable to hold the interrupt request number we reserve
// stored so it can be released on exit
short int gpio_rxd_irq = 0;

// initialize the interrupt request handler for receiving data
int seatalk_init_hardware_irq(void) {
  // hook irq for RxD GPIO pin
  // get IRQ number for input pin
  if ((gpio_rxd_irq = gpio_to_irq(GPIO_RXD_PIN)) < 0) {
    pr_info("Unable to map GPIO pin to irq");
    goto cleanup;
  }
  // set up interrupt vector for START_BIT_DIRECTION (falling edge if using normal sense)
  if (request_irq(gpio_rxd_irq, (irq_handler_t) rxd_irq_handler, START_BIT_DIRECTION, GPIO_RXD_DESC, GPIO_DEVICE_DESC)) {
    pr_info("Unable to request IRQ %d", gpio_rxd_irq);
    goto cleanup;
  }
  pr_info("Hooked falling-edge IRQ %d for GPIO pin %d", gpio_rxd_irq, GPIO_RXD_PIN);
  // initialize debounce timer
  return 0;

cleanup:
  gpio_free(GPIO_TXD_PIN);
  gpio_free(GPIO_RXD_PIN);
  return -1;
}

// release the GPIO pins
void seatalk_exit_hardware_signal(void) {
  // release RxD and TxD pins
  gpio_free(GPIO_TXD_PIN);
  gpio_free(GPIO_RXD_PIN);
  // cancel timers
  hrtimer_cancel(&hrtimer_rxd);
  hrtimer_cancel(&hrtimer_txd);
  return;
}

// release the interrupt request handler
void seatalk_exit_hardware_irq(void) {
  // release IRQ
  free_irq(gpio_rxd_irq, GPIO_DEVICE_DESC);
}

