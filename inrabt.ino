/**
@author Shakti Phartiyal
@desc Web radio player and Bluetooth speaker
**/
//TO BE RUN ON ESP32 ONLY------------
//ADVANCED WEB PAGE CONTROL-----------------------
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiManager.h>//https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>//https://github.com/bblanchon/ArduinoJson
#include <EEPROM.h>
#include <Arduino.h>
#include <WiFi.h>
#include "Audio.h"
#include <HTTPClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "BluetoothA2DPSink.h"
#include <WebServer.h>


WebServer server(8080);
const char* PAGE_TOP PROGMEM = "<html><head><title>INRABT</title><meta name=\"viewport\" content=\"width=device-width, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no\"></head><body><fieldset id=\"parent\"><legend>INRABT</legend><fieldset><legend>WEBRADIO CONTROLS</legend><button id=\"volp\" type=\"button\">Volume +</button><button id=\"volm\" type=\"button\">Volume -</button><hr/><button id=\"stp\" type=\"button\">Station +</button><button id=\"stm\" type=\"button\">Station -</button></fieldset><fieldset><legend>MODE</legend><button id=\"mbt\" type=\"button\">Bluetooth</button></fieldset><fieldset><legend>CHANNELS</legend><div><ol id=\"chm\"></ol><button type=\"button\" id=\"addCh\">Add</button> | <button type=\"button\" id=\"cmpCh\">Compile</button></div><br><textarea id=\"chls\" cols=\"50\" rows=\"25\" placeholder='[{\"id\": 1, \"name\": \"Sample Station\", \"url\": \"http://someurl\"}]'>";
const char* PAGE_BOTTOM PROGMEM = "</textarea><br><button id=\"uchls\" type=\"button\">Update</button></fieldset></fieldset></body><script type=\"text/javascript\">let chm=document.getElementById('chm');function gets(e){let t=document.getElementById('parent');t.disabled=!0,fetch(e).then(e=>e.text()).then(e=>{t.disabled=!1})}function cmpCh(){let e=chm.getElementsByTagName('li'),t=1,n=[];for(let l of e){let e=l.getElementsByClassName('name')[0].value.trim(),d=l.getElementsByClassName('url')[0].value.trim();e&&d&&n.push({id:t,name:e,url:d}),t++}document.getElementById('chls').value=JSON.stringify(n)}function addCh(){let e=document.createElement('li'),t=document.createElement('input');t.placeholder='Name',t.classList.add('name'),t.type='text',e.appendChild(t);let n=document.createElement('input');n.placeholder='URL',n.classList.add('url'),n.type='text',e.appendChild(n);let l=document.createElement('button');l.type='button',l.innerText='Remove',l.onclick=(e=>{e.target.parentElement.remove()}),e.appendChild(l),chm.appendChild(e)}function prepareChm(){let e=document.getElementById('chls').value.trim();if(e){try{e=JSON.parse(e)}catch(e){return alert('INVALID VALUE FORMAT'),!1}for(let t of e){let e=document.createElement('li'),n=document.createElement('input');n.placeholder='Name',n.classList.add('name'),n.type='text',n.value=t.name,e.appendChild(n);let l=document.createElement('input');l.placeholder='URL',l.classList.add('url'),l.type='text',l.value=t.url,e.appendChild(l);let d=document.createElement('button');d.type='button',d.innerText='Remove',d.onclick=(e=>{e.target.parentElement.remove()}),e.appendChild(d),chm.appendChild(e)}}else alert('INVALID VALUE')}document.getElementById('uchls').addEventListener('click',()=>{let e=document.getElementById('chls').value.trim();if(e){try{JSON.parse(e)}catch(e){return alert('INVALID VALUE FORMAT'),!1}fetch('/uchls',{method:'POST',body:e}).then(e=>e.text()).then(e=>{'OK'==e?alert('OK'):alert('ERROR')})}else alert('INVALID VALUE')}),document.getElementById('mbt').addEventListener('click',()=>{gets('/mbt')}),document.getElementById('volp').addEventListener('click',()=>{gets('/volp')}),document.getElementById('volm').addEventListener('click',()=>{gets('/volm')}),document.getElementById('stp').addEventListener('click',()=>{gets('/stp')}),document.getElementById('stm').addEventListener('click',()=>{gets('/stm')}),document.addEventListener('DOMContentLoaded',()=>{prepareChm()}),document.getElementById('addCh').addEventListener('click',()=>{addCh()}),document.getElementById('cmpCh').addEventListener('click',()=>{cmpCh()});</script></html>";

#define Sprintln(a) //(Serial.println(a)) //uncomment to start serial displays for debugging

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Rotary Encoder Inputs
#define inputCLK 19
#define inputDT 18
#define rotaryBtn 5
#define modeBtn 23
uint8_t counter = 0; 
uint8_t currentStateCLK;
uint8_t previousStateCLK; 


String stationName, playingTitle, stationUrl;
uint8_t volume = 10;
uint8_t currentStation = 1;
bool volumeMode = true;
volatile bool buttonPressed = false;
bool isBTMode = false;

char radio_server[100] PROGMEM = "https://example.com";
char wifiSSID[32] PROGMEM = "";
char wifiPassword[64] PROGMEM = "";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Sprintln("Should save config");
  shouldSaveConfig = true;
}

#define I2S_DOUT     25
#define I2S_BCLK      27
#define I2S_LRC        26
Audio audio; //external DAC
BluetoothA2DPSink a2dp_sink;


void IRAM_ATTR isr() {
  buttonPressed = true;
}

void showBootMessage()
{
  String bootMessage = "INRABT";
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(25, 20);
  display.println(bootMessage); 
  display.display();
}

void setup()
{
  Serial.begin(115200);
  Sprintln();

  pinMode (inputCLK,INPUT);
  pinMode (inputDT,INPUT);


  pinMode (rotaryBtn,INPUT_PULLUP);
  pinMode (modeBtn,INPUT_PULLUP);
  attachInterrupt(rotaryBtn, isr, RISING);
  // Read the initial state of inputCLK
  // Assign to previousStateCLK variable
  previousStateCLK = digitalRead(inputCLK);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Sprintln(F("SSD1306 allocation failed"));
  }
  
  //clean FS, for testing
  //SPIFFS.format();

  display.clearDisplay();
  showBootMessage();  

  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Reading preferences.."); 
  display.display();
  //read preferences from FS json
  readPrefs();

  if(isBTMode)
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(10, 10);
    display.println("Bluetooth"); 
    display.setCursor(35, 35);
    display.println("Mode"); 
    display.display();
    
    i2s_pin_config_t my_pin_config = {
        .bck_io_num = 27,
        .ws_io_num = 26,
        .data_out_num = 25,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    a2dp_sink.set_pin_config(my_pin_config);
    a2dp_sink.start("INRABT");
  }
  else
  {
    //read configuration from FS json
    Sprintln("mounting FS...");
    if (SPIFFS.begin(true))
    {
      Sprintln("mounted file system");
      if (SPIFFS.exists("/config.json")) {
        //file exists, reading and loading
        Sprintln("reading config file");
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile)
        {
          Sprintln("opened config file");
          size_t size = configFile.size();
          // Allocate a buffer to store contents of the file.
          std::unique_ptr<char[]> buf(new char[size]);
          configFile.readBytes(buf.get(), size);
  #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
          DynamicJsonDocument json(1024);
          auto deserializeError = deserializeJson(json, buf.get());
          serializeJson(json, Serial);
          if ( ! deserializeError ) {
  #else
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.parseObject(buf.get());
          json.printTo(Serial);
          if (json.success()) {
  #endif
            Sprintln("\nparsed json");
            strcpy(radio_server, json["radio_server"]);
            strcpy(wifiSSID, json["wifiSSID"]);
            strcpy(wifiPassword, json["wifiPassword"]);
          }
          else
          {
            Sprintln("failed to load json config");
          }
          configFile.close();
        }
      }
    }
    else
    {
      Sprintln("failed to mount FS");
    }
    //end read
    delay(1000);  
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Connecting to WiFi.."); 
    display.display();
    
    if(wifiSSID)
    {
      WiFi.begin(wifiSSID, wifiPassword);
    }
    
    WiFiManagerParameter custom_radio_server("radio_server", "radio server", radio_server, 100);
    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    //add all your parameters here
    wifiManager.addParameter(&custom_radio_server);
    //reset settings - for testing
    //wifiManager.resetSettings();
    wifiManager.setMinimumSignalQuality(5);
    //wifiManager.setTimeout(120);
    if (!wifiManager.autoConnect("INRABTConfig", "password"))
    {
      Sprintln("failed to connect and hit timeout");
      delay(3000);
      ESP.restart();
      delay(5000);
    }
    Sprintln("connected...yeey :)");
  
    strcpy(radio_server, custom_radio_server.getValue());
  
    if (shouldSaveConfig)
    {
      Sprintln("saving config");
  #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
      DynamicJsonDocument json(1024);
  #else
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
  #endif
      json["radio_server"] = radio_server;
      json["wifiSSID"] = String(WiFi.SSID());
      json["wifiPassword"] = String(WiFi.psk());
  
      File configFile = SPIFFS.open("/config.json", "w+");
      if (!configFile)
      {
        Sprintln("failed to open config file for writing");
      }
  
  #ifdef ARDUINOJSON_VERSION_MAJOR >= 6
      serializeJson(json, Serial);
      serializeJson(json, configFile);
  #else
      json.printTo(Serial);
      json.printTo(configFile);
  #endif
      configFile.close();
      //end save
    }
    Sprintln("local ip");
    Sprintln(WiFi.localIP());
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Connected to WiFi."); 
    display.setCursor(0, 10);
    display.println(WiFi.localIP()); 
    display.display();
    delay(1000);
    //---------------------------------------------CONNECTIVITY DONE------------------------------
    //---------------------------------------------MAIN LOGIC STARTS------------------------------
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Checking station list.."); 
    display.display();
    checkStationList();
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume);
    selectStation('=');  

    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", PAGE_TOP+loadAllStations()+PAGE_BOTTOM);
    });
    server.on("/mbt", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      if(isBTMode)
      {
        server.send(200, "text/html", "OK");
        return;
      }
      isBTMode = true;
      savePrefs();
      server.send(200, "text/html", "OK");
      ESP.restart();    
    });
    server.on("/uchls", HTTP_POST, []() {
      updateStations(server.arg(0));
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", "OK");
    });
    server.on("/volp", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      setTheVolume('+');
      server.send(200, "text/html", "OK");
    });
    server.on("/volm", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      setTheVolume('-');
      server.send(200, "text/html", "OK");
    });
    server.on("/stp", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      selectStation('+');
      server.send(200, "text/html", "OK");
    });
    server.on("/stm", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      selectStation('-');
      server.send(200, "text/html", "OK");
    });
    server.begin();
  }
}

void loop()
{  
// @TODO in an Interrupt
//  if(digitalRead(modeBtn) == LOW)
//  {
//    SPIFFS.format();
//    isBTMode = !isBTMode;    
//    savePrefs();
//    ESP.restart();
//  }
  if(isBTMode)
  {
    if(buttonPressed)
    {
      isBTMode = false;
      savePrefs();
      ESP.restart();
    }
  }
  else
  {
    audio.loop();
    server.handleClient();
    currentStateCLK = digitalRead(inputCLK);
    if (currentStateCLK != previousStateCLK)
    {
     // the encoder is rotating clockwise
     if (digitalRead(inputDT) != currentStateCLK)
     {
       counter ++;
       if(volumeMode)
       {
        setTheVolume('+');
       }
       else
       {
        selectStation('+');
       }
     }
     else
     {
       // Encoder is rotating counterclockwise
       counter --;
       if(volumeMode)
       {
        setTheVolume('-');
       }
       else
       {
        selectStation('-');
       }
     }
     previousStateCLK = currentStateCLK;
    }
  
    if(buttonPressed)
    {
      buttonPressed = false;
      switchMode();
    }

  }
}

void audio_showstreamtitle(const char *info)
{
  Sprintln("streamtitle");
  Sprintln(info);
  if(playingTitle == String(info))
  {
    return;
  }
  playingTitle = String(info);
  if(playingTitle.length() < 1)
  {
    playingTitle = "(-_-)";
  }
  if(playingTitle.length() >= 38)
  {
    playingTitle = playingTitle.substring(0, 38)+"..";
  }
  updateDisplay();
}


void updateDisplay()
{
  //DO NOT USE ANY SERIAL PRINTS OR DELAYS
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  //station name
  display.fillRect(0, 0, display.width()-1, 12, BLACK); //24
  display.setCursor(0, 0);
  display.println(stationName); 

  
  display.drawLine(0, 13, display.width()-1, 13, WHITE); //25

  //playing title
  display.fillRect(0, 15, display.width()-1, 25, BLACK); //27 25
  display.setCursor(0, 17);//29
  display.println(playingTitle);
  
  display.drawLine(0, 41, display.width()-1, 41, WHITE);//53

  //volume / mode
  display.fillRect(0, 42, display.width()-1, 10, BLACK);//54 10
  if(volumeMode)
  {
    display.setCursor(26, 43); //26 56
    display.println("VOLUME "+String((int)round(volume/0.21))+"%");
    display.drawRect(1, 44, 23, 5, WHITE); //1 57 23 5
    display.fillRect(2, 45, volume, 4, WHITE); //2 58 4
  }
  else
  {
    display.setCursor(0, 44); //0 57
    display.println("SELECT STATION");   
  }
  display.drawLine(0, 53, display.width()-1, 53, WHITE);//53
  display.setCursor(0, 56);
  display.println("SIP: "+WiFi.localIP().toString());
  display.display();
}


void switchMode()
{
  volumeMode = !volumeMode;
  updateDisplay();
}


void setTheVolume(char op)
{
  if(op == '+' && volume != 21)
  {
    volume++;
  }
  if(op == '-' && volume !=0)
  {
    volume--;
  }
  audio.setVolume(volume);
  savePrefs();
  updateDisplay();
}

void selectStation(char op)
{
  if(op == '+')
  {
    currentStation++;
  }
  else if(op == '-')
  {
    currentStation--;
  }
  loadStation();
}


void savePrefs()
{
    Sprintln("saving prefs");
#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif
    json["volume"] = volume;
    json["currentStation"] = currentStation;
    json["isBTMode"] = isBTMode;
    File prefsFile = SPIFFS.open("/prefs.json", "w+");
    if (!prefsFile)
    {
      Sprintln("failed to open prefs file for writing");
    }
#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, prefsFile);
#else
    json.printTo(Serial);
    json.printTo(prefsFile);
#endif
    prefsFile.close();
    //end save
}


void readPrefs()
{
  if (SPIFFS.begin())
  {
    Sprintln("mounted file system");
    if (SPIFFS.exists("/prefs.json"))
    {
      //file exists, reading and loading
      Sprintln("reading prefs file");
      File configFile = SPIFFS.open("/prefs.json", "r");
      if (configFile)
      {
        Sprintln("opened prefs file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
#ifdef ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Sprintln("\nparsed json");
          volume = json["volume"];
          currentStation = json["currentStation"];
          isBTMode = json["isBTMode"];
          //strcpy(toVariable, json["key"]); //if string
        }
        else
        {
          Sprintln("failed to load json prefs");
        }
        configFile.close();
      }
    }
  }
  else
  {
    Sprintln("failed to mount FS");
  }
}

void loadStation()
{
  if (SPIFFS.begin())
  {
    Sprintln("mounted file system");
    if (SPIFFS.exists("/stations.json"))
    {
      Sprintln("reading stations file");
      File stationsFile = SPIFFS.open("/stations.json", "r");
      if (stationsFile)
      {
        Sprintln("opened stations file");
        size_t size = stationsFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        stationsFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(2048);
        auto deserializeError = deserializeJson(json, buf.get());
        
        //serializeJson(json, Serial);
        
        if(deserializeError)
        {
          Serial.print("deserializeJson() failed: ");
          Serial.println(deserializeError.c_str());
          return;
        }
        stationsFile.close();
        JsonArray array = json.as<JsonArray>();
        if(currentStation < 1)
        {
          currentStation = array.size();
        }
        if(currentStation > array.size())
        {
          currentStation = 1;
        }
        Sprintln("Loaded Station");
        serializeJson(json[currentStation-1], Serial);
        currentStation = json[currentStation-1]["id"];
        stationName = json[currentStation-1]["name"].as<String>();
        if(stationName.length() < 1)
        {
          stationName = "(-_-)";
        }
        else if(stationName.length() >= 20)
        {
          stationName = stationName.substring(0, 20)+"..";
        }
        stationUrl = json[currentStation-1]["url"].as<String>();
        updateDisplay();
        audio.connecttohost(stationUrl.c_str());
        savePrefs();
      }
    }
    else
    {
      Sprintln("stations.json does not exist! Checking!!");
      checkStationList();
    }
  }
  else
  {
    Sprintln("failed to mount FS");
  }
}

String loadAllStations()
{
  if (SPIFFS.begin())
  {
    Sprintln("mounted file system");
    if (SPIFFS.exists("/stations.json"))
    {
      Sprintln("reading stations file");
      File stationsFile = SPIFFS.open("/stations.json", "r");
      if (stationsFile && stationsFile.size())
      {
          String fileData;      
          while (stationsFile.available()){
            fileData += char(stationsFile.read());
          }
          stationsFile.close();
          Sprintln("ALL STATIONS");
          Sprintln(fileData);          
          return fileData;
      }
    }
    else
    {
      Sprintln("stations.json does not exist!");
    }
  }
  else
  {
    Sprintln("failed to mount FS");
  }
  return "";
}


void checkStationList()
{
  if(SPIFFS.begin())
  {
    if(!SPIFFS.exists("/stations.json"))
    {
      updateStations("[{\"id\":1,\"name\":\"Please configure\",\"url\":\"https://localhost\"}]");
    }
  }
  else
  {
    Sprintln("failed to mount FS");
  }
}

bool updateStations(String stations)
{
  File stationsFile = SPIFFS.open("/stations.json", "w+");
  if (!stationsFile)
  {
    return false;
  }
  stationsFile.print(stations);
  stationsFile.close();
  return true;  
}


void startBTMode() //@TODO
{
//a2dp_sink.play();
//a2dp_sink.pause();
//a2dp_sink.stop();
//a2dp_sink.next();
//a2dp_sink.previous();
}
