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
#include <Adafruit_NeoPixel.h>

// LCD SETUP
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Neopixel setup
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(8, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
int lightPos = 0; // Light index variable
float lastLightCycle = 0; // Light timing variable

// Hardware variables
uint8_t   displayMode = 2;               // What info should the LCD show
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
  0x00, 0x00, 0x00, 0x00,       // dataBlock[11-23] = random hex value
  0x00, 0x00, 0x00, 0x00,       // dataBlock[24-31] = CAFE CAFE CAFE CAFE CAFE
  0x00, 0x00, 0x00, 0x00,       // Occupies all user available memory on card
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
uint8_t firstPage = 8;   // First page from which we read/write the 16 dataBlock bytes
uint8_t lastPage = 15;   // First page from which we read/write the 16 dataBlock bytes


int oldBalance = 0;
int newBalance = 0;
float priceFloat = 0;

String oldBalanceString;
String newBalanceString;
String priceString = "0.00"; //Display variables

float drinkWeight = 0;
float containerWeight = 0;



STATUS sysStatus;

//==============================LCD FUNCTIONS===================================
void activateDisplay(int del){
  display.display();
  delay(del);
  display.clearDisplay();
}

void setDisplayWeight(float weight){
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.print("Vol:");
  display.println(String(weight,0)+"ml");
}

void invalidCard(String str){
  Serial.println(str);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Carte invalide!");
  display.display();
  delay(5000);
}

float weightToCashFloat(){
  float numCups = scale.getReadWeight()/250;
  return numCups*PRICE_PER_CUP;
}

String weightToCashString(){
  float numCups = scale.getReadWeight()/250;
  return String((numCups*PRICE_PER_CUP)/100);
}

String cashToString(float cents){
  return String(cents/100);
}

void setDisplayInfo(uint8_t displayMode){
    if(displayMode == 0){ // Calib mode
      display.setTextColor(BLACK, WHITE);
      display.println("Calib:");
      display.println(String(scale.getCalibrationFactor(),0));
      display.println("Pot: "+ String(analogRead(POT_PIN)));
    }
    else if(displayMode == 1){ //Debug mode
      display.setTextColor(BLACK, WHITE);
      display.print("lstStblWght: ");
      display.println((int)scale.getLastStableWeight());
      display.print("sysStatus: ");
      display.println(enumToString(sysStatus));
      display.println("oldBalance: "+cashToString(oldBalance)+"$");
      display.println("newBalance: "+cashToString(newBalance)+"$");
      display.println("Paid: "+priceString);
    }
    else if(displayMode == 2){ // User mode
      display.setTextColor(WHITE);
      display.setTextSize(1);
      String status = enumToString(sysStatus);
      if( status == "STANDBY" ){
        if(scale.getReadWeight() > 10 && !scale.isStable()){
          display.println("");
          display.println("Stabilisation");
          display.println("en cours!");
        }
        else{
          display.println("");
          display.print("Veuillez deposer une tasse NFC");
        }
      }
      if( status == "AUTHENTICATION" ){
        display.println("");
        display.print("Lecture de la carte...");
      }
      else if( status == "SERVING" ){
        if(scale.getReadWeight() >= 0){
          display.println("");
          display.print("Prix: ");
          display.println(priceString+"$");
          display.print("Solde: ");
          display.println(oldBalanceString+"$");
        }
        else if(scale.getReadWeight() <= -5){
          display.println("");
          display.print("Veuillez redeposer votre tasse!");
        }
      }
      else if( status == "DONE" ){
        display.println("");
        display.print("Prix final: ");
        display.println(priceString+"$");
        display.print("Nouveau solde: ");
        display.println(newBalanceString+"$");
        display.println("");
        display.println("Veuillez retirer la");
        display.println("tasse!");
      }
    }
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
  Serial.println("Dollars:"+String(dollars)+" Cents:"+String(cents));
  dataBlock[0] = dollars;
  dataBlock[1] = cents;
  dataBlock[2] = sign;
  for(int i=4 ; i < uid->size+4 ;i++){
    dataBlock[i] = (uid->uidByte)[i-4];
  }

  // Fill with random values to add "randomness" to encryption
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
  for(int i=0; i<sizeof(dataBlock); i+=4){
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

// Validate card format
bool isUserCard(byte* dataBlock) {
  if(dataBlock[24]==0XCA && dataBlock[25]==0XFE &&
     dataBlock[26]==0XCA && dataBlock[27]==0XFE &&
     dataBlock[28]==0XCA && dataBlock[29]==0XFE &&
     dataBlock[30]==0XCA && dataBlock[31]==0XFE){
       return true;
  }
  return false;
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
  return;
}

// All red
void errorLights(){
  initLights();
  return;
}

// All blue
void standbyLights(){
  clearLights();
  for(byte i=0; i<8; i+=1){
    pixels.setPixelColor(i, pixels.Color(0, 0, 255));
  }
  pixels.show();
  return;
}

// Purple fade-in-out
void stabilityLights(){
  if(lightPos>255){ // Up-down
    for(byte i=0; i< 8 ;i+=1){
      pixels.setPixelColor(i, pixels.Color(255-(lightPos%255), 0, 255-(lightPos%255)));
    }
  }else{ // Down-up
    for(byte i=0; i< 8 ;i+=1){
      pixels.setPixelColor(i, pixels.Color(lightPos, 0, lightPos));
    }
  }
  lightPos += 40;
  lightPos = lightPos >= 508 ? 1:lightPos;
  lastLightCycle = millis();
  pixels.show();
  return;
}

// Green fade-in-out
void waitForServeLights(){
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
  pixels.show();
  return;
}

// Yellow left-right
void servingLights(){
  clearLights();
  if(lightPos>8){ // Right to left
    pixels.setPixelColor(7-(lightPos%7), pixels.Color(255, 255, 0));
  }else{ // Left to right
    pixels.setPixelColor(lightPos, pixels.Color(255, 255, 0));
  }
  lightPos++;
  lightPos = lightPos == 14 ? 0:lightPos;
  lastLightCycle = millis();
  pixels.show();
  return;
}

// Green flash
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
  return;
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
    return;
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
  return;
}

// For debugging purposes
void serialCommands(){
  if(Serial.available())
  {
    String temp = Serial.readString();
    if(temp.substring(0,4) == "help"){
      Serial.println("cm to toggle calibration mode");
      Serial.println("0 to show calibration data on display");
      Serial.println("1 to show debug data");
      Serial.println("2 to show user mode");
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
      Serial.println("Display now showing debug data");
      displayMode = 1;
    }
    else if(temp.substring(0,1) == "2"){
      Serial.println("Display now showing user mode");
      displayMode = 2;
    }
  }
  return;
}


//==============================================================================
// Main usage loop
void userLoop(){

  switch(sysStatus){

    case STANDBY:{

        //Counter sensor drift and taring fluctuation
        if(scale.isDrifting()){
          initLights();
          scale.reset();
        }
        //Stabilized with weight greater than 10 grams
        if(scale.isStable() && (int) scale.getLastStableWeight() > 10){
          containerWeight= scale.getLastStableWeight();
          scale.reset();
          lightPos = 0; // Reset light management variable
          sysStatus = AUTHENTICATION;// Cup is on scale, we can check for nfc chip
        }else if(scale.getReadWeight() > 10 && !scale.isStable()){
          // Currently stabilizing
          stabilityLights();
        }else{
          // Nothing to stabilize
          standbyLights();
        }
      }
    break;

    case AUTHENTICATION:{
        unsigned long waitStart = millis();
        bool waitForNFC = true;
        Serial.println("AUTHENTICATING!");
        //We wait for a card to be read whithin a 3 sec time window
        // Serial.print(String(waitStart));
        while(millis()-waitStart< 3000){
          stabilityLights();
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
            // Check for compatibility
            if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
                Serial.println(F("This sample only works with MIFARE Ultralight."));
                sysStatus = STANDBY;
                return;
            }
            Serial.println("READ A CARD!");

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
              sysStatus = STANDBY;
            }
            break;
          }
        }

        // Card read, go ahead and do stuff
        if(!waitForNFC){
          // Previous balance read on card-coffee bought
          oldBalance = dataBlockToCash(dataBlock);
          oldBalanceString = cashToString(oldBalance);
          waitForNFC = true;
          sysStatus = SERVING;
        }
        else{ // Card not read, return to standby...for now
          errorLights();
          display.clearDisplay();
          display.setCursor(0,0);
          display.println("Aucune carte lue!");
          display.display();
          delay(3500);
          sysStatus = STANDBY;
        }
      }
    break;

    case SERVING:{
        if(scale.getReadWeight() > 10){
          servingLights();

          priceFloat = weightToCashFloat();
          priceString = cashToString(priceFloat);

          if(scale.isStable() && containerWeight != scale.getLastStableWeight()){
            drinkWeight = scale.getLastStableWeight();

            // Cup is on scale, we can check for nfc chip
            sysStatus = PAYMENT;
          }
        }else if(scale.getReadWeight() <= -5){
          errorLights();
        }
        else{
          waitForServeLights();
        }
      }
    break;

    case PAYMENT:{
        unsigned long waitStart = millis();
        bool waitForNFC = true;
        // Serial.println("PAYMENT MODE "+String(waitStart));
        //We wait for a card to be read whithin a 3 sec time window
        while(millis()-waitStart< 3000){
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
            // Check for compatibility
            if (MFRC522::PICC_GetType(nfc.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL) {
                Serial.println(F("This sample only works with MIFARE Ultralight."));
                sysStatus = STANDBY;
                return;
            }

            // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
            if(matchDatablockUid(dataBlock, &nfc.uid)){
                waitForNFC = false;
            }
            else{
              Serial.println("Error, UID does not match UID in chip");
              sysStatus = STANDBY;
            }
            break;
          }
        }
        // Card read, do stuff
        if(!waitForNFC){
          int priceInt = drinkWeight/250*PRICE_PER_CUP;
          newBalance = (oldBalance - priceInt);
          priceFloat = (float) priceInt/100; // Adjusting for division precision loss
          Serial.println("int price "+String(priceInt) + " vs price " + String(priceFloat));
          // Prepare dataBlock to write to chip
          cashToDataBlock(dataBlock, newBalance, &nfc.uid);

          //Encrypt data before writing
          cipherInit(&chacha);
          chacha.decrypt(dataBlock, dataBlock, sizeof(dataBlock));

          // Write one page (4 bytes) at a time (Chip constraint)
          writeDatablockToCard(dataBlock, firstPage);

          // Halt PICC
          nfc.PICC_HaltA();

          newBalanceString = cashToString(newBalance);
          priceString = cashToString(priceInt);

          lastLightCycle = 0;
          sysStatus = DONE;
        }

        else{ // Card not read, return to standby...for now
          sysStatus = STANDBY;
        }
      }
    break;

    case DONE:{
        doneLights();
        if(scale.isStable() && (int) scale.getLastStableWeight() < 0 &&
           !nfc.PICC_IsNewCardPresent()){
          oldBalance = 0;
          newBalance = 0;
          priceFloat = 0;
          priceString = "0.00";
          containerWeight = 0;
          drinkWeight = 0;
          scale.reset();
          sysStatus = STANDBY;
        }
      }
    break;
  }
}

// Hardware initialization
void setup()   {
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
  display.println("Initialisation...");
  display.println("");
  display.println("Ne pas toucher SVP!");
  display.display();

  // NFC Startup
  SPI.begin();        // Init SPI bus
  nfc.PCD_Init(); // Init MFRC522 card

  scale.init();
  sysStatus = STANDBY;
}

// Main function
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
