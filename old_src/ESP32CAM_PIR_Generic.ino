/*************************************************************************************************************************************************
 *  TITLE: This sketch takes an image when motion is detected using a PIR sensor module that connected to pin GPIO 13 
 *  Watch the video to learn more. 
 *  YouTube Video: https://youtu.be/KTRwBBLEsXg
 *  by Tech StudyCell
 *************************************************************************************************************************************************/

/********************************************************************************************************************
 *  Preferences--> Aditional boards Manager URLs : https://dl.espressif.com/dl/package_esp32_index.json, http://arduino.esp8266.com/stable/package_esp8266com_index.json
 *  Board Settings:
 *  Board: "ESP32 Wrover Module"
 *  Upload Speed: "921600"
 *  Flash Frequency: "80MHz"
 *  Flash Mode: "QIO"
 *  Partition Scheme: "Hue APP (3MB No OTA/1MB SPIFFS)"
 *  Core Debug Level: "None"
 *  COM Port: Depends *On Your System*
 *  
 *  GPIO 0 must be connected to GND pin while uploading the sketch
 *  After connecting GPIO 0 to GND pin, press the ESP32 CAM on-board RESET button to put the board in flashing mode
 *********************************************************************************************************************/
 
#include "esp_camera.h"
#include "Arduino.h"
#include "FS.h"                // SD Card ESP32
#include "SD_MMC.h"            // SD Card ESP32
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"
#include <WiFi.h>
#include <EEPROM.h> 
//#include "ESP_Mail_Client.h"
#include <EMailSender.h>
#include <SPIFFS.h>
#include "ESP32_MailClient.h"

// read and write from flash memory
// define the number of bytes you want to access
#define EEPROM_SIZE 4
 
RTC_DATA_ATTR int bootCount = 0;

// Pin definition for CAMERA_MODEL_AI_THINKER
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
// Photo File Name to save in SPIFFS
#define FILE_PHOTO "photo.jpg"
#define FILE_PHOTO_SLASH "/photo.jpg"
// To send Email using Gmail use port 465 (SSL) and SMTP Server smtp.gmail.com
// YOU MUST ENABLE less secure app option https://myaccount.google.com/lesssecureapps?pli=1
#define emailSenderAccount    "emailSenderAccount@gmail.com"
#define emailSenderPassword   ""
#define smtpServer            "smtp.gmail.com"
#define smtpServerPort        465
#define emailSubject          "ESP32-CAM Photo Captured"
#define emailRecipient        "emailRecipient@gmail.com"
const char* ssid = "WIFI";
const char* password = "password";

// The Email Sending data object contains config and data to send
//SMTPSession smtpData;
//EMailSender(const char* email_login, const char* email_password, const char* email_from, const char* name_from, const char* smtp_server, uint16_t smtp_port );
EMailSender emailSend(emailSenderAccount, emailSenderPassword,emailRecipient,emailSubject,smtpServer,smtpServerPort); 
int pictureNumber = 0;
 bool ok = 0;
 // The Email Sending data object contains config and data to send
SMTPData smtpData;

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}
// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( camera_fb_t *fbp ) {
  camera_fb_t * fb = fbp; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly
  if (!SPIFFS.begin(true)) 
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    bool formatted = SPIFFS.format();
    if ( formatted ) 
    {
      Serial.println("SPIFFS formatted successfully");
    } 
    else 
    {
      Serial.println("Error formatting");
    }
    ESP.restart();
  }
  else 
  {
    delay(500);
    bool formatted = SPIFFS.format();
    if ( formatted ) 
    {
      Serial.println("SPIFFS formatted successfully");
    } 
    else 
    {
      Serial.println("Error formatting");
    }
    Serial.println("SPIFFS mounted successfully");
     // do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");
    //digitalWrite(4, HIGH);
    //fb = esp_camera_fb_get();
    //digitalWrite(4, LOW);
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
   // esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  //} while ( !ok );
  }
  
 
}
void sendEmailPhoto_via_Gmail( String pic_path, int storage_type) 
{
    String path_slash;
    EMailSender::EMailMessage message;
    message.subject = "ESP32 CAM email";
    message.message = "Ciao,  ESP32 CAM a detectat miscare ! /"+ pic_path;

    EMailSender::FileDescriptior fileDescriptor[1];
    fileDescriptor[0].filename = F(pic_path.c_str());
    path_slash = "/"+pic_path;
    fileDescriptor[0].url = F(path_slash.c_str());
    fileDescriptor[0].mime = "image/jpg";
    fileDescriptor[0].encode64 = true;
    fileDescriptor[0].storageType = EMailSender::EMAIL_STORAGE_TYPE_SPIFFS;

    EMailSender::Attachments attachs = {1, fileDescriptor};
   // emailSend.setSMTPPort(587);
    EMailSender::Response resp = emailSend.send(emailRecipient, message, attachs); //, attachs

    Serial.println("Sending status: ");
    Serial.println(resp.status);
    Serial.println(resp.code);
    Serial.println(resp.desc);
    
}
int sendPhoto( void ) {
  int ret_value = -1;
  // Preparing email
 // delay(1000);
  Serial.println("Sending email...");
  // Set the SMTP Server Email host, port, account and password
  smtpData.setLogin(smtpServer, smtpServerPort, emailSenderAccount, emailSenderPassword);
  
  // Set the sender name and Email
  smtpData.setSender("ESP32-CAM", emailSenderAccount);
  
  // Set Email priority or importance High, Normal, Low or 1 to 5 (1 is highest)
  smtpData.setPriority("High");

  // Set the subject
  smtpData.setSubject(emailSubject);
    
  // Set the email message in HTML format
  smtpData.setMessage("<h2>Photo captured with ESP32-CAM and attached in this email.</h2>", true);
  // Set the email message in text format
  //smtpData.setMessage("Photo captured with ESP32-CAM and attached in this email.", false);

  // Add recipients, can add more than one recipient
  smtpData.addRecipient(emailRecipient);
  //smtpData.addRecipient(emailRecipient2);

  // Add attach files from SPIFFS
  smtpData.addAttachFile(FILE_PHOTO_SLASH, "image/jpg");
  // Set the storage type to attach files in your email (SPIFFS)
  smtpData.setFileStorageType(MailClientStorageType::SPIFFS);

  smtpData.setSendCallback(sendCallback);
  delay(500);
  
  // Start sending Email, can be set callback function to track the status
  if (!MailClient.sendMail(smtpData))
  {
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());
    ret_value = 1;
  }
  else
  {
    ret_value = 2;
    Serial.println("Email sent! " );
  }
  // Clear all data from Email object to free memory
  smtpData.empty();
  return ret_value;
}
// Callback function to get the Email sending status
void sendCallback(SendStatus msg) {
  //Print the current status
  Serial.println(msg.info());
}
void camera_sensor_setup()
{
  sensor_t * s = esp_camera_sensor_get();
  s->set_contrast(s, 2);    //min=-2, max=2
  s->set_brightness(s, 2);  //min=-2, max=2
  s->set_saturation(s, 2);  //min=-2, max=2
//  s->set_brightness(s, 0);     // -2 to 2
//  s->set_contrast(s, 0);       // -2 to 2
//  s->set_saturation(s, 0);     // -2 to 2
//s->set_special_effect(s, 2); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
//s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
//s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
//s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
//s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
//s->set_aec2(s, 0);           // 0 = disable , 1 = enable
//s->set_ae_level(s, 0);       // -2 to 2
//s->set_aec_value(s, 300);    // 0 to 1200
//s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
//s->set_agc_gain(s, 0);       // 0 to 30
//s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
//s->set_bpc(s, 0);            // 0 = disable , 1 = enable
//s->set_wpc(s, 1);            // 0 = disable , 1 = enable
//s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
//s->set_lenc(s, 1);           // 0 = disable , 1 = enable
//s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
//s->set_vflip(s, 1);          // 0 = disable , 1 = enable
//s->set_dcw(s, 1);            // 0 = disable , 1 = enable
//s->set_colorbar(s, 0);       // 0 = disable , 1 = enable 
}  
void setup() {
  File file_spiffs;
  camera_fb_t * fb = NULL;
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  Serial.begin(115200);
 
  Serial.setDebugOutput(true);
 
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
  
  
  pinMode(4, INPUT);
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);
  rtc_gpio_hold_dis(GPIO_NUM_13);
 
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
 
  // Init Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

   //set the camera parameters
 // camera_sensor_setup();
  sensor_t * s = esp_camera_sensor_get();
  s->set_contrast(s, 2);    //min=-2, max=2
  s->set_brightness(s, 2);  //min=-2, max=2
  s->set_saturation(s, 1);  //min=-2, max=2
  s->set_vflip(s, 1);       // 0 = disable , 1 = enable
  s->set_hmirror(s, 1);     // 0 = disable , 1 = enable
  delay(100);               //wait a little for settings to take effect
 
  Serial.println("Starting SD Card");
 
  delay(500);
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    //return;
  }
 
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    //return;
  }
  
  // Take Picture with Camera
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    Serial.println("Exiting now"); 
    while(1);   //wait here as something is not right
  }
  
  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;
 
  // Path where new picture will be saved in SD Card
  String path = "/picture" + String(pictureNumber) +".jpg";
  String path_email = "picture" + String(pictureNumber) +".jpg";
  fs::FS &fs = SD_MMC;
  Serial.printf("Picture file name: %s\n", path.c_str());
 //create new file
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
    Serial.println("Exiting now"); 
   // while(1);   //wait here as something is not right
  }
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
  }
  file.close();
  
  delay(1000);
    // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
  pinMode(4, OUTPUT);  //GPIO for LED flash
  digitalWrite(4, LOW);  //turn OFF flash LED
  rtc_gpio_hold_en(GPIO_NUM_4);  //make sure flash is held LOW in sleep
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  //sendPhoto(path);
  if(WiFi.status() == WL_CONNECTED) 
  {
    //capturePhotoSaveSpiffs(fb);
    if (!SPIFFS.begin(true)) 
    {
      Serial.println("An Error has occurred while mounting SPIFFS");
      bool formatted = SPIFFS.format();
      if ( formatted ) 
      {
        Serial.println("SPIFFS formatted successfully");
      } 
      else 
      {
        Serial.println("Error formatting");
      }
      ESP.restart();
    }
    else 
    {
      delay(500);
      bool formatted = SPIFFS.format();
      if ( formatted ) 
      {
        Serial.println("SPIFFS formatted successfully");
      } 
      else 
      {
        Serial.println("Error formatting");
      }
      Serial.println("SPIFFS mounted successfully");
      // Take a photo with the camera
      Serial.println("Taking a photo...");
      //digitalWrite(4, HIGH);
      //fb = esp_camera_fb_get();
      //digitalWrite(4, LOW);
      if (!fb) {
        Serial.println("Camera capture failed");
        return;
      }
      // Photo file name
      Serial.printf("Picture file name: %s\n", FILE_PHOTO_SLASH);
      file_spiffs = SPIFFS.open(FILE_PHOTO_SLASH, FILE_WRITE);
      // Insert the data in the photo file
      if (!file_spiffs) 
      {
       Serial.println("Failed to open file in writing mode");
      }
      else 
      {
        file_spiffs.write(fb->buf, fb->len); // payload (image), payload length
        Serial.print("The picture has been saved in ");
        Serial.print(FILE_PHOTO);
        Serial.print(" - Size: ");
        Serial.print(file_spiffs.size());
        Serial.println(" bytes");
      }

      // esp_camera_fb_return(fb);
      // check if file has been correctly saved in SPIFFS
     
    }
   if( file_spiffs.size() >100)
   {
    (void)sendPhoto();
   }
  }
  // Close the file
  file_spiffs.close();
    
  esp_camera_fb_return(fb);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);
  Serial.println("Going to sleep now");
  delay(3000);
  esp_deep_sleep_start();
  Serial.println("Now in Deep Sleep Mode");
} 
 
void loop() {
 
}
