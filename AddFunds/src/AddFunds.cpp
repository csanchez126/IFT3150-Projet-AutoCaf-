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

// Neopixel setup
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(8, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
int lightPos = 0; // Light index variable
float lastLightCycle = 0; // Light timing variable

MFRC522::StatusCode nfcStatus;  // NFC read/write status
byte dataBlock[] = {            // Data structure to read/write from/to card
  0x00, 0x00, 0x00, 0x00,       // dataBlock[0] = dollars
  0x00, 0x00, 0x00, 0x00,       // dataBlock[1] = cents
  0x00, 0x00, 0x00, 0x00,       // dataBlock[2] = 0 if positive balance, 1 if negative
  0x00, 0x00, 0x00, 0x00,       // dataBlock[4-10] = UID
  0x00, 0x00, 0x00, 0x00,       // dataBlock[11-23] = random hex value
  0x00, 0x00, 0x00, 0x00,       // dataBlock[24-31] = CAFE CAFE CAFE CAFE CAFE
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
uint8_t displayMode = 2;
STATUS sysStatus;

//==============================LCD FUNCTIONS===================================
void activateDisplay(int del){
  display.display();
  delay(del);
  display.clearDisplay();
}

//==============================NEOPIXEL FUNCTIONS=========================
// Turn off lights
void clearLights(){
  for(byte i=0; i<8; i+=1){
    pixels.setPixelColor(i, pixels.Color(0, 0, 0));
  }
}
// All red
void initLights(){
  clearLights();
  for(byte i=0; i<8; i+=1){
    pixels.setPixelColor(i, pixels.Color(255, 0, 0));
  }
  pixels.show();
}

// All red
void errorLights(){
  initLights();
}

// All blue
void standbyLights(){
  clearLights();
  for(byte i=0; i<8; i+=1){
    pixels.setPixelColor(i, pixels.Color(0, 0, 255));
  }
  pixels.show();
}

// Green fade-in-out
void waitForUserCardLights(){
  if(millis() - lastLightCycle > 75){
    if(lightPos>255){ // Up-down
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(0, 255-(lightPos%255), 0));
      }
    }else{ // Down-up
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(0, lightPos, 0));
      }
    }
    lightPos += 40;
    lightPos = lightPos >= 508 ? 1:lightPos;
    lastLightCycle = millis();
  }
  pixels.show();
}

// Red fade-in-out
void writingLights(){
  if(millis() - lastLightCycle > 75){
    if(lightPos>255){ // Up-down
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(255-(lightPos%255), 255-(lightPos%255), 0));
      }
    }else{ // Down-up
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(0, lightPos, 0));
      }
    }
    lightPos += 40;
    lightPos = lightPos >= 508 ? 1:lightPos;
    lastLightCycle = millis();
  }
  pixels.show();
}

// Green blink
void doneLights(){
  lightPos = lightPos > 1? 0:lightPos;
  if(millis() - lastLightCycle > 200){
    if(lightPos == 1){
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(0, 255, 0));
      }
      lightPos = 0;
    }
    else if(lightPos == 0){
      for(byte i=0; i< 8 ;i+=1){
        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
      }
      lightPos = 1;
    }
    pixels.show();
    lastLightCycle = millis();
  }
}


//==============================NFC/DATABLOCK FUNCTIONS=========================
void clearDataBlock(byte* dataBlock){
  for(byte i=0; i<sizeof(dataBlock) ; i+=1){
    dataBlock[i] = 0X00;
  }
}

// Check admin funding card validity
bool isFundCard(byte* dataBlock) {
  if(dataBlock[0]==0XCA && dataBlock[1]==0XFE &&
     dataBlock[2]==0XCA && dataBlock[3]==0XFE){
       return true;
  }
  return false;
}

// Check user card validity
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

// Tell user their card is invalid
void invalidCard(String str){
  Serial.println(str);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Carte invalide!");
  display.display();
  delay(2000);
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

//==============================================================================
// Main usage loop
void userLoop(){
  switch(sysStatus){
    case STANDBY:{
      standbyLights();
      if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
        // Check for compatibility
        if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
            String str = "This program only works with MIFARE Ultralight";
            invalidCard(str);
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
          display.println("Carte de "+String(fundCardDataBlock[12])+"$ lue");
          display.println("Deposez une carte usager");

          dump_byte_array(fundCardDataBlock, sizeof(fundCardDataBlock));
          fundsToAdd = ((float) fundCardDataBlock[12]) * 100;

          // Halt PICC
          nfc.PICC_HaltA();
          clearDataBlock(fundCardDataBlock);
          lastLightCycle = 0;
          sysStatus = AUTHENTICATION;
        }
        else{
          errorLights();
          String str = "Error, UID does not match UID in chip or this is not a fund card : ";
          invalidCard(str);
          dump_byte_array(fundCardDataBlock, sizeof(fundCardDataBlock));
          sysStatus = STANDBY;
        }
      }
      else{
        display.setCursor(0,0);
        display.println("Deposez une carte");
        display.println("administrateur...");
      }
    }
    break;

    case AUTHENTICATION:{
      unsigned long waitStart = millis();
      bool cardRead = false;
      //We wait for a card to be read whithin a 6 sec time window
      while(millis()-waitStart< 6000){
        waitForUserCardLights();
        if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
          // Check for compatibility
          if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
              errorLights();
              String str = "This sample only works with MIFARE Ultralight.";
              Serial.println(str);
              invalidCard(str);
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
            sysStatus = PAYMENT;
            return;
          }
          else{
            Serial.println("Error, UID does not match UID in chip");
            sysStatus = STANDBY;
            return;
          }

        }
      }
      // After 6 second wait without reading a user card
      errorLights();
      Serial.println("No customer card, returning to standby");
      display.setCursor(0,0);
      display.println("Aucune carte lue...");
      display.display();
      delay(1000);
      sysStatus = STANDBY;
    }
    break;

    case PAYMENT:{
      // Debug+display management
      oldBalance = (float) dataBlockToCash(dataBlock);
      newBalance = oldBalance + fundsToAdd;

      Serial.println("oldBalance: "+String(oldBalance));
      Serial.println("newBalance: "+String(newBalance));

      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Carte usager valide lue!");
      display.println("Solde courant: "+String(((float)dataBlockToCash(dataBlock)/100)));
      display.println("Nouveau solde: "+String((float)newBalance/100));
      display.println("");
      display.println("Ecriture en cours...");
      display.display();


      // Prepare dataBlock to write to chip
      cashToDataBlock(dataBlock, newBalance, &nfc.uid);
      Serial.println("Data to be encrypted and written: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));

      //Encrypt data before writing
      cipherInit(&chacha);
      chacha.encrypt(dataBlock, dataBlock, sizeof(dataBlock));

      // Write one page (4 bytes) at a time (Chip constraint)
      writeDatablockToCard(dataBlock, firstPage);

      long now = millis();
      while(millis() - now < 2000){
        writingLights();
      }

      //Debugging and verification
      Serial.println("Card dump: ");
      nfc.PICC_DumpToSerial(&(nfc.uid));
      nfc.PICC_CopyMifareUltralightData(firstPage, lastPage, dataBlock);
      cipherInit(&chacha);
      chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));
      Serial.println("Now written on card: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));

      // Halt PICC, close card stream
      nfc.PICC_HaltA();
      clearDataBlock(dataBlock);
      sysStatus = DONE;
    }
    break;

    case DONE:{
        doneLights();
        if(!nfc.PICC_IsNewCardPresent()){
          sysStatus = STANDBY;
        }
      }
    break;
  }
}

void setup()   {
  pinMode(BUTTON_PIN, INPUT);
  Serial.begin(9600);
  Serial.println("Starting! Enter 'help' to see command list");

  // Neopixel setup
  pixels.begin();
  pixels.show();
  initLights();
  lightPos = 0;

  //LCD Startup
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Initializing...");
  display.display();

  // NFC Startup
  SPI.begin();        // Init SPI bus
  nfc.PCD_Init(); // Init MFRC522 card

  sysStatus = STANDBY;
}

void loop() {
  userLoop();       //After weight and stability updates, use info accordingly in use case loop
  activateDisplay(1);
}
