#ifndef _SCALE_H
#define _SCALE_H
#include "HX711.h"

class Scale
{
private:
  HX711* scale;
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
  float sensorWeight;
  float lastReadWeight;

public:
  Scale(int DOUT, int CLK);
  void fluctuationListen();
  void stabilityCheck();
  float getSensorWeight();
  void updateWeight();
  float getLastStableWeight();
  float getLastReadWeight();
  float getContainerWeight();
  void resetDrink();

  void setStandbySensitivity();
  void setServingSensitivity();
  void setClearingSensitivity();

  void setCalibrationFactor(float fact);
  float getCalibrationFactor();

  float getWeightVar();

  void init();
  void reset();
};

#endif
