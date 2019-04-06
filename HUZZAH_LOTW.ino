#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "FS.h"

extern "C" {
#include<user_interface.h>
}

bool hadFirstUpdate = false;

// Credentials
char ssid[] = "SSID";
char pass[] = "PASSWORD";
char lotw_user[] = "USER";
char lotw_pass[] = "PASSWORD";

// Fingerprint for LOTW
const uint8_t fingerprint[20] = {0x8C, 0xA1, 0xC9, 0x5C, 0x09, 0x75, 0x13, 0x2C, 0x6E, 0x98, 0x93, 0xE2, 0x2A, 0x9E, 0xC9, 0x6D, 0xFC, 0xD6, 0xEE, 0x58};

ESP8266WiFiMulti WiFiMulti;

Adafruit_SSD1306 display = Adafruit_SSD1306();

// Updates
unsigned long previousMillis = 0;
const int updateIntervalMinutes = 1;

void setup()
{
    Serial.begin(115200);
    
    // Initialize networking
    WiFi.mode(WIFI_STA);
    WiFiMulti.addAP(ssid, pass);
    
    // Adding some delay seems to help with display getting initialized
    delay(1000);
    
    // Initialize and clear display
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.display();
    
    // Initialize SPIFFS, need a displayed error here
    bool spiffsEnabled = SPIFFS.begin();
}

void loop() {
    // wait for WiFi connection
    if ((WiFiMulti.run() == WL_CONNECTED))
    {
        
        unsigned long currentMillis = millis();
        
        if (currentMillis - previousMillis >= (updateIntervalMinutes * (60 *1000)) || !hadFirstUpdate)
        {
            // save the last time we updated
            previousMillis = currentMillis;
            updateQSLCount();
        }
    }
    else
    {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0,0);
        display.print("Connecting to WiFi");
        display.display();
        delay(500);
    }
}


// LOTW update
void updateQSLCount()
{
    hadFirstUpdate = true;
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    
    client->setFingerprint(fingerprint);
    
    HTTPClient https;
    
    String URL = "https://lotw.arrl.org/lotwuser/lotwreport.adi?login=";
    URL += String(lotw_user);
    URL += "&password=";
    URL += String(lotw_pass);
    URL += "&qso_query=1&qso_qsl=yes";
    
    String last = lastQSL();
    if(last.length() > 6)
    {
        URL += "&qso_qslsince=";
        URL += getSubstring(last, ' ', 0);
        URL += "+";
        String timeString = getSubstring(last, ' ', 1);
        timeString = timeString.substring(0, 8);
        timeString.replace(":", "%3A");
        URL += timeString;
    }
    
    if (https.begin(*client, URL))
    {  // HTTPS
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0,0);
        display.print("Connecting to LOTW");
        display.display();
        
        // start connection and send HTTP header
        int httpCode = https.GET();
        
        // httpCode will be negative on error
        if (httpCode > 0)
        {
            // HTTP header has been send and Server response header has been handled
            Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
            
            // file found at server
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
            {
                String payload = https.getString();
                
                String qslCount = "";
                String lastQSLDate = "";
                
                for(int i = 0; i < 20; i++)
                {
                    String line = getSubstring(payload, '\n', i);
                    
                    if( line.startsWith("<APP_LoTW_LASTQSL"))
                    {
                        lastQSLDate = getSubstring(line, '>', 1);
                        setLastQSL(lastQSLDate);
                    }
                    
                    if( line.startsWith("<APP_LoTW_NUMREC"))
                    {
                        qslCount = getSubstring(line, '>', 1);
                    }
                }
                
                Serial.println(payload);
                
                display.clearDisplay();
                display.setTextSize(1);
                display.setCursor(0,0);
                display.println("Last QSL date:");
                display.println(lastQSLDate);
                display.setTextSize(2);
                
                display.print(qslCount);
                display.println(" New QSLs");
                display.display();
                
            }
        }
        else
        {
            Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }
        
        https.end();
    }
    else
    {
        Serial.printf("[HTTPS] Unable to connect\n");
    }
}

String getSubstring(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;
    
    for (int i = 0; i <= maxIndex && found <= index; i++)
    {
        if (data.charAt(i) == separator || i == maxIndex)
        {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// Retrieve last date time
String lastQSL()
{
    String dateTime = "";
    
    File f = SPIFFS.open("/f.txt", "r");
    
    if (!f)
    {
        Serial.println("File doesn't exist yet. Creating it");
        SPIFFS.format();
        Serial.println("Spiffs formatted");
        
        // open the file in write mode
        File f = SPIFFS.open("/f.txt", "w");
        if (!f) 
        {
            Serial.println("file creation failed");
        }
        // now write two lines in key/value style with  end-of-line characters
    } 
    else 
    {
        // we could open the file
        while(f.available()) 
        {
            //Lets read line by line from the file
            dateTime = f.readStringUntil('\n');
        }
    }
    f.close();
    
    return dateTime;
}

// Write last QSL date time
void setLastQSL(String dateTime)
{
    File f = SPIFFS.open("/f.txt", "w");
    
    if (!f) 
    {
        Serial.println("Failed to create file");
    }
    else
    {
        f.println(dateTime);
    }
    
    f.close();
}
