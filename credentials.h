//WiFi Connection configuration
const char *OTAName = "name";         // A name and a password for the OTA service
const char *OTAPassword = "password";
// *************************************
// ################ WiFi Client ################
const char* ssid     = "essid";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = "alongpassword";     

const char* appassword = "accespoint"; // I will broadcast the encryption of the network 

IPAddress mqttServer(192,168,x,x);
char *mqtt_server = "192.168.x.x";
char *mqtt_username = "user";
char *mqtt_password = "pass";

char *device_name = "DHT"; // can be freely set, e.g. your name
