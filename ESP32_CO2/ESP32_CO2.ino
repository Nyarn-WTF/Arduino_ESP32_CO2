#include <Arduino.h>
#include <Ambient.h>
#include <Wire.h>
#include <time.h>
#include <WiFi.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define SSD1306_I2C_ADDRESS   0x3C
#define JST     3600* 9
#define DHTTYPE DHT22
#define DHT22_PIN 15

#define SSID "hogehoge"
#define PASS "hugahuga"
#define CID "piyopiyo"
#define RIGHTKEY "foobar"

struct Response {
  word header;
  byte vh;
  byte vl;
  byte temp;
  byte tails[4];
} __attribute__((packed));;

const char* ssid = SSID;
const char* password = PASS;

unsigned int channelId = CID;
const char* writeKey = RIGHTKEY;

void Get_rtc();
void Get_data(void *pvParameters);
void Push_data(void *pvParameters);
void Print_data(float Co2, float templa, float humid);

Adafruit_SSD1306 display(-1);
WiFiClient client;
Ambient ambient;
QueueHandle_t Sensor_data;
DHT dht(DHTPIN, DHTTYPE);
SoftwareSerial MHZ19Serial(33,25);

void setup(){
    xTaskCreatePinnedToCore(Get_data, "Get_data", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(Push_data, "Push_data", 4096, NULL, 6, NULL, 1);
    display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS);
    ambient.begin(channelId, writeKey, &client);
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    pinMode(DHT22_PIN, INPUT);
    MHZ19Serial.begin(9600);
}

void loop(){
    Get_rtc();
    delay(1000*60*30);
}

void Get_rtc(){
    configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
}

void Get_data(void *pvParameters){
    const byte b[] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}
    float templa,humid,co2;
    float ppm;
    struct Response response;
    BaseType_t xStatus;
    float SendValues[] = {0,0,0};
    while(1){
        MHZ19Serial.write(b, sizeof Request);
        MHZ19Serial.readBytes((char *)&response, 9);
        if (response.header != 0x86ff)continue;
        ppm = static_cast<float>((response.vh << 8) + response.vl);
        
        templa = 5.0/9.0*(dht.readTemperature(true)-32.0);
        humid = dht.readHumidity();
        
        Print_data(ppm,templa,humid);

        xStatus = xQueueSend(xQueue, &SendValues, 0);
        if(xStatus != pdPASS){
            display.clearDisplay();
            display.setCursor(0,0);
            display.println("QUEUE SEND ERROR");
            display.display();
        }
        delay(500);
    }
}

void Push_data(void *pvParameters){
    BaseType_t xStatus;
    float ReceivedValues[] = {0,0,0};
    const TickType_t xTicksToWait = 500U; // [ms]
    time_t t = time(NULL),oldtime = t;

    while(1){
        t = time(NULL);
        xStatus = xQueueReceive(Sensor_data, &ReceivedValues, xTicksToWait);
        if(xStatus == pdPASS){
            if(t-oldtime >= 60*1000){
                ambient.set(1,(int)ReceivedValues[0]);
                ambient.set(2,ReceivedValues[1]);
                ambient.set(3,ReceivedValues[2]);
                ambient.send();
                oldtime = t;
            }
        }else{
            if(uxQueueMessagesWaiting(Sensor_data) != 0){
                display.clearDisplay();
                display.setCursor(0,0);
                display.println("QUEUE RES ERROR");
                display.display();
            }            
        }
        delay(100);
    }
}

void Print_data(float Co2, float templa, float humid){
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("CO2 : %d ppm",Co2);
    display.println("TMP : %lf C",templa);
    display.println("HMD : %lf %",humid);
    display.println("DCF : %lf",0.81*templa+0.01*humid*(0.99*templa-14.3)+46.3);
    if(Co2 > 2000){
        display.setCursor(20, 0);
        display.println("Ventilate now!");
    }
    display.display();
}