#ifndef MYLIB_h
#define MYLIB_h

//Scale functions
void  fluctuationListen();
void  stabilityCheck();
float getSensorWeight();
void  updateWeight();

//LCD functions
void  setDisplayInfo(int displayMode);
void  activateDisplay(float del);
void  setDisplayWeight(float weight);

//Misc./Hardware functions
void  readButtonTare();
float potCalibration();
void  waitBlink(int min, int max, int led);
void  alert();
void  setStandbySensitivity();
void  setServingSensitivity();
void  setClearingSensitivity();
void  serialCommands();

#endif
