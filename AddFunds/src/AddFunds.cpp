#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Consts.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <ChaCha.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <Adafruit_NeoPixel.h>

// Hardware SETUP
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif
MFRC522 nfc(SS_PIN, RST_PIN);

MFRC522::StatusCode nfcStatus;  // NFC read/write status
byte dataBlock[] = {            // Data structure to read/write from/to card
  0x00, 0x00, 0x00, 0x00,       // dataBlock[0] = dollars
  0x00, 0x00, 0x00, 0x00,       // dataBlock[1] = cents
  0x00, 0x00, 0x00, 0x00,       // dataBlock[2] = 0 if positive balance, 1 if negative
  0x00, 0x00, 0x00, 0x00,       // dataBlock[4-10] = UID
  0x00, 0x00, 0x00, 0x00,       // dataBlock[11-40] = random hex value
  0x00, 0x00, 0x00, 0x00,       // dataBlock[41-48] = CAFE CAFE CAFE CAFE CAFE
  0x00, 0x00, 0x00, 0x00,       // Occupies all user available memory on card
  0x00, 0x00, 0x00, 0x00
};

byte fundCardDataBlock[] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

float fundsToAdd = 0;

// Crypto data
byte key[] = {
    0xCB, 0x45, 0xF4, 0x6C,
    0xDD, 0xA7, 0xC0, 0x01,
    0x5E, 0xE2, 0xA0, 0x72,
    0x2E, 0xD8, 0x24, 0xFD};
byte iv[] = {101,109,103,104,105,106,107,108};
byte counter[] = {109, 110, 111, 112, 113, 114, 115, 116};
ChaCha chacha;


int   oldBalance = 0;
float newBalance = 0;
float price = 0;
uint8_t firstPage = 8; // First page from which we read/write the 16 dataBlock bytes
uint8_t lastPage = 15; // First page from which we read/write the 16 dataBlock bytes

STATUS sysStatus;

//==============================LCD FUNCTIONS===================================
void setDisplayInfo(int sysStatus){

  display.setTextColor(BLACK, WHITE); // 'inverted' text
  display.print("lstStblWght: ");
  display.print("sysStatus: ");
  display.println(enumToString(sysStatus));
  display.println("oldBalance: "+String((((float) oldBalance)/100)));
  display.println("newBalance: "+String((newBalance/100)));
  display.println("Paid: "+String(price));
}

void activateDisplay(int del){
  display.display();
  delay(del);
}

//==============================NFC/DATABLOCK FUNCTIONS=========================
void clearDataBlock(byte* dataBlock){
  for(byte i=0; i<sizeof(dataBlock) ; i+=1){
    dataBlock[i] = 0X00;
  }
}

bool isFundCard(byte* dataBlock) {
  if(dataBlock[0]==0XCA && dataBlock[1]==0XFE &&
     dataBlock[2]==0XCA && dataBlock[3]==0XFE){
       return true;
  }
  return false;
}

bool isUserCard(byte* dataBlock) {
  if(dataBlock[24]==0XCA && dataBlock[25]==0XFE &&
     dataBlock[26]==0XCA && dataBlock[27]==0XFE &&
     dataBlock[28]==0XCA && dataBlock[29]==0XFE &&
     dataBlock[30]==0XCA && dataBlock[31]==0XFE){
       return true;
  }
  return false;
}

// Returns in cents the balance on card
int dataBlockToCash(byte* dataBlock){
  int balance = ((int) dataBlock[0]) *100 + ((int) dataBlock[1]);
  balance *= dataBlock[2] == 0 ? 1 : -1;
  return balance;
}


// Converts amount in cents to our dataBlock structure
void cashToDataBlock(byte* dataBlock, int cash, MFRC522::Uid * uid){
  byte sign = cash<0 ? 1 : 0;
  cash *= cash<0 ? -1 : 1;
  int dollars = cash/100;
  int cents   = cash % 100;
  Serial.println("Dollars: "+String(dollars)+" Cents: "+String(cents));
  dataBlock[0] = dollars;
  dataBlock[1] = cents;
  dataBlock[2] = sign;
  for(int i=4 ; i < uid->size+4 ;i++){
    dataBlock[i] = (uid->uidByte)[i-4];
  }

  // Fill with random values to add randomness to encryption
  srand(millis());
  for(int i=11; i < 24;i++){
    dataBlock[i] = rand() % 256;
  }
  for(int i=24; i < 32 ; i++){
    if(i%2 == 0){
      dataBlock[i] = 0xCA;
    }
    else{
      dataBlock[i] = 0xFE;
    }
  }
}


// Write dataBlock one page (4 bytes) at a time
void writeDatablockToCard(byte* dataBlock, int firstPage){
  for(int i=0; i<sizeof(dataBlock);i+=4){
    nfcStatus = (MFRC522::StatusCode) nfc.MIFARE_Ultralight_Write(firstPage+(i/4), dataBlock+i, 4);
    if (nfcStatus != MFRC522::STATUS_OK) {
        Serial.print(F("MIFARE_Write() failed: "));
        Serial.println(nfc.GetStatusCodeName(nfcStatus));
    }
  }
}

// Reads uid decoded in dataBlock, compares with hardcoded UID on card
bool matchDatablockUid(byte* dataBlock, MFRC522::Uid * uid){
  for(int i=0; i<uid->size; i++){
    if(dataBlock[i+4] != (uid->uidByte)[i]){
      return false;
    }
  }
  return true;
}

// Initalizint ChaCha, must be done before every encrypt/decrypt
void cipherInit(ChaCha *cipher){
    cipher->clear();
    if (!cipher->setKey(key, 16)) {
        Serial.println("Error setKey");
    }
    if (!cipher->setIV(iv, 8)) {
        Serial.println("Error setIV");
    }
    if (!cipher->setCounter(counter, 8)) {
        Serial.println("Error setCounter");
    }
}

//==============================MISC/HARDWARE FUNCTIONS=========================

// Display everything about the card
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
        if(i%4==3){
          Serial.println();
        }
    }
}

// Flash light x times, NOT OPTIMAL, NOT FINAL
void flash(int del, int led, int times){
  for(int i=0; i < times ; i++){
    digitalWrite(led,HIGH);
    delay(del);
    digitalWrite(led,LOW);
  }
}

//==============================================================================
// Main usage loop
void userLoop(){
  switch(sysStatus){
    case STANDBY:{
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Standing by...");
      if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
        // Check for compatibility
        if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
            Serial.println(F("This sample only works with MIFARE Ultralight."));
            sysStatus = STANDBY;
            return;
        }

        // Read and decrypt data from card into dataBlock;
        nfc.PICC_CopyMifareUltralightData(firstPage, lastPage, fundCardDataBlock);
        cipherInit(&chacha);
        chacha.decrypt(fundCardDataBlock, fundCardDataBlock, sizeof(fundCardDataBlock));
        nfc.PICC_DumpMifareUltralightToSerial();
        // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
        // and card is a funding card
        if(isFundCard(fundCardDataBlock) && matchDatablockUid(fundCardDataBlock, &nfc.uid)){
          Serial.println("Decrypted a "+String(fundCardDataBlock[12])+"$ fund card");
          display.clearDisplay();
          display.setCursor(0,0);
          display.setTextColor(BLACK, WHITE); // 'inverted' text
          display.println("Read a "+String(fundCardDataBlock[12])+"$ fund card");

          dump_byte_array(fundCardDataBlock, sizeof(fundCardDataBlock));
          fundsToAdd = ((float) fundCardDataBlock[12]) * 100;

          // Halt PICC
          nfc.PICC_HaltA();
          clearDataBlock(fundCardDataBlock);
          sysStatus = AUTHENTICATION;
        }
        else{
          Serial.println("Error, UID does not match UID in chip or this is not a fund card : ");
          dump_byte_array(fundCardDataBlock, sizeof(fundCardDataBlock));
          flash(100, RED_LED_PIN, 15);
          sysStatus = STANDBY;
        }
      }
    }
    break;

    case AUTHENTICATION:{
      digitalWrite(RED_LED_PIN, HIGH);
      unsigned long waitStart = millis();

      Serial.println("Waiting for user card....");
      display.println("Waiting for user card...");
      display.display();

      bool cardRead = false;
      //We wait for a card to be read whithin a 6 sec time window
      while(millis()-waitStart< 6000){
        if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
          // Check for compatibility
          Serial.println("read new card");
          if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
              Serial.println(F("This sample only works with MIFARE Ultralight."));
              sysStatus = STANDBY;
              return;
          }
          // Read and decrypt data from card into dataBlock;
          nfc.PICC_CopyMifareUltralightData(firstPage, lastPage, dataBlock);
          cipherInit(&chacha);
          chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));
          // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
          if(isUserCard(dataBlock) && matchDatablockUid(dataBlock, &nfc.uid)){
            cardRead = true;
            Serial.println("Decrypted dataBlock (decrypted byte[]) : ");
            dump_byte_array(dataBlock, sizeof(dataBlock));
            Serial.println("Current balance: "+String(((float)dataBlockToCash(dataBlock)/100)));
            display.println("Current balance: "+String(((float)dataBlockToCash(dataBlock)/100)));
            display.display();
            delay(2000);
            sysStatus = PAYMENT;
            return;
          }
          else{
            Serial.println("Error, UID does not match UID in chip");
            flash(100, RED_LED_PIN, 15);
            sysStatus = STANDBY;
            return;
          }

        }
      }
      Serial.println("No customer card, returning to standby");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("No card read...");
      display.display();
      delay(2000);
      sysStatus = STANDBY;
    }
    break;

    case PAYMENT:{
      oldBalance = (float) dataBlockToCash(dataBlock);
      Serial.println("oldBalance: "+String(oldBalance));
      newBalance = oldBalance + fundsToAdd;
      Serial.println("newBalance: "+String(newBalance));
      display.println("New balance: "+String((float)newBalance/100));

      // Prepare dataBlock to write to chip
      cashToDataBlock(dataBlock, newBalance, &nfc.uid);
      Serial.println("Data to be encrypted and written: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));
      //Encrypt data before writing
      cipherInit(&chacha);
      chacha.encrypt(dataBlock, dataBlock, sizeof(dataBlock));

      // Write one page (4 bytes) at a time (Chip constraint)
      writeDatablockToCard(dataBlock, firstPage);

      Serial.println("Card dump: ");
      nfc.PICC_DumpToSerial(&(nfc.uid));

      nfc.PICC_CopyMifareUltralightData(firstPage, lastPage, dataBlock);
      cipherInit(&chacha);
      chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));
      Serial.println("Now written on card: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));

      // Halt PICC
      nfc.PICC_HaltA();
      clearDataBlock(dataBlock);
      sysStatus = CLEARING;
    }
    break;

    case CLEARING:{
        if(!nfc.PICC_IsNewCardPresent()){
          Serial.println("Customer left ");
          display.println("Customer left ");
          delay(2000);
          digitalWrite(GREEN_LED_PIN, HIGH);
          sysStatus = STANDBY;
        }
      }
    break;
  }
}

void setup()   {
  pinMode(BUTTON_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH);
  Serial.begin(9600);
  Serial.println("Starting! Enter 'help' to see command list");

  // Neopixel setup
  pixels.begin();
  pixels.show(); // Initialize all pixels to 'off'

  //LCD Startup
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Initializing...");

  // NFC Startup
  SPI.begin();        // Init SPI bus
  nfc.PCD_Init(); // Init MFRC522 card

  sysStatus = STANDBY;
}

void loop() {
  userLoop();       //After weight and stability updates, use info accordingly in use case loop

  //Display management
  activateDisplay(1);

}
