/**
 * ----------------------------------------------------------------------------
 * This is a MFRC522 library example; see https://github.com/miguelbalboa/rfid
 * for further details and other examples.
 * 
 * NOTE: The library file MFRC522.h has a lot of useful info. Please read it.
 * 
 * Released into the public domain.
 * ----------------------------------------------------------------------------
 * This sample shows how to read and write data blocks on a MIFARE Classic PICC
 * (= card/tag).
 * 
 * BEWARE: Data will be written to the PICC, in sector #1 (blocks #4 to #7).
 * 
 * 
 * Typical pin layout used:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 * 
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <ChaCha.h>
#include <string.h>
#include <avr/pgmspace.h>

constexpr uint8_t RST_PIN = 3;     // Configurable, see typical pin layout above
constexpr uint8_t SS_PIN = 53;     // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance.

byte dataBlock[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

byte encrypted[] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};



byte key[] = {
    0xCB, 0x45, 0xF4, 0x6C, 
    0xDD, 0xA7, 0xC0, 0x01, 
    0x5E, 0xE2, 0xA0, 0x72, 
    0x2E, 0xD8, 0x24, 0xFD};
byte iv[] = {101,109,103,104,105,106,107,108};
byte counter[] = {109, 110, 111, 112, 113, 114, 115, 116};


float adminCardValue = 10;
float balanceToSet = 5;

byte page = 8;
MFRC522::StatusCode status;
byte buffer[sizeof(dataBlock)];
byte size = sizeof(buffer);

bool user = false;
bool reading= false;

ChaCha chacha;

/**
 * Initialize.
 */
 
void setup() {
    Serial.begin(9600); // Initialize serial communications with the PC
    while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
    SPI.begin();        // Init SPI bus
    mfrc522.PCD_Init(); // Init MFRC522 card
    Serial.println(F("BEWARE: Data can be written to the PICC, on pages #8 to 15"));
}

/**
 * Main loop.
 */
void loop() {
    serialCommands();
    // Look for new cards
    if ( ! mfrc522.PICC_IsNewCardPresent())
        return;

    // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial())
        return;

    // Show some details of the PICC (that is: the tag/card)
    Serial.print(F("Card UID:"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.println();
    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));

    // Check for compatibility
    if (piccType != MFRC522::PICC_TYPE_MIFARE_UL) {
        Serial.println(F("This sample only works with MIFARE Ultralight C."));
        return;
    }

    

    // Show the whole memory as it currently is
    Serial.println(F("Current data in memory;"));
    mfrc522.PICC_DumpMifareUltralightToSerial();
    Serial.println();
    
    if(reading){
      Serial.println(F("Copying pages 8 to 15 into dataBlock... "));
      mfrc522.PICC_CopyMifareUltralightData(8,15, dataBlock);
      Serial.println("dataBlock contains encrypted data: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));

      cipherInit(&chacha);
      chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));
      
      Serial.println("dataBlock after decryption: ");
      dump_byte_array(dataBlock, sizeof(dataBlock));    
      
      Serial.println("Current balance: "+String(((float)dataBlockToCash(dataBlock)/100)));
      
      if(matchDatablockUid(dataBlock, &mfrc522.uid)){
        Serial.println("UID Matches, authentication successful");
      }else{
        Serial.println("UID mismatch!! Authentication FAILED :(");
      }
    }
    else if(!reading){  
      
      if(user){
        cashToDataBlock(dataBlock, balanceToSet*100, &mfrc522.uid);
      }else if(!user){
        fundCashToDataBlock(dataBlock, adminCardValue, &mfrc522.uid);
      }
      
      Serial.print(F("Writing data into pages 8 to 15: "));
      dump_byte_array(dataBlock, sizeof(dataBlock));
 
      //Encrypt before writing
      Serial.println();
      Serial.println("Encrypting before writing...");
      cipherInit(&chacha);
      chacha.encrypt(encrypted, dataBlock, sizeof(dataBlock));
      Serial.println("Encrypted dataBlock : ");
      dump_byte_array(encrypted, sizeof(encrypted));
      Serial.println();
      
      // Writing page by page
      for(int i=0; i<sizeof(encrypted);i+=4){
        dump_byte_array(encrypted+i, 4);
        status = (MFRC522::StatusCode) mfrc522.MIFARE_Ultralight_Write(page+(i/4), encrypted+i, 4);
        if (status != MFRC522::STATUS_OK) {
            Serial.print(F("MIFARE_Write() failed: "));
            Serial.println(mfrc522.GetStatusCodeName(status));
        }
      }
      
      // Read data from the block (again, should now be what we have written)
      Serial.println(F("Reading data after write from page 8..."));
      mfrc522.PICC_CopyMifareUltralightData(8,15, buffer);
      Serial.println(F("Data in pages 8 to 15 ")); 
      dump_byte_array(buffer, sizeof(buffer)); 
      Serial.println();
      
    }
    // Halt PICC
    mfrc522.PICC_HaltA();
    // Stop encryption on PCD
    mfrc522.PCD_StopCrypto1();
    
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
        if(i%4==3){
          Serial.println();
        }
    }
}

void cipherInit(ChaCha *cipher){
    cipher->clear();
    if (!cipher->setKey(key, 16)) {
        Serial.println("Error setKey");
        return false;
    }
    if (!cipher->setIV(iv, 8)) {
        Serial.println("Error setIV");
        return false;
    }
    if (!cipher->setCounter(counter, 8)) {
        Serial.println("Error setCounter");
        return false;
    }
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

// Converts amount in cents to our dataBlock structure
void fundCashToDataBlock(byte* dataBlock, int dollars, MFRC522::Uid * uid){

  for(int i=0 ; i < 4 ;i++){
    if(i%2 == 0){
      dataBlock[i] = 0xCA;
    }
    else{
      dataBlock[i] = 0xFE;
    }
  }
  for(int i=4 ; i < uid->size+4 ;i++){
    dataBlock[i] = (uid->uidByte)[i-4];
  }
  dataBlock[12] = dollars;

  // Fill with random values to add randomness to encryption
  srand(millis());
  for(int i=13; i < 32;i++){
    dataBlock[i] = rand() % 256;
  }

}

int dataBlockToCash(byte* dataBlock){
  int balance = ((int) dataBlock[0]) *100 + ((int) dataBlock[1]);
  balance *= dataBlock[3] > 0? 1: -1;
  return balance; 
}

bool matchDatablockUid(byte* dataBlock, MFRC522::Uid * uid){
  for(int i=0; i<uid->size; i++){
    if(dataBlock[i+4] != (uid->uidByte)[i]){
      return false; 
    }
  }
  return true;
}


void serialCommands(){
  if(Serial.available())
  {
    String temp = Serial.readString();
    if(temp.substring(0,1) =="a"){
      adminCardValue = temp.substring(2).toFloat();
      Serial.println("Admin card value changed to "+String(adminCardValue)+"$");
    }
    else if(temp == "u"){
      user = !user;
      if(user){
        Serial.println("Writing user cards");
      }
      else if(!user){
        Serial.println("Writing admin cards");
      }
    }
    else if(temp == "r"){
      reading = !reading;
      if(reading){
        Serial.println("Reading cards");
      }
      else if(!reading){
        Serial.println("Writing cards");
      }
    }
    else{ 
      balanceToSet = temp.toFloat();
      Serial.println("Balance set to "+ String(balanceToSet));
    }
    
  }
}

