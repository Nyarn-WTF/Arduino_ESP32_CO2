#include <Arduino.h>
#include <Ambient.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_SSD1306.h>

#define SSD1306_I2C_ADDRESS   0x3C

void Get_rtc(void *pvParameters);
void Get_data(void *pvParameters);
void Push_data(void *pvParameters);
void Print_data(int Co2, double templa, double humid);

Adafruit_SSD1306 display(-1);

void setup(){
    xTaskCreatePinnedToCore(Get_rtc,"Get_rtc", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(Get_data, "Get_data", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(Push_data, "Push_data", 4096, NULL, 6, NULL, 1);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
}

void loop(){

}

void Get_rtc(void *pvParameters)
void Get_data(void *pvParameters)
void Push_data(void *pvParameters)
void Print_data(int Co2, double templa, double humid){
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("CO2 : %d ppm",Co2);
    display.println("TMP : %lf C",templa);
    display.println("HMD : %lf %",humid);
    display.println("DCF : %lf",0.81*templa+0.01*humid*(0.99*templa-14.3)+46.3);
    if(Co2>2000){
        display.setCursor(20, 0);
        display.println("Ventilate now!");
    }
    display.display();
}