#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

// ==========================================
// PIN DEFINITIONS & CONSTANTS (Spec v4.5)
// ==========================================
#define PB_MOSI   PB0  // Master Out Slave In[cite: 1]
#define PB_MISO   PB1  // Master In Slave Out[cite: 1]
#define PB_SCK    PB2  // Serial Clock[cite: 1]
#define PB_GPIO   PB3  // Dual-mode pin: Analog Input (ADC3) or Digital Input[cite: 1]
#define PB_CS     PB4  // Chip Select & PCINT4 Wake-up Pin[cite: 1]

// ==========================================
// MEMORY & REGISTER MAP STORAGE
// ==========================================
static uint8_t user_ram[32];     // Addresses 0–31: General-purpose volatile RAM[cite: 1]
static uint8_t flag0 = 0x00;     // Address 32: Control & Status Register[cite: 1]
static uint8_t flag1 = 0x00;     // Address 33: Status, Address & ADC Data Register[cite: 1]
static uint8_t flag2 = 0x00;     // Address 34: Dynamic Payload & Result Storage[cite: 1]

// Deferred EEPROM Write Buffers
static uint8_t pending_eeprom_addr = 0;
static uint8_t pending_eeprom_data = 0;

// ==========================================
// FUNCTION PROTOTYPES (Modular Architecture)
// ==========================================
void power_down(void);
void adc_handling(void);
void gpio_handling(void);
uint8_t ram_management(uint8_t addr, uint8_t data, uint8_t is_write);
uint8_t eeprom_read(uint8_t addr);
void eeprom_write_handling(void);
uint8_t spi_transfer_byte(uint8_t out_byte);
void spi_management(void);

// ==========================================
// INTERRUPT SERVICE ROUTINE (Wake-Up)
// ==========================================
// Pin Change Interrupt on PB4 (CS falling edge)[cite: 1]
ISR(PCINT0_vect) {
    if (!(PINB & (1 << PB_CS))) {
        // Disable PCINT during active transaction to prevent re-triggering
        GIMSK &= ~(1 << PCIE);
        spi_management();
        // Re-enable PCINT after transaction completes
        GIMSK |= (1 << PCIE);
    }
}

// ==========================================
// 1. POWER DOWN / SLEEP MANAGEMENT
// ==========================================
void power_down(void) {
    // Set Power-Down sleep mode (SM1 = 1, SM0 = 0) and enable sleep (SE = 1)
    MCUCR = (1 << SM1) | (1 << SE);
    
    // Execute assembly sleep instruction
    __asm__ __volatile__("sleep" ::: "memory");
    
    // Clear sleep enable flag upon wake-up
    MCUCR &= ~(1 << SE);
}

// ==========================================
// 2. ADC HANDLING ARCHITECTURE
// ==========================================
void adc_handling(void) {
    // Active only when ADC is ON (FLAG0.bit4 = 1) and no EEPROM write is pending[cite: 1]
    if ((flag0 & (1 << 4)) && (!(flag0 & (1 << 0)))) {
        // Select ADC3 channel (PB3) via MUX[1:0] = 11
        ADMUX = (1 << MUX1) | (1 << MUX0);
        
        // Enable ADC and start conversion
        ADCSRA = (1 << ADEN) | (1 << ADSC);
        while (ADCSRA & (1 << ADSC)); // Wait for conversion completion
        
        uint8_t low = ADCL;
        uint8_t high = ADCH;
        
        // Populate FLAG1 bits 1–0 and FLAG2 with conversion results[cite: 1]
        flag1 = (flag1 & ~0x03) | (low & 0x03);
        flag2 = high;
    } else if (!(flag0 & (1 << 4))) {
        // Ensure ADC is fully disabled for power saving when FLAG0.bit4 = 0[cite: 1]
        ADCSRA &= ~(1 << ADEN);
    }
}

// ==========================================
// 3. DUAL-MODE GPIO HANDLING
// ==========================================
void gpio_handling(void) {
    if (!(flag0 & (1 << 4))) {
        // ADC OFF: PB3 functions as a floating digital input[cite: 1]
        DDRB &= ~(1 << PB_GPIO);
    } else {
        // ADC ON: PB3 functions as an analog input channel[cite: 1]
        DDRB &= ~(1 << PB_GPIO);
    }
}

// ==========================================
// 4. RAM & REGISTER MANAGEMENT
// ==========================================
uint8_t ram_management(uint8_t addr, uint8_t data, uint8_t is_write) {
    if (addr < 32) {
        if (is_write) {
            // Check RAM Read-Only protection state (FLAG0.bit5)[cite: 1]
            if (!(flag0 & (1 << 5))) {
                user_ram[addr] = data;
            }
        }
        return user_ram[addr];
    } 
    else if (addr == 32) { // FLAG0 (Control & Status)[cite: 1]
        if (is_write) {
            // Allow writing to control bits (Bits 4–1), protect internal status bits
            flag0 = (flag0 & 0xE1) | (data & 0x1E);
        }
        return flag0;
    } 
    else if (addr == 33) { // FLAG1 (Status & Address) - Read-Only via SPI[cite: 1]
        return flag1;
    } 
    else if (addr == 34) { // FLAG2 (Dynamic Payload) - Read-Only via SPI[cite: 1]
        return flag2;
    } 
    else if (addr == 35) { // GPIO Digital Read Slot (RAM-mapped address 35)[cite: 1]
        return (PINB & (1 << PB_GPIO)) ? 0x01 : 0x00;
    }
    return 0x00;
}

// ==========================================
// 5. EEPROM READ ROUTINE
// ==========================================
uint8_t eeprom_read(uint8_t addr) {
    if (addr > 63) return 0xFF;
    while (EECR & (1 << EEPE)); // Wait for previous write to clear
    EEAR = addr;
    EECR |= (1 << EERE);
    return EEDR;
}

// ==========================================
// 6. EEPROM WRITE & VERIFICATION HANDLING
// ==========================================
void eeprom_write_handling(void) {
    // Executed post-CS HIGH when PENDING flag is set (FLAG0.bit0 == 1)[cite: 1, 2]
    if (flag0 & (1 << 0)) {
        uint8_t addr = pending_eeprom_addr;
        uint8_t payload = pending_eeprom_data;
        
        if (addr <= 62) { // Address 63 is strictly protected[cite: 1]
            // Pre-Write Internal Read to prevent unnecessary wear[cite: 1, 2]
            uint8_t current_val = eeprom_read(addr);
            
            if (current_val == payload) {
                flag1 |= (1 << 7);  // Set write skip status[cite: 1, 2]
                flag1 &= ~(1 << 6); // Clear verification flag
            } else {
                flag1 &= ~(1 << 7); // Clear write skip status
                
                while (EECR & (1 << EEPE));
                EEAR = addr;
                EEDR = payload;
                
                // Atomic EEPROM write sequence
                EECR |= (1 << EEMPE);
                EECR |= (1 << EEPE);
                
                // Hardware EEPE Polling Guard (ensures charge pump finishes)[cite: 2]
                while (EECR & (1 << EEPE));
                
                // Post-Write Verification Read[cite: 1, 2]
                uint8_t verify_val = eeprom_read(addr);
                if (verify_val == payload) {
                    flag1 |= (1 << 6);  // Verification success[cite: 1]
                } else {
                    flag1 &= ~(1 << 6); // Verification failure
                }
            }
        }
        
        // Cleanup state[cite: 1, 2]
        flag2 = 0x00;
        flag0 &= ~(1 << 0); // Reset PENDING flag
    }
}

// ==========================================
// 7. BIT-BANGED SPI MANAGEMENT & PROTOCOL
// ==========================================
uint8_t spi_transfer_byte(uint8_t out_byte) {
    uint8_t in_byte = 0;
    for (uint8_t i = 0; i < 8; i++) {
        // Shift out on MISO (PB1)
        if (out_byte & 0x80) {
            PORTB |= (1 << PB_MISO);
        } else {
            PORTB &= ~(1 << PB_MISO);
        }
        out_byte <<= 1;

        // Wait for SCK (PB2) rising edge
        while (!(PINB & (1 << PB_SCK))) {
            if (PINB & (1 << PB_CS)) return 0; // Abort if CS goes high
        }

        // Sample MOSI (PB0)
        in_byte <<= 1;
        if (PINB & (1 << PB_MOSI)) {
            in_byte |= 0x01;
        }

        // Wait for SCK (PB2) falling edge
        while (PINB & (1 << PB_SCK)) {
            if (PINB & (1 << PB_CS)) return 0;
        }
    }
    return in_byte;
}

void spi_management(void) {
    // Check if system is busy; if so, reject with 0xAA error code[cite: 1]
    if (flag0 & (1 << 7)) {
        while (!(PINB & (1 << PB_CS))) {
            spi_transfer_byte(0xAA);
        }
        return;
    }

    // Receive command header byte: [2-bit opcode][6-bit address/modifier][cite: 1]
    uint8_t cmd = spi_transfer_byte(0x00);
    uint8_t opcode = (cmd >> 6) & 0x03;
    uint8_t addr_mod = cmd & 0x3F;

    if (opcode == 0x00) {
        // RAM & Register Read[cite: 1]
        uint8_t val = ram_management(addr_mod, 0, 0);
        spi_transfer_byte(val);
    } 
    else if (opcode == 0x01) {
        // RAM & Register Write[cite: 1]
        uint8_t payload = spi_transfer_byte(0x00);
        if (addr_mod != 33 && addr_mod != 34) { // Block FLAG1/FLAG2 writes[cite: 1]
            ram_management(addr_mod, payload, 1);
        }
    } 
    else if (opcode == 0x02 || opcode == 0x03) {
        if (opcode == 0x02) {
            // ROM Read[cite: 1]
            uint8_t rom_val = eeprom_read(addr_mod);
            spi_transfer_byte(rom_val);
        } else {
            // ROM Write (Deferred until CS goes HIGH)[cite: 1]
            uint8_t rom_payload = spi_transfer_byte(0x00);
            if (addr_mod <= 62 && !(flag0 & (1 << 6))) { // Check ROM Read-Only protection[cite: 1]
                pending_eeprom_addr = addr_mod;
                pending_eeprom_data = rom_payload;
                flag1 = (flag1 & ~0xFC) | ((addr_mod & 0x3F) << 2); // Store target address[cite: 1]
                flag2 = rom_payload; // Buffer payload[cite: 1]
                flag0 |= (1 << 0);   // Set PENDING flag[cite: 1]
            }
        }
    }

    // Wait for framing boundary (CS goes HIGH)[cite: 1, 2]
    while (!(PINB & (1 << PB_CS)));

    // Auto-Sleep Governor: Execute deferred write post-transaction if pending[cite: 1, 2]
    if (flag0 & (1 << 0)) {
        flag0 |= (1 << 7); // Set BUSY flag
        eeprom_write_handling();
        flag0 &= ~(1 << 7); // Clear BUSY flag
    }
}

// ==========================================
// MAIN ENTRY POINT (Bare-Metal)
// ==========================================
int main(void) {
    // Disable ADC by default for power conservation[cite: 1]
    ADCSRA &= ~(1 << ADEN);

    // Configure Pin Directions: MISO as output, others as inputs
    DDRB = (1 << PB_MISO);
    PORTB = (1 << PB_CS); // Enable pull-up resistor on CS (PB4)

    // Enable Pin Change Interrupt on PB4 (`CS`) for hardware wake-up[cite: 1]
    GIMSK |= (1 << PCIE);
    PCMSK |= (1 << PCINT4);

    // Enable Global Interrupts
    sei();

    // Infinite Background Loop
    while (1) {
        gpio_handling();
        adc_handling();

        // Enter deep power-down sleep if no jobs are pending and ADC is OFF[cite: 1, 2]
        if (!(flag0 & (1 << 0)) && !(flag0 & (1 << 4))) {
            power_down();
        }
    }

    return 0;
}