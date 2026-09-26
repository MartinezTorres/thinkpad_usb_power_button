// SPDX-License-Identifier: MIT
// ZY12PDN: STM32F030F4 + FUSB302B, Lenovo dock button.
// I2C wiring/bit-banging derived from Manuel Bleichenbacher's MIT code (2020).
#include <stdint.h>
#include <string.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/scb.h>


// PA10 = SCL (push-pull: this board has no SCL pull-up), PA9 = SDA.
uint8_t i2c_byte(uint8_t value, bool read, bool nack = false) {
    uint8_t result = 0;
    for (int bit = 7; bit >= 0; --bit) {
        if (read || (value & (1u << bit))) gpio_set(GPIOA, GPIO9);
        else gpio_clear(GPIOA, GPIO9);
        for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
        gpio_set(GPIOA, GPIO10);
        for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
        result = uint8_t((result << 1) | (gpio_get(GPIOA, GPIO9) != 0));
        gpio_clear(GPIOA, GPIO10);
    }
    // Receive the slave's ACK on writes; send ACK/NACK on reads.
    if (read && !nack) gpio_clear(GPIOA, GPIO9);
    else gpio_set(GPIOA, GPIO9);
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    gpio_set(GPIOA, GPIO10);
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    if (!read && gpio_get(GPIOA, GPIO9)) for (;;) { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO5); uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 500) {} gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 500) {} } // NACK: red/off forever
    gpio_clear(GPIOA, GPIO10);
    gpio_set(GPIOA, GPIO9);
    return result;
}

// Read/write one register, or a FIFO buffer, at FUSB302 I2C address 0x22.
uint8_t fusb(uint8_t reg, bool read, uint8_t value = 0,
             uint8_t* bytes = nullptr, unsigned count = 1) {
    if (!bytes) bytes = &value;
    gpio_clear(GPIOA, GPIO9);                       // START
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    gpio_clear(GPIOA, GPIO10);
    i2c_byte(0x44, false);
    i2c_byte(reg, false);
    if (read) {
        for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
        gpio_set(GPIOA, GPIO10);
        for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
        gpio_clear(GPIOA, GPIO9);                   // repeated START
        for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
        gpio_clear(GPIOA, GPIO10);
        i2c_byte(0x45, false);
    }
    for (unsigned i = 0; i < count; ++i) {
        if (read) bytes[i] = i2c_byte(0xff, true, i + 1 == count);
        else i2c_byte(bytes[i], false);
    }
    gpio_clear(GPIOA, GPIO9);
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    gpio_set(GPIOA, GPIO10);
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    gpio_set(GPIOA, GPIO9);                         // STOP
    for (volatile unsigned i = 0; i < 10; ++i) asm volatile("nop");
    return bytes[0];
}

// Send one SOP packet and block until the FUSB302 reports GoodCRC received.
void send(unsigned& tx_id, unsigned type, const uint32_t* words = nullptr,
          unsigned count = 0) {
    const uint16_t header = (count << 12) | (tx_id << 9) | 0x0040 | type;
    uint8_t fifo[40] = {0x12, 0x12, 0x12, 0x13, uint8_t(0x80 | (2 + 4 * count)),
                        uint8_t(header), uint8_t(header >> 8)};
    if (count) memcpy(fifo + 7, words, count * 4);
    unsigned length = 7 + count * 4;
    fifo[length++] = 0xff;                         // JAM_CRC
    fifo[length++] = 0x14;                         // EOP
    fifo[length++] = 0xfe;                         // TXOFF
    fifo[length++] = 0xa1;                         // TXON
    fusb(0x3e, true);                              // clear old TX indication
    fusb(0x43, false, 0, fifo, length);
    while (!(fusb(0x3e, true) & 0x04)) {}           // TXSENT; no recovery
    tx_id = (tx_id + 1) & 7;
}

unsigned tx_id = 0;
int rx_id = -1;

uint32_t pending_request_button_bits = 0;
uint32_t pending_ack_button_bits = 0;


unsigned poll() {

    if (fusb(0x41, true) & 0x20) return 0;          // RX FIFO empty
    uint8_t prefix[3], payload[32];
    fusb(0x43, true, 0, prefix, sizeof(prefix));
    const uint16_t header = prefix[1] | (uint16_t(prefix[2]) << 8);
    const unsigned count = (header >> 12) & 7, type = header & 31;
    fusb(0x43, true, 0, payload, count * 4 + 4);    // payload and CRC
    if ((prefix[0] & 0xe0) != 0xe0 || (header & 0x8000)) return 0;
    // USB-PD CRC-32 over this packet's header and payload, excluding the SOP token.
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < 2 + count * 4; ++i) {
        crc ^= i < 2 ? prefix[i + 1] : payload[i - 2];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    uint32_t received_crc;
    memcpy(&received_crc, payload + count * 4, sizeof(received_crc)); // little-endian
    if ((crc ^ 0xffffffffu) != received_crc) for (;;) { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO5 | GPIO7); uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 500) {} gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 500) {} } // CRC: purple/off forever
    if (!count && type == 1) return 0;             // hardware handles GoodCRC

    const int id = (header >> 9) & 7;
    if (!count && type == 13) {
        tx_id = 0; rx_id = -1;
        send(tx_id, 3);
        return 0;
    }
    if (id == rx_id) return 0;
    rx_id = id;
    uint32_t received[7] = {};
    memcpy(received, payload, count * 4);

    if (count && type == 1) {                      // Source_Capabilities
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t pdo = received[i];
            if ((pdo >> 30) == 0 && ((pdo >> 10) & 1023) == 100 &&
                (pdo & 1023) >= 10) {              // fixed 5 V, at least 100 mA
                const uint32_t request = ((i + 1) << 28) | 0x0100280a;
                send(tx_id, 2, &request, 1);       // Request 5 V / 100 mA
                break;
            }
        }
    } else if (!count && (type == 3 || type == 6)) {
        return type;                              // Accept / PS_RDY
    } else if (!count && type == 8) {              // Get_Sink_Cap
        const uint32_t capability = (100u << 10) | 10u;
        send(tx_id, 4, &capability, 1);
    } else if (!count && (type == 7 || type == 9 || type == 10 || type == 11)) {
        send(tx_id, 4);                            // Reject role swaps etc.
    } else if (count && type == 15) {
        const uint32_t request = received[0];
        if (!(request & 0x8000) || (request & 0xc0)) return 0;
        const unsigned vendor = request >> 16, command = request & 31;
        const unsigned position = (request >> 8) & 7;
        uint32_t reply[4] = {(request & 0xffff871fu) | 0x40, 0, 0, 0};
        unsigned length = 1;
        if (vendor == 0xff00 && command == 1) {     // Discover Identity
            reply[1] = 0x140017ef;                 // modal peripheral, Lenovo VID
            reply[2] = 0;                         // no certification XID
            reply[3] = 0xfffe0001;                 // experimental PID/version
            length = 4;
        } else if (vendor == 0xff00 && command == 2) {
            reply[1] = 0x17ef0000;                 // Discover SVIDs
            length = 2;
        } else if (vendor == 0x17ef && command == 3) {
            reply[1] = 1;                          // Discover Modes: one mode
            length = 2;
        } else if (vendor == 0x17ef && command == 4 && position == 1) {
            send(tx_id, 15, reply, length);         // Enter Mode ACK
            return 0x104;
        } else if (vendor == 0x17ef && command == 0x10 && count >= 2 && (position == 0 || position == 1)) {

            pending_ack_button_bits &= ~( received[1] << 8 );
            reply[1] = (received[1] & 0x00ffffffu) | pending_request_button_bits;
            send(tx_id, 15, reply, 3);              // Lenovo status + button event
            pending_ack_button_bits |= (pending_request_button_bits & 0x0E000000);
            pending_request_button_bits = 0;
            return 0x110;
        } else {
            if (command == 6) return 0;            // Attention has no VDM reply
            reply[0] = (reply[0] & ~0xc0u) | 0x80; // unsupported VDM: NAK
        }
        send(tx_id, 15, reply, length);
    }
    return 0;
}


inline void green()  { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO6); }
inline void red()    { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO5); }
inline void purple() { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO5 | GPIO7); }
inline void yellow() { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO5 | GPIO6); }
inline void blue()  { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO7); }
inline void cyan()  { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); gpio_clear(GPIOA, GPIO6 | GPIO7); }
inline void white() { gpio_clear(GPIOA, GPIO5 | GPIO6 | GPIO7); }
inline void off()   { gpio_set(GPIOA, GPIO5 | GPIO6 | GPIO7); }

#define error_leds(a, b, c, d) do {uint16_t since = timer_get_counter(TIM14); a(); while (uint16_t(timer_get_counter(TIM14) - since) < b); c(); while (uint16_t(timer_get_counter(TIM14) - since) < b+d); } while(true)


inline void try_send(uint32_t buttons) {

    yellow();
    {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 1000) poll(); }
    pending_request_button_bits = buttons;
    while (!(fusb(0x41, true) & 0x20)) poll(); // clear queue

    {uint16_t since = timer_get_counter(TIM14); while ((pending_request_button_bits || pending_ack_button_bits) && uint16_t(timer_get_counter(TIM14) - since) < 100) poll(); }

    if (pending_request_button_bits) {
        red();

        const uint32_t attention = 0x17ef8006;
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 100); }
        send(tx_id, 15, &attention, 1);
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 100); }

        {uint16_t since = timer_get_counter(TIM14); while ((pending_request_button_bits) && uint16_t(timer_get_counter(TIM14) - since) < 10000) poll(); }
        {uint16_t since = timer_get_counter(TIM14); while ((pending_ack_button_bits) && uint16_t(timer_get_counter(TIM14) - since) < 1000) poll(); }
    }

    if (pending_request_button_bits) error_leds(yellow, 1000, red, 3000);

    if (pending_ack_button_bits) {
        purple();

        const uint32_t attention = 0x17ef8006;
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 100); }
        send(tx_id, 15, &attention, 1);
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 100); }

        {uint16_t since = timer_get_counter(TIM14); while ((pending_ack_button_bits) && uint16_t(timer_get_counter(TIM14) - since) < 10000) poll(); }
        white();
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 1000) poll(); }
    }
    if (pending_ack_button_bits & 0x08000000) error_leds(blue, 1000, red, 1000);
    if (pending_ack_button_bits & 0x04000000) error_leds(yellow, 1000, red, 1000);
    if (pending_ack_button_bits & 0x02000000) error_leds(white, 1000, red, 1000);
}

int main() {
    const uint16_t leds = GPIO5 | GPIO6 | GPIO7;    // use 0 to disable the LEDs
    rcc_clock_setup_in_hsi_out_48mhz();
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOF);
    rcc_periph_clock_enable(RCC_TIM14);
    timer_set_prescaler(TIM14, 48000 - 1);          // free-running milliseconds
    timer_set_period(TIM14, 65535);
    timer_generate_event(TIM14, TIM_EGR_UG);
    timer_enable_counter(TIM14);                    // no interrupts or globals
    gpio_set(GPIOA, GPIO9 | GPIO10);
    gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO9 | GPIO10 | leds);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, GPIO9);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO10);
    gpio_mode_setup(GPIOF, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO1);

    for (int i = 0; i < 0; i++) {
        red();                                         
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 1000) {} } // Wait 4 seconds for Lenovo to awake
        purple();
        {uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 1000) {} } // Wait 4 seconds for Lenovo to awake
    }

    yellow();

    // Start the PHY. PA13 stays SWDIO; the interrupt pin is never enabled.
    fusb(0x0c, false, 3);                           // software + PD reset
    { uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 10) {} }
    fusb(0x0b, false, 0x0f);                        // keep PHY powered
    fusb(0x06, false, 0x20);                        // mask INT_N
    fusb(0x0a, false, 0xaf);                        // same event masks as working PHY
    fusb(0x0e, false, 0);
    fusb(0x0f, false, 0);
    fusb(0x05, false, 0x60);                        // receive threshold

    // Find the connected CC wire; do nothing else until attachment.
    unsigned cc = 1;
    for (;;) {
        fusb(0x02, false, cc == 1 ? 0x07 : 0x0b);  // Rd on both, measure one
        { uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 10) {} }
        fusb(0x40, true);
        if (fusb(0x40, true) & 3) break;
        cc = 3 - cc;
    }
    fusb(0x09, false, 7);                           // PHY's normal PD retries
    fusb(0x03, false, cc == 1 ? 0x25 : 0x26);      // Sink/UFP, PD2, auto GoodCRC

    // Complete power negotiation, then Lenovo mode entry and its first query.
    green();
    while (poll() != 3) {}              // Request -> Accept
    red();
    while (poll() != 6) {}              // PS_RDY
    purple();
    while (poll() != 0x104) {}          // Enter Mode -> our ACK
    white();
    while (poll() != 0x110) {}          // first status -> our ACK
    yellow();

    { uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 8000) poll(); }

    try_send(0x01000000);
    try_send(0x0D000000);
    try_send(0x01000000);
    { uint16_t since = timer_get_counter(TIM14); while (uint16_t(timer_get_counter(TIM14) - since) < 4000) poll(); }
    try_send(0x0D000000);

    green();
    for (;;) {
        

        while (gpio_get(GPIOF, GPIO1)) poll();                                                                         // wait for button press 
        try_send(0x0D000000);
        purple();
        while (!gpio_get(GPIOF, GPIO1)) poll();
        green();
    }
}
