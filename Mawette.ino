/* ---------------- INCLUDES -------------------- */
#include <Audio.h>
#include <SPI.h>
#include <SD.h>
#include <ILI9341_t3.h>
#include <MFRC522.h>


/* ---------------- PIN DEFINES ----------------- */
//SPI
#define SPI_MOSI    11
#define SPI_SCLK    13
#define SPI_MISO    12

//TFT
#define TFT_DC      15
#define TFT_CS      10
#define TFT_RST     4
#define TFT_LED_PWR 19

//RFID
#define RFID_RST    17
#define RFID_CS     20

//SD
#define SD_CS       9

//AUDIO
#define AUDIO_PWR   0

/* --------------- OTHER DEFINES ---------------- */
//Buffer for BMP
#define BUFFPIXEL 240

/* --------------- Power Macros ----------------- */
#define AudioOff() digitalWrite(AUDIO_PWR,LOW)
#define AudioOn()  digitalWrite(AUDIO_PWR,HIGH)
#define RFIDOff()  digitalWrite(RFID_RST,LOW)
#define RFIDOn()   mfrc522.PCD_Init()
#define TFTOff()   digitalWrite(TFT_LED_PWR,LOW)
#define TFTOn()    digitalWrite(TFT_LED_PWR,HIGH)

/* ---------- Globals (Objects and Vars)--------- */ 
//TFT
ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC, TFT_RST, SPI_MOSI, SPI_SCLK, SPI_MISO);

//RFID
MFRC522 mfrc522(RFID_CS, RFID_RST);

//Audio
AudioPlaySdWav           playSdWav1;
AudioMixer4              mixer1;
AudioOutputAnalog        dac1;
AudioConnection          patchCord1(playSdWav1, 0, mixer1, 0);
AudioConnection          patchCord2(mixer1, dac1);

//Autres
 enum StateMachine {INIT, WAIT_RFID, GOTO_SLEEP, WAKE_UP, READ_SD};
 StateMachine State= INIT;
 uint32_t CardID;
 
/**************************************************/
/*                 FONCTIONS                      */
/**************************************************/

//==================================================
// Setup 
void setup(void) {
// All Inactive pendant le setup
  pinMode(SD_CS, INPUT_PULLUP);
  pinMode(RFID_CS, INPUT_PULLUP);
  pinMode(TFT_CS, INPUT_PULLUP);
  pinMode(RFID_RST, OUTPUT);
  pinMode(TFT_LED_PWR, OUTPUT);
  pinMode(AUDIO_PWR, OUTPUT);
  AudioOff();
  RFIDOff();
  TFTOff();

//Pour Debug  
Serial.begin(9600);

//Setup SPI
  SPI.setMOSI(SPI_MOSI);
  SPI.setMISO(SPI_MISO);
  SPI.setSCK(SPI_SCLK);

//Setup Audio
  AudioMemory(8);
  delay(200);
  mixer1.gain(0,0.3);

//Setup TFT 
  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setRotation(2);
    
//Setup SDCard  
  while (!SD.begin(SD_CS)) {
    TFTOn();
    tft.println(F("failed to access SD card!"));
    delay(2000);
  }
}


//==================================================
// Loop Function 
void loop(void) {
 
 switch (State)
 {
  
// --- INIT 
  case INIT : 
  Serial.println("INIT");
    // Hide the Display
    tft.fillScreen(ILI9341_BLACK);
    TFTOff(); 

    // Enable RFID
    CardID = 0;
    RFIDOn();
    State = WAIT_RFID;    
  Serial.println("WAIT_RFID");
  break;
  
  
// --- WAIT_RFID  
  case WAIT_RFID :
    //Gestion Sleep ICI (verif millis si pas sortie de Wakeup, sinon juste une fois)
    
    //
    if ( ! mfrc522.PICC_IsNewCardPresent() || ! mfrc522.PICC_ReadCardSerial() ) {
      return;
    }
    if(mfrc522.uid.size != 4)
      return;

    Serial.println("RFID Read");    
    for (byte i = 0; i < mfrc522.uid.size; i++)
      CardID +=  mfrc522.uid.uidByte[i] << 8*i;
    
    RFIDOff();
    State = READ_SD;
    delay(200);
  break;
  
  
// --- GOTO_SLEEP 
  Serial.println("GOTO_SLEEP");
  case GOTO_SLEEP : 
    AudioOff();
    RFIDOff();
    TFTOff();
    //Ajout IT Timer pour WakeUp
  break;
  
  
// --- WAKE_UP  
  Serial.println("WAKE_UP");
  case WAKE_UP : 
    State = INIT;
  break;
  
  
// --- READ_SD 
  Serial.println("READ_SD"); 
  case READ_SD :
    char Filename[256];

    sprintf(Filename, "%08x/pic.bmp",CardID);
  //Read BMP File
    Serial.println(Filename);
    bmpDraw(Filename, 0, 0);

  //read WAV File
    sprintf(Filename, "%08x/WAVE.WAV",CardID);
    if (SD.exists(Filename))
    {
      Serial.println(Filename);
      AudioOn();
      delay(500);
    }      
    if (playSdWav1.play(Filename))
    {
      // A brief delay for the library read WAV info
      delay(5);
      // Simply wait for the file to finish playing.
      while (playSdWav1.isPlaying()) {}
    }

    else
    {
          Serial.println("File not Found");
    }
    delay(2000);
  //Turn Off Audio and TFT, then wait for New RFID
    AudioOff();
    TFTOff();
    State = INIT;
  break;
  
  
// --- default  
  Serial.println("default");
  default : 
    State = INIT;
  break;
 }
}


//===========================================================
// Try Draw using writeRect
void bmpDraw(const char *filename, uint8_t x, uint16_t y) {

  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint16_t buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

 // uint16_t awColors[320];  // hold colors for one row at a time...
  uint16_t ImageData[320][240] = {0xAA55};

  if((x >= tft.width()) || (y >= tft.height())) return;

  // Open requested file on SD card
  if (!(bmpFile = SD.open(filename))) {
    tft.setCursor(0,0);
    tft.print(F("File not found"));
    return;
  }
  
  // Parse BMP header
  if(read16(bmpFile) == 0x4D42) { // BMP signature
    read32(bmpFile);
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
     // Read DIB header
    read32(bmpFile);
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if(read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel

      if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if(bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if((x+w-1) >= tft.width())  w = tft.width()  - x;
        if((y+h-1) >= tft.height()) h = tft.height() - y;

        for (row=0; row<h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if(bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }

          for (col=0; col<w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            ImageData[row][col] = tft.color565(r,g,b);
          } // end pixel
        } // end scanline
        tft.writeRect(0, 0, w, h, (uint16_t *)ImageData);
        TFTOn();

      } // end goodBmp
    }
  }

  bmpFile.close();
  if(!goodBmp) Serial.println(F("BMP format not recognized."));
}


//==================================================
// read16 and  read32
// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.
uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

