#ifndef STATUS_h
#define STATUS_h
#include "Arduino.h"

enum STATUS {
  STANDBY,        //Container weight 0, tare scale, wait for customer
  AUTHENTICATION, //Container and chip detected
  SERVING,        //Container weight set, pouring coffee
  PAYMENT,        //Pouring done, finished weight stuff, writing to chip
  CLEARING        //Transaction finished, look for negative container weight then tare
};

String enumToString(int sys);

#endif
