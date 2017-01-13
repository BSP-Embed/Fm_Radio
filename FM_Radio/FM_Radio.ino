/***********************************************************************************/
/* This Application for FM Radio With Internet Clock                               */
/* written by Prashanth from BSP Embed.                                            */   
/* This code is availbale from GitHub.                                             */ 
/* Demo is available on YouTube BSP Embed                                          */
/* This is an example for our Monochrome OLEDs based on SSD1306 drivers            */
/*  Pick one up today in the adafruit shop!                                        */ 
/*  ------> http://www.adafruit.com/category/63_98                                 */
/* This example is for a 128x64 size display using I2C to communicate              */
/* 3 pins are required to interface (2 I2C and one reset)                          */
/* Adafruit invests time and resources providing this open source code,            */
/* please support Adafruit and open-source hardware by purchasing                  */
/* products from Adafruit!                                                         */
/* Written by Limor Fried/Ladyada for Adafruit Industries.                         */
/* BSD license, check license.txt for more information                             */
/* All text above, and the splash screen must be included in any redistribution    */ 
/************************************************************************************/
#include <Wire.h>
#include <EEPROM.h>
#include <Encoder.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define F_MIN                 88UL        /* in MHz */    
#define F_MAX                 108UL
#define STEP_SIZE             0.1         /* in MHz */
#define FREQ_MODE             0
#define CHAN_MODE             1
#define MAGIC_NO              96          /* For EEPROM Detection */
#define MAGIC_ADD             0           /* EEPROM ADDRESS */
#define MODE_ADD              1
#define CHAN_FREQ_BASE        2
#define NO_CHANNELS           10 + 1      /* Channels + VFO */
#define ERROR_PRESS           0
#define SHORT_PRESS           1
#define MEDIUM_PRESS          2
#define TRUE                  1
#define FALSE                 0
#define S_PRESS_TIME          500         /* in milli seconds */
#define M_PRESS_TIME          1500
#define DLY_SEC               1000        /* Display Hold for Info*/
#define TZ_HR                 5           /* +5.30 INDIAN Time Zone */
#define TZ_MIN                30  
#define TIME_ZONE             (TZ_HR * 60 * 60) + (TZ_MIN * 60)
#define OLED_RESET            4
#define SSD1306_LCDHEIGHT     64

/* PIN Definition */
#define ENCODER_A             12                      
#define ENCODER_B             13                      
#define ENCODER_BTN           14  

/* Define Macros */
#define WriteMode()           do {                                \
                                EEPROM.begin(512);                \
                                EEPROM.write(MODE_ADD, Mode);     \
                                EEPROM.commit();                  \
                                EEPROM.end();                     \
                              } while(0)                                 
#define ReadEncoder()         (myEnc.read() / 4)
#define FreqToPll(x)          ((4 * ((x * 1000000) + 225000)) / 32768)   /*According To Datasheet */

/* Default FM Radio Station Frequencies */
float Channels[]          = {104.0, 91.1, 91.9, 92.7, 93.5, 94.3, 95.0, 98.3, 101.3, 102.9, 104.0};     
char ModeFreChlStr[]      = {'F','0','1','2','3','4','5','6','7','8','9'};
                              
/* Global Variables */
volatile float            RxDispFreq;
volatile uint8_t          Mode;            /* Frequency Or Channel Mode */
volatile uint8_t          ChanNo;

boolean FreqChng          = FALSE;
boolean ChnChng           = FALSE;
boolean StrChn            = FALSE;
boolean StrChnChng        = FALSE;

long newPosition;
long oldPosition;

const char* ssid          = "WIFI SSID";          /* Your Router's SSID */
const char* password      = "WIFI PASSWORD";     /* Your Router's WiFi Password */

int seconds;
int minutes;
int hours;
unsigned long timeNow;
unsigned long OldTime     = 0;

char line[80];
unsigned long epoch;
unsigned int localPort    = 2390;              /* local port to listen for UDP packets */

IPAddress timeServerIP;                       /* time.nist.gov NTP server address */
const char* ntpServerName   = "time.nist.gov";
const int NTP_PACKET_SIZE   = 48;             /* NTP time stamp is in the first 48 bytes */
byte packetBuffer[NTP_PACKET_SIZE];           /* buffer to hold incoming & outgoing packets */

WiFiUDP udp;                                  /* A UDP instance for send & receive packets */
Encoder myEnc(ENCODER_A, ENCODER_B);
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
  #error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void setup()   {         
  Wire.begin(0,2);                            /* I2C OLED SDA, SCL */
  Serial.begin(9600);
  ReadEEPROM();
  SetClk();
  pinMode(ENCODER_BTN, INPUT_PULLUP);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c);  /* Initialize OLED */
  display.display();
  delay(250);
  ConnectAP();
  GetTime();
  WiFi.mode(WIFI_OFF);
  DispInit();
  newPosition = oldPosition = ReadEncoder();
}
void loop() {
  /* Read an encoder */
  newPosition = ReadEncoder();
  if (newPosition != oldPosition) {
    if (newPosition > oldPosition)
      set_frequency(1);
    else
      set_frequency(-1);
    oldPosition = newPosition;
    Serial.println(newPosition);
  }
  /* Update the display & Clk  for Freq Change*/
  if (FreqChng) {
    FreqChang();
    FreqChng = FALSE;
  }
  /* Update the display & Clk  for Channel Change*/
  if(ChnChng) {
    ChanlChanged();
    ChnChng = FALSE;
  }
  /* Update for button Press */
  switch (get_button()) {
    case SHORT_PRESS:   if (StrChn) StrChnMEM();
                        else if (Mode >= 1 && Mode <= 10)   /* It Channel Mode */
                            ChngModeToFreq();
                        else 
                            ChngModeToChn();
                        break;
    case MEDIUM_PRESS:  StoreChn(); break;
    default: break;
  }
  /* Store Channel to Memory */
  if (StrChn && StrChnChng) {
     DispMode();
     StrChnChng = FALSE;
  }
  TimeCheck();
}
/**************************************/
/* Connect to WiFi Router             */
/**************************************/
void ConnectAP(void) {
  WiFi.mode(WIFI_STA);    /* Set WiFi to station mode */
  WiFi.disconnect();     /* disconnect from an AP if it was Previously  connected*/
  delay(100);
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  display.clearDisplay();              /* For Display */
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Connecting");
  display.setCursor(0,18);
  display.display();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    display.print('.');
    display.display();
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  udp.begin(localPort);             /* -- Start UDP -- */
  Serial.println("Ready!");
}                      
/**************************************/
/* Store VFO into EEPROM         */
/**************************************/
void StoreChn(void) {
  if (Mode == FREQ_MODE) {
    StrChn = TRUE;
    display.setTextColor(0xFFFF, 0);
    display.setCursor(0,18);
    display.println("      ");
    display.display();
    display.setCursor(0,18);
    display.setTextColor(WHITE);
    display.println("ST CHN");
    display.display();
    Mode = ChanNo = 1;
    DispMode();
  }
}
/**************************************/
/* Store VFO to Channel in EEPROM         */
/**************************************/
void StrChnMEM(void) {
    StrChn = FALSE;
    Channels[ChanNo] = RxDispFreq; /* store into Array */
    WriteChannel(ChanNo);               /* store in EEPROM */
    display.setTextColor(0xFFFF, 0);
    display.setCursor(0,18);
    display.println("          ");
    display.display();
    display.setTextColor(WHITE);
    display.setCursor(0,18);
    display.println("CHN STORED");
    display.display();
    delay(DLY_SEC);
    DispInit();
}
/**************************************/
/* Display Mode/Channel Numer         */
/* and store in EEPROM                */
/**************************************/
void ChngModeToChn(void) {
    RxDispFreq = Channels[ChanNo];
    Mode = ChanNo;
    DispFreq();
    DispMode();
    WriteMode();
    Mode = CHAN_MODE;
}
/**************************************/
/* Display Mode to Frequency          */
/* and store in EEPROM                */
/**************************************/
void ChngModeToFreq(void) {
  RxDispFreq = Channels[FREQ_MODE];
  Mode = FREQ_MODE;
  DispFreq();
  WriteMode();
  DispMode();
}
/**************************************/
/* Display Frequency & Store in EEPROM*/
/**************************************/
void FreqChang(void) {
  Channels[Mode] = RxDispFreq;
  DispFreq();
  Channels[Mode] = RxDispFreq;
  WriteChannel(Mode);
}
/**************************************/
/* Intialize the Display              */
/**************************************/
void DispInit(void) {
  display.clearDisplay();                   /* Clear the buffer */
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("F      MHz");
  display.setCursor(0,18);
  display.println("T   :  :  ");
  display.display();
  DispMode();
  DispFreq();
  DispTime();  
}
/**************************************/
/* Display Mode or channel Number     */
/**************************************/
void DispMode(void) {
  display.setTextColor(0xFFFF, 0);
  display.setCursor(0,0);
  display.println(" ");
  display.display();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.print(ModeFreChlStr[Mode]);
  display.display();
}
/**************************************/
/* Change the frequency               */
/* dir = 1    Increment               */
/* dir = -1   Decrement               */
/**************************************/
void set_frequency(short dir){
  if (StrChn == TRUE) {
    switch(dir){
      case 1: if (++ChanNo >= NO_CHANNELS) ChanNo = 1; break;
      case -1: if (--ChanNo <= 0) ChanNo = NO_CHANNELS - 1; break;
    }
    Mode = ChanNo;
    StrChnChng = TRUE;
  }
  else if (Mode == FREQ_MODE) {
    switch(dir) {
      case 1: RxDispFreq += STEP_SIZE; break;
      case -1: RxDispFreq -= STEP_SIZE; break;
    }
    if(RxDispFreq > F_MAX)      /* check overflow */
      RxDispFreq = F_MIN;
    if(RxDispFreq < F_MIN)
      RxDispFreq = F_MAX;
     FreqChng = TRUE;
 } else {
    switch(dir){
      case 1: if (++ChanNo >= NO_CHANNELS) ChanNo = 1; break;
      case -1: if (--ChanNo <= 0) ChanNo = NO_CHANNELS - 1; break;
    }
    ChnChng = TRUE; 
 }
}
/**************************************/
/* Display Channel Change & Store      */
/**************************************/
void ChanlChanged() {
  RxDispFreq = Channels[ChanNo];
  DispFreq();
  Mode = ChanNo;
  DispMode();
  WriteMode();
  EEPROM.end();  
}
/**************************************/
/* Read the button with debouncing    */
/**************************************/
uint8_t get_button() {
  if (!digitalRead(ENCODER_BTN)) {
    delay(20);
    if (!digitalRead(ENCODER_BTN)) {
      long strttime = millis();
      while (!digitalRead(ENCODER_BTN));
      long Duration = millis() -  strttime; 
      if (Duration > S_PRESS_TIME)
        return MEDIUM_PRESS;
      else
        return SHORT_PRESS;          
    }
  }
  return ERROR_PRESS;
}
/**************************************/
/* Displays the frequency             */
/**************************************/
void DispFreq(){
  display.setTextColor(0xFFFF, 0);
  display.setCursor(20,0);
  display.println("     ");
  display.display();
  delay(1);
  display.setTextColor(WHITE);
  display.setCursor(20,0);
  display.print(RxDispFreq,1);
  display.display();
  SetClk();                              /* Generate Clock */
}
/**********************************************/
/* Set Clock for Both Receiver & Transmitter */
/*********************************************/
void SetClk() {
  float Freq;
  uint16_t Pll;
  if (Mode >= 1 && Mode <= 10)
    Freq = Channels[ChanNo];
  else
    Freq = Channels[FREQ_MODE];  
  Pll = FreqToPll(Freq); 
  Wire.beginTransmission(0x60);       /* start talking to the radio */
  Wire.write(Pll >> 8);               /* Get Higher Byte */
  Wire.write(Pll & 0XFF);             /* Get Lower Byte */
  Wire.write(0xB0);                 
  Wire.write(0x12);                 
  Wire.write(0x00);                
  Wire.endTransmission();
  delay(10); 
}
/**************************************/
/* Read Frequency, ModeOP, Channel No */
/* If EEPROM is blank, Write Defaults */
/**************************************/
void ReadEEPROM(void){
  uint8_t i;
  EEPROM.begin(512);
  if (EEPROM.read(MAGIC_ADD) != MAGIC_NO) { /* New EEPROM & Default Values*/
      EEPROM.write(MAGIC_ADD, MAGIC_NO);
      EEPROM.commit();
      for (i = 0; i < NO_CHANNELS; i++) {
        RxDispFreq = Channels[i];
        WriteChannel(i);
      }
      Mode = FREQ_MODE;
      WriteMode();
      RxDispFreq = Channels[0];
      Mode = FREQ_MODE;
      ChanNo = 1;
      Serial.println("New EEPROM");
      Serial.println(EEPROM.read(MAGIC_ADD));
   } else {                                  /* Read Entire Array */
      for (i = 0; i < NO_CHANNELS; i++) {
        ReadChannel(i);
        Channels[i] = RxDispFreq;
      }
      Mode = ReadMode();                     /* Restore power off data */
      RxDispFreq = Channels[Mode];
      if (Mode != FREQ_MODE) ChanNo = Mode;
      else ChanNo = 1;
      Serial.println("OLD EEPROM");
  }  
  EEPROM.end();  
}
/**************************************/
/*Write Frequency and Mode of Operation*/
/* encoder for frequency change       */
/**************************************/
void WriteChannel(uint8_t MemLoc) {
   uint8_t Add;
   EEPROM.begin(512);
   Add = CHAN_FREQ_BASE + (15 * MemLoc); 
   WriteDispFreq(Add);
   EEPROM.commit();
   EEPROM.end();  
}
/**************************************/
/*Read Frequency and Mode of Operation*/
/**************************************/
void ReadChannel(uint8_t MemLoc) {
   uint8_t Add;
   Add = CHAN_FREQ_BASE + (15 * MemLoc); 
   ReadDispFreq(Add);
}
/**************************************/
/*Read Frequency From EEPROM as string*/
/**************************************/
void WriteDispFreq(uint8_t FreqAdd) {
  byte i , j;
  String FreqStr;
  FreqStr = String(RxDispFreq);
  i = FreqAdd; j = 0;
  while (FreqStr[j] != '\0') { 
       EEPROM.write(i, FreqStr[j]);  
       EEPROM.commit();
       i++, j++;
  }  
  EEPROM.write(i, '\0');
  EEPROM.commit();
}
/**************************************/
/* Read Frequency From EEPROM         */
/* and store as integer               */
/**************************************/
void ReadDispFreq(uint8_t FreqAdd) {
  byte i, j;
  char StrName[10];
  String inString;
  i = FreqAdd; j = 0;
  while ((StrName[j] = EEPROM.read(i)) != '\0') {
           i++; j++;
  }
  StrName[j] = '\0';
  inString = StrName;
  RxDispFreq = inString.toFloat();
}
uint8_t ReadMode(void){
  uint8_t x;
  x = EEPROM.read(MODE_ADD);     
  EEPROM.end();
  return x;
}   
/**************************************/
/*      Update Time                   */
/**************************************/
void TimeCheck(void) {
  timeNow = millis() / 1000;      /* the number of seconds passed */
  if (timeNow != OldTime) {
    if (++seconds >= 60) {
      seconds = 0; 
      if (++minutes >= 60){ 
        minutes = 0;
        if (++hours >= 24)
          hours = 0;
        DisplayValue(20, hours);
      }
      DisplayValue(60, minutes);
     }
    DisplayValue(100, seconds);
    OldTime = timeNow;
  }
}
/**************************************/
/* Display Values, at given location  */
/**************************************/
void DispTime(void) {
  DisplayValue(20, hours);
  DisplayValue(60, minutes);
  DisplayValue(100, seconds);
}
/**************************************/
/* Display Values, at given location  */
/**************************************/
void DisplayValue(uint8_t addr, uint8_t value) {
  display.setTextColor(0xFFFF, 0);
  display.setCursor(addr,18);
  display.println("  ");                /* Clear the location */
  display.display();
  display.setTextColor(WHITE);
  display.setCursor(addr,18);
  display.print(value);               /* Update the info */
  display.display(); 
}
/**************************************/
/*          Get Time From Internet    */
/**************************************/
void GetTime(void) {
  WiFi.hostByName(ntpServerName, timeServerIP);  /* get a random server from the pool */
  sendNTPpacket(timeServerIP);                  /* send an NTP packet to a time server */
  int cb = udp.parsePacket();
  while  (!cb) {
    Serial.println("Fail");
    sendNTPpacket(timeServerIP);
    cb = udp.parsePacket();
  }
  Serial.print("OK ");
  Serial.println(cb);                             
  udp.read(packetBuffer, NTP_PACKET_SIZE);        /* read the packet into the buffer */
  /* the timestamp starts at byte 40 of the received packet and is four bytes */
  /* or two words, long. First, esxtract the two words */
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);  /* combine the four bytes (two words) into a long integer */
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);   /* this is NTP time (seconds since Jan 1 1900): */
  unsigned long secsSince1900 = highWord << 16 | lowWord; /* now convert NTP time into everyday time */
  const unsigned long seventyYears = 2208988800UL;   /* Unix time starts on Jan 1 1970. In seconds, that's 2208988800: */
  epoch = secsSince1900 - seventyYears;
  unsigned long adjustedEpoch = epoch + TIME_ZONE;   /* If Time Zone is -Ve, Change sign to -Ve */
  hours = (adjustedEpoch  % 86400L) / 3600; 
  if (hours < 0) hours += 12;
  minutes = (adjustedEpoch  % 3600) / 60;
  seconds = adjustedEpoch % 60;
}
/**************************************/
/* send an NTP request to the         */
/* time server at the given address   */
/**************************************/
unsigned long sendNTPpacket(IPAddress& address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);    /* set all bytes in the buffer to 0 */
  /* Initialize values needed to form NTP request */
  packetBuffer[0]   = 0b11100011;   /* LI, Version, Mode */
  packetBuffer[1]   = 0;            /* Stratum, or type of clock */
  packetBuffer[2]   = 6;            /* Polling Interval */
  packetBuffer[3]   = 0xEC;         /* Peer Clock Precision */
  /* 8 bytes of zero for Root Delay & Root Dispersion */
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  /* all NTP fields have been given values, now *
  /* you can send a packet requesting a timestamp */
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
