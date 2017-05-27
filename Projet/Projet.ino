#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

// LCD SETUP
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// ================Worry free global values=======================
//Pin declaration
#define DOUT  3
#define CLK  2
const int buttonPin = 5;     // the number of the pushbutton pin
const int greenLedPin = 6;
const int redLedPin = 7;
int potPin = A0;    // select the input pin for the potentiometer

HX711 scale(DOUT, CLK);
float base_calibration_factor = 225;
float calibration_factor = 225; 
float calibration_multiplier = 1;
float pin_inc = 10;
float inc_sens = 20;
int displayMode = 2;

//=================SENSITIVE global stuff========================
int   loopDelta = 20;
float weightVarThresh = 0.5;
float weightDiffThresh = 5;
bool  stableWait = true;
int   stableTime = 3000;
long  fluctuationTime = 0;
bool  taring = false;
bool  endingTransaction = false;
bool  calibrationMode = false;

float containerWeight;
float drinkWeight;
float lastWeight = 0;
float weightVar;
float lastStableWeight;
float valueRead;

int   potValue = 0;  // variable to store the value coming from the sensor
int   buttonState;
int   lastButtonState = LOW;   // the previous reading from the input pin

// the following variables are unsigned long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

enum MODE {
  STANDBY,        //Container weight 0, tare scale, wait for customer
  AUTHENTICATION, //Container and chip detected
  SERVING,        //Container weight set, pouring coffee
  PAYMENT,        //Pouring done, finished weight stuff, writing to chip
  CLEARING        //Transaction finished, look for negative container weight then tare
};

MODE sysStatus;

void setup()   {                
  Serial.begin(9600);
  Serial.println("Starting! Enter 'help' to see command list");
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
  // init done
  pinMode(buttonPin, INPUT);
  pinMode(greenLedPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  scale.set_scale();
  scale.tare();  //Reset the scale to 0
  digitalWrite(greenLedPin, HIGH);
  sysStatus = STANDBY;
}


void loop() {
   
  switch(sysStatus){
    case STANDBY:
      updateWeight();
      if(containerWeight>1){
        sysStatus = AUTHENTICATION;
        Serial.println("CONTAINER WEIGHT: "+String(containerWeight));
      }
    break;
    case AUTHENTICATION:
      // IF NOTHING IS READ WHITIN A CERTAIN DELAY, 
      // INTERPRET FLUCTUATION AS A FALSE POSITIVE AND REVERT TO STANDBY
      Serial.println("SIMULATING READING NFC CHIP BEEP BOOP");
      delay(1000);
      Serial.println("3..");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, POUR SOME JAVA ON ME");
      setServingSensitivity();
      sysStatus = SERVING;
    break;
    case SERVING:
    
      if(valueRead <=2 && valueRead >= 0 ){
        waitForUser();
      }
      
      if(valueRead<0){
        Serial.println("ALERT! ALERT! COFFEE THIEF :((");
        alert();
        containerWeight = 0; 
        scale.tare();
      }
      updateWeight();
    break;
    case PAYMENT:
      Serial.println("SIMULATING NFC PAYMENT BEEP BOOP");
      delay(1000);
      Serial.println("3..");
      delay(1000);
      Serial.println("2...");
      delay(1000);
      Serial.println("1....");
      delay(1000);
      Serial.println("ALL GOOD, ENJOY YOUR DRINK");
      setClearingSensitivity();
      sysStatus = CLEARING;
    break;
    case CLEARING:
      updateWeight();
      float instance = millis();
      bool toggle = false;
      while(scaleWeight() > 0){
        if(millis()-instance>100){
          instance = millis();
          toggle = !toggle;
          digitalWrite(greenLedPin, toggle);
          instance = millis();
         }
            
      }
//      digitalWrite(greenLedPin, LOW);
      
      if(lastStableWeight < 0){
        Serial.println("CUSTOMER LEFT");
        containerWeight = 0;
        drinkWeight = 0;
        setStandbySensitivity();
        digitalWrite(greenLedPin, HIGH);
        scale.tare();
        sysStatus = STANDBY;
      }
    break;
  }
      
  //Display management
  setDisplayWeight(valueRead);
  setDisplayInfo();
  activateDisplay(loopDelta);

  //Hardware and serial stuff
  serialCommands();
  readButtonTare();
}

void updateWeight(){
  //Scale activity
  if(calibrationMode){
    calibration_factor = potCalibration();
  }
  valueRead = scaleWeight();
  weightVar = (valueRead - lastWeight)/loopDelta; //Delta weight/delta Time
  lastWeight = valueRead;
  stabilityCheck();
}

void stabilityCheck(){
  if(abs(weightVar)>weightVarThresh){ //If genuine fluctuation
    if(!taring && !endingTransaction){
      //Serial.println("Flux detected: "+String(weightVar,2)+", went from "+String((int) abs(weightVar*loopDelta))+" to "+String(valueRead,2));
      stableWait = true;
      fluctuationTime = millis();
      digitalWrite(redLedPin, HIGH);
      digitalWrite(greenLedPin, LOW);
    }else{ // Bypass fluctuation from taring scale
      taring = false;
      endingTransaction = false;
      stableWait = false;
    }
    
  }
  if(stableWait){
    
    if((millis()-fluctuationTime) > stableTime){
      stableWait = false;
      //Weight stabilized
      if(abs(lastStableWeight-valueRead)>weightDiffThresh){
        lastStableWeight = valueRead;
        Serial.println("Last stable weight: "+String(lastStableWeight, 3));
//        taring = true;
//        scale.tare();
        
        switch(sysStatus){
          case STANDBY:
            taring = true;
            scale.tare();
            containerWeight = lastStableWeight;
            break;
          case SERVING:
            taring = true;
            scale.tare();
            Serial.println("Serving done after 4 second stability");
            drinkWeight = lastStableWeight;
            Serial.println("Drink weight: "+String(drinkWeight));
            delay(2000);
            Serial.println("Hold on for payment");
            sysStatus = PAYMENT;
          break;
          case CLEARING:
            endingTransaction = true;
            digitalWrite(redLedPin, LOW);
            digitalWrite(greenLedPin, HIGH);
          break;
        }
      }
      else{
        Serial.println("Not stable");
      }
    }
  }
}

void activateDisplay(float del){
  display.display();
  delay(del);
  display.clearDisplay();
}

void readButtonTare(){
  int reading = digitalRead(buttonPin);
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
        scale.tare();
      }
    }
  }
  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}

float scaleWeight(){
  scale.set_scale(calibration_factor); //Adjust to this calibration factor
  float valueRead = scale.get_units()*calibration_multiplier;
  if(valueRead < 0 && valueRead > -1) //Remove -0 displaying
    valueRead = 0;
  return valueRead;
}

void setDisplayWeight(float weight){
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(String(weight,0)+"g");
}

float potCalibration(){
  float offsetReadValue = analogRead(potPin)/inc_sens;
  return base_calibration_factor+(offsetReadValue*pin_inc);
}

void setDisplayInfo(){
    if(displayMode == 0){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("Calib:"); 
      display.println(String(calibration_factor,0));
      display.println("Pot: "+ String(analogRead(potPin)));
    }
    else if(displayMode == 1){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("WeightVar: "); 
      display.println(weightVar);
    }
    else if(displayMode == 2){
      display.setTextColor(BLACK, WHITE); // 'inverted' text
      display.println("sysStatus: "); 
      display.println(enumToString(sysStatus));
    }
}

void setStandbySensitivity(){
  weightVarThresh = 0.5;
  weightDiffThresh = 5;
  stableTime = 3000;
}

void setServingSensitivity(){
  weightVarThresh = 0.15;
  weightDiffThresh = 2;
  stableTime = 4000;
}
void setClearingSensitivity(){
  weightVarThresh = 2;
  weightDiffThresh = 50;
  stableTime = 1500;
}


void serialCommands(){
  if(Serial.available())
  {
    String temp = Serial.readString();
    if(temp.substring(0,4) == "help"){
      Serial.println("cf to set base_calibration_factor");
      Serial.println("pi to set pin_inc (pot increments");
      Serial.println("is to set inc_sens (pot sensitivity)");
      Serial.println("cf to set calibration_multiplier");
      Serial.println("cm to set calibration_multiplier");
      Serial.println("0 to show calibration data on display");
      Serial.println("1 to show other crap on display");
    }
    else if(temp.substring(0,2) == "cf"){
      Serial.println("Setting calibration_factor to: "+temp.substring(3));
      base_calibration_factor = temp.substring(3).toFloat();
    }
    else if(temp.substring(0,2) == "pi"){
      Serial.println("Setting pin_inc to: "+temp.substring(3));
      pin_inc = temp.substring(3).toFloat();
    }
    else if(temp.substring(0,2) == "is"){
      Serial.println("Setting inc_sens to: "+temp.substring(3));
      inc_sens = temp.substring(3).toFloat();
    }
    else if(temp.substring(0,2) == "cf"){
      Serial.println("Setting calibration_multiplier to: "+temp.substring(3));
      calibration_multiplier = temp.substring(3).toFloat();
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
  }
}

String enumToString(int sys){
  switch(sys){
    case 0:
      return "STANDBY";
    case 1:
      return "AUTHENTICATION";
    case 2:
      return "SERVING";
    case 3:
      return "PAYMENT";
    case 4:
      return "CLEARING";
  }
}

void alert(){
  int blinkCount = 0;
  float instance = millis();
  bool flag = false;
  bool toggle = false;
  while(!flag){
    if(millis()-instance>200){
      instance = millis();
      toggle = !toggle;
      digitalWrite(redLedPin, toggle);
      digitalWrite(greenLedPin, !toggle);
      
      blinkCount++;
    }
    if(blinkCount == 20){
      flag = true;
    }
  }
  sysStatus = STANDBY;
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);
}

void waitForUser(){
  float instance = millis();
  bool toggle = false;
  while(scaleWeight() <= 1){
    if(millis()-instance>100){
      instance = millis();
      toggle = !toggle;
      digitalWrite(greenLedPin, toggle);
      instance = millis();
    }
  }
  digitalWrite(greenLedPin, LOW);
}

