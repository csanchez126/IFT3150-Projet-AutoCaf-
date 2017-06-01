#include <Arduino.h>
#include <HX711.h>
#include <Scale.h>
#include <Consts.h>

extern STATUS sysStatus;

Scale::Scale(int dout, int clk){
  scale = new HX711(dout, clk);
  calibration_factor = 200;
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
  sensorWeight = 0;
  lastReadWeight = 0;
}

void Scale::fluctuationListen(){
  if(abs(weightVar)>weightVarThresh){ //If genuine fluctuation
    if(!isTaring && !endingTransaction){ //If weight fluction not from system status change of tare
      //Serial.println("Flux detected: "+String(weightVar,2)+", went from "+String((int) abs(weightVar*loopDelta))+" to "+String(sensorWeight,2));
      waitingForStability = true;
      fluctuationTime = millis();
      digitalWrite(RED_LED_PIN, HIGH);
      digitalWrite(GREEN_LED_PIN, LOW);
    }else{ // Bypass fluctuation from isTaring scale
      isTaring = false;
      endingTransaction = false;
      waitingForStability = false;
    }
  }
}

void Scale::stabilityCheck(){
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
            scale->tare();
            containerWeight = lastStableWeight;
            break;
          case SERVING:
            isTaring = true;
            scale->tare();
            Serial.println("Serving done after 4 second stability");
            drinkWeight = lastStableWeight;
            Serial.println("Drink weight: "+String(drinkWeight));
            Serial.println("Hold on for payment");
            sysStatus = PAYMENT;
          break;
          case CLEARING:
            endingTransaction = true;
            digitalWrite(RED_LED_PIN, LOW);
            digitalWrite(GREEN_LED_PIN, HIGH);
          break;
        }
      }
      else{
        Serial.println("Not stable");
      }
    }
  }
}

float Scale::getSensorWeight(){
  scale->set_scale(calibration_factor); //Adjust to this calibration factor
  float sensorWeight = scale->get_units();
  if(sensorWeight < 0 && sensorWeight > -1) //Remove -0 displaying
    sensorWeight = 0;
  return sensorWeight;
}

void Scale::updateWeight(){
  //Scale activity
  sensorWeight = getSensorWeight();
  weightVar = (sensorWeight - lastReadWeight)/LOOP_DELTA; //Delta weight/delta Time
  lastReadWeight = sensorWeight;
}

float Scale::getContainerWeight(){
  return containerWeight;
}

void Scale::resetDrink(){
  containerWeight = 0;
  drinkWeight = 0;
}

void Scale::init(){
  scale->set_scale();
  scale->tare();
}

void Scale::reset(){
  scale->tare();
}

float Scale::getLastStableWeight(){
  return lastStableWeight;
}

float Scale::getLastReadWeight(){
  return lastReadWeight;
}

void Scale::setStandbySensitivity(){
  weightVarThresh = 0.5;
  weightDiffThresh = 5;
  stableTime = 3000;
}

void Scale::setServingSensitivity(){
  weightVarThresh = 0.15;
  weightDiffThresh = 2;
  stableTime = 4000;
}
void Scale::setClearingSensitivity(){
  weightVarThresh = 2;
  weightDiffThresh = 50;
  stableTime = 1500;
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
