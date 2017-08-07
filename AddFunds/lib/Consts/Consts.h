#ifndef _CONSTS
#define _CONSTS_h
#include "Arduino.h"

enum STATUS {
  STANDBY,        //Container weight 0, tare scale, wait for customer
  AUTHENTICATION, //Container and chip detected
  SERVING,        //Container weight set, pouring coffee
  PAYMENT,        //Pouring done, finished weight stuff, writing to chip
  CLEARING        //Transaction finished, look for negative container weight then tare
};

const uint8_t BUTTON_PIN = 5;     // the number of the pushbutton pin
const uint8_t GREEN_LED_PIN = 6;
const uint8_t RED_LED_PIN = 7;
const uint8_t POT_PIN = A0;    // select the input pin for the potentiometer
const uint8_t BASE_CALIBRATION_OFFSET = 200;
const uint8_t PRICE_PER_CUP = 50; //cents per 250ml

// LCD Pins
const uint8_t OLED_RESET = 4;

// Scale pins
const uint8_t DOUT = 3;
const uint8_t CLK = 2;

// For RFID Reader
const uint8_t RST_PIN =9;
const uint8_t SS_PIN = 53;

String enumToString(uint8_t sys);

#endif
