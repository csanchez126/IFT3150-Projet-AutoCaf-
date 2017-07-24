#ifndef _SCALE_H
#define _SCALE_H
#include "HX711.h"

class Scale
{
private:
  HX711* hx711;
  float calibration_factor;

  float weightVarThresh;
  float weightDiffThresh;
  int   stableTime;
  bool  waitingForStability;
  long  fluctuationTime;
  bool  isTaring;
  bool  endingTransaction;

  float containerWeight;
  float drinkWeight;
  float weightVar;
  float lastStableWeight;
  float readWeight;
  float lastReadWeight;

  float start;
  float zero;

public:
  Scale(uint8_t DOUT, uint8_t CLK);
  void  stabilityCheck();
  float getSensorWeight();
  void  updateWeight();
  float getLastStableWeight();
  float getReadWeight();

  void resetDrink();

  void  setCalibrationFactor(float fact);
  float getCalibrationFactor();

  float getWeightVar();

  void init();
  void reset();

  bool isStable();

  void  setContainerWeight(float weight);
  float getContainerWeight();
  void  setDrinkWeight(float weight);
  float getDrinkWeight();

};

#endif
