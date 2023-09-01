/*


  emonBase rfm69pi LowPowerLabs radio format

   ------------------------------------------
  Part of the openenergymonitor.org project

  Authors: Glyn Hudson, Trystan Lea & Robert Wall
  Builds upon JCW JeeLabs RF69 Driver and Arduino

  Licence: GNU GPL V3


Change Log:
*/
const char *firmware_version = {"1.1.1\n\r"};
/*

*********************************************************************************************
*                                                                                           *
*                                  IMPORTANT NOTE                                           *
*                                                                                           *
* When compiling for the RFM2Pi:                                                            *
*  In IDE, set Board to "Arduino Pro or Pro Mini" & Processor to "ATmega328P (3.3V, 8MHz)"  *
* When compiling for the RFM69Pi:                                                           *
*  In IDE, set Board to "Arduino Uno"                                                       *
*                                                                                           *
* The output file used must NOT be the "with_bootloader" version, else the processor will   *
*  be locked.                                                                               *
*********************************************************************************************
*/

// #define SAMPPIN 19
#include "Arduino.h"
#include <avr/pgmspace.h>
#include <EEPROM.h>

// OEM EEPROM library
#include "emonEProm.h"

//Prototypes
bool lockout(char c);

// Used to select radio format
#define RFM69_JEELIB_NATIVE 2
#define RFM69_LOW_POWER_LABS 3
// This is now part of the PIO build chain - platform.ini
//#define RadioFormat RFM69_JEELIB_NATIVE

// RFM interface
#if RadioFormat == RFM69_LOW_POWER_LABS
#include <RFM69.h>
RFM69 radio;
#else
#if defined(MEGATINYCORE)
//For ATTINY1624 - the JEELIB driver clashes library names with "spi.h" which makes 
//this code fail here without manual editing of the rf69.h file
#include "SPIDev1624.h" // Requires "RFM69 Native" JeeLib Driver
#include "rf69.h"
RF69<SPIDev1624<PIN_SPI_SS>> rf;
#else
#include "spi.h" // Requires "RFM69 Native" JeeLib Driver
#include "rf69.h"
RF69<SpiDev10> rf;
#endif

#endif

byte nativeMsg[66]; // 'Native' format message buffer

#define MAXMSG 66                                                      // Max length of o/g message
char outmsg[MAXMSG];                                                   // outgoing message (to emonGLCD etc)
byte outmsgLength;                                                     // length of message: non-zero length triggers transmission

bool verbose = true;

struct
{ // Ancilliary information
  byte srcNode = 0;
  byte msgLength = 0;
  signed char rssi = -127;
  bool crc = false;
} rfInfo;

enum rfband {RFM_433MHZ = 1, RFM_868MHZ, RFM_915MHZ };                 // frequency band.
bool rfChanged = false;                                                // marker to re-initialise radio
#define RFTX 0x01                                                      // Control of RFM - transmit enabled


//----------------------------emonBase Settings------------------------------------------------------------------------------------------------
const unsigned long BAUD_RATE = 38400;

void single_LED_flash(void);
void double_LED_flash(void);
void getCalibration(void);
static void showString(PGM_P s);

//---------------------------- Settings - Stored in EEPROM and shared with config.ino ------------------------------------------------
struct
{
  byte RF_freq = RFM_433MHZ; // Frequency of radio module can be RFM_433MHZ, RFM_868MHZ or RFM_915MHZ.
  byte rfPower = 25;         // Power when transmitting
  byte networkGroup = 210;   // wireless network group, must be the same as emonBase / emonPi and emonGLCD. OEM default is 210
  byte nodeID = 5;           // node ID for this emonBase.
  byte rfOn = 0x1;           // Turn transmitter on -receiver is always on
} EEProm;

uint16_t eepromSig = 0x0017; // oemEProm signature - see oemEEProm Library documentation for details.

//--------------------------- hard-wired connections ----------------------------------------------------------------------------------------
#if defined(MEGATINYCORE)
const byte LEDpin = PIN_A5; // USB board LED - on when HIGH
#else
const byte LEDpin = 9; // emonPi LED - on when HIGH
#endif

// Use D5 for ISR timimg checks - only if Pi is not connected!

//-------------------------------------------------------------------------------------------------------------------------------------------


/**************************************************************************************************************************************************
 *
 * SETUP        Set up & start the radio
 *
 ***************************************************************************************************************************************************/
void setup()
{
#if defined(MEGATINYCORE)
  // LED
  PORTA.DIRSET = PIN5_bm;
  PORTA.OUTSET = PIN5_bm;

  // Allow UPDI to take control for the first 8 seconds of boot up, this allows programming the ATTINY1624 chip
  // at power on, before the serial port is opened and UPDI comms lost
  int countdown = 8000 / 100;
  while (countdown > 0)
  {
    PORTA.OUTTGL = PIN5_bm;
    delay(100);
    countdown--;
  }
#endif

  // Set I/O pins, print initial message, read configuration from EEPROM
  pinMode(LEDpin, OUTPUT);
  digitalWrite(LEDpin, HIGH);
  Serial.begin(BAUD_RATE);
#if defined(MEGATINYCORE)
  Serial.print(F("|emonBase_RFM69CW_USB V"));
#else
  Serial.print(F("|emonBase_rfm69pi_LPL V"));
#endif
  Serial.write(firmware_version);
  Serial.println(F("|OpenEnergyMonitor.org"));
  load_config(); // Load RF config from EEPROM (if any exists)

#ifdef SAMPPIN
  pinMode(SAMPPIN, OUTPUT);
  digitalWrite(SAMPPIN, LOW);
#endif

#if !defined(MEGATINYCORE)
   delay(2000);
#endif

#if RadioFormat == RFM69_LOW_POWER_LABS
  radio.initialize(RF69_433MHZ, EEProm.nodeID, EEProm.networkGroup);
  radio.encrypt("89txbe4p8aik5kt3");  
#else
  rf.init(EEProm.nodeID, EEProm.networkGroup,
          EEProm.RF_freq == RFM_915MHZ ? 915 // Fall through to 433 MHz Band @ 434 MHz
                                       : (EEProm.RF_freq == RFM_868MHZ ? 868 : 434));
#endif
  digitalWrite(LEDpin, LOW);


}

/**************************************************************************************************************************************************
 *
 * LOOP         Poll the radio for incoming data, and the serial input for calibration & outgoing r.f. data
 *
 ***************************************************************************************************************************************************/

void loop()
{

  //-------------------------------------------------------------------------------------------------------------------------------------------
  // RF Data handler - inbound ****************************************************************************************************************
  //-------------------------------------------------------------------------------------------------------------------------------------------

#if RadioFormat == RFM69_LOW_POWER_LABS
  if (radio.receiveDone())
  {
    // Copy
    rfInfo.srcNode = radio.SENDERID;
    rfInfo.msgLength = radio.DATALEN;
    for (byte i = 0; i < radio.DATALEN; i++)
    {
      nativeMsg[i] = radio.DATA[i];
    }
    rfInfo.rssi = radio.readRSSI();

    // Send ACK
    if (radio.ACKRequested())
    {
      radio.sendACK();
    }

    // Print frame
    Serial.print(F("OK"));
    Serial.print(F(" "));
    Serial.print(rfInfo.srcNode, DEC);
    Serial.print(F(" "));
    for (byte i = 0; i < rfInfo.msgLength; i++)
    {
      Serial.print((word)nativeMsg[i]);
      Serial.print(F(" "));
    }
    Serial.print(F("("));
    Serial.print(rfInfo.rssi);
    Serial.print(F(")"));
    Serial.println();

    // Flash LED
    double_LED_flash();
  }
#else
  int len = rf.receive(&nativeMsg, sizeof(nativeMsg)); // Poll the RFM buffer and extract the data
  // Serial.print("len=");  Serial.println(len);
  if (len > 1)
  {
    rfInfo.crc = true;
    rfInfo.msgLength = len;
    rfInfo.srcNode = nativeMsg[1];
    rfInfo.rssi = -rf.rssi / 2;

    // send serial data
    Serial.print(F("OK")); // Bad packets (crc failure) are discarded by RFM69CW
    Serial.print(F(" "));
    Serial.print(rfInfo.srcNode); // Extract and print node ID
    Serial.print(F(" "));
    for (byte i = 2; i < rfInfo.msgLength; ++i)
    {
      Serial.print((word)nativeMsg[i]);
      Serial.print(F(" "));
    }
    Serial.print(F("("));
    Serial.print(rfInfo.rssi);
    Serial.print(F(")"));
    Serial.println();
    double_LED_flash();
  }
#endif
  //-------------------------------------------------------------------------------------------------------------------------------------------
  // RF Data handler - outbound ***************************************************************************************************************
  //-------------------------------------------------------------------------------------------------------------------------------------------

  if ((EEProm.rfOn & RFTX) && outmsgLength)
  { // if command 'outmsg' is waiting to be sent then let's send it
    showString(PSTR(" -> "));
    Serial.print((word)outmsgLength);
    showString(PSTR(" b\n"));

#if RadioFormat == RFM69_LOW_POWER_LABS
    radio.send(0, (void *)outmsg, outmsgLength);
#else
    rf.send(0, (void *)outmsg, outmsgLength); //  void RF69<SPI>::send (uint8_t header, const void* ptr, int len) {
#endif

    outmsgLength = 0;
    single_LED_flash();
  }

  //-------------------------------------------------------------------------------------------------------------------------------------------
  // Calibration Data handler *****************************************************************************************************************
  //-------------------------------------------------------------------------------------------------------------------------------------------

  if (Serial.available())                                              // Serial input from RPi for configuration/calibration
  {
    getCalibration(); // If serial input is received from RPi
    double_LED_flash();
    if (rfChanged)
    {
#if RadioFormat == RFM69_LOW_POWER_LABS
      radio.initialize(RF69_433MHZ, EEProm.nodeID, EEProm.networkGroup);
      radio.encrypt("89txbe4p8aik5kt3");
#else
      rf.init(EEProm.nodeID, EEProm.networkGroup,
              EEProm.RF_freq == RFM_915MHZ ? 915 // Fall through to 433 MHz Band @ 434 MHz
                                           : (EEProm.RF_freq == RFM_868MHZ ? 868 : 434));
#endif

      rfChanged = false;
    }
  }

}


// LED flash
void single_LED_flash(void)
{
  digitalWrite(LEDpin, HIGH);  delay(30); digitalWrite(LEDpin, LOW);
}

void double_LED_flash(void)
{
  digitalWrite(LEDpin, HIGH);  delay(10); digitalWrite(LEDpin, LOW); delay(30); 
  digitalWrite(LEDpin, HIGH);  delay(10); digitalWrite(LEDpin, LOW);
}
