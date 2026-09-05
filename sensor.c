/*
 * ============================================================================
 * Project: LPC2148 10-Bit ADC Humidity Sensor Interface with I2C 16x2 LCD
 * Target Microcontroller: NXP LPC2148 (ARM7TDMI-S)
 * Compiler: Keil uVision (ARMCC / RealView)
 * ============================================================================
 */

#include <lpc214x.h>
#include <stdio.h>

// I2C Slave Address for PCF8574 I2C LCD backpack
#define LCD_ADDR 0x27 

char display_str[32];

/* ============================================================================
 * 1. DELAY FUNCTION
 * ============================================================================ */
void delay(unsigned int time) 
{
    unsigned int i, j;
    for (i = 0; i < time; i++) {
        for (j = 0; j < 800; j++);
    }
}

/* ============================================================================
 * 2. I2C0 DRIVER FUNCTIONS (Master Mode)
 * ============================================================================ */

// Initialize I2C0 on P0.2 (SCL0) and P0.3 (SDA0) at 100 kHz
void init_i2c(void) 
{
    // PINSEL0 bits 5:4 = 01 (SCL0 on P0.2), bits 7:6 = 01 (SDA0 on P0.3)
    PINSEL0 |= 0x00000050; 
    
    I2C0CONCLR = 0x6C;  // Clear AAC, SIC, STAC, I2ENC control bits
    I2C0SCLH = 60;      // Set SCL High time (at 12MHz PCLK -> ~100kHz)
    I2C0SCLL = 60;      // Set SCL Low time
    I2C0CONSET = 0x40;  // Enable I2C0 interface (I2EN = 1)
}

// Generate I2C START Condition
void i2c_start(void) 
{
    I2C0CONSET = 0x20;             // Set STA bit (STA = 1)
    while (!(I2C0CONSET & 0x08));  // Wait until SI flag is set (Interrupt flag = 1)
    I2C0CONCLR = 0x28;             // Clear STA and SI flags
}

// Generate I2C STOP Condition
void i2c_stop(void) 
{
    I2C0CONSET = 0x10;             // Set STO bit (STO = 1)
    I2C0CONCLR = 0x08;             // Clear SI flag
    delay(5);
}

// Write 1 Byte over I2C and wait for ACK
void i2c_write(unsigned char data) 
{
    I2C0DAT = data;                // Load byte into I2C Data Register
    I2C0CONCLR = 0x08;             // Clear SI flag to trigger hardware transmission
    while (!(I2C0CONSET & 0x08));  // Wait until SI flag is set (Byte sent & ACK received)
    delay(1);                      // Small stabilization delay
}

/* ============================================================================
 * 3. I2C 16x2 LCD DRIVER (4-bit Nibble Mode via PCF8574 Expander)
 * ============================================================================ */

// Send Command (is_data = 0) or Character (is_data = 1) to LCD over I2C
void lcd_send(unsigned char data, unsigned char is_data) 
{
    unsigned char upper = data & 0xF0;          // Upper 4-bits
    unsigned char lower = (data << 4) & 0xF0;   // Lower 4-bits
    unsigned char mode  = is_data ? 0x01 : 0x00;// RS pin: 1 for Data, 0 for Command
    unsigned char backlight = 0x08;             // Backlight ON bit

    i2c_start();
    i2c_write(LCD_ADDR << 1);                   // Send 7-bit Address + Write bit (0)
    
    // Send Upper Nibble with Enable Pulse (High then Low)
    i2c_write(upper | mode | backlight | 0x04); // EN = 1
    i2c_write(upper | mode | backlight);        // EN = 0
    
    // Send Lower Nibble with Enable Pulse (High then Low)
    i2c_write(lower | mode | backlight | 0x04); // EN = 1
    i2c_write(lower | mode | backlight);        // EN = 0
    
    i2c_stop();
}

// Initialize 16x2 LCD in 4-bit mode
void init_lcd(void) 
{
    delay(100);
    lcd_send(0x33, 0); // 4-bit initialization sequence
    lcd_send(0x32, 0); 
    lcd_send(0x28, 0); // 2-line mode, 5x8 character font
    lcd_send(0x0C, 0); // Display ON, Cursor OFF
    lcd_send(0x06, 0); // Auto-increment cursor position
    lcd_send(0x01, 0); // Clear screen
    delay(50);
}

// Set cursor to (row: 0 or 1, col: 0 to 15)
void lcd_cursor(unsigned char row, unsigned char col) 
{
    unsigned char addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send(addr, 0);
}

// Print string on LCD
void lcd_print(char *str) 
{
    while (*str) {
        lcd_send(*str++, 1);
    }
}

/* ============================================================================
 * 4. 10-BIT ADC DRIVER (Channel AD0.4 on Pin P0.25)
 * ============================================================================ */
unsigned int read_adc(void) 
{
    static int adc_init = 0;
    
    if (!adc_init) {
        // PINSEL1 bits 19:18 = 01 (Select AD0.4 on Pin P0.25)
        PINSEL1 |= (1 << 18); 
        adc_init = 1;
    }
    
    // AD0CR Configuration:
    // Bit 4  (0x10)     = Select Channel AD0.4
    // Bits 15:8 (10<<8) = CLKDIV = 10 (ADC Clock <= 4.5MHz)
    // Bit 21 (1<<21)    = PDN = 1 (Power ON ADC)
    // Bit 24 (1<<24)    = START = 001 (Start conversion now)
    AD0CR = 0x09200410; 
    
    // Wait until conversion is DONE (Bit 31 of AD0GDR flips to 1)
    while (!(AD0GDR & (1U << 31))); 
    
    // Extract 10-bit result from Bits 15:6
    return (AD0GDR >> 6) & 0x03FF; 
}

/* ============================================================================
 * 5. MAIN APPLICATION LOOP
 * ============================================================================ */
int main(void) 
{
    unsigned int adc_result;
    
    init_i2c(); // Initialize I2C0 peripheral
    init_lcd(); // Initialize 16x2 LCD
    
    while (1) 
    {
        // 1. Read the 10-bit humidity/moisture sensor voltage
        adc_result = read_adc(); 
        
        // 2. Display raw ADC value on Line 1
        lcd_cursor(0, 0);
        sprintf(display_str, "ADC: %u    ", adc_result);
        lcd_print(display_str);
        
        // 3. Evaluate Moisture Threshold (512 corresponds to 2.5V threshold)
        lcd_cursor(1, 0);
        if (adc_result > 512) {
            lcd_print("Moisture Detected  ");
        } else {
            lcd_print("Dry (Low Moisture) ");
        }
        
        delay(1000); // Sample every 1 second
    }
}