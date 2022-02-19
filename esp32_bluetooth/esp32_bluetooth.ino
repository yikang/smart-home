
/* 
 *  Project for Henry Garage
 *  Bluetooth control enabled 
 *   - send command 
 *   - set wifi password
 *  
 *  web server with Image and with button to control
 *  
 *  -- command:  
 *      x: to open/close 
 *      w: set up wifi 
 *      q: echo wifi information
*/ 

#include "BluetoothSerial.h"
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h"

#define PART_BOUNDARY "123456789000000000000987654321"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// camera 
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

BluetoothSerial SerialBT;
int received;// received value will be stored in this variable
char receivedChar;// received value will be stored as CHAR in this variable

String ssid = ""; 
String password = "";

// command
const char pressWifi='w'; // enter wifi information
const char pressOn ='x'; // open door 
const char pressQ = 'q'; //
int flagOn = 0; 
const int LEDpin = 3 ; //33:internal board led, 3 is output

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->width > 400){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
    //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
  }
  return res;
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  //Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  }
}



void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_HenryGarage"); //Bluetooth device name
  Serial.println("The device started, now you can pair it with bluetooth!");
  pinMode(LEDpin, OUTPUT);

   camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    SerialBT.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  
}

void control_pin(){
    if(flagOn == 0){
        SerialBT.println("Turn OFF:");// write on BT app
        Serial.println("LED OFF:");//write on serial monitor
        digitalWrite(LEDpin, LOW);// turn the LED off 
        flagOn =  1 ; 
      }else{
         SerialBT.println("Turn ON:");// write on BT app
         Serial.println("LED ON:");//write on serial monitor
         digitalWrite(LEDpin, HIGH);// turn the LED ON
         flagOn = 0;
      }
}

void init_wifi(){
  
      SerialBT.println("enter wifi ssid:");
       
       ssid  = SerialBT.readString(); 
       while(1){
         ssid = SerialBT.readString();
         if(ssid!="") { break; }
       }
       
       SerialBT.println("you entered:");
       ssid.trim();
       SerialBT.println(ssid);

       SerialBT.println("entner password:"); 
       while(1){
         password = SerialBT.readString();
         if(password!="") { break; }
       }
       SerialBT.println("you entered:");
       password.trim();
       SerialBT.println(password);
       
       //const char* ssid = "home39-2G";
       //const char* password = "euler@math";
       WiFi.begin(ssid.c_str(), password.c_str());
       int ct = 0; 
       while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          SerialBT.print(".");
          ct = ct + 1;
          if(ct >100) 
          {
            SerialBT.print("wifi connect timout!");
            break;
          }
        }
        if(WiFi.status() == WL_CONNECTED)
        {
           SerialBT.println("");
           SerialBT.println("WiFi connected");
        
           SerialBT.print("Camera Stream Ready! Go to: http://");
           SerialBT.print(WiFi.localIP());
           startCameraServer();
        }
}

void wifi_echo(){
  
       if(WiFi.status() == WL_CONNECTED){
           SerialBT.println("WiFi connected");
           SerialBT.println("ssid:");
           SerialBT.println(ssid);
           SerialBT.println("password:");
           SerialBT.println(password);
           SerialBT.print(WiFi.localIP());
        }else{
          SerialBT.println("WiFi not connected");
        }
}

void loop() {
  
   receivedChar =(char)SerialBT.read();

  if (Serial.available()) {
    SerialBT.write(Serial.read());
  
  }
  
  if (SerialBT.available()) {
    
    SerialBT.print("Received:");// write on BT app
    SerialBT.println(receivedChar);// write on BT app      
    //Serial.print ("Received:");//print on serial monitor
    //Serial.println(receivedChar);//print on serial monitor    
    if(receivedChar == pressOn)
    {
      control_pin();
    }
    
    if(receivedChar == pressWifi)
    {
      init_wifi();
    }

    if(receivedChar == pressQ){
      wifi_echo();
       
    }
  }
  delay(20);
  
}
