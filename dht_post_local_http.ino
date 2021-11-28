#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
//#include <Dns.h>
#include "espconn.h"
#include <MQTTClient.h>  // https://cables.gl/docs/3_communication/mqtt_arduino/mqtt_arduino
#include <PubSubClient.h> // also MQTT https://iotdesignpro.com/projects/how-to-connect-esp8266-with-mqtt

#include "credentials.h"; // WLAN / MQTT user credentials

#define ONE_HOUR 3600000UL

#define measurement_interval_millis 600000
#define light_duration_millis 0
#define wifilimit 20 // seconds
#define subnet 4     // cannot collide with subnet on which it connects via the AP
#define human_name "Guestroom ESP8266 Web Interface"
//#define subnet 2     // cannot collide with subnet on which it connects via the AP
//#define human_name "Livingroom ESP8266 Web Interface"
//#define subnet 6     // cannot collide with subnet on which it connects via the AP
//#define human_name "Bedroom ESP8266 Web Interface"

//#define subnet 11     // cannot collide with subnet on which it connects via the AP
//#define human_name "Bonsai ESP8266 Web Interface"

String observable = "Temperature";
//String observable = "Soil";

String PARAM_INPUT_1 = "input1"; // the value that I control from the web interface
unsigned long update_frequency = measurement_interval_millis;
unsigned long flash_duration = light_duration_millis;
boolean globally_ignore_ntp = false;


// ################ MQTT setup ################
MQTTClient client;


// ################ Http Server ################
ESP8266WebServer server(80); // Create a webserver object that listens for HTTP request on port 80
// *************************************

// ################ FS ################
File fsUploadFile; // a File variable to temporarily store the received file
// *************************************

// ################ wifiMulti ################
ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

// *************************************

// ################ FS ################
const char *mdnsName = "esp8266"; // Domain name for the mDNS responder
// *************************************

// ################ UDP ################
WiFiUDP UDP; // Create an instance of the WiFiUDP class to send and receive UDP messages
// *************************************

// ################ NTP ################

//  IPAddress timeServer;
//
//  DNSClient dns;
//  dns.begin(Ethernet.dnsServerIP());
//
//  if(dns.getHostByName("pool.ntp.org",timeServer) == 1) {
//    Serial.print(F("ntp = "));
//    Serial.println(timeServer);
//  }
//  else Serial.print(F("dns lookup failed"));
IPAddress DNS_IP(8, 8, 8, 8);

IPAddress timeServerIP; // The time.nist.gov NTP server's IP address
//const char* ntpServerName = "192.168.114.1";
//const char *ntpServerName = "132.163.96.4"; //"time.nist.gov"; //"pool.ntp.org"; // "185.19.184.35";
//const char *ntpServerName = "time.nist.gov";
//const char* ntpServerName = "185.19.184.35";     //"it.pool.ntp.org";
//const char* ntpServerName = "162.159.200.1";   //"pool.ntp.org"; //  162.159.200.1
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48;     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; // A buffer to hold incoming and outgoing packets
// *************************************

char *make_APSSID(int _subnet)
{
  char apssid[24];
  char basenameSSID[9] = "DAQnet2";
  sprintf(apssid, "%s %i", basenameSSID, _subnet);
  return apssid;
}

//sprintf(apssid,"%s %i",basenameSSID,subnet);
//char[] apssid="DAQnet-4";  // I will broadcast the ssid name of the network
char *apssid = make_APSSID(subnet);

// if WiFi.mode(WIFI_AP_STA); and WiFi.softAP

// ################ DHT ################
#include <DHT.h>
#include <DHT_U.h>
// DHT 11 sensor
#define DHTPIN 5
#define DHTTYPE DHT22
// DHT sensor
DHT dht(DHTPIN, DHTTYPE, 15);
// *************************************

void startWiFi()
{
  boolean connect_as_client = true; // if false: try to make AP and not connect to any network
                                    // if true: make an AP (as emergency measure), get an IP as client, make an AP with the IP as ESSID

  // ######## EMERGENCY AP #########
  WiFiClient clientAP; // Client name for AP mode
  WiFi.mode(WIFI_AP_STA);
  IPAddress AP_local_IP(192, 168, subnet, 1);
  IPAddress AP_gateway(192, 168, subnet, 1);
  IPAddress AP_subnet(255, 255, 255, 0);
  WiFi.softAPConfig(AP_local_IP, AP_gateway, AP_subnet);

  boolean launched_AP = WiFi.softAP(apssid, appassword, 1, 0); // Start AP mode
  // void softAP(const char* ssid, const char* passphrase, int channel = 1, int ssid_hidden = 0);
  if (launched_AP == true)
  {
    Serial.println("AP Ready");
    Serial.print("AP IP = ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("AP Failed!");
  }

  if (launched_AP == false)
  {
    connect_as_client = true;
  }


  // ######## REGULAR CLIENT CONNECTION TO AP #########
  if (connect_as_client)
  {
    WiFiClient clientSTA; // client mod for station mod
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password); // Connect to the network
    //IPAddress ip(192,168,114,200);   // Comment this line to get an IP via DHCP
    //IPAddress gateway(192,168,114,1);
    //IPAddress subnet(255,255,255,0);
    //WiFi.config(ip, gateway, subnet);
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println(" ...");

    int i = 0;
    while (WiFi.status() != WL_CONNECTED)
    { // Wait for the Wi-Fi to connect
      delay(1000);
      Serial.print(++i);
      Serial.print(' ');
      if (i > 20)
      {
        Serial.println('Could not connect for 20 seconds; going without internet');
        break;
      }
    }

    Serial.println('\n');
    Serial.println("Connection established!");
    Serial.print("IP address:\t");
    Serial.println(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS-0:");
    Serial.println(WiFi.dnsIP(0));
    Serial.print("DNS-1: ");
    Serial.println(WiFi.dnsIP(1));

    Serial.println("...........................");



    // void startMQTT() {
    // setup MQTT
    Serial.print("Connecting to MQTT-server "); Serial.println(mqtt_server);
    client.begin(mqtt_server, clientSTA);
    while (!client.connect(device_name, mqtt_username, mqtt_password)) {
        Serial.print(".");
        delay(1000);
    }
    //}

    // #######  NEW AP ??? #######

    //IPAddress dns1(8, 8, 8, 8);  //Google dns
    //IPAddress dns2(8, 8, 4, 4);  //Google dns
    // print your WiFi shield's IP address:
    //WiFi.setDNS(dns1);
    // Serial.println("Manual DNS configured: "); Serial.println(WiFi.dnsIP(0) );

    Serial.println("Launching new Access Point"); // Send the IP address of the ESP8266 to the computer

    //    char* make_APSSID_IP(int _subnet, char* IP){
    //      char apssid[24];
    //      char basenameSSID[9] = "DAQnet";
    //      sprintf(apssid,"%s %i %s",basenameSSID,_subnet,IP);
    //      return apssid;
    //    }

    IPAddress ip = WiFi.localIP();

    char apssidIP[24];
    sprintf(apssidIP, "%i@%d.%d.%d.%d", subnet, ip[0], ip[1], ip[2], ip[3]);
    Serial.println(apssidIP);
    launched_AP = WiFi.softAP(apssidIP, appassword, 1, 0); // Start AP mode
                                                           // void softAP(const char* ssid, const char* passphrase, int channel = 1, int ssid_hidden = 0); (0 = broadcast SSID, 1 = hide SSID)
    if (launched_AP == true)
    {
      Serial.println("AP Ready");
      Serial.print("AP IP = ");
      Serial.println(WiFi.softAPIP());
    }
    else
    {
      Serial.println("AP Failed!");
    }
  }
}




void startUDP()
{
  Serial.println("Starting UDP");
  UDP.begin(123); // Start listening for UDP messages to port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
}

void startOTA()
{ // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

String listAllFilesNameSize()
{
  String human_output = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next())
  { // List the file system contents
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();

    String _fileName = fileName;
    _fileName.replace("/", "");
    human_output += fileName + "\t" + formatBytes(fileSize) + " <a href=\"" + fileName + "\">DOWNLOAD</a>" + "    <a href=\"rm?file=" + _fileName + "\">rm</a>" + "\r\n <br>";
    Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
  }
  Serial.printf("\n");
  return human_output;
}

void startSPIFFS()
{                 // Start the SPIFFS and list all contents
  SPIFFS.begin(); // Start the SPI Flash File System (SPIFFS)
  //SPIFFS.remove("/temp.csv");
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    { // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

void startMDNS()
{                       // Start the mDNS responder
  MDNS.begin(mdnsName); // start the multicast domain name server
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startServer()
{ // Start a HTTP server with a file read handler and an upload handler

  char *_apssid = human_name; //make_APSSID(subnet);

  // *)
  server.on(
      "/edit.html", HTTP_POST, []() { // If a POST request is sent to the /edit.html address,
        server.send(200, "text/plain", "");
      },
      handleFileUpload); // go to 'handleFileUpload'

  // *)
  //server.on("/", HTTP_GET, handleRoot  ); // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/", HTTP_GET, [_apssid]() {
    Serial.print(_apssid);
    handleRoot(_apssid);
    Serial.print("Will make the root html content for : " + String(_apssid));
  }); // Call the 'handleRoot' function when a client requests URI "/"

  // *)
  server.on("/clear", HTTP_GET, handleClear); // Call the 'handleClear' function when a POST request is made to URI "/clear"

  server.on("/setting", HTTP_GET, handleSetting); // Call the 'handleClear' function when a POST request is made to URI "/clear"

  // *)
  server.on("/rm", HTTP_GET, handleRemoveFile); // Call the 'handleClear' function when a POST request is made to URI "/rm?file=temp.csv"

  server.on("/delete", []() { // []() makes a lambda and the local variables in [] become accessible to it
                              // https://en.cppreference.com/w/cpp/language/lambda
                              // https://forum.arduino.cc/index.php?topic=598441.0
    String file_name = server.arg("rm");
    Serial.print("Will remove: " + file_name);

  });

  // *)
  server.onNotFound(handleNotFound); // if someone requests any other file or page, go to function 'handleNotFound'
  // and check if the file exists

  server.begin(); // start the HTTP server
  Serial.println("*** HTTP server started.");
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri()))
  { // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

void handleRoot(char *apssid)
{ // When URI / is requested, send a web page with a button to perform the actions

  String download_string = "<form method=\"get\" action=\"temp.csv\" download><button type=\"submit\">Download temp.csv</button></form>";
  String clear_string = "<form method=\"GET\" action=\"clear\"><input type=\"submit\" value=\"Clear temp.csv\"></form>";
  String list_of_files = listAllFilesNameSize();
  char *title = apssid;
  //sprintf(title,"<h1> %s </h1>",apssid);
  String _title;
  _title = "<h1>" + String(title) + "</h1>";

  String string_subnet = String(subnet);
  String sub_title = "<h2>Also available on private subnet: " + string_subnet + "</h2>";
  String flashing_notice = "This device flashes for " + String(flash_duration) + " milliseconds";
  String form = "<br><form action=\"/setting\"> Update frequency (ms): <input type=\"text\" name=\"frequency\" value=\"" + String(update_frequency) + "\">  <input type=\"submit\" value=\"Submit\"><br>";
  String form2 = "LED flash duration (ms): <input type=\"text\" name=\"flash\" value=\"" + String(flash_duration) + "\">  <input type=\"submit\" value=\"Submit\"></form><br>";

  server.send(200, "text/html", _title + sub_title + flashing_notice + "<h3>Actions</h3>" + download_string + " " + clear_string + "<br>" + "<h3>Files</h3>" + list_of_files + form + form2);
}

bool handleRemoveFile()
{ // If a POST request is made to URI /clear
  String path;
  path = "/" + server.arg("file");

  Serial.println("handleClear: " + path);
  if (SPIFFS.exists(path))
  {
    if (SPIFFS.remove(path))
    {
      server.send(200, "text/html", "Done");
      return true;
    }
  }
  else
  {
    Serial.println("not found: " + path);
    server.send(200, "text/html", "Not found:" + path);
    return false;
  }
}

void handleSetting()
{ // If a POST request is made to URI /clear
  String value;
  value = server.arg("frequency");
  Serial.println("handleSetting(" + value + ")");
  Serial.println("update_frequency: " + String(update_frequency));
  update_frequency = value.toInt(); // atof?
  Serial.println("update_frequency: " + String(update_frequency));

  value = server.arg("flash");
  Serial.println("handleSetting(" + value + ")");
  Serial.println("flash_duration: " + String(flash_duration));
  flash_duration = value.toInt(); // atof?
  Serial.println("flash_duration: " + String(flash_duration));

  server.send(200, "text/plain", "New measurement frequency: " + String(update_frequency) + " milliseconds. \n " + "New LED flash duration: " + String(flash_duration) + " milliseconds.");
  // will use `value` to update the parameter I want to change
  //return true;
}

bool handleClear()
{ // If a POST request is made to URI /clear
  String path;
  path = "/temp.csv";
  Serial.println("handleClear: " + path);
  if (SPIFFS.exists(path))
  {
    if (SPIFFS.remove(path))
    {
      server.send(200, "text/html", "Done");
      return true;
    }
  }
  else
  {
    Serial.println("not found: " + path);
    server.send(200, "text/html", "Not found:" + path);
    return false;
  }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/"))
    path += "index.html";                    // If a folder is requested, send the index file
  String contentType = getContentType(path); // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  {                                                     // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                      // If there's a compressed version available
      path += ".gz";                                    // Use the compressed verion
    File file = SPIFFS.open(path, "r");                 // Open the file
    size_t sent = server.streamFile(file, contentType); // Send it to the client
    file.close();                                       // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path); // If the file doesn't exist, return false
  return false;
}

void handleFileUpload()
{ // upload a new file to the SPIFFS
  HTTPUpload &upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START)
  {
    path = upload.filename;
    if (!path.startsWith("/"))
      path = "/" + path;
    if (!path.endsWith(".gz"))
    {                                   // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz"; // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))    // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: ");
    Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w"); // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (fsUploadFile)
    {                       // If the file was successfully created
      fsUploadFile.close(); // Close the file again
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html"); // Redirect the client to the success page
      server.send(303);
    }
    else
    {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes)
{ // convert sizes in bytes to KB and MB
  if (bytes < 1024)
  {
    return String(bytes) + "B";
  }
  else if (bytes < (1024 * 1024))
  {
    return String(bytes / 1024.0) + "KB";
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename)
{ // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  return "text/plain";
}

unsigned long getTime()
{ // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  if (UDP.parsePacket() == 0)
  { // If there's no response (yet)
    return 0;
  }

  UDP.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (packetBuffer[40] << 24) | (packetBuffer[41] << 16) | (packetBuffer[42] << 8) | packetBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

unsigned long getInterval()
{ // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  return update_frequency;
}

unsigned long getDuration()
{ // Check if the time server has responded, if so, get the UNIX time, otherwise, return 0
  return flash_duration;
}

void sendNTPpacket(IPAddress &address)
{
  Serial.println("Sending NTP request");
  memset(packetBuffer, 0, NTP_PACKET_SIZE); // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011; // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(packetBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

/*__________________________________________________________SENSORS_FUNCTIONS__________________________________________________________*/

String readDHTstring()
{

  String temp_hum;
  // Grab the current state of the sensor
  float humidity_data = (float)dht.readHumidity();
  float temperature_data = (float)dht.readTemperature();
  bool write_always = false;
  if (write_always || (false == isnan(temperature_data) && false == isnan(humidity_data) && humidity_data < 100.0 && temperature_data < 500.00))
  {
    //timeLastCheck = timeNow;

    //float temp = dht.readTemperature();
    //float humi = dht.readHumidity();

    String string_temp = String(temperature_data, 2);
    temp_hum = string_temp;
    String string_humi = String(humidity_data, 2);
    temp_hum += "," + string_humi;

    String stringOut = String(string_temp + " ˚C " + string_humi + "%");

    //char*  output = strcat("Temperature",str(temp));

    Serial.println(stringOut);

    //Serial.println(F("Humidity"));
    //Serial.println(humi);
    //server.handleClient();
  }
  return temp_hum;
}

String readSoil() {
  float moisture_percentage;

  float analogValue = analogRead(A0); // read the analog signal
  float maxvalue = 1024.0;

  moisture_percentage = ( 100.00 - ( (analogValue/maxvalue) * 100.00 ) );

  float air=946;
  float water=640;

  Serial.print("Soil Moisture(base-"); Serial.print(int(maxvalue)); Serial.print(" in Percentage) = ");
  Serial.print(moisture_percentage);
  Serial.println("%");
  
  Serial.print("{\"water\":"); Serial.print(water);Serial.print("}");
  Serial.print("{\"air\":"); Serial.print(air);Serial.print("}");
  Serial.print("{\"this soil\":"); Serial.print(analogValue);Serial.print("}");Serial.println("");
  
  Serial.print("Soil Moisture(base-"); Serial.print(int(water));Serial.print(";");Serial.print(int(maxvalue)); Serial.print(" in Percentage) = ");
  Serial.print(100*(analogValue - maxvalue)/(water - maxvalue ));Serial.println("%");

   return String(analogValue);
}


/*__________________________________________________________SETUP__________________________________________________________*/

void setup()
{

  pinMode(LED_BUILTIN, OUTPUT); // Initialize the LED_BUILTIN pin as an output
  // Init sensor
  dht.begin();

  // put your setup code here, to run once:
  Serial.begin(115200); // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println('\n');
  if (  !globally_ignore_ntp  ) {
    WiFi.hostByName(ntpServerName, timeServerIP); // Get the IP address of the NTP server
  }
  startWiFi(); // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection

  startOTA(); // Start the OTA service

  startSPIFFS(); // Start the SPIFFS and list all contents

  startMDNS(); // Start the mDNS responder

  startServer(); // Start a HTTP server with a file read handler and an upload handler

  //startMQTT();

  startUDP(); // Start listening for UDP messages to port 123

  //Serial.print("Trying to get the IP address of the NTP server server IP:\t");
  //IPAddress DNS_IP( 8, 8, 8, 8 );
  //espconn_dns_setserver(0, DNS_IP);//to set the primary DNS to 8.8.8.8
  //Serial.print("Manually set DNS: ");
  //WiFi.dnsIP(0).printTo(Serial); //to read the primary DNS
  //WiFi.hostByName(ntpServerName, timeServerIP); // Get the IP address of the NTP server
  //Serial.print("Time server IP:\t");
  //Serial.println(timeServerIP);
  if (!globally_ignore_ntp)
  {
    sendNTPpacket(timeServerIP);
  }
  delay(500);
}

/*_______________________________________________________PRE-LOOP__________________________________________________________*/

const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

//const unsigned long intervalTemp = 60000;   // Do a temperature measurement every minute 60K millis
//const unsigned long intervalTemp = 180000;   // Do a temperature measurement every 30 minutes
//const unsigned long intervalTemp = measurement_interval_millis;   // Do a temperature measurement every 30 minutes
//const
//unsigned long intervalTemp = update_frequency;   // Do a temperature measurement every 30 minutes

unsigned long prevTemp = 0;
bool tmpRequested = false;
const unsigned long DS_delay = 750; // Reading the temperature from the DS18x20 can take up to 750ms

uint32_t timeUNIX = 0; // The most recent timestamp received from the time server

int old_connected = 0;

void led_on(int millisec)
{
  digitalWrite(LED_BUILTIN, LOW);  // Turn the LED on
                                   // (Note that LOW is the voltage level  but actually the LED is on; this is because  it is acive low on the ESP-01)
  delay(millisec);                 // Wait for millisec millisecond
  digitalWrite(LED_BUILTIN, HIGH); // Turn the LED off by making the voltage HIGH
}

/*__________________________________________________________LOOP__________________________________________________________*/

void loop()
{

  int refused_ntp=0;
  digitalWrite(LED_BUILTIN, HIGH); // Turn the LED off by making the voltage HIGH

  unsigned long intervalTemp = getInterval();
  unsigned long LED_flash_duration = getDuration();

  unsigned long currentMillis = millis();
  boolean ignore_ntp = globally_ignore_ntp; // ### IF NTP is not available it can be skipped altogether

  if (currentMillis - prevNTP > intervalNTP)
  { // Request the time from the time server every hour
    Serial.print(intervalNTP);
    Serial.print("  elapsed. Sending NTP packet at t=");
    Serial.println(currentMillis);
    prevNTP = currentMillis;
    if (!globally_ignore_ntp && !ignore_ntp)
    {
      sendNTPpacket(timeServerIP);
    }
    else
    {
      Serial.println("...NTP refesh skipped");
    }
  }

  uint32_t time = getTime(); // Check if the time server has responded, if so, get the UNIX time

  
  if (time)
  {

    float unixYears = time / 3.1415927 / 10000000;
    if (unixYears > 50 && unixYears < 55)
    {
      timeUNIX = time;
      Serial.print("(getTime)-\t");
      Serial.print("NTP response:\t");
      Serial.println(timeUNIX);
      lastNTPResponse = millis();
    }
    else
    {
      Serial.print("*** NTP ");
      Serial.print(time);
      Serial.println(" was rejected ***");
    }
  }
  else if ((millis() - lastNTPResponse) > 24UL * ONE_HOUR)
  {
    Serial.println("More than 24 hours since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  if (timeUNIX != 0 || ignore_ntp)
  {
    /// *************************
    /// carry out the measurement
    /// *************************
    if (currentMillis - prevTemp > intervalTemp)
    { // Every intervalTemp, request the temperature
      //      tempSensors.requestTemperatures(); // Request the temperature from the sensor (it takes some time to read it)
      //      tmpRequested = true;
      prevTemp = currentMillis;

      if (LED_flash_duration > 0)
      {
        led_on(LED_flash_duration);
      }

      //String observable = "Temperature";

      //void carry_out_measurement(String observable) {
      //}
      
      Serial.println(observable+" requested");
      //    }
      //if (currentMillis - prevTemp > DS_delay && tmpRequested) { // 750 ms after requesting the temperature
      uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
      // The actual time is the last NTP time plus the time that has elapsed since the last NTP response
      //tmpRequested = false;

      String obs;
      if (observable=="Temperature") {
          // ******* Get the temperature from the sensor *******
          String temp = readDHTstring(); // Get the temperature from the sensor
          obs = temp;
          // *****************************************************************
      }

      if (observable=="Soil") {
        String soil = readSoil();
        obs = soil;
      }

      Serial.printf("Appending temperature to file: %lu,", actualTime);
      Serial.println(obs);
      File tempLog = SPIFFS.open("/temp.csv", "a"); // Write the time and the temperature to the csv file
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.println(obs);
      tempLog.close();
      tempLog = SPIFFS.open("/last_temp.csv", "w+"); // Write the time and the temperature to the csv file
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.println(obs);
      tempLog.close();
      listAllFilesNameSize();

      
    }
  }
  else
  {
    // If we didn't receive an NTP response yet, send another request
    if (!globally_ignore_ntp && !ignore_ntp)
    {
      
      Serial.print("We didn't receive an NTP response yet, sending a new request. This is n.");
      Serial.print(refused_ntp);
      Serial.print(" to ");
      Serial.println(timeServerIP);
      sendNTPpacket(timeServerIP);
      delay(1500);
      refused_ntp++;
    }
    else
    {
      Serial.println("...Skipped");
    }
  }

  server.handleClient(); // run the server
  ArduinoOTA.handle();   // listen for OTA events

  int new_connected = WiFi.softAPgetStationNum();
  if (new_connected != old_connected)
  {
    Serial.printf("Stations connected to soft-AP = %d\n", new_connected); // count connected wifi clients
    old_connected = new_connected;
  }
  //delay(intervalTemp);
} //end of loop
