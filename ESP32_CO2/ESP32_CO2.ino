#include <Arduino.h>
#include <Ambient.h>
#include <Wire.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <SSD1306Wire.h>
#include <DHT.h>
#include <SoftwareSerial.h>
#include <esp_wpa2.h>
#include "./user.h"

#define SSD1306_I2C_ADDRESS   0x3c
#define JST     3600* 9
#define DHTTYPE DHT22
#define DHT22_PIN 15

typedef struct 
{
    float co2;
    float templa;
    float humid;
}sen_data_t;


const char* ssid = SSID;
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
DHT dht{DHT22_PIN, DHTTYPE};
SoftwareSerial MHZ19Serial(33,25);
SSD1306Wire display(0x3c, 21, 22);

void setup(){
    Serial.begin(115200);
    pinMode(2,OUTPUT);
    display.init();
    Con_wifi();
    Get_rtc();
    dht.begin();
    MHZ19Serial.begin(9600);
    ambient.begin(channelId, writeKey, &client);
    Sensor_data = xQueueCreate(3, sizeof(sen_data_t));
    if(Sensor_data != NULL){
        xTaskCreatePinnedToCore(Get_data, "Get_data", 8192, NULL, 2, (TaskHandle_t *)NULL,1);
        xTaskCreatePinnedToCore(Push_data, "Push_data", 8192, NULL, 1, (TaskHandle_t *)NULL,1);
        Serial.println("Task created");
    }else{
        Serial.println("Rebooting...");
        ESP.restart();
    }
    Serial.println("Setup end");
}

void loop(){
    Serial.println("loop");
    if(WiFi.status() != WL_CONNECTED) ESP.restart();
    Get_rtc();
    delay(60*1000*10);
}

void Con_wifi(){
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD)); 
    esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
    esp_wifi_sta_wpa2_ent_enable(&config); 
    WiFi.begin(ssid);
    Serial.println("WiFi.begin");
    delay(1000);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    if (client.connect(host, 80)) {
        String url = "/rele/rele1.txt";
        client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "User-Agent: ESP32\r\n" + "Connection: close\r\n\r\n");

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
            Serial.println(".");
        }
        String line = client.readStringUntil('\n');
        Serial.println(line);
    }else{
        Serial.println("Connection unsucessful");
    }  
}

void Get_rtc(){
    Serial.println("Get_rtc");
    configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    time_t t = time(NULL);
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        Serial.println("Failed to obtain time");
    }else{
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }
}

void Get_data(void *pvParameters){
    static const byte b[] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    float templa,humid,co2;
    float ppm;
    unsigned char response[9];
    BaseType_t xStatus;
    sen_data_t SendValues;
    int cnt=0;
    while(1){
        MHZ19Serial.write(b, 9);
        delay(1000);
        MHZ19Serial.readBytes(response, 9);
        if (response[0] != 0xFF){
            exit(0);
        }
        if (response[1] != 0x86){
            exit(0);
        }
        unsigned int responseHigh = (unsigned int) response[2];
        unsigned int responseLow = (unsigned int) response[3];
        int ppm = (256 * responseHigh) + responseLow;
        templa = 5.0/9.0*(dht.readTemperature(true)-32.0);
        humid = dht.readHumidity();

        //Print_data(ppm,templa,humid);
        SendValues.co2 = ppm;
        SendValues.templa = templa;
        SendValues.humid = humid;
        xStatus = xQueueSend(Sensor_data, &SendValues, 0);
        if(xStatus != pdPASS){
            display.clear();
            display.drawString(0,0,"QUEUE SEND ERROR");
            display.display();
            if(cnt>10){
                display.clear();
                display.drawString(0,0,"QUEUE SEND ERROR Rebooting");
                display.display();
                delay(1000);
                ESP.restart();
            }
            cnt++;
        }
    }
    delay(1000);
}

void Push_data(void *pvParameters){
    BaseType_t xStatus;
    sen_data_t ReceivedValues;
    const TickType_t xTicksToWait = 500U; // [ms]
    time_t t = time(NULL),oldtime = t;
    int cnt=0;
    sen_data_t sum={0,0,0};
    while(1){
        t = time(NULL);
        struct tm timeinfo;
        xStatus = xQueueReceive(Sensor_data, &ReceivedValues, xTicksToWait);
        Print_data(ReceivedValues.co2,ReceivedValues.templa,ReceivedValues.humid);
        Serial.println("t="+static_cast<String>(t)+",old="+static_cast<String>(oldtime));
        if(xStatus == pdPASS){
            sum.co2+=ReceivedValues.co2;
            sum.templa+=ReceivedValues.templa;
            sum.humid+=ReceivedValues.humid;
            if(t-oldtime >= 60){
                ambient.set(1,sum.co2/(float)cnt);
                ambient.set(2,sum.templa/(float)cnt);
                ambient.set(3,sum.humid/(float)cnt);
                ambient.send();
                oldtime = t;
                Serial.println("pushed");
                cnt=0;
                sum={0,0,0};
            }
        }else{
            if(uxQueueMessagesWaiting(Sensor_data) != 0){
                display.clear();
                display.drawString(0,0,"QUEUE RES ERROR");
                display.display();
            }            
        }
        cnt++;
        delay(1000);
    }
}

void Print_data(float Co2, float templa, float humid){
    static bool sts = false;
    if (sts==false){
        digitalWrite(2,HIGH);
        sts = true;
    }else{
        digitalWrite(2,LOW);
        sts = false;
    }
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