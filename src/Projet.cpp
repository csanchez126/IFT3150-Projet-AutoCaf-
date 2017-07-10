#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Consts.h>
#include <Scale.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <ChaCha.h>
#include <string.h>
#include <avr/pgmspace.h>

// LCD SETUP
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Hardware variables
uint8_t   displayMode = 1;               // What info should the LCD show
uint8_t   potValue = 0;                  // variable to store the value coming from the sensor
uint8_t   buttonState;                   // For manual taring
uint8_t   lastButtonState = LOW;         // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers
bool  calibrationMode = false;
Scale scale(DOUT,CLK);
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
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

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
uint8_t firstPage = 4; // First page from which we read/write the 16 dataBlock bytes
uint8_t lastPage = 15; // First page from which we read/write the 16 dataBlock bytes

STATUS sysStatus;

//==============================LCD FUNCTIONS===================================
void setDisplayInfo(uint8_t displayMode){
    if(displayMode == 0){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("Calib:");
      display.println(String(scale.getCalibrationFactor(),0));
      display.println("Pot: "+ String(analogRead(POT_PIN)));
    }
    else if(displayMode == 1){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.print("lstStblWght: ");
      display.println((int)scale.getLastStableWeight());
      display.print("sysStatus: ");
      display.println(enumToString(sysStatus));
      display.println("oldBalance: "+String((((float) oldBalance)/100)));
      display.println("newBalance: "+String((newBalance/100)));
      display.println("Paid: "+String(price));
    }
}

void activateDisplay(int del){
  display.display();
  delay(del);
  display.clearDisplay();
}

void setDisplayWeight(float weight){
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(String(weight,0)+"g");
}

//==============================NFC/DATABLOCK FUNCTIONS=========================

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
  for(int i=11; i < 40;i++){
    dataBlock[i] = rand() % 256;
  }
  for(int i=40; i < 48 ; i++){
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
  for(int i=0; i<16;i+=4){
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

// Set calibration factor
float potCalibration(){
  return BASE_CALIBRATION_OFFSET+(analogRead(POT_PIN)/20);
}

// Manually tare scale
void buttonReset(){
  uint8_t reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == HIGH) {
        scale.reset();
      }
    }
  }
  lastButtonState = reading;
}

// Blink led if weight between min and max
void waitBlink(uint8_t min, uint8_t max, uint8_t led){
  float instance = millis();
  bool toggle = false;
  while(scale.getSensorWeight() <= max && scale.getSensorWeight() >= min){
    if(millis()-instance>100){
      instance = millis();
      toggle = !toggle;
      digitalWrite(led, toggle);
      instance = millis();
    }
  }
  digitalWrite(led, LOW);
}

// For debugging purposes
void serialCommands(){
  if(Serial.available())
  {
    String temp = Serial.readString();
    if(temp.substring(0,4) == "help"){
      Serial.println("cm to toggle calibration mode");
      Serial.println("0 to show calibration data on display");
      Serial.println("1 to show transaction data");
    }
    else if(temp.substring(0,2) == "cm"){
      if(temp.substring(3) == "true"){
        calibrationMode = true;
      }
      else if(temp.substring(3) == "false"){
        calibrationMode = false;
      }
    }
    else if(temp.substring(0,1) == "0"){
      Serial.println("Display now showing calibration data");
      displayMode = 0;
    }
    else if(temp.substring(0,1) == "1"){
      Serial.println("Display now showing transaction data");
      displayMode = 1;
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
        if(scale.getLastStableWeight() < -5 && scale.getReadWeight() < -5){
          scale.reset(); //Counter sensor drift
        }
        //Stabilized with weight greater than 10 grams
        if(scale.isStable() && (int) scale.getLastStableWeight() > 10){
          scale.setContainerWeight(scale.getLastStableWeight());
          scale.reset();
          // Cup is on scale, we can check for nfc chip
          sysStatus = AUTHENTICATION;
        }
      }
    break;

    case AUTHENTICATION:{
        digitalWrite(RED_LED_PIN, HIGH);
        unsigned long waitStart = millis();
        bool waitForNFC = true;

        //We wait for a card to be read whithin a 3 sec time window
        // Serial.print(String(waitStart));
        while(millis()-waitStart< 3000){
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){

            // Check for compatibility
            if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
                Serial.println(F("This sample only works with MIFARE Ultralight C."));
                return;
            }

            // Read and decrypt data from card into dataBlock;
            nfc.PICC_CopyMifareUltralightData(firstPage, lastPage, dataBlock);
            cipherInit(&chacha);
            chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));

            // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
            if(matchDatablockUid(dataBlock, &nfc.uid)){
              Serial.println("Decrypted dataBlock (decrypted byte[]) : ");
              dump_byte_array(dataBlock, sizeof(dataBlock));
              Serial.println("Current balance: "+String(((float)dataBlockToCash(dataBlock)/100)));
              waitForNFC = false;
            }
            else{
              Serial.println("Error, UID does not match UID in chip");
              flash(100, RED_LED_PIN, 15);
              sysStatus = STANDBY;
            }
            break;
          }
        }

        // Card read, go ahead and do stuff
        if(!waitForNFC){
          // Previous balance read on card-coffee bought
          oldBalance = (float) dataBlockToCash(dataBlock);
          waitForNFC = true;
          digitalWrite(RED_LED_PIN, LOW);
          sysStatus = SERVING;
        }
        else{ // Card not read, return to standby...for now
          flash(100, RED_LED_PIN, 15);
          sysStatus = STANDBY;
        }
      }
    break;

    case SERVING:{
        if(scale.getReadWeight() > 10){
          price = (scale.getReadWeight()/250*PRICE_PER_CUP)/100;
          if(scale.isStable() && scale.getContainerWeight() != scale.getLastStableWeight()){
            scale.setDrinkWeight(scale.getLastStableWeight());

            // Cup is on scale, we can check for nfc chip
            sysStatus = PAYMENT;
          }
        }
      }
    break;

    case PAYMENT:{
        digitalWrite(RED_LED_PIN, HIGH);
        unsigned long waitStart = millis();
        bool waitForNFC = true;
        // Serial.println("PAYMENT MODE "+String(waitStart));
        //We wait for a card to be read whithin a 3 sec time window
        while(millis()-waitStart< 3000){
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
            // Check for compatibility
            if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
                Serial.println(F("This sample only works with MIFARE Ultralight C."));
                return;
            }

            // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
            if(matchDatablockUid(dataBlock, &nfc.uid)){
                waitForNFC = false;
            }
            else{
              Serial.println("Error, UID does not match UID in chip");
              flash(100, RED_LED_PIN, 15);
              sysStatus = STANDBY;
            }
            break;
          }
        }
        // Card read, do stuff
        if(!waitForNFC){

          newBalance = (oldBalance - scale.getDrinkWeight()/250*PRICE_PER_CUP);
          price = (scale.getDrinkWeight()/250*PRICE_PER_CUP)/100;
          // Prepare dataBlock to write to chip
          cashToDataBlock(dataBlock, newBalance, &nfc.uid);

          //Encrypt data before writing
          cipherInit(&chacha);
          chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));

          // Write one page (4 bytes) at a time (Chip constraint)
          writeDatablockToCard(dataBlock, firstPage);

          // Halt PICC
          nfc.PICC_HaltA();
          digitalWrite(RED_LED_PIN, LOW);
          sysStatus = CLEARING;
        }

        else{ // Card not read, return to standby...for now
          flash(100, RED_LED_PIN, 15);
          sysStatus = STANDBY;
        }
      }
    break;

    case CLEARING:{
        waitBlink(0, 1000, GREEN_LED_PIN);
        if(scale.isStable() && (int) scale.getLastStableWeight() < 0 &&
           !nfc.PICC_IsNewCardPresent()){
          // Serial.println("CUSTOMER LEFT");
          oldBalance = 0;
          newBalance = 0;
          price = 0;
          scale.resetDrink();
          digitalWrite(GREEN_LED_PIN, HIGH);
          scale.reset();
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

  scale.init();
  sysStatus = STANDBY;
}

void loop() {
  scale.updateWeight();   //Read scale and update weight related values
  scale.stabilityCheck(); //Checks weight related values and determines stability
  userLoop();       //After weight and stability updates, use info accordingly in use case loop

  //Display management
  setDisplayWeight(scale.getReadWeight());
  setDisplayInfo(displayMode);
  activateDisplay(1);

  //Hardware and serial stuff
  serialCommands();
  buttonReset();
  if(calibrationMode){
    scale.setCalibrationFactor(potCalibration());
  }
}
