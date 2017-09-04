#include "Arduino.h"

String enumToString(uint8_t sys){
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
      return "DONE";
  }
  return "SYS STATUS ERROR";
}
