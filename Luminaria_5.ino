//#define STATIC_IP

#include <ArduinoJson.h>
#include "FS.h"
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
//#include <ESP8266mDNS.h>
 
ESP8266WebServer server(80);
const char* device = "Luminaria_5";
String JSON_file = "";
char *server_ip = "192.168.43.124";
char *coordinador = "192.168.1.250";

//Direccionamiento para modo STA
IPAddress ip_sta(192,168,1,103);     
IPAddress gateway_sta(192,168,1,1);   
IPAddress netmask_sta(255,255,255,0);
//Direccionamiento para modo AP
IPAddress ip_ap(192,168,11,4);
IPAddress gateway_ap(192,168,11,1);
IPAddress netmask_ap(255,255,255,0);

#define PIN_PWM 14
//Con PIN_PWM = 14 la pata real es la 5ta del integrado contando desde RST para el lado de 3V3 y la 4ta del conector
//Con PIN_PWM = 15 la pata real es la penúltima del integrado contando desde TX para el lado de 5V (es la séptima del conector izquierdo)

#define DIMMER_APAGADO 0
#define DIMMER_BAJO    60
#define DIMMER_MEDIO   225
#define DIMMER_ALTO    1023

#define TIEMPO_ENVIO_COORD 60000  //un minuto en miliseg

typedef enum luminaria_state{ESTADO_1, ESTADO_2, ESTADO_3, ESTADO_ERR};
 
const char* mySsid = device;
const char* password = "luminaria";
const char * _ssid = "";
const char * _pass = "";

String modo_manual = "";
String modo_automatico = "";
String dimmer_target = "APAGADO";
String current_dimmer = "APAGADO";
String luz_defecto_sin_mov = "APAGADO";
String luz_con_mov = "MEDIO";

luminaria_state current_lum_state = ESTADO_1;
unsigned int long previous_millis = 0;
unsigned int long previous_millis_wifi = 0;
unsigned int long previous_millis_light = 0;
unsigned int tiempo_luz_mov = 5000;
bool first_wifi_disconnection = false;
bool waiting_in_AP_mode = false;
bool encender_luminaria = false;
unsigned int consumo = 0;

/*Varables donde se guardan los horarios seteados desde el server*/
unsigned int hours_init_state_1 = 12;
unsigned int mins_init_state_1 = 0;

unsigned int hours_init_state_2 = 5;
unsigned int mins_init_state_2 = 0;

unsigned int hours_init_state_3 = 8;
unsigned int mins_init_state_3 = 0;

String Estado_1_Reposo = "";
String Estado_1_Mov = "";
String Estado_2_Reposo = "";
String Estado_2_Mov = "";
String Estado_3_Reposo = "";
String Estado_3_Mov = "";

/******************** Variables para NTP ******************/
static const char ntpServerName[] = "us.pool.ntp.org";
const int timeZone = -3;     // Zona Argentina

unsigned int year_esp = 2018;
unsigned int month_esp = 1;
unsigned int day_esp = 1;
unsigned int hours_esp = 10;
unsigned int minutes_esp = 30;
unsigned int secs_esp = 15;

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
/********************************************************/

StaticJsonDocument<1200> jsonBuffer_general;
File configFile;

///////// Funciones //////////////////////////////////////////////////
time_t          getNtpTime();
void            digitalClockDisplay();
void            printDigits(int digits);
void            sendNTPpacket(IPAddress &address);
void            Save_NTP_Time();
bool            Dimmer_Changed();
void            Set_Luminaire_At_Level(String);
unsigned int    Get_Dimmer_Number(String);
luminaria_state Get_Luminaria_State();
void            Guardar_en_Memoria();
void            GET_Sens_Mov_Config(DynamicJsonDocument);
void            GET_Dimmer_Config(DynamicJsonDocument);
void            GET_States_Config(DynamicJsonDocument);
void            Imprimir_Hora();
void            Imprimir_Estados();
void            handleConfig();
void            handle_Send_Config();
void            handle_Send_Status();
void            handle_WiFi_Server();
void            handleSettingsUpdate();
void            handleSettingsServer();
bool            wifiConnect(const char *, const char *);
void            handle_Sens_Mov();
void            Identificar();

char webpage[] PROGMEM = R"=====(
<html>
<head>
</head>
<body>
<p>
<form>
<div>
  <label for="ssid">SSID</label>
  <input value="" id="ssid" placeholder="SSID"/>
</div>
<div>
  <label for="password">PASSWORD</label>
  <input value="" type="password" id="password" placeholder="PASSWORD"/>
</div>
<div>
<button onclick="myFunction_wifi()"> Guardar </button>
</div>
</form>
</p>
<p>
<form>
  <label for="server">SERVER</label>
  <input value="" id="server" placeholder="SERVER"/>
</form>
  <button onclick="myFunction_server()"> Guardar </button>
</p>
</body>
<script>
function myFunction_wifi()
{
  var ssid = document.getElementById("ssid").value;
  var password = document.getElementById("password").value;
  
  var data = {ssid:ssid, password:password};
  console.log(data);

  var xhr = new XMLHttpRequest();
  var url = "/settings";

  xhr.onreadystatechange = function(){
    
    if(this.onreadyState == 4 && this.status == 200)
    {
      console.log("Status = 4");
      console.log(xhr.responseText);
    }
  };

  xhr.open("POST", url, true);
  xhr.send(JSON.stringify(data));
};

function myFunction_server(){

  var server = document.getElementById("server").value;
  var data = {server:server};
  console.log(data);

  var xhr = new XMLHttpRequest();
  var url = "/settings_server";

  xhr.onreadystatechange = function(){

    setTimeout(function(){
      
    
      if(this.onreadyState == 4 && this.status == 200)
      {
        console.log("Status = 4");
        console.log(xhr.responseText);
      }
    }, 10000);
  };

  xhr.open("POST", url, true);
  xhr.send(JSON.stringify(data));
};
</script>
</html>
)=====";

void setup() {
 
    pinMode(PIN_PWM, OUTPUT);
    //analogWrite(PIN_PWM, 0);
    
    Serial.begin(115200);

    JSON_file += "/";
    JSON_file += device;
    JSON_file += ".json";

    Serial.println("Mounting FS...");
    if (!SPIFFS.begin()) {
      Serial.println("Failed to mount file system");
      return;
    }
    delay(1000);

    Tomar_Configuraciones_WiFi();
    wifiConnect(_ssid, _pass);
  
    server.on("/config", handleConfig); //Associate the handler function to the path
    server.on("/get_config", handle_Send_Config);
    server.on("/status", handle_Send_Status);
    server.on("/wifi_from_server", handle_WiFi_Server);
    server.on("/ap",[](){server.send_P(200,"text/html",webpage);});
    server.on("/settings", HTTP_POST, handleSettingsUpdate);
    server.on("/settings_server", HTTP_POST, handleSettingsServer);
    server.on("/sens_mov", handle_Sens_Mov);
    server.on("/identificar", handle_Identificar);
    
    server.begin(); //Start the server
    Serial.println("Web Server listening");

    Serial.println("Starting UDP");
    Udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(Udp.localPort());
    Serial.println("waiting for sync (NTP)");
    setSyncProvider(getNtpTime);
    setSyncInterval(300);

   /*if (!MDNS.begin(device)) 
   {             
     Serial.println("Error iniciando mDNS");
   }
   Serial.println("mDNS iniciado");*/

    Serial.println("Tomando configuraciones de la luminaria");
    Tomar_Configuraciones();
    Imprimir_Configuraciones();
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop() {
    //yield();
    server.handleClient(); //Handling of incoming requests

    if (timeStatus() != timeNotSet)
    {
      if (now() != prevDisplay)  //update the display only if time has changed
      {
        prevDisplay = now();
        Save_NTP_Time();
        //digitalClockDisplay();
      }
    }
    
    if(WiFi.status() != WL_CONNECTED)
    {    
      if(!first_wifi_disconnection)
      {
        previous_millis_wifi = millis();
        first_wifi_disconnection = true;
        Serial.println("WiFi disconnected. Waiting for configuration in AP mode");
      }
      else if(millis() - previous_millis_wifi < 300000)
      {
          if(!waiting_in_AP_mode)
          {
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(ip_ap, gateway_ap, netmask_ap);
            WiFi.softAP(mySsid, password);
            waiting_in_AP_mode = true;
          }   
      }
      else
      {
        Serial.println("Tomando configuraciones de WiFi guardadas en archivo");
        if(SPIFFS.exists("/wifi.json"))
        {

          File configFile = SPIFFS.open("/wifi.json", "r");
          if (!configFile) {
           Serial.println("Failed to open config file");
          }

          StaticJsonDocument<1024> jsonBuffer;
          DeserializationError error = deserializeJson(jsonBuffer, configFile);

          if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
          }

          _ssid = jsonBuffer["ssid"];
          _pass = jsonBuffer["password"];
          Serial.println("Configuración recuperada: ");
          Serial.print("SSID: ");
          Serial.print(_ssid);
          Serial.print(" password: ");
          Serial.println(_pass);
        }

        if(wifiConnect(_ssid, _pass))
        {
          first_wifi_disconnection = false;
          waiting_in_AP_mode = false; 
        }
        else
        {
          previous_millis_wifi = millis();
        }
      }      
    }

    Handle_Light();
    if(Dimmer_Changed())
    {
      Set_Luminaire_At_Level(dimmer_target);
    }
    
    if(millis() - previous_millis > TIEMPO_ENVIO_COORD)
    {
      //Imprimir_Hora();
      //Imprimir_Estados();
      Serial.print(device);
      Serial.print(": ");
      Serial.println(WiFi.localIP());
      Enviar_Datos_Coordinador();
      
      previous_millis = millis();
    }
}

/*************************************** Funciones ********************************************/
/**********************************************************************************************/
 
void handleConfig() { 
 
  if (server.hasArg("plain")== false){ //Check if body received
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Config not received");
    return;
  }

  String message_received = server.arg("plain");
  Serial.println(message_received);
  
  int j = message_received.indexOf('&');
  String string_modo = message_received.substring(0,j);
  int k = string_modo.indexOf('=');
  String _modo = string_modo.substring(0,k);

  if(_modo == "Modo_Manual")
  {
    modo_manual = "SI";
    modo_automatico = "";

    String string_modo_manual = message_received.substring(j+1, message_received.length());
    int l = string_modo_manual.indexOf('=');
    String _dimmer = string_modo_manual.substring(l+1, string_modo_manual.length());
    Serial.println(_dimmer);

    jsonBuffer_general["Modo_Manual"] = modo_manual;
    jsonBuffer_general["Modo_Automatico"] = modo_automatico;
    jsonBuffer_general["Dimmer"] = _dimmer;
    dimmer_target = _dimmer;
  }
  else
  {
    modo_manual = "NO";
    String string_modo_automatico = string_modo.substring(k+1, string_modo.length());
    
    if(string_modo_automatico == "SENSOR")
    {
      modo_automatico = "SENSOR";
      
      String _config_modo_sensor = message_received.substring(j+1, message_received.length());

      int m = _config_modo_sensor.indexOf('&');
      String string_modo_reposo = _config_modo_sensor.substring(0, m);
      int n = string_modo_reposo.indexOf('=');
      String _modo_reposo = string_modo_reposo.substring(n+1, string_modo_reposo.length());

      String _config_modo_sensor_2 = _config_modo_sensor.substring(m+1, _config_modo_sensor.length());
      
      int o = _config_modo_sensor_2.indexOf('&');
      String string_modo_movimiento = _config_modo_sensor_2.substring(0, o);
      int p = string_modo_movimiento.indexOf('=');
      String _modo_movimiento = string_modo_movimiento.substring(p+1, string_modo_movimiento.length());

      String _config_modo_sensor_3 = _config_modo_sensor_2.substring(o+1, _config_modo_sensor_2.length());

      int q = _config_modo_sensor_3.indexOf('=');
      String _tiempo_encendido = _config_modo_sensor_3.substring(q+1, _config_modo_sensor_3.length());

      jsonBuffer_general["Modo_Manual"] = modo_manual;
      jsonBuffer_general["Modo_Automatico"] = modo_automatico;
  
      luz_defecto_sin_mov = _modo_reposo;
      jsonBuffer_general["Luz_Defecto_Sin_Mov"] = _modo_reposo;
      luz_con_mov = _modo_movimiento;
      jsonBuffer_general["Luz_Con_Mov"] = _modo_movimiento;
      tiempo_luz_mov = _tiempo_encendido.toInt()*1000;
      jsonBuffer_general["Tiempo_Luz_Mov"] = _tiempo_encendido.toInt();
    }
    else
    {
      modo_automatico = "ESTADOS";

      String _config_modo_estados = message_received.substring(j+1, message_received.length());

      int r = _config_modo_estados.indexOf('&');
      String E1_h = _config_modo_estados.substring(0, r);
      int s = E1_h.indexOf('=');
      hours_init_state_1 = E1_h.substring(s+1, E1_h.length()).toInt();

      String _config_modo_estados_2 = _config_modo_estados.substring(r+1, _config_modo_estados.length());

      int t = _config_modo_estados_2.indexOf('&');
      String E1_m = _config_modo_estados_2.substring(0, t);
      int u = E1_m.indexOf('=');
      mins_init_state_1 = E1_m.substring(u+1, E1_m.length()).toInt();

      String _config_modo_estados_3 = _config_modo_estados_2.substring(t+1, _config_modo_estados_2.length());

      int v = _config_modo_estados_3.indexOf('&');
      String E2_h = _config_modo_estados_3.substring(0, v);
      int w = E2_h.indexOf('=');
      hours_init_state_2 = E2_h.substring(w+1, E2_h.length()).toInt();
      
      String _config_modo_estados_4 = _config_modo_estados_3.substring(v+1, _config_modo_estados_3.length());

      int x = _config_modo_estados_4.indexOf('&');
      String E2_m = _config_modo_estados_4.substring(0, x);
      int y = E2_m.indexOf('=');
      mins_init_state_2 = E2_m.substring(y+1, E2_m.length()).toInt();

      Serial.println(hours_init_state_1);
      Serial.println(mins_init_state_1);
      Serial.println(hours_init_state_2);
      Serial.println(mins_init_state_2);
    }
  }

  Guardar_en_Memoria();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Config Recibida");
}

void handle_Send_Config(){
  
  Serial.println("Request de enviar configuración al server");

  configFile = SPIFFS.open(JSON_file, "r");
  delay(500);
  if (!configFile) {
    Serial.println("Failed to open config file");
    //Hacer algo acá
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    //Hacer algo acá
  }
  
  StaticJsonDocument<1024> jsonBuffer;
  DeserializationError error = deserializeJson(jsonBuffer, configFile);

  String json_to_send;
  serializeJson(jsonBuffer, json_to_send);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  configFile.close();

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/json", json_to_send);
  Serial.println(json_to_send);

}

void handle_WiFi_Server(){
  
  Serial.println("Requerimiento web de cambio de WiFi");

  String data = server.arg("plain");
  Serial.print("Received data: ");
  Serial.println(data);

  int j = data.indexOf('&');
  String string_1 = data.substring(0,j);
  String string_2 = data.substring(j+1, data.length());

  int i = string_1.indexOf('=');
  String ssid = string_1.substring(i+1, string_1.length());
  i = string_2.indexOf('=');
  String password = string_2.substring(i+1, string_2.length());

  Serial.println(ssid);
  Serial.println(password);

  StaticJsonDocument<1024> jsonBuffer_wifi;

  jsonBuffer_wifi["ssid"] = ssid;
  jsonBuffer_wifi["password"] = password;
  
  configFile = SPIFFS.open("/wifi.json", "w");
      delay(500);
      if (!configFile) {
        Serial.println("Failed to open config file");
      }

  if (serializeJson(jsonBuffer_wifi, configFile) == 0) {
     Serial.println(F("Failed to write to file"));
  }
  configFile.close();
      
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Recibido");

  wifiConnect(jsonBuffer_wifi["ssid"], jsonBuffer_wifi["password"]);
}

void handle_Send_Status(){
  
  Serial.println("Requerimiento web de status");

  String message = "<!DOCTYPE html><html><title>Status de ";
  message += device;
  message += "</title><body><p>Nombre del dispositivo: ";
  message += device;
  message += "</p><p>Direccion IP: ";
  message += WiFi.localIP().toString();
  message += "</p><p>Estado de Dimmer: ";
  message += current_dimmer;
  message += "</p><p>Modo manual:  ";
  message += modo_manual;
  if(modo_manual == "NO")
  {
    message += "</p><p>Modo automatico:  ";
    message += modo_automatico;
  }
  if(modo_automatico == "SENSOR")
  {
    message += "    ";
    message += "Nivel luz sin movimiento: ";
    message += luz_defecto_sin_mov;
    message += "    ";
    message += "Nivel luz con movimiento: ";
    message += luz_con_mov;
    message += "</br>Tiempo de encendido: ";
    message += tiempo_luz_mov;
  }
  else if(modo_automatico == "ESTADOS")
  {
    message += "</br>";
    message += "Horario Inicio Estado_1: ";
    if(hours_init_state_1 < 10)
    {
      message += "0";
    }
    message += hours_init_state_1;
    message += ":";
    if(mins_init_state_1 < 10)
    {
      message += "0";
    }
    message +=mins_init_state_1;
    message += " hs.";
    message += "    ";
    message += "Estado_1_Reposo: ";
    message += Estado_1_Reposo;
    message += "   ";
    message += "Estado_1_Movimiento: ";
    message += Estado_1_Mov;

    message += "</br>";
    message += "Horario Inicio Estado_2: ";
    if(hours_init_state_2 < 10)
    {
      message += "0";
    }
    message += hours_init_state_2;
    message += ":";
    if(mins_init_state_2 < 10)
    {
      message += "0";
    }
    message +=mins_init_state_2;
    message += " hs.";
    message += "    ";
    message += "Estado_2_Reposo: ";
    message += Estado_2_Reposo;
    message += "   ";
    message += "Estado_2_Movimiento: ";
    message += Estado_2_Mov;


    message += "</br>";
    message += "Horario Inicio Estado_3: ";
    if(hours_init_state_3 < 10)
    {
      message += "0";
    }
    message += hours_init_state_3;
    message += ":";
    if(mins_init_state_3 < 10)
    {
      message += "0";
    }
    message +=mins_init_state_3;
    message += " hs.";
    message += "    ";
    message += "Estado_3_Reposo: ";
    message += Estado_3_Reposo;
    message += "   ";
    message += "Estado_3_Movimiento: ";
    message += Estado_3_Mov;
    message += "</br>Tiempo de encendido: ";
    message += tiempo_luz_mov;
  }
  message += "</p></body></html>";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", message);
}

void handleSettingsUpdate(){
  
  String data = server.arg("plain");
  Serial.print("Received data: ");
  Serial.println(data);

  StaticJsonDocument<1024> jsonBuffer;
  DeserializationError error = deserializeJson(jsonBuffer, data);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  if(wifiConnect(jsonBuffer["ssid"],jsonBuffer["password"]))
  {
    File configFile = SPIFFS.open("/wifi.json", "w");
    serializeJson(jsonBuffer, configFile);
    configFile.close();
  }
  
  //Probar cambiando el texto application/json
  server.send(200, "text/plain", "{\"status\":\"OK\"}");
  //delay(2000);
}

void handleSettingsServer(){
  
  Serial.println("Requerimiento web de cambio de Server");
  String data = server.arg("plain");
  Serial.println(data);

  StaticJsonDocument<100> jsonBuffer;
  DeserializationError error = deserializeJson(jsonBuffer, data);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  String aux = jsonBuffer["server"];
  aux.toCharArray(server_ip, 16); 

  jsonBuffer_general["server"] = server_ip;

  configFile = SPIFFS.open(JSON_file, "w");
  delay(500);
  if (!configFile) {
    Serial.println("Failed to open config file");
  }

  if (serializeJson(jsonBuffer_general, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  
  configFile.close();
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Recibido");
}

void handle_Sens_Mov()
{
  Serial.println("Sensor de movimiento");

  encender_luminaria = true;
}

void handle_Identificar()
{
  Serial.println("Requerimiento de identificar");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Identificar recibido");

  Identificar();
}

bool wifiConnect(const char* _ssid, const char* _pass)
{

  Serial.println("Connecting to ");
  Serial.print("Network: ");
  Serial.println(_ssid);
  Serial.print("With password: ");
  Serial.println(_pass);
    
  WiFi.mode(WIFI_STA);
  #ifdef STATIC_IP
    WiFi.config(ip_sta, gateway_sta, netmask_sta);
  #endif  
  WiFi.hostname(device);
  WiFi.begin(_ssid, _pass);

  unsigned long startTime = millis();
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if(millis() - startTime > 15000)    //10 segundos para poder conectarse al WiFi configurado
    {
      //WiFi.softAPdisconnect(true);
      //WiFi.disconnect();
      break;
    }
  }
  
  if(WiFi.status() == WL_CONNECTED)
  { 
    Serial.println("WiFi connected");
    Serial.print("Network: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  else
  {
    Serial.println("WiFi not connected. Starting AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ip_ap, gateway_ap, netmask_ap);
    WiFi.softAP(mySsid, password);
    return false;
  }
  //WiFi.printDiag(Serial);
}
/*
void GET_Sens_Mov_Config(DynamicJsonDocument doc){
  
  modo_manual = doc["Modo_Manual"].as<char*>();
  jsonBuffer_general["Modo_Manual"] = doc["Modo_Manual"];
  modo_automatico = doc["Modo_Automatico"].as<char*>();
  jsonBuffer_general["Modo_Automatico"] = doc["Modo_Automatico"];
  
  luz_defecto_sin_mov = doc["Luz_Defecto_Sin_Mov"].as<char*>();
  jsonBuffer_general["Luz_Defecto_Sin_Mov"] = doc["Luz_Defecto_Sin_Mov"];
  
  luz_con_mov = doc["Luz_Con_Mov"].as<char*>();
  jsonBuffer_general["Luz_Con_Mov"] = doc["Luz_Con_Mov"];

  jsonBuffer_general["Tiempo_Luz_Mov"] = doc["Tiempo_Luz_Mov"];
  tiempo_luz_mov = 1000*doc["Tiempo_Luz_Mov"].as<int>();
}
*/
/*void GET_Dimmer_Config(DynamicJsonDocument doc){

  jsonBuffer_general["Dimmer"] = doc["Dimmer"];
  dimmer_target = doc["Dimmer"].as<char*>();

  modo_manual = doc["Modo_Manual"].as<char*>();
  jsonBuffer_general["Modo_Manual"] = doc["Modo_Manual"];
  modo_automatico = doc["Modo_Automatico"].as<char*>();
  jsonBuffer_general["Modo_Automatico"] = doc["Modo_Automatico"];
}*/

void GET_States_Config(DynamicJsonDocument doc){

  modo_manual = doc["Modo_Manual"].as<char*>();
  jsonBuffer_general["Modo_Manual"] = doc["Modo_Manual"];
  modo_automatico = doc["Modo_Automatico"].as<char*>();
  jsonBuffer_general["Modo_Automatico"] = doc["Modo_Automatico"];
  
  jsonBuffer_general["Horas_Inicio_Estado_1"] = doc["Horas_Inicio_Estado_1"];
  jsonBuffer_general["Minutos_Inicio_Estado_1"] = doc["Minutos_Inicio_Estado_1"];
  hours_init_state_1 = doc["Horas_Inicio_Estado_1"].as<int>();
  mins_init_state_1 = doc["Minutos_Inicio_Estado_1"].as<int>();

  jsonBuffer_general["Horas_Inicio_Estado_2"] = doc["Horas_Inicio_Estado_2"];
  jsonBuffer_general["Minutos_Inicio_Estado_2"] = doc["Minutos_Inicio_Estado_2"];
  hours_init_state_2 = doc["Horas_Inicio_Estado_2"].as<int>();
  mins_init_state_2 = doc["Minutos_Inicio_Estado_2"].as<int>();

  jsonBuffer_general["Horas_Inicio_Estado_3"] = doc["Horas_Inicio_Estado_3"];
  jsonBuffer_general["Minutos_Inicio_Estado_3"] = doc["Minutos_Inicio_Estado_3"];
  hours_init_state_3 = doc["Horas_Inicio_Estado_3"].as<int>();
  mins_init_state_3 = doc["Minutos_Inicio_Estado_3"].as<int>();

  jsonBuffer_general["Estado_1_Reposo"] = doc["Estado_1_Reposo"];
  jsonBuffer_general["Estado_1_Mov"] = doc["Estado_1_Mov"];
  jsonBuffer_general["Estado_2_Reposo"] = doc["Estado_2_Reposo"];
  jsonBuffer_general["Estado_2_Mov"] = doc["Estado_2_Mov"];
  jsonBuffer_general["Estado_3_Reposo"] = doc["Estado_3_Reposo"];
  jsonBuffer_general["Estado_3_Mov"] = doc["Estado_3_Mov"];
  Estado_1_Reposo = doc["Estado_1_Reposo"].as<char*>();
  Estado_1_Mov = doc["Estado_1_Mov"].as<char*>();
  Estado_2_Reposo = doc["Estado_2_Reposo"].as<char*>();
  Estado_2_Mov = doc["Estado_2_Mov"].as<char*>();
  Estado_3_Reposo = doc["Estado_3_Reposo"].as<char*>();
  Estado_3_Mov = doc["Estado_3_Mov"].as<char*>();

  tiempo_luz_mov = 1000*doc["Tiempo_Luz_Mov"].as<int>();
}

luminaria_state Get_Luminaria_State(){
  
  int minutes_state_1 = hours_init_state_1*60 + mins_init_state_1;    //Horario para el inicio del ESTADO_1 convertido a minutos
  int minutes_state_2 = hours_init_state_2*60 + mins_init_state_2;    //Horario para el inicio del ESTADO_2 convertido a minutos
  int minutes_state_3 = hours_init_state_3*60 + mins_init_state_3;    //Horario para el inicio del ESTADO_3 convertido a minutos

  int minutes_state_2_E1;     //Minutos que pasaron desde la hora inicial del ESTADO_1
  int minutes_state_3_E1;     //Minutos que pasaron desde la hora inicial del ESTADO_1

  if(minutes_state_2 > minutes_state_1)
  {
    minutes_state_2_E1 = minutes_state_2;
  }
  else
  {
    minutes_state_2_E1 = (24*60 - minutes_state_1) + minutes_state_2 + minutes_state_1;
  }

  if(minutes_state_3 > minutes_state_2_E1)
  {
    minutes_state_3_E1 = minutes_state_3;
  }
  else
  {
    if(minutes_state_3 > minutes_state_2)
    {
      minutes_state_3_E1 = minutes_state_3 - minutes_state_2 + minutes_state_2_E1;
    }
    else
    {
      minutes_state_3_E1 = (24*60 - minutes_state_2) + minutes_state_3 + minutes_state_2_E1;
    }
  }
  
  int j = hours_esp*60 + minutes_esp;   //Hora actual convertida a minutos desde las 00:00 hs

  if(j < minutes_state_1)
  {
    j = (24*60 - minutes_state_1) + j + minutes_state_1;
  }

  if(j < minutes_state_2_E1 )
  {
    return ESTADO_1;
  }
  else if(j >= minutes_state_2_E1  &&  j < minutes_state_3_E1 )
  {
    return ESTADO_2;
  }
  else return ESTADO_3;
}

void Save_NTP_Time(){
  
  year_esp = year();
  month_esp = month();
  day_esp = day();
  hours_esp = hour();
  minutes_esp = minute();
  secs_esp = second();
}

void Set_Luminaire_At_Level(String level){

  unsigned int time_between_levels = 15;   //3 segundos para pasar de un nivel a otro
  int multiplier = 1;
    
  unsigned int current_level = Get_Dimmer_Number(current_dimmer);
  unsigned int target_level  = Get_Dimmer_Number(level);

  unsigned int long aux_current_millis = millis();
  unsigned int long aux_previous_millis = millis();

  float diff_between_levels;
  int j;
  
  if(current_level > target_level)
  {
    diff_between_levels = current_level - target_level;
    multiplier = -1;
  }
  else
  {
    diff_between_levels = target_level - current_level;
    multiplier = 1;
  }

  aux_current_millis = millis();
        
  for(j=0; j<time_between_levels; j++)
  {
    aux_previous_millis = aux_current_millis;
    Serial.print("Cuenta: ");
    Serial.println(current_level + (j+1)*multiplier*(diff_between_levels/time_between_levels));
     
    analogWrite(PIN_PWM,  current_level + (j+1)*multiplier*(diff_between_levels/time_between_levels));
    
    while( (aux_current_millis - aux_previous_millis) < 200)
    {
      aux_current_millis = millis();
      wdt_reset();
    }    
  }

    current_dimmer = dimmer_target;
    consumo = Get_Consumo(current_dimmer);
}

unsigned int Get_Dimmer_Number(String Lum_Level){

  unsigned int aux = 0;  //Variable para que devuelva la funcion
  
  if(Lum_Level == "APAGADO")
  {
    aux = DIMMER_APAGADO;
  }
  else if(Lum_Level == "BAJO")
  {
    aux = DIMMER_BAJO;
  }
  else if(Lum_Level == "MEDIO")
  {
    aux = DIMMER_MEDIO;
  }
  else if(Lum_Level == "ALTO")
  {
    aux = DIMMER_ALTO;
  }
  else aux = 10;
                     
  return aux;                                    
}

void digitalClockDisplay()
{
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();
}

void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Serial.println("");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

bool Dimmer_Changed(){

  if(current_dimmer != dimmer_target)    return true;
  else return false;
}

void Imprimir_Hora(){

  Serial.print("Hora y fecha ESP actual: ");
  Serial.print(hours_esp);
  Serial.print(":");
  Serial.print(minutes_esp);
  Serial.print(":");
  Serial.print(secs_esp);
  Serial.print(" del ");
  Serial.print(day_esp);
  Serial.print("/");
  Serial.print(month_esp);
  Serial.print("/");
  Serial.println(year_esp);
  Serial.println();
}

void Imprimir_Estados(){

    Serial.print("ESTADO_1: comienzo a las ");
    Serial.print(hours_init_state_1);
    Serial.print(":");
    Serial.print(mins_init_state_1);
    Serial.println();
    Serial.print("ESTADO_2: comienzo a las ");
    Serial.print(hours_init_state_2);
    Serial.print(":");
    Serial.print(mins_init_state_2);
    Serial.println();
    Serial.print("ESTADO_3: comienzo a las ");
    Serial.print(hours_init_state_3);
    Serial.print(":");
    Serial.print(mins_init_state_3);
    Serial.println();
    Serial.print("Estado actual: ");

    current_lum_state = Get_Luminaria_State();
    switch(current_lum_state){

      case ESTADO_1:  Serial.println("ESTADO_1");
                      break;
      case ESTADO_2:  Serial.println("ESTADO_2");
                      break;
      case ESTADO_3:  Serial.println("ESTADO_3");
                      break;                                
    }
    Serial.println();
}

void Identificar(){
  
  unsigned long int _previous_millis = millis();
  unsigned int i=0;
  String target_level = "APAGADO";
  
  while(i < 5){
    
    Set_Luminaire_At_Level(target_level);

    if(millis() - _previous_millis > 5000)
    {
      if(target_level == "APAGADO")
      {
        target_level = "ALTO";
      }
      else if(target_level == "ALTO")
      {
        target_level = "APAGADO";
      }

    i++;
    _previous_millis = millis();
    }
  }
}

void Tomar_Configuraciones(){

  configFile = SPIFFS.open(JSON_file, "r");
  delay(500);
  if (!configFile) {
  Serial.println("Failed to open config file");
  return;
  }

  size_t size = configFile.size();
  if (size > 1024) {
  Serial.println("Config file size is too large");
  return;
  }

  DeserializationError error = deserializeJson(jsonBuffer_general, configFile);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }

  const char *aux = jsonBuffer_general["Modo_Manual"];
  modo_manual = String(aux);
  
  aux = jsonBuffer_general["Modo_Automatico"];
  modo_automatico = String(aux);

  aux = jsonBuffer_general["Dimmer"];
  current_dimmer = String(aux);
  
  aux = jsonBuffer_general["Luz_Defecto_Sin_Mov"];
  luz_defecto_sin_mov = aux;
  aux = jsonBuffer_general["Luz_Con_Mov"];
  luz_con_mov = aux;

  unsigned int aux2 = jsonBuffer_general["Horas_Inicio_Estado_1"];
  hours_init_state_1 = aux2;
  aux2 = jsonBuffer_general["Minutos_Inicio_Estado_1"];
  mins_init_state_1 = aux2;
  aux2 = jsonBuffer_general["Horas_Inicio_Estado_2"];
  hours_init_state_2 = aux2;
  aux2 = jsonBuffer_general["Minutos_Inicio_Estado_2"];
  mins_init_state_2 = aux2;
  aux2 = jsonBuffer_general["Horas_Inicio_Estado_3"];
  hours_init_state_3 = aux2;
  aux2 = jsonBuffer_general["Minutos_Inicio_Estado_3"];
  mins_init_state_3 = aux2;

  aux = jsonBuffer_general["Estado_1_Reposo"];
  Estado_1_Reposo = aux;
  aux = jsonBuffer_general["Estado_1_Mov"];
  Estado_1_Mov = aux;
  aux = jsonBuffer_general["Estado_2_Reposo"];
  Estado_2_Reposo = aux;
  aux = jsonBuffer_general["Estado_2_Mov"];
  Estado_2_Mov = aux;
  aux = jsonBuffer_general["Estado_3_Reposo"];
  Estado_3_Reposo = aux;
  aux = jsonBuffer_general["Estado_3_Mov"];
  Estado_3_Mov = aux;

  aux2 = jsonBuffer_general["Tiempo_Luz_Mov"];
  tiempo_luz_mov = 1000*aux2;

  const char *aux3 = jsonBuffer_general["server"];
  memcpy(server_ip,aux3,strlen(aux3));
  server_ip[strlen(aux3)] = '\0';

  /*const char *aux4 = jsonBuffer_general["coordinador"];
  memcpy(coordinador,aux4,strlen(aux4));
  coordinador[strlen(aux4)] = '\0';*/
 
  configFile.close();

  Serial.println("Todas las configuraciones tomadas");
  Serial.println("");
}

void Imprimir_Configuraciones(){

  Serial.println("Configuraciones del dispositivo");
  Serial.print("Nombre de dispositivo: ");
  Serial.println(device);
  Serial.print("Modo: ");

  if(modo_manual == "SI")
  {
    Serial.println("Manual");
  }
  else
  {
    Serial.println("Automatico");
    Serial.print("Modo automatico: ");

    if(modo_automatico == "SENSOR")
    {
      Serial.println("Sensor"); 
    }
    else Serial.println("Estados");
  }

  Serial.print("Server_IP: ");
  Serial.println(server_ip);
  Serial.print("Coordinador: ");
  Serial.println(coordinador);
  Serial.println("");
  Serial.println("");
}

void Tomar_Configuraciones_WiFi(){
    
    Serial.println("Tomando configuraciones de WiFi guardadas en archivo");
    if(SPIFFS.exists("/wifi.json"))
    {

      File configFile = SPIFFS.open("/wifi.json", "r");
      if (!configFile) {
        Serial.println("Failed to open config file");
      }
      //else size_t size = configFile.size();

      StaticJsonDocument<1024> jsonBuffer;
      DeserializationError error = deserializeJson(jsonBuffer, configFile);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        //return;
      }
      configFile.close();
      //serializeJsonPretty(jsonBuffer, Serial);

      _ssid = jsonBuffer["ssid"];
      _pass = jsonBuffer["password"];
      Serial.println("Configuración recuperada: ");
      Serial.print("SSID: ");
      Serial.print(_ssid);
      Serial.print(" password: ");
      Serial.println(_pass);
    }
}

void Handle_Light(){

  if(modo_automatico == "SENSOR")
  {
    if(encender_luminaria)
    {
       dimmer_target = luz_con_mov;
       previous_millis_light = millis();
       encender_luminaria = false;
    }
    else
    {
      if(millis() - previous_millis_light > tiempo_luz_mov)
      {
        dimmer_target = luz_defecto_sin_mov;
      }
    }
  }
  else if(modo_automatico == "ESTADOS") 
  {
    current_lum_state = Get_Luminaria_State();
    if(current_lum_state == ESTADO_1)
    {
      if(encender_luminaria)
      {
        dimmer_target = Estado_1_Mov;
        previous_millis_light = millis();
        encender_luminaria = false;
      }
      else
      {
        if(millis() - previous_millis_light > tiempo_luz_mov)
        {
          dimmer_target = Estado_1_Reposo;
        }  
      }
    }
    else if(current_lum_state == ESTADO_2)
    {
      if(encender_luminaria)
      {
        dimmer_target = Estado_2_Mov;
        previous_millis_light = millis();
        encender_luminaria = false;
      }
      else
      {
        if(millis() - previous_millis_light > tiempo_luz_mov)
        {
          dimmer_target = Estado_2_Reposo;
        }  
      }
    }
    else if(current_lum_state == ESTADO_3)
    {
      if(encender_luminaria)
      {
        dimmer_target = Estado_3_Mov;
        previous_millis_light = millis();
        encender_luminaria = false;
      }
      else
      {
        if(millis() - previous_millis_light > tiempo_luz_mov)
        {
          dimmer_target = Estado_3_Reposo;
        }  
      }
    }
  }
}

void Enviar_Datos_Coordinador(){

  HTTPClient http;

  String destination = "http://";
  destination += coordinador;
  destination += "/receive_msg";
  http.begin(destination);

  http.addHeader("Content-Type", "text/plain");

  String message = "{\"device\":";
  message += device;
  message += ",";
  message += "\"consumo\":";
  message += consumo;
  message += "}";
  

  int httpCode = http.POST(message);   //Send the request
  if(httpCode < 0)
  {
    Serial.print("Error: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }
  
  /*else  //Funcion del orto la concha de tu madre
  {
    String payload = http.getString();                  //Get the response payload
  }*/
 
  Serial.println(httpCode);   //Print HTTP return code
  //Serial.println(payload);    //Print request response payload
 
  http.end();  //Close connection
}

unsigned int Get_Consumo(String current_dimmer){

  if(current_dimmer == "APAGADO")
  {
    return 0;
  }
  else if(current_dimmer == "BAJO")
  {
    return 10;
  }
  else if(current_dimmer == "MEDIO")
  {
    return 25;
  }
  else if(current_dimmer == "ALTO")
  {
    return 40;
  }
}

void Guardar_en_Memoria() {

  configFile = SPIFFS.open(JSON_file, "w");
  delay(500);
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  if (serializeJson(jsonBuffer_general, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
    return;
  }
  
  configFile.close();
      
  //thing.call_endpoint("email");
}
