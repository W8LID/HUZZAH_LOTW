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
char pass[] = "PASS";
char lotw_user[] = "USER";
char lotw_pass[] = "PASS";

// Fingerprint for LOTW this will expire eventually
const uint8_t fingerprint[20] = {0x8C, 0xA1, 0xC9, 0x5C, 0x09, 0x75, 0x13, 0x2C, 0x6E, 0x98, 0x93, 0xE2, 0x2A, 0x9E, 0xC9, 0x6D, 0xFC, 0xD6, 0xEE, 0x58};

ESP8266WiFiMulti WiFiMulti;

Adafruit_SSD1306 display = Adafruit_SSD1306();

// Updates
unsigned long previousMillis = 0;
unsigned long previousMillisScreen = 0;

const int updateIntervalSecScreen = 5;
const int updateIntervalMinutes = 15;

int newQSLCount = 0;
int newQSLSinceReset = 0;
int screenIndex = 0;
String lastQSLDate = "";
String lastQSLTime = "";

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
    
    // Initialize SPIFFS, need a displayed error here if not available
    bool spiffsEnabled = SPIFFS.begin();
}

void loop() {
    // wait for WiFi connection
    if ((WiFiMulti.run() == WL_CONNECTED))
    {
        
        // Non blocking updates        
        if (millis() - previousMillis >= (updateIntervalMinutes * (60 *1000)) || !hadFirstUpdate)
        {
            // save the last time we updated
            previousMillis = millis();
            updateQSLCount();
        }
                // Non blocking updates        
        if (millis() - previousMillisScreen >= (updateIntervalSecScreen * 1000))
        {
            // save the last time we updated
            if(screenIndex >= 3)
            {
              screenIndex = 0;
            }
            else
            {
              screenIndex++;
            }
            showScreen(screenIndex);
            // we reset the timer in showScreen() so caller gets their full time
            //previousMillisScreen = millis();
        }
    }
    else
    {
        // Waiting on WiFi
        pauseForWiFi();
    }
}


// LOTW update
void updateQSLCount()
{
    hadFirstUpdate = true;
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    
    client->setFingerprint(fingerprint);
    
    HTTPClient https;
    
    if (https.begin(*client, urlForQuery()))
    {  // HTTPS
        // Show whats happening
        displayLotwConnection();
        Serial.println("Fetching update...");

        // start connection and send HTTP header
        int httpCode = https.GET();
        
        // httpCode will be negative on error
        if (httpCode > 0)
        {
            // HTTP header has been send and Server response header has been handled
            //Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
            
            // file found at server
            if (httpCode == HTTP_CODE_OK)
            {
                Serial.println("Update received.");

                String payload = https.getString();
                
                String qslCount = "";
                String lastQSLDateTime = "";
                
                for(int i = 0; i < 20; i++)
                {
                    String line = getDelimitedSubstring(payload, '\n', i);
                    
                    if( line.startsWith("<APP_LoTW_LASTQSL"))
                    {
                        lastQSLDateTime = getDelimitedSubstring(line, '>', 1);
                        setLastQSL(lastQSLDateTime.substring(0, 19)); //Truncate extra whitespace
                    }
                    
                    if( line.startsWith("<APP_LoTW_NUMREC"))
                    {
                        qslCount = getDelimitedSubstring(line, '>', 1);
                    }
                }
                
                newQSLCount = qslCount.toInt();
                // Subtract the one QSL we always get for the last date time
                newQSLCount = newQSLCount - 1;

                if(newQSLCount > 0)
                {
                    newQSLSinceReset += newQSLCount;
                    sendNotification(newQSLCount);
                }
                screenIndex = 2;
                showScreen(screenIndex);
            }
            else
            {
                displayHttpError(String(httpCode));
            }
        }
        else
        {
            displayHttpError(https.errorToString(httpCode));
            // Errored out, this should go o the display instead
            Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }
        
        https.end();
    }
    else
    {
        Serial.printf("[HTTPS] Unable to connect\n");
    }
}

// 
String getDelimitedSubstring(String data, char separator, int index)
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
    
    File f = SPIFFS.open("/dt.txt", "r");
    
    if (!f)
    {
        Serial.println("File doesn't exist yet. Creating it");
        SPIFFS.format();
        Serial.println("Spiffs formatted");
        
        // open the file in write mode
        File f = SPIFFS.open("/dt.txt", "w");
        if (!f) 
        {
            Serial.println("file creation failed");
        }
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
    // Remove returns
    dateTime.replace("\r", "");

    f.close();
    
    return dateTime;
}


// Write last QSL date time
void setLastQSL(String dateTime)
{
    File f = SPIFFS.open("/dt.txt", "w");
    
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

// Send notification on new QSLs
void sendNotification(int numNewQSL)
{
  //Send an email/SMS when we have something new
  Serial.println("Sending notification!");
  Serial.print("Total new since this update: ");
  Serial.println(numNewQSL);
  Serial.print("Total new since last reset: ");
  Serial.println(newQSLSinceReset);
}

// Diplay errors
void displayHttpError(String errorCode)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("[HTTPS] Failed");
    display.println("Error was:");
    display.setTextSize(2);
    display.println(errorCode);
    display.display();
}

void displayLotwConnection()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Contacting LOTW");
    display.display();
}

// Display waiting on WiFi
void pauseForWiFi()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.print("Connecting to WiFi");
    display.display();
    delay(500);
}

// URL Builder
String urlForQuery()
{
    String URL = "https://lotw.arrl.org/lotwuser/lotwreport.adi?login=";
    URL += String(lotw_user);
    URL += "&password=";
    URL += String(lotw_pass);
    URL += "&qso_query=1&qso_qsl=yes";

    // If we have a previous update saved then use it
    String last = lastQSL();
    if(last.length() == 19) // Check for valid DT length
    {
        lastQSLDate = getDelimitedSubstring(last, ' ', 0);
        lastQSLTime = getDelimitedSubstring(last, ' ', 1);

        URL += "&qso_qslsince=";
        URL += lastQSLDate;
        URL += "+";
        String timeStringForURL = lastQSLTime;
        timeStringForURL.replace(":", "%3A");
        URL += timeStringForURL;
    }

    return URL;
}

void resetNewAccumulator()
{
    newQSLSinceReset = 0;
}

void showScreen(int index)
{
    switch(index)
    {
      case 0:
      {
          // Display details
          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(0,0);
          display.println("Latest QSL date:");
          display.println();
          display.setTextSize(2);
          display.println(lastQSLDate);
          display.display();
      }
      break;

      case 1:
      {
          // Display details
          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(0,0);
          display.println("Latest QSL time:");
          display.println();
          display.setTextSize(2);
          display.println(lastQSLTime + "z");
          display.display();
      }
      break;

      case 2:
      {
          // Display details
          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(0,0);
          display.println("New QSLs this update:");
          display.println();
          display.setTextSize(2);
          display.println(newQSLCount);
          display.display();
      }
      break;

     case 3:
      {
          // Display details
          display.clearDisplay();
          display.setTextSize(1);
          display.setCursor(0,0);
          display.println("New QSLs since reset:");
          display.println();
          display.setTextSize(2);
          display.println(newQSLSinceReset);
          display.display();
      }
      break;
    }
    previousMillisScreen = millis();
}
