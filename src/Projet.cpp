#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Consts.h>
#include <Scale.h>
#include <MFRC522.h>

// LCD SETUP
Adafruit_SSD1306 display(OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Hardware variables
int   displayMode = 1;               // What info should the LCD show
int   potValue = 0;                  // variable to store the value coming from the sensor
int   buttonState;                   // For manual taring
int   lastButtonState = LOW;         // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers
bool  calibrationMode = false;
Scale scale(DOUT,CLK);
MFRC522 nfc(SS_PIN, RST_PIN);

MFRC522::StatusCode nfcStatus;  // NFC read/write status
byte dataBlock[] = {            // Array to read/write from/to card
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
int   oldBalance = 0;
float newBalance = 0;
float price = 0;
int   firstPage = 12; // First page from which we read/write the 16 dataBlock bytes
STATUS sysStatus;

//==============================LCD FUNCTIONS===================================
void setDisplayInfo(int displayMode){
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

int dataBlockToCash(byte* dataBlock){
  int balance = ((int) dataBlock[0]) *100 + ((int) dataBlock[1]);
  balance *= dataBlock[3] == 0? 1: -1;
  return balance;
}

void cashToDataBlock(byte* dataBlock, int cash, MFRC522::Uid * uid){
  byte sign = cash<0 ? 1 : 0;
  byte dollars = cash/100;
  byte cents   = cash % 100;
  dataBlock[0] = dollars;
  dataBlock[1] = cents;
  dataBlock[2] = sign;
  for(int i=4 ;i<uid->size+4;i++){
    dataBlock[i] = (uid->uidByte)[i-4];
  }
  for(int i=11 ;i<16;i++){
    dataBlock[i] = 0x00;
  }
}

void writeDatablockToCard(byte* dataBlock, int firstPage){
  for(int i=0; i<16;i+=4){
    nfcStatus = (MFRC522::StatusCode) nfc.MIFARE_Ultralight_Write(firstPage+(i/4), dataBlock+i, 4);
    if (nfcStatus != MFRC522::STATUS_OK) {
        Serial.print(F("MIFARE_Write() failed: "));
        Serial.println(nfc.GetStatusCodeName(nfcStatus));
    }
  }
}

//==============================MISC/HARDWARE FUNCTIONS=========================

float potCalibration(){
  return BASE_CALIBRATION_OFFSET+(analogRead(POT_PIN)/20);
}

void buttonReset(){
  int reading = digitalRead(BUTTON_PIN);
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
void waitBlink(int min, int max, int led){
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

//==============================================================================
void userLoop(){
  switch(sysStatus){

    case STANDBY:{
        if(scale.getLastStableWeight() < -5 && scale.getReadWeight() < -5){
          scale.reset();
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
        Serial.print(String(waitStart));
        while(millis()-waitStart< 3000){
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
            // Hard coded UID matches "decoded" UID in chip memory starting at firstPage+1
            if(nfc.PICC_MatchUIDDataBlock(firstPage+1, &nfc.uid)){
              waitForNFC = false;
            }else{
              Serial.println("Error, UID does not match UID in chip");
            }
            break;
          }
        }

        // Card read, go ahead and do stuff
        if(!waitForNFC){
          // Read account info from card into dataBlock;
          nfc.PICC_CopyMifareUltralightData(firstPage, dataBlock);
          // Previous balance read on card-coffee bought
          oldBalance = (float) dataBlockToCash(dataBlock);
          waitForNFC = true;
          digitalWrite(RED_LED_PIN, LOW);
          sysStatus = SERVING;
        }
        else{ // Card not read, return to standby...for now
          digitalWrite(RED_LED_PIN, LOW);delay(100);
          digitalWrite(RED_LED_PIN, HIGH);delay(100);
          digitalWrite(RED_LED_PIN, LOW);delay(100);
          digitalWrite(RED_LED_PIN, HIGH);delay(100);
          sysStatus = STANDBY;
        }
      }
    break;

    case SERVING:{
        if(scale.getReadWeight() > 10){
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
        Serial.println("PAYMENT MODE "+String(waitStart));
        //We wait for a card to be read whithin a 3 sec time window
        while(millis()-waitStart< 3000){
          if(nfc.PICC_IsNewCardPresent() && nfc.PICC_ReadCardSerial()){
            // Hard coded UID matches "decoded" UID in chip page
            if(nfc.PICC_MatchUIDDataBlock(firstPage+1, &nfc.uid)){
              waitForNFC = false;
            }else{
              Serial.println("Error, UID does not match UID in chip");
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
          // Write one page (4 bytes) at a time (Chip constraint)
          writeDatablockToCard(dataBlock, firstPage);
          // Halt PICC
          nfc.PICC_HaltA();
          digitalWrite(RED_LED_PIN, LOW);
          sysStatus = CLEARING;
        }

        else{ // Card not read, return to standby...for now
          digitalWrite(RED_LED_PIN, LOW);delay(100);
          digitalWrite(RED_LED_PIN, HIGH);delay(100);
          digitalWrite(RED_LED_PIN, LOW);delay(100);
          digitalWrite(RED_LED_PIN, HIGH);delay(100);
          sysStatus = STANDBY;
        }
      }
    break;

    case CLEARING:{
        waitBlink(0, 10000, GREEN_LED_PIN);
        if(scale.isStable() && (int) scale.getLastStableWeight() < 0 &&
           !nfc.PICC_IsNewCardPresent()){
          Serial.println("CUSTOMER LEFT");
          oldBalance = 0;
          newBalance = 0;
          price = 0;
          scale.resetDrink();
          scale.setStandbySensitivity();
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
