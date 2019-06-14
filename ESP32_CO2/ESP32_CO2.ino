#include <Arduino.h>
#include <Ambient.h>
#include <Wire.h>
#include <time.h>
#include <WiFi.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <SoftwareSerial.h>
#include <esp_wpa2.h>

#define SSD1306_I2C_ADDRESS   0x3C
#define JST     3600* 9
#define DHTTYPE DHT22
#define DHT22_PIN 15

#define EAP_IDENTITY "匿名IDとか呼ばれるもの" //if connecting from another corporation, use identity@organisation.domain in Eduroam
#define SSID "HUGAAAAA
#define EAP_USERNAME "ユーザー名"
#define EAP_PASSWORD "パスワード" //your Eduroam password
#define HOST "接続確認のホスト名"//external server domain for HTTP connection after authentification Example"arduino.php5.sk"
#define CID 334
#define RIGHTKEY "aslsdfkj"

struct Response {
  word header;
  byte vh;
  byte vl;
  byte temp;
  byte tails[4];
} __attribute__((packed));;

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

Adafruit_SSD1306 display(-1);
WiFiClient client;
Ambient ambient;
QueueHandle_t Sensor_data;
DHT dht(DHT22_PIN, DHTTYPE);
SoftwareSerial MHZ19Serial(33,25);

void setup(){
    xTaskCreatePinnedToCore(Get_data, "Get_data", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(Push_data, "Push_data", 4096, NULL, 6, NULL, 1);
    display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS);
    ambient.begin(channelId, writeKey, &client);
    Con_wifi();
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    pinMode(DHT22_PIN, INPUT);
    MHZ19Serial.begin(9600);
}

void loop(){
    Get_rtc();
    delay(1000*60*30);
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
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        counter++;
        if(counter>=60){ //after 30 seconds timeout - reset board
            ESP.restart();
        }
    }
}

void Get_rtc(){
    configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
}

void Get_data(void *pvParameters){
    const byte b[] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
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
    String temp;
    display.clearDisplay();
    display.setCursor(0, 10);
    temp = "CO2 :" + static_cast<String>(Co2) +"ppm";
    display.println(temp);
    temp = "TMP :" + static_cast<String>(templa) +"C";
    display.println(temp);
    temp = temp = "HMD :" + static_cast<String>(humid) +"%";
    display.println(temp);
    temp = temp = "DCF :" + static_cast<String>(0.81*templa+0.01*humid*(0.99*templa-14.3)+46.3);
    display.println(temp);
    if(Co2 > 2000){
        display.setCursor(20, 0);
        display.println("Ventilate now!");
    }
    display.display();
}
