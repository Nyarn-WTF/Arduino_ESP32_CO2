#include <Arduino.h>
#include <Ambient.h>
#include <Wire.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include <SSD1306Wire.h>
#include <DHT.h>
#include <SoftwareSerial.h>
#include <esp_wpa2.h>

#define SSD1306_I2C_ADDRESS   0x3c
#define JST     3600* 9
#define DHTTYPE DHT22
#define DHT22_PIN 15

#define EAP_IDENTITY "" //if connecting from another corporation, use identity@organisation.domain in Eduroam
#define EAP_USERNAME "s16054"
#define EAP_PASSWORD "S16054@tokyo.kosen" //your Eduroam password
#define HOST "arduino.php5.sk"//external server domain for HTTP connection after authentification Example"arduino.php5.sk"
#define CID 10898
#define RIGHTKEY "0eeccfa6cd9bfdfb"

struct Response {
  word header;
  byte vh;
  byte vl;
  byte temp;
  byte tails[4];
} __attribute__((packed));;

const char* ssid = "Hazamaru AP";
int counter = 0;
unsigned int channelId = CID;
const char* writeKey = RIGHTKEY;
const char* host = HOST; 

void Con_wifi();
void Get_rtc();
void Get_data(void *pvParameters);
void Push_data(void *pvParameters);
void Print_data(float Co2, float templa, float humid);

WiFiClient client;
Ambient ambient;
QueueHandle_t Sensor_data;
DHT dht(DHT22_PIN, DHTTYPE);
SoftwareSerial MHZ19Serial(33,25);
SSD1306Wire display(0x3c, 21, 22);

void setup(){
    pinMode(2,OUTPUT);
    display.init();
    Con_wifi();
    Get_rtc();
    pinMode(DHT22_PIN, INPUT);
    MHZ19Serial.begin(9600);
    ambient.begin(channelId, writeKey, &client);
    xTaskCreatePinnedToCore(Get_data, "Get_data", 512, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(Push_data, "Push_data", 512, NULL, 1, NULL, 1);

}

void loop(){

}

void Con_wifi(){
    //WiFi.disconnectはtrueを入れないと切ってくれない
    WiFi.disconnect(true);  //disconnect form wifi to set new wifi connection
    //WiFi.modeは必須
    WiFi.mode(WIFI_STA); //init wifi mode
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY)); 
    //provide identity
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME)); 
    //provide username --> identity and username is same
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD)); 
    //provide password
    esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT(); //set config settings to default
    esp_wifi_sta_wpa2_ent_enable(&config); //set config settings to enable function
    WiFi.begin(ssid); //connect to wifi
    delay(1000);
    int sts=0;
    while (WiFi.status() != WL_CONNECTED) {
        if(sts==0){
            digitalWrite(2,HIGH);
            sts=1;
        }else {
            digitalWrite(2,LOW);
            sts=0;
        }
        display.clear();
        display.drawString(0, 0, static_cast<String>(counter));
        display.display();
        delay(500);
        counter++;
        if(counter>=120){ //after 30 seconds timeout - reset board
            ESP.restart();
        }
    }
    display.clear();
    display.drawString(0,0,WiFi.SSID());
    display.display();
}

void Get_rtc(){
    configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
}

void Get_data(void *pvParameters){
    static const byte b[] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    float templa,humid,co2;
    float ppm;
    struct Response response;
    BaseType_t xStatus;
    float SendValues[] = {0,0,0};
    while(1){
        MHZ19Serial.write(b, sizeof b);
        MHZ19Serial.readBytes((char *)&response, 9);
        if (response.header != 0x86ff)continue;
        ppm = static_cast<float>((response.vh << 8) + response.vl);
        
        templa = 5.0/9.0*(dht.readTemperature(true)-32.0);
        humid = dht.readHumidity();
        
        Print_data(ppm,templa,humid);

        xStatus = xQueueSend(Sensor_data, &SendValues, 0);
        if(xStatus != pdPASS){
            display.clear();
            display.drawString(0,0,"QUEUE SEND ERROR");
            display.display();
        }
        delay(2000);
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
                display.clear();
                display.drawString(0,0,"QUEUE RES ERROR");
                display.display();
            }            
        }
        delay(1000);
    }
}

void Print_data(float Co2, float templa, float humid){
    String temp;
    display.clear();
    temp = "CO2 :" + static_cast<String>(Co2) +"ppm";
    display.drawString(0,20,temp);
    temp = "TMP :" + static_cast<String>(templa) +"C";
    display.drawString(0,30,temp);
    temp = temp = "HMD :" + static_cast<String>(humid) +"%";
    display.drawString(0,40,temp);
    temp = temp = "DCF :" + static_cast<String>(0.81*templa+0.01*humid*(0.99*templa-14.3)+46.3);
    display.drawString(0,50,temp);
    if(Co2 > 2000){
        display.drawString(20,10,"Ventilate now!");
    }
    display.display();
}
