// SPI setup for ATmega (JeeNode) and ATtiny (JeeNode Micro)
// ATtiny thx to @woelfs, see http://jeelabs.net/boards/11/topics/6493

// https://jeelabs.org/book/1522c/index.html

/*
 SPCR: SPI Control Register
 Bit 7: SPIE - Enables the SPI interrupt when 1
 Bit 6: SPE  - Enables the SPI when 1
 Bit 5: DORD - Sends data least Significant Bit First when 1, most Significant Bit first when 0
 Bit 4: MSTR - Sets the Arduino in master mode when 1, slave mode when 0
 Bit 3: CPOL - Sets the data clock to be idle when high if set to 1, idle when low if set to 0
 Bit 2: CPHA - Samples data on the falling edge of the data clock when 1, rising edge when 0
 Bits 1-0: SPR1 and SPR0 - Sets the SPI speed, 00 is fastest (4MHz) 11 is slowest (250KHz)

 SPSR: SPI Status register
 SPIF: SPI Interrupt Flag
 
*/

#include <SPI.h> 

/* Class for generic Arduino style SPI library / support for ATTINY1624/megatinycore */
template<int N>
class SPIDev1624 {

public:
  static void master (int div) {
    digitalWrite(N, 1);
    pinMode(N, OUTPUT);
    SPI.begin();
  }

  static uint8_t rwReg (uint8_t cmd, uint8_t val) {
    digitalWrite(N, 0);
    SPI.transfer(cmd);
    uint8_t in = SPI.transfer(val);
    digitalWrite(N, 1);
    return in;
  }
};
