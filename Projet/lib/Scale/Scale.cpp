#include <Arduino.h>
#include <HX711.h>
#include <Scale.h>
#include <Consts.h>

extern STATUS sysStatus;

Scale::Scale(uint8_t dout, uint8_t clk){
  hx711 = new HX711(dout, clk);
  calibration_factor = 221;
  weightVarThresh = 0.5;
  weightDiffThresh = 5;
  stableTime = 3000;
  waitingForStability = true;
  fluctuationTime = 0;
  isTaring = false;
  endingTransaction = false;

  containerWeight = 0;
  drinkWeight = 0;
  weightVar = 0;
  lastStableWeight = 0;
  readWeight = 0;
  lastReadWeight = 0;

  start = millis();
  zero = 0;
}

void Scale::stabilityCheck(){
  if(abs(readWeight-lastStableWeight) > 1 && !isTaring){
    //Serial.println("Weight Diff: "+String(abs(readWeight-lastStableWeight)));
    waitingForStability = true; // Change of weight detected
  }else{ // Bypass fluctuation from isTaring scale
    isTaring = false;
  }
  if(waitingForStability){
    if(abs(readWeight-lastReadWeight)<1){
      if(millis()-start > 3000){
        //STABLE WEIGHT!
        waitingForStability = false;
        lastStableWeight = readWeight;
      }
    }else{
      start = millis();
    }
  }
}

float Scale::getSensorWeight(){
  return hx711->get_units();
}

void Scale::updateWeight(){
  //Scale activity
  lastReadWeight = readWeight;
  readWeight = getSensorWeight();
  if(readWeight < 0 && readWeight > -1){ //Remove -0 displaying
    readWeight = 0;
  }
}

void Scale::resetDrink(){
  containerWeight = 0;
  drinkWeight = 0;
}

void Scale::init(){
  hx711->set_scale(calibration_factor);
  hx711->tare();
  float start = millis();
  float initialWeight = getSensorWeight();
  waitingForStability = true;
  while(waitingForStability){
    if(abs(initialWeight-getSensorWeight())<1){
      if((millis()-start) >= 3000){
        zero = initialWeight;
        lastStableWeight = zero;
        waitingForStability = false;
      }
    }else{
      start = millis();
    }
  }
}

void Scale::reset(){
  isTaring = true;
  hx711->tare();
}

float Scale::getLastStableWeight(){
  return lastStableWeight;
}

float Scale::getReadWeight(){
  return readWeight;
}

void Scale::setCalibrationFactor(float fact){
  calibration_factor = fact;
}

float Scale::getCalibrationFactor(){
  return calibration_factor;
}

float Scale::getWeightVar(){
  return weightVar;
}

bool Scale::isStable(){
  return !waitingForStability;
}

void Scale::setContainerWeight(float weight){
  containerWeight = weight;
  return;
}
float Scale::getContainerWeight(){
  return containerWeight;
}
void Scale::setDrinkWeight(float weight){
  drinkWeight = weight;
  return;
}
float Scale::getDrinkWeight(){
  return drinkWeight;
}

bool Scale::isDrifting(){
  if(getLastStableWeight() != 0 && getReadWeight() < 0){
    return true;
  }
  return false;
}
