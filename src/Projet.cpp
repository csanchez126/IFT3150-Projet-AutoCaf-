#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Consts.h>
#include <Scale.h>

//TJRS auto 0, comparer avec un 0 absolu
//Graphviz


// LCD SETUP
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

#define DOUT  3
#define CLK  2
Scale scale(DOUT,CLK);

int displayMode = 3;  // What info should the LCD show

int   potValue = 0;                  // variable to store the value coming from the sensor
int   buttonState;                   // For manual taring
int   lastButtonState = LOW;         // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers
bool  calibrationMode = false;


STATUS sysStatus;

//==============================LCD FUNCTIONS====================================
void setDisplayInfo(int displayMode){
    if(displayMode == 0){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("Calib:");
      display.println(String(scale.getCalibrationFactor(),0));
      display.println("Pot: "+ String(analogRead(POT_PIN)));
    }
    else if(displayMode == 1){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("WeightVar: ");
      display.println(scale.getWeightVar());
    }
    else if(displayMode == 2){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("sysStatus: ");
      display.println(enumToString(sysStatus));
    }
    else if(displayMode == 3){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("lastStableWeight: ");
      display.println((int)scale.getLastStableWeight());
    }
}

void activateDisplay(int del){
  display.display();
  delay(del);
  display.clearDisplay();
}

void setDisplayWeight(float weight){
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(String(weight,0)+"g");
}

//==============================MISC/HARDWARE FUNCTIONS====================================

float potCalibration(){
  return BASE_CALIBRATION_OFFSET+(analogRead(POT_PIN)/20);
}

void buttonReset(){
  int reading = digitalRead(BUTTON_PIN);
  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:
    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;
      // Read a button toggle
      if (buttonState == HIGH) {
        scale.reset();
      }
    }
  }
  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
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


void alert(){
  int blinkCount = 0;
  float instance = millis();
  bool flag = false;
  bool toggle = false;

  Serial.println("ALERT! ALERT! COFFEE THIEF :((");

  while(!flag){
    if(millis()-instance>200){
      instance = millis();
      toggle = !toggle;
      digitalWrite(RED_LED_PIN, toggle);
      digitalWrite(GREEN_LED_PIN, !toggle);
      blinkCount++;
    }
    if(blinkCount == 20){
      flag = true;
    }
  }
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
}



void serialCommands(){
  if(Serial.available())
  {
    String temp = Serial.readString();
    if(temp.substring(0,4) == "help"){
      Serial.println("cm to toggle calibration mode");
      Serial.println("0 to show calibration data on display");
      Serial.println("1 to show weightVariation on display");
      Serial.println("2 to show sysStatus on display");
      Serial.println("3 to show lastStableWeight on display");
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
      Serial.println("Display now showing weight variation");
      displayMode = 1;
    }
    else if(temp.substring(0,1) == "2"){
      Serial.println("Display now showing sysStatus");
      displayMode = 2;
    }
    else if(temp.substring(0,1) == "3"){
      Serial.println("Display now showing lastStableWeight");
      displayMode = 3;
    }
  }
}

//==============================================================
//==============================================================
void userLoop2(){
  switch(sysStatus){
    case STANDBY:
     //Stabilized with
      if(scale.isStable() && (int) scale.getLastStableWeight() > 10){
        scale.setContainerWeight(scale.getLastStableWeight());
        scale.reset();
        // Cup is on scale, we can check for nfc chip
        sysStatus = AUTHENTICATION;
      }
    break;
    case AUTHENTICATION:
      // if(nfc_validated) sysStatus = SERVING
      // else some error message , sysStatus = SERVING
      Serial.println("SIMULATING READING NFC CHIP BEEP BOOP");
      delay(1000); Serial.println("2..."); delay(1000);Serial.println("1....");
      delay(1000); Serial.println("ALL GOOD, POUR SOME JAVA ON ME");
      sysStatus = SERVING;

    break;
    case SERVING:
      if(scale.getReadWeight() > 10){
        if(scale.isStable() && scale.getContainerWeight() != scale.getLastStableWeight()){
          scale.setDrinkWeight(scale.getLastStableWeight());
          // Cup is on scale, we can check for nfc chip
          sysStatus = PAYMENT;
        }
      }

    break;
    case PAYMENT:
      Serial.println("SIMULATING READING NFC CHIP BEEP BOOP");
      delay(1000); Serial.println("2..."); delay(1000);Serial.println("1....");
      delay(1000); Serial.println("ALL GOOD, ENJOY YOUR DRINK");

      sysStatus = CLEARING;
    break;
    case CLEARING:
      waitBlink(0, 10000, GREEN_LED_PIN);
      if(scale.isStable() && (int) scale.getLastStableWeight() < 0){
        Serial.println("CUSTOMER LEFT");
        scale.resetDrink();
        scale.setStandbySensitivity();
        digitalWrite(GREEN_LED_PIN, HIGH);
        scale.reset();
        sysStatus = STANDBY;
      }
    break;
  }
}


void userLoop(){
  switch(sysStatus){
    case STANDBY:
      if(scale.getContainerWeight() > 1){
        sysStatus = AUTHENTICATION;
        Serial.println("CONTAINER WEIGHT: "+String(scale.getContainerWeight()));
      }
    break;
    case AUTHENTICATION:
      // IF NOTHING IS READ WHITIN A CERTAIN DELAY,
      // INTERPRET FLUCTUATION AS A FALSE POSITIVE AND REVERT TO STANDBY
      Serial.println("SIMULATING READING NFC CHIP BEEP BOOP");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, POUR SOME JAVA ON ME");
      scale.setServingSensitivity();
      sysStatus = SERVING;
    break;
    case SERVING:
      waitBlink(-10, 2, GREEN_LED_PIN);
    break;
    case PAYMENT:
      Serial.println("SIMULATING NFC PAYMENT BEEP BOOP");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, ENJOY YOUR DRINK");
      scale.setClearingSensitivity();
      sysStatus = CLEARING;
    break;
    case CLEARING:
      waitBlink(0, 10000, GREEN_LED_PIN);
      if(scale.getLastStableWeight() < 0){
        Serial.println("CUSTOMER LEFT");
        scale.resetDrink();
        scale.setStandbySensitivity();
        digitalWrite(GREEN_LED_PIN, HIGH);
        scale.reset();
        sysStatus = STANDBY;
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
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Initializing...");
  scale.init();
  sysStatus = STANDBY;
}

void loop() {
  scale.updateWeight();   //Read scale and update weight related values
  scale.stabilityCheck2(); //Checks weight related values and determines stability
  userLoop2();       //After weight and stability updates, use info accordingly in use case loop

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
