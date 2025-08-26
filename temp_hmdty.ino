//Arduino UNO
//SCL   A5
//SDA   A4

#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT11.h>

#define trig 12
#define echo 11

#define RX 3
#define TX 4

#define relay 5

#define num "+639151635499"

#define o_trig_temp 30    //30°C
#define o_trig_hmdty 60   //60%
#define water_lvl_trig 100 

#define scroll_delay 10

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT11 o_dht11(2);
DHT11 i_dht11(6);
SoftwareSerial gsm(RX, TX);

void setup() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing GSM");

    InitializeGSM();

    Serial.begin(9600);

    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT);
    pinMode(relay, OUTPUT);
    digitalWrite(relay, HIGH);

    lcd.clear();
}

int display_mode = 0; //default

int i_temp = 0, i_hmdty = 0;
int o_temp = 0, o_hmdty = 0;

unsigned long pump_timer = 0;
bool is_pump_active = false;
//const unsigned long timerDuration = 180000;
const unsigned long pump_timer_dur = 10000;

int i_res, o_res;
long duration_us;
int distance_us;

unsigned long current_time = 0, last_time = 0, mode_timer_duration = 5000;
float water_lvl;

void loop() {
    current_time = millis();

    CheckSMS();
    
    i_res = i_dht11.readTemperatureHumidity(i_temp, i_hmdty);
    o_res = o_dht11.readTemperatureHumidity(o_temp, o_hmdty);
    water_lvl = map(GetDistance(), 20, 100, 100, 0); //assuming 100cm deep 

    //check outdoor temperature
    // if ( (o_temp >= o_trig_temp || o_hmdty >= o_trig_hmdty) && !is_pump_active )
    // {
    //     is_pump_active = true;
    //     pump_timer = current_time;
    //     digitalWrite(relay, LOW);   //turn on relay         
    //     //OnTempSMS(num);
    // }    

    //check water level

    //activate pump, timer
    // if (is_pump_active)
    // {
    //     displayMode = 2;
    //     DisplayTimer();
    // }
    // else
    // {
        
    // }

    if (current_time - last_time > mode_timer_duration) //cycle though display modes
    {
        lcd.clear();
        display_mode++;
        if (display_mode > 2) display_mode = 0;
        last_time = current_time;
    }

    //cycle between display modes
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
        case 3:
            DisplayTimer();
        break;
    }   
}

void DisplayTimer()
{
    // if (isPumpActive)
    // {
    //     unsigned long elapsed = globalCurrentTimer - pumpTimer;

    //     if (elapsed >= globalTimerDuration)
    //     {
    //         isPumpActive = false;
    //         digitalWrite(relay, HIGH);

    //         lcd.clear();
    //         displayMode = 0;
    //     }
    //     else
    //     {
    //         unsigned long remaining = (pumpTimerDuration - elapsed) / 1000; //seconds left
    //         lcd.clear();
    //         lcd.setCursor(0, 0);
    //         lcd.print("Pump active for");

    //         lcd.setCursor(0, 1);
    //         lcd.print(remaining);
    //         lcd.print("s");
    //     }
    // }
}

void InitializeGSM()
{    
    gsm.begin(9600);

    gsm.println("AT");
    updateGSM();
    delay(500);

    gsm.println("AT+CLTS=1");
    updateGSM();
    delay(500);
    
    gsm.println("AT&W");
    updateGSM();
    delay(500);

    gsm.println("AT+CMGF=1"); //text mode
    updateGSM();
    delay(500);

    gsm.println("AT+CNMI=1,2,0,0,0"); //push SMS
    updateGSM();
    delay(500);

    Serial.print("Initialized: ");
    Serial.println(formatTime(getTime()));
}

void DisplayTemperatureAndHumidity(int setting)
{
    switch (setting)
    {
        case 0:
            if (i_res == 0) 
            {
                //print on serial monitor
                Serial.print("In temp: ");
                Serial.print(i_temp);
                Serial.print(" °C\tIndoor Humidity: ");
                Serial.print(i_hmdty);
                Serial.println(" %");

                //print on LCD
                String i_temp_msg = "Room Temp: " + String(i_temp) + (char)223 + "C";
                lcd.setCursor(0, 0);
                lcd.print(i_temp_msg);

                String i_hmdty_msg = "Room Hmdty: " + String(i_hmdty) +  "%";
                lcd.setCursor(0, 1);
                lcd.print(i_hmdty_msg);
            } 
            else 
            {
                //print error message based on the error code.
                lcd.print(DHT11::getErrorString(i_res));
            }
        break;

        case 1:
            if (o_res == 0) 
            {
                //print on serial monitor
                Serial.print("Outdoor Temperature: ");
                Serial.print(o_temp);
                Serial.print(" °C\tOutdoor Humidity: ");
                Serial.print(o_hmdty);
                Serial.println(" %");

                //print on LCD
                String o_temp_msg = "Roof Temp: " + String(o_temp) + (char)223 + "C";
                lcd.setCursor(0, 0);
                lcd.print(o_temp_msg);

                String o_hmdty_msg = "Roof Hmdty: " + String(o_hmdty) + "%";
                lcd.setCursor(0, 1);
                lcd.print(o_hmdty_msg);
            } 
            else 
            {
                //print error message based on the error code.
                lcd.print(DHT11::getErrorString(o_res));
            }
        break;
    }
    
}

void DisplayWaterLevel()
{
    lcd.setCursor(0, 0);
    lcd.print(GetDistance());
    lcd.print(" cm");

    lcd.setCursor(0, 1);
    lcd.print("Water lvl: ");
    lcd.print((int)water_lvl);
    lcd.print("%");
}

float GetDistance()
{
    digitalWrite(trig, LOW);
    delayMicroseconds(2);

    digitalWrite(trig, HIGH);
    delayMicroseconds(20);

    digitalWrite(trig, LOW);

    duration_us = pulseIn(echo, HIGH);

    return duration_us * 0.034 / 2;
}

void SendSMS(String number)
{   
    String dateTime = GetDateTime();
    updateGSM();
    Serial.println(dateTime);

    if (time != "")
    {
        gsm.println("AT+CMGF=1");
        updateGSM();
        delay(500);

        gsm.print("AT+CMGS=\"");
        gsm.print(number);
        gsm.println("\"");
        updateGSM();
        delay(500);

        gsm.print(FormatTime(dateTime));
        gsm.print(". Trigger temp. reached: ");
        gsm.print(i_temp);
        gsm.print("°C");
        gsm.print(", Hmdty : ");
        gsm.print(humidity);
        gsm.print("%. Pump active.");
        gsm.print((char)26);
        updateGSM();
    }   
}

void WaterLowSMS(String number, float waterLevel)
{
    String time = getTime();
    updateGSM();
    Serial.println(time);

    if (time != "")
    {
        gsm.println("AT+CMGF=1");
        updateGSM();
        delay(500);

        // gsm.println("AT+CMGS=\"+639151635499\"");
        // updateGSM();
        // delay(500);

        gsm.print("AT+CMGS=\"");
        gsm.print(number);
        gsm.println("\"");
        updateGSM();
        delay(500);

        gsm.print(formatTime(time));
        gsm.print(" Water level Low: ");
        gsm.print(waterLevel);
        gsm.print("%. Please refill water container.");
        gsm.print((char)26);
        updateGSM();
    }  
}

void updateGSM()
{
    delay(500);
    while(gsm.available()) Serial.write((char)gsm.read());
    while(Serial.available()) gsm.write(Serial.read());
}

String GetDateTime()
{
    String dateTime = "";
    gsm.println("AT+CCLK?");
    delay(500);

    while(gsm.available()) dateTime += (char)gsm.read();
    return dateTime.trim();
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

void CheckSMS()
{
    static String smsBuffer = "";

    while (gsm.available())
    {
        char c = gsm.read();
        smsBuffer += c;

        if ( c == '\n' )
        {
            Serial.println(smsBuffer);

            String upperMsg = smsBuffer;
            upperMsg.toUpperCase();            

            if (upperMsg.indexOf("ON") != -1)
            {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("pump: ON");
                delay(5000);
            }
            else if (upperMsg.indexOf("OFF") != -1)
            {
                //off pump
            }

            smsBuffer = "";
        }
    }
}