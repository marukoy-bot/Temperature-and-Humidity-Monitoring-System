//Arduino UNO
//SCL   A5
//SDA   A4
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT11.h>
#include <config.h>

#define trig 12
#define echo 11
#define RX 3
#define TX 4
#define relay 5
#define off false
#define on true
#define o_trig_temp 30    //30°C
#define o_trig_hmdty 60   //60%
#define water_lvl_trig 100 
#define scroll_delay 500

LiquidCrystal_I2C lcd(0x27, 16, 2);

DHT11 o_dht11(2);
DHT11 i_dht11(6);

SoftwareSerial gsm(RX, TX);

void InitializeGSM();
void CheckSMS();    
void UpdateSensors();
void CheckSensorValues();
void DisplayTemperatureAndHumidity(int setting);
void DisplayWaterLevel();
void ToggleSprinkler(bool state);
void UpdateGSM();
void DeleteSMS();
void CheckSMS();
void SendSMS(String number, String message);
float GetDistance();

void setup() {
    Serial.begin(115200);
    Serial.println("Initializing...");
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing GSM");

    InitializeGSM();

    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT_PULLUP);
    digitalWrite(echo, HIGH);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, HIGH);

    lcd.clear();
}

int display_mode = 0; //default
int i_temp = 0, i_hmdty = 0;
int o_temp = 0, o_hmdty = 0;
unsigned long sprinkler_timer = 0;
bool is_sprinkler_active = false;
const unsigned long sprinkler_timer_dur = 5000;
int i_res, o_res;
long duration_us;
int distance_us;
unsigned long current_time = 0, last_time = 0, mode_timer_duration = 5000;
float water_lvl_percent = 0;
bool isSMSCooldown = false;
const unsigned long smsCooldown = 300000; // 5 min in ms
bool SYSTEM_PAUSE = false;
bool manualOverride = false;
unsigned long sensor_last_time = 0;

void loop() {
    current_time = millis();

    if (current_time - sensor_last_time > 1000) {
        sensor_last_time = current_time;
        UpdateSensors();
        CheckSensorValues();
    }

    CheckSMS();    

    if (!SYSTEM_PAUSE)
    {
        if (current_time - last_time > mode_timer_duration) //cycle though display modes
        {
            lcd.clear();
            display_mode++;
            if (display_mode > 2) display_mode = 0;
            last_time = current_time;
        }

        switch(display_mode)
        {
        case 0:
            DisplayTemperatureAndHumidity(0);
        break;
        case 1:
            DisplayTemperatureAndHumidity(1);
        break;
        case 2:        
            DisplayWaterLevel();
        break;
        }   
    }    
}

unsigned long cooldownTimer = 0, cooldownTimerDuration = 300000;
void CheckSensorValues()
{
    if (!manualOverride) {
        if (o_temp >= 30 && o_hmdty <= 70) {
            ToggleSprinkler(on);            
            if (!isSMSCooldown)
            {
                SYSTEM_PAUSE = true;
                isSMSCooldown = true;
                cooldownTimer = millis();
                ToggleSprinkler(on);

                String msg =
                    "[VAPOR SYSTEM ALERT]\n\nRoof: "   + String(o_temp)   + " C | " + String(o_hmdty) + "% RH\n" +
                    "Indoor: " + String(i_temp)   + " C | " + String(i_hmdty) + "% RH\n" +
                    "Water Level: "  + String((int)water_lvl_percent) + "%\n" +
                    "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

                SendSMS(num, msg);
                Serial.println("Message Sent");
                SYSTEM_PAUSE = false;
            }
        }
        else if (o_temp <= 25 || o_hmdty >= 75) {
            ToggleSprinkler(off);
        }
    }

    if (water_lvl_percent <= 20)
    {
        if (!isSMSCooldown)
        {
            SYSTEM_PAUSE = true;
            isSMSCooldown = true;
            cooldownTimer = millis();

            String msg = "[WATER TANK STATUS]\n\nWater level is LOW (" + String((int)water_lvl_percent) + "%). Please refill soon.";
            SendSMS(num, msg);
            SYSTEM_PAUSE = false;
        }
    }

    if (isSMSCooldown)
    {
        unsigned long elapsed = millis() - cooldownTimer;
        if (elapsed >= cooldownTimerDuration)
        {
            isSMSCooldown = false;
        }
        else
        {
            unsigned long timeLeft = (cooldownTimerDuration - elapsed) / 1000;
            Serial.println("SMS Cooldown: " + String(timeLeft) + "s");
        }
    }    
}

void UpdateSensors()
{
    i_res = i_dht11.readTemperatureHumidity(i_temp, i_hmdty);
    o_res = o_dht11.readTemperatureHumidity(o_temp, o_hmdty);

    float dist = GetDistance();

    // Convert distance → percent
    const float empty_dist = 45.0; // cm
    const float full_dist  = 20.0; // cm
    water_lvl_percent = ((empty_dist - dist) / (empty_dist - full_dist)) * 100.0;

    if (water_lvl_percent > 100) water_lvl_percent = 100;
    if (water_lvl_percent < 0)   water_lvl_percent = 0;

    // Debug print
    Serial.print("Indoor: " + String(i_temp) + " °C, " + String(i_hmdty) + "% RH | Roof: " + 
                 String(o_temp) + " °C, " + String(o_hmdty) + "% RH | "); 
    Serial.println("Water level: " + String(water_lvl_percent) + "% (raw: " + dist + " cm)");
}

String toUCS2(String text) {
    String out = "";
    for (unsigned int i = 0; i < text.length(); i++) {
        char c = text[i];
        unsigned int code;

        if ((unsigned char)c == 0xB0) {
            code = 0x00B0;  // degree symbol
        } else {
            code = (unsigned char)c;  // normal ASCII
        }

        char buf[5];
        sprintf(buf, "%04X", code);
        out += buf;
    }
    return out;
}

String decodeUCS2(String hex) {
    String result = "";
    for (unsigned int i = 0; i < hex.length(); i += 4) {
        String part = hex.substring(i, i + 4);
        char c = (char) strtol(part.c_str(), NULL, 16);
        result += c;
    }
    return result;
}

void InitializeGSM()
{    
    gsm.begin(9600);

    gsm.println("AT");
    delay(500);
    UpdateGSM();

    gsm.println("AT+CSCS=\"GSM\"");
    delay(500);
    UpdateGSM(); 

    gsm.println("AT+CNMI=1,2,0,0,0");
    delay(500);
    UpdateGSM(); 

    gsm.println("AT+CMGF=1");
    delay(500);
    UpdateGSM(); 

    DeleteSMS();
    Serial.println("Initialized");
}


void DisplayTemperatureAndHumidity(int setting) // 1 = indoor; 0 = roof
{    
    if (i_res == 0 || o_res == 0) 
    {       

        //print on LCD
        lcd.setCursor(0, 0);
        lcd.print(setting ? "Indoor: " : "Roof: ");
        
        lcd.setCursor(0, 1);
        lcd.print(String(setting ? i_temp : o_temp) + (char)223 + "C | " + String(setting ? i_hmdty : o_hmdty) + "% RH");
    } 
    else 
    {
        //print error message based on the error code.
        lcd.print(DHT11::getErrorString(i_res));
    }    
}

void DisplayWaterLevel()
{
    lcd.setCursor(0, 0);
    lcd.print("Water Level:   "); // add spaces to clear old chars

    lcd.setCursor(0, 1);
    lcd.print((int)water_lvl_percent);
    lcd.print("%    "); // spaces to overwrite old digits

    //Serial.println("Water Level: " + (String)water_lvl + "%"); 
}

float GetDistance()
{
    digitalWrite(trig, LOW);
    delayMicroseconds(2);

    digitalWrite(trig, HIGH);
    delayMicroseconds(15);

    digitalWrite(trig, LOW);

    float dur = pulseIn(echo, HIGH, 26000);                                                       

    return dur / 58;
}

void SendSMS(String number, String message)
{
    SYSTEM_PAUSE = true;

    gsm.println("AT+CMGF=1");
    UpdateGSM();
    delay(250);

    gsm.println("AT+CMGS=\"" + number + "\"");
    UpdateGSM();
    delay(1000);    

    gsm.print(message);
    gsm.print(char(26));
    UpdateGSM();
    delay(1000);

    SYSTEM_PAUSE = false;
}

void UpdateGSM()
{
    //while (Serial.available()) gsm.write(Serial.read());
    while(gsm.available()) Serial.write((char)gsm.read());
}

String GetDateTime()
{
    String dateTime = "";
    gsm.println("AT+CCLK?");
    delay(500);

    while(gsm.available()) dateTime += (char)gsm.read();
    dateTime.trim();

    return dateTime;
}

String FormatTime(String cclk)
{
    if (cclk.length() < 17) return "";

    int yy   = cclk.substring(0, 2).toInt();
    int MM   = cclk.substring(3, 5).toInt();
    int dd   = cclk.substring(6, 8).toInt();
    int hh   = cclk.substring(9, 11).toInt();
    int min  = cclk.substring(12, 14).toInt();

    int yyyy = 2000 + yy;

    // 12-hour conversion
    String ampm = (hh >= 12) ? "PM" : "AM";
    int hour12 = (hh % 12 == 0) ? 12 : hh % 12;

    char buf[30];
    sprintf(buf, "%02d/%02d/%04d, %d:%02d %s", 
            MM, dd, yyyy, hour12, min, ampm.c_str());

    return String(buf);
}

void ToggleSprinkler(bool state)
{
    is_sprinkler_active = state;
    digitalWrite(relay, !is_sprinkler_active);
}

void DeleteSMS(){
    String Comm;
    Comm = "AT+CMGDA=";
    Comm += "\x22";
    Comm += "DEL ALL";
    Comm += "\x22";
    gsm.println(Comm);

    String CommRep = gsm.readString();
    Serial.print("Reply: "), Serial.println(CommRep);
}


void CheckSMS()
{
    static String smsBuffer = "";
    //static bool waitingForMsg = false;

    if (gsm.available())
    {
        String smsBuffer = gsm.readString();        
        //smsBuffer = decodeUCS2(smsBuffer);
        smsBuffer.toUpperCase();
        Serial.print(smsBuffer);
        //delay(2000); 

        if (smsBuffer.indexOf("VAPOR ON") > -1)
        {
            manualOverride = true;  // force ON
            ToggleSprinkler(true);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: ON");
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR OFF") > -1 )
        {
            manualOverride = false; // release override
            ToggleSprinkler(false);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("SPRINKLER: OFF");
            delay(3000);
            lcd.clear();
        }
        else if (smsBuffer.indexOf("VAPOR STATUS") > -1)
        {
            SYSTEM_PAUSE = true;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Sending Status ");            

            String msg =
                "[VAPOR SYSTEM ALERT]\n\nRoof: "   + String(o_temp)   + " C | " + String(o_hmdty) + "% RH\n" +
                "Indoor: " + String(i_temp)   + " C | " + String(i_hmdty) + "% RH\n" +
                "Water Level: "  + String((int)water_lvl_percent) + "%\n" +
                "Status: SPRINKLER "   + String(is_sprinkler_active ? "ON" : "OFF");

            SendSMS(num, msg);
            lcd.clear();
            SYSTEM_PAUSE = false;
        }            
    }
}
