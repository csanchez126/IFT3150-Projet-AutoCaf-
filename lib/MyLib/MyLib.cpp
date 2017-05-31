#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include <Status.h>

extern Adafruit_SSD1306 display;
extern HX711 scale;
extern int greenLedPin;
extern int redLedPin;
extern int buttonPin;  // the number of the pushbutton pin
extern int potPin;    // select the input pin for the potentiometer

extern float base_calibration_factor;
extern float calibration_factor;
extern float calibration_multiplier;
extern float pin_inc;   // calibration_factor increments
extern float inc_sens;  // Values the pot has to increase before incrementing calibration_factor
extern int displayMode;  // What info should the LCD show

extern int   potValue;                  // variable to store the value coming from the sensor
extern int   buttonState;                   // For manual taring
extern int   lastButtonState;         // the previous reading from the input pin
extern unsigned long lastDebounceTime;  // the last time the output pin was toggled
extern unsigned long debounceDelay;    // the debounce time; increase if the output flickers

//=================SENSITIVE global stuff========================
extern int   loopDelta;
extern bool  calibrationMode;
extern float lastReadWeight;

extern float weightVarThresh;
extern float weightDiffThresh;
extern int   stableTime;
extern bool  waitingForStability;
extern long  fluctuationTime;
extern bool  isTaring;
extern bool  endingTransaction;

extern float containerWeight;
extern float drinkWeight;
extern float weightVar;
extern float lastStableWeight;
extern float sensorWeight;

extern STATUS sysStatus;

//==============================SCALE FUNCTIONS====================================
void fluctuationListen(){
  if(abs(weightVar)>weightVarThresh){ //If genuine fluctuation
    if(!isTaring && !endingTransaction){ //If weight fluction not from system status change of tare
      //Serial.println("Flux detected: "+String(weightVar,2)+", went from "+String((int) abs(weightVar*loopDelta))+" to "+String(sensorWeight,2));
      waitingForStability = true;
      fluctuationTime = millis();
      digitalWrite(redLedPin, HIGH);
      digitalWrite(greenLedPin, LOW);
    }else{ // Bypass fluctuation from isTaring scale
      isTaring = false;
      endingTransaction = false;
      waitingForStability = false;
    }
  }
}

void stabilityCheck(){
  fluctuationListen();
  if(waitingForStability){
    if((millis()-fluctuationTime) > stableTime){
      waitingForStability = false;
      //Weight stabilized,
      if(abs(lastStableWeight-sensorWeight)> weightDiffThresh ){
        lastStableWeight = sensorWeight;
        Serial.println("Last stable weight: "+String(lastStableWeight, 3));

        // Context of our stability
        switch(sysStatus){
          case STANDBY:
            isTaring = true;
            scale.tare();
            containerWeight = lastStableWeight;
            break;
          case SERVING:
            isTaring = true;
            scale.tare();
            Serial.println("Serving done after 4 second stability");
            drinkWeight = lastStableWeight;
            Serial.println("Drink weight: "+String(drinkWeight));
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

float getSensorWeight(){
  scale.set_scale(calibration_factor); //Adjust to this calibration factor
  float sensorWeight = scale.get_units()*calibration_multiplier;
  if(sensorWeight < 0 && sensorWeight > -1) //Remove -0 displaying
    sensorWeight = 0;
  return sensorWeight;
}

float potCalibration(){
  float offsetReadValue = analogRead(potPin)/inc_sens;
  return base_calibration_factor+(offsetReadValue*pin_inc);
}

void updateWeight(){
  //Scale activity
  if(calibrationMode){
    calibration_factor = potCalibration();
  }
  sensorWeight = getSensorWeight();
  weightVar = (sensorWeight - lastReadWeight)/loopDelta; //Delta weight/delta Time
  lastReadWeight = sensorWeight;
}

//==============================LCD FUNCTIONS====================================
void setDisplayInfo(int displayMode){
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

void activateDisplay(float del){
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

// Blink led if weight between min and max
void waitBlink(int min, int max, int led){
  float instance = millis();
  bool toggle = false;
  while(getSensorWeight() <= max && getSensorWeight() >= min){
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
      digitalWrite(redLedPin, toggle);
      digitalWrite(greenLedPin, !toggle);
      blinkCount++;
    }
    if(blinkCount == 20){
      flag = true;
    }
  }
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, HIGH);
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
