/* sketch of Si5351 Clock Generator for Arduino */

/* Degign for driving on ATmega328P internal 8MHz */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <Wire.h>
#include <EEPROM.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#elif defined(ESP8266)
#include <pgmspace.h>
#else
#endif
#include <FONTX2.h>

#include <ssd1306_i2c.h>  // 128x64
#include <si5351_i2c.h>

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT  64

#define CG_MAGICNUMBER 0x12345679

#define CG_COUNTTOSLEEP 200

#define CG_PIN_RW    2
#define CG_PIN_CONST 3
#define CG_PIN_L     4
#define CG_PIN_R     5
#define CG_PIN_A     6

enum {
  CG_UI_PLL = 0,
  CG_UI_CLK0 = 10,
  CG_UI_CLK1 = 20,
  CG_UI_CLK2 = 30,
  CG_UI_CLK0_EN = 40,
  CG_UI_CLK1_EN,
  CG_UI_CLK2_EN,
  CG_UI_LOAD,
  CG_UI_SAVE,
  CG_UI_DEFAULT
};

typedef struct CG_t_ {
  uint32_t u32MagicNumber;
  uint32_t u32PLLFreq;
  uint32_t u32PrePLLFreq;
  uint32_t u32ClkFreq[3];
  bool bClk[3];
  bool bValidPLL;
  bool bValidClk[3];
  bool bRW;
  bool bConst;
  uint16_t u16Cursor;
  uint16_t u16PreCursor;
} CG_t;

static CG_t g_tCG;
static bool g_bFirst = true;
static uint32_t g_u32CountToSleep;

SSD1306_I2C g_oDisplay;
Si5351_I2C g_oGenerator;

void* g_pLock = NULL;

extern FONTX2_Header_ANK_t tNaga10K;

static void SSD1306_I2C_BeginTransmission(const uint8_t u8I2CAddress) {
  Wire.beginTransmission(u8I2CAddress);
}

static void SSD1306_I2C_Write(const uint8_t u8Value) {
  Wire.write(u8Value);
}

static void SSD1306_I2C_EndTransmission(void) {
  Wire.endTransmission();
}

static void Si5351_I2C_BeginTransmission(const uint8_t u8Address) {
  Wire.beginTransmission(u8Address);
}

static void Si5351_I2C_RequestFrom(const uint8_t u8Address, const uint8_t u8Count) {
  Wire.requestFrom(u8Address, u8Count);
}

static uint8_t Si5351_I2C_Read(const uint8_t u8Address) {
  (void)u8Address;
  return Wire.read();
}

static void Si5351_I2C_Write(const uint8_t u8Value) {
  Wire.write(u8Value);
}

static void Si5351_I2C_EndTransmission(void) {
  Wire.endTransmission();
}

static void CG_MemoryBarrier(void) {
}

unsigned int FONTX2_ANK_GetFontImage(uint8_t* pu8FontImage, const FONTX2_Header_ANK_t* ptANK, const uint8_t u8Code) {
  uint8_t u8Data, u8LineSize;

  if(pu8FontImage == NULL || ptANK == NULL) {
    return 0;
  }

  if(ptANK->tCommon.u8FontWidth % 8 == 0) {
    u8LineSize = ptANK->tCommon.u8FontWidth / 8;
  } else {
    u8LineSize = ptANK->tCommon.u8FontWidth / 8 + 1;
  }

#if defined(__AVR__)
  for(u8Data = 0; u8Data < ptANK->tCommon.u8FontHeight * u8LineSize; u8Data++) {
    pu8FontImage[u8Data] = pgm_read_byte_near(ptANK->pu8FontImage + (uint32_t)u8Code * ptANK->tCommon.u8FontHeight * u8LineSize + u8Data);
  }
#elif defined(ESP8266)
  memcpy_P(pu8FontImage, &ptANK->pu8FontImage[(uint32_t)u8Code * ptANK->tCommon.u8FontHeight * u8LineSize], ptANK->tCommon.u8FontHeight * u8LineSize);
#else
  for(u8Data = 0; u8Data < ptANK->tCommon.u8FontHeight * u8LineSize; u8Data++) {
    pu8FontImage[u8Data] = ptANK->pu8FontImage[(uint32_t)u8Code * ptANK->tCommon.u8FontHeight * u8LineSize + u8Data];
  }
#endif

  return 1;
}

void FontImageDraw(
  SSD1306_I2C* poDisplay,
  const FONTX2_Header_ANK_t* ptANK,
  const uint8_t u8X,
  const uint8_t u8Y,
  const char* strString,
  const bool bInvert,
  const uint8_t u8Color
) {
  uint32_t k = 0;
  uint8_t i, j;
  uint8_t au8FontImage[64];

  while(strString[k]) {
    FONTX2_ANK_GetFontImage(au8FontImage, ptANK, strString[k]);
    if(u8X * (ptANK->tCommon.u8FontWidth + 1) >= DISPLAY_WIDTH || u8Y >= DISPLAY_HEIGHT) {
      break;
    }
    for(j = 0; j < ptANK->tCommon.u8FontHeight; j++) {
      for(i = 0; i < ptANK->tCommon.u8FontWidth; i++) {
        if(((au8FontImage[j] >> (7 - i)) & 0x1) == 0) {
          poDisplay->drawPixel((u8X + k) * (ptANK->tCommon.u8FontWidth + 1) + i, u8Y * ptANK->tCommon.u8FontHeight + j, bInvert ? u8Color : 0);
        } else {
          poDisplay->drawPixel((u8X + k) * (ptANK->tCommon.u8FontWidth + 1) + i, u8Y * ptANK->tCommon.u8FontHeight + j, bInvert ? 0 : u8Color);
        }
      }
      poDisplay->drawPixel((u8X + k) * (ptANK->tCommon.u8FontWidth + 1) + ptANK->tCommon.u8FontWidth, u8Y * ptANK->tCommon.u8FontHeight + j, bInvert ? 1 : 0);
    }
    k++;
  }
}

static void CG_Update_Clk(CG_t* ptCG, Si5351_CLKNo_t tCLKNo) {
  bool bRes = false;
  Si5351_MS_t tMS;
  uint32_t u32Ratio;

  if(ptCG->bValidPLL) {
    u32Ratio = ptCG->u32PLLFreq / ptCG->u32ClkFreq[tCLKNo];
    if(u32Ratio >= 6 && u32Ratio <= 1800) {
      ptCG->bValidClk[tCLKNo] = true;
    } else {
      ptCG->bValidClk[tCLKNo] = false;
    }

    if(ptCG->bValidClk[tCLKNo]) {
      bRes = Si5351_I2C::calcMSClk(&tMS, ui64_to_f64(ptCG->u32PLLFreq), ui64_to_f64(ptCG->u32ClkFreq[tCLKNo]));
    }
  }

  if(bRes) {
    g_oGenerator.setMS(tCLKNo, &tMS);
    if(ptCG->bClk[tCLKNo]) {
      g_oGenerator.setOutputEnable(1 << tCLKNo);
      g_oGenerator.setClkPowerDown(tCLKNo, false);
      FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3 + tCLKNo, "OFF", false, 1);
    } else {
      g_oGenerator.setOutputDisable(1 << tCLKNo);
      g_oGenerator.setClkPowerDown(tCLKNo, true);
      FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3 + tCLKNo, "ON ", false, 1);
    }
  } else {
    g_oGenerator.setOutputDisable(1 << tCLKNo);
    g_oGenerator.setClkPowerDown(tCLKNo, true);
    FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3 + tCLKNo, "NG ", false, 1);
  }
}

static void CG_Update_PLL(CG_t* ptCG) {
  bool bRes = false;
  Si5351_MS_t tMS;
  uint32_t u32Ratio;

  u32Ratio = ptCG->u32PLLFreq / SI5351_XTAL_FREQ;
  if(u32Ratio >= 15 && u32Ratio <= 90) {
    ptCG->bValidPLL = true;
    bRes = Si5351_I2C::calcMSPLL(&tMS, ui64_to_f64(SI5351_XTAL_FREQ), ui64_to_f64(ptCG->u32PLLFreq));
  } else {
    ptCG->bValidPLL = false;
  }

  g_oGenerator.setOutputDisable(0x07);
  if(bRes) {
    g_oGenerator.setMSA(&tMS);
    g_oGenerator.PLLSoftReset();
    FontImageDraw(&g_oDisplay, &tNaga10K, 20, 2, "  ", false, 1);
    CG_Update_Clk(ptCG, SI5351_CLK0);
    CG_Update_Clk(ptCG, SI5351_CLK1);
    CG_Update_Clk(ptCG, SI5351_CLK2);
  } else {
    FontImageDraw(&g_oDisplay, &tNaga10K, 20, 2, "NG", false, 1);
  }
}

static void CG_SetDefault(CG_t* ptCG) {
  ptCG->u32MagicNumber = CG_MAGICNUMBER;
  ptCG->u32PLLFreq = 500000000;
  ptCG->u32PrePLLFreq = 0;
  ptCG->u32ClkFreq[0] = 10000000;
  ptCG->u32ClkFreq[1] = 10000000;
  ptCG->u32ClkFreq[2] = 10000000;
  ptCG->bClk[0] = false;
  ptCG->bClk[1] = false;
  ptCG->bClk[2] = false;
  ptCG->u16Cursor = CG_UI_PLL;

  CG_Update_PLL(ptCG);
}

static void CG_Load(CG_t* ptCG) {
  uint32_t u32Address;

  for(u32Address = 0; u32Address < sizeof(CG_t); u32Address++) {
    *((uint8_t*)ptCG + u32Address) = EEPROM.read(u32Address);
  }

  if(ptCG->u32MagicNumber != CG_MAGICNUMBER) {
    CG_SetDefault(ptCG);
  } else {
    CG_Update_PLL(ptCG);
  }

  ptCG->u16Cursor = CG_UI_PLL;
  ptCG->bRW = digitalRead(2);
  ptCG->bConst = digitalRead(3);
}

static void CG_Save(CG_t* ptCG) {
  uint32_t u32Address;

  for(u32Address = 0; u32Address < sizeof(CG_t); u32Address++) {
    EEPROM.write(u32Address, *((uint8_t*)ptCG + u32Address));
  }
}

static uint8_t CG_GetDigit(const uint32_t u32Value) {
  uint8_t u8Digit = 0;
  uint32_t u32Base;

  for(u32Base = 10; u32Base <= u32Value; u32Base = u32Base *= 10) {
    u8Digit++;
  }

  return u8Digit;
}

static uint8_t CG_GetDigitValue(const uint32_t u32Value, const uint8_t u8Digit) {
  uint8_t u8Count;
  uint32_t u32Base = 1;

  for(u8Count = 0; u8Count < u8Digit; u8Count++) {
    u32Base *= 10;
  }

  return (u32Value / u32Base) % 10;
}

static void CG_SetDigitValue(uint32_t* pu32Value, const uint8_t u8Digit, const uint8_t u8Value) {
  uint8_t u8Count;
  uint32_t u32Base = 1;
  uint32_t u32Temp;

  for(u8Count = 0; u8Count < u8Digit; u8Count++) {
    u32Base *= 10;
  }
  u32Temp = ((*pu32Value / (u32Base * 10)) * 10 + u8Value) * u32Base;
  u32Temp += *pu32Value % u32Base;
  *pu32Value = u32Temp;
}

static void CG_Update(CG_t* ptCG) {
  bool bRes;
  char strTemp[16];
  uint32_t u32Ratio;

  if(g_bFirst) {
    FontImageDraw(&g_oDisplay, &tNaga10K, 2, 0, "Si5351 Clock Generator", false, 1);
    FontImageDraw(&g_oDisplay, &tNaga10K, 3, 2, "PLL", false, 1);
    FontImageDraw(&g_oDisplay, &tNaga10K, 3, 3, "CLK0", false, 1);
    FontImageDraw(&g_oDisplay, &tNaga10K, 3, 4, "CLK1", false, 1);
    FontImageDraw(&g_oDisplay, &tNaga10K, 3, 5, "CLK2", false, 1);
  }
  if(g_bFirst || ((ptCG->u16PreCursor >= CG_UI_PLL) && (ptCG->u16PreCursor < CG_UI_CLK0))) {
    sprintf(strTemp, "%10luHz", ptCG->u32PLLFreq);
    FontImageDraw(&g_oDisplay, &tNaga10K, 7, 2, strTemp, false, 1);
  }
  if(g_bFirst || ((ptCG->u16PreCursor >= CG_UI_CLK0) && (ptCG->u16PreCursor < CG_UI_CLK1))) {
    sprintf(strTemp, "%9luHz", ptCG->u32ClkFreq[0]);
    FontImageDraw(&g_oDisplay, &tNaga10K, 8, 3, strTemp, false, 1);
  }
  if(g_bFirst || (ptCG->u16PreCursor == CG_UI_CLK0_EN)) {
    CG_Update_Clk(ptCG, SI5351_CLK0);
  }
  if(g_bFirst || ((ptCG->u16PreCursor >= CG_UI_CLK1) && (ptCG->u16PreCursor < CG_UI_CLK2))) {
    sprintf(strTemp, "%9luHz", ptCG->u32ClkFreq[1]);
    FontImageDraw(&g_oDisplay, &tNaga10K, 8, 4, strTemp, false, 1);
  }
  if(g_bFirst || (ptCG->u16PreCursor == CG_UI_CLK1_EN)) {
    CG_Update_Clk(ptCG, SI5351_CLK1);
  }
  if(g_bFirst || ((ptCG->u16PreCursor >= CG_UI_CLK2) && (ptCG->u16PreCursor < CG_UI_CLK0_EN))) {
    sprintf(strTemp, "%9luHz", ptCG->u32ClkFreq[2]);
    FontImageDraw(&g_oDisplay, &tNaga10K, 8, 5, strTemp, false, 1);
  }
  if(g_bFirst || (ptCG->u16PreCursor == CG_UI_CLK2_EN)) {
    CG_Update_Clk(ptCG, SI5351_CLK2);
  }
  if(!ptCG->bConst) {
    if(!ptCG->bRW && (g_bFirst || (ptCG->u16PreCursor == CG_UI_LOAD))) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 3, 7, "LOAD", false, 1);
    }
    if(!ptCG->bRW && (g_bFirst || (ptCG->u16PreCursor == CG_UI_SAVE))) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 9, 7, "SAVE", false, 1);
    }
    if(g_bFirst || (ptCG->u16PreCursor == CG_UI_DEFAULT)) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 15, 7, "DEFAULT", false, 1);
    }
  }

  if(!ptCG->bConst) {
    if(ptCG->u16Cursor >= CG_UI_PLL && ptCG->u16Cursor < CG_UI_CLK0) {
      sprintf(strTemp, "%d", CG_GetDigitValue(ptCG->u32PLLFreq, ptCG->u16Cursor - CG_UI_PLL));
      FontImageDraw(&g_oDisplay, &tNaga10K, ((CG_UI_PLL + 16) - ptCG->u16Cursor), 2, strTemp, true, 1);
    } else if(ptCG->u16Cursor >= CG_UI_CLK0 && ptCG->u16Cursor < CG_UI_CLK1) {
      sprintf(strTemp, "%d", CG_GetDigitValue(ptCG->u32ClkFreq[0], ptCG->u16Cursor - CG_UI_CLK0));
      FontImageDraw(&g_oDisplay, &tNaga10K, ((CG_UI_CLK0 + 16) - ptCG->u16Cursor), 3, strTemp, true, 1);
    } else if(ptCG->u16Cursor >= CG_UI_CLK1 && ptCG->u16Cursor < CG_UI_CLK2) {
      sprintf(strTemp, "%d", CG_GetDigitValue(ptCG->u32ClkFreq[1], ptCG->u16Cursor - CG_UI_CLK1));
      FontImageDraw(&g_oDisplay, &tNaga10K, ((CG_UI_CLK1 + 16) - ptCG->u16Cursor), 4, strTemp, true, 1);
    } else if(ptCG->u16Cursor >= CG_UI_CLK2 && ptCG->u16Cursor < CG_UI_CLK0_EN) {
      sprintf(strTemp, "%d", CG_GetDigitValue(ptCG->u32ClkFreq[2], ptCG->u16Cursor - CG_UI_CLK2));
      FontImageDraw(&g_oDisplay, &tNaga10K, ((CG_UI_CLK2 + 16) - ptCG->u16Cursor), 5, strTemp, true, 1);
    } else if(ptCG->u16Cursor == CG_UI_CLK0_EN) {
      if(!ptCG->bValidPLL || !ptCG->bValidClk[0]) {
        FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3, "NG", true, 1);
      } else {
        if(ptCG->bClk[0]) {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3, "OFF", true, 1);
        } else {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 3, "ON", true, 1);
        }
      }
    } else if(ptCG->u16Cursor == CG_UI_CLK1_EN) {
      if(!ptCG->bValidPLL || !ptCG->bValidClk[1]) {
        FontImageDraw(&g_oDisplay, &tNaga10K, 20, 4, "NG", true, 1);
      } else {
        if(ptCG->bClk[1]) {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 4, "OFF", true, 1);
        } else {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 4, "ON", true, 1);
        }
      }
    } else if(ptCG->u16Cursor == CG_UI_CLK2_EN) {
      if(!ptCG->bValidPLL || !ptCG->bValidClk[2]) {
        FontImageDraw(&g_oDisplay, &tNaga10K, 20, 5, "NG", true, 1);
      } else {
        if(ptCG->bClk[2]) {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 5, "OFF", true, 1);
        } else {
          FontImageDraw(&g_oDisplay, &tNaga10K, 20, 5, "ON", true, 1);
        }
      }
    } else if(ptCG->u16Cursor == CG_UI_LOAD) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 3, 7, "LOAD", true, 1);
    } else if(ptCG->u16Cursor == CG_UI_SAVE) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 9, 7, "SAVE", true, 1);
    } else if(ptCG->u16Cursor == CG_UI_DEFAULT) {
      FontImageDraw(&g_oDisplay, &tNaga10K, 15, 7, "DEFAULT", true, 1);
    }
  }
}

static void CG_Initialize(CG_t* ptCG) {
  pinMode(CG_PIN_RW,    INPUT_PULLUP);  // RW
  pinMode(CG_PIN_CONST, INPUT_PULLUP);  // Const
  pinMode(CG_PIN_L,     INPUT_PULLUP);  // L
  pinMode(CG_PIN_R,     INPUT_PULLUP);  // R
  pinMode(CG_PIN_A,     INPUT_PULLUP);  // A

  g_oDisplay.initialize(
    SSD1306_I2C_BeginTransmission,
    SSD1306_I2C_Write,
    SSD1306_I2C_EndTransmission,
    CG_MemoryBarrier,
    &g_pLock,
    0x3C, 128, 64, 0xFF
  );

  g_oGenerator.initialize(
    Si5351_I2C_BeginTransmission,
    Si5351_I2C_RequestFrom,
    Si5351_I2C_Read,
    Si5351_I2C_Write,
    Si5351_I2C_EndTransmission,
    CG_MemoryBarrier,
    &g_pLock
  );

  Wire.begin();
  Wire.setClock(400000);

  g_oDisplay.initDevice();
  g_oDisplay.clear();

  g_oGenerator.initDevice();
  g_oGenerator.setSSPDisable();

  g_oGenerator.setPLLA_SRC(SI5351_PLL_SRC_XTAL);
  g_oGenerator.setClkPowerDown(SI5351_CLK0, true);
  g_oGenerator.setClkPowerDown(SI5351_CLK1, true);
  g_oGenerator.setClkPowerDown(SI5351_CLK2, true);
  g_oGenerator.setClkMSSource(SI5351_CLK0, SI5351_CLK_MS_SRC_PLLA);
  g_oGenerator.setClkMSSource(SI5351_CLK1, SI5351_CLK_MS_SRC_PLLA);
  g_oGenerator.setClkMSSource(SI5351_CLK2, SI5351_CLK_MS_SRC_PLLA);
  g_oGenerator.setClkSrc(SI5351_CLK0, SI5351_CLK_SRC_MS);
  g_oGenerator.setClkSrc(SI5351_CLK1, SI5351_CLK_SRC_MS);
  g_oGenerator.setClkSrc(SI5351_CLK2, SI5351_CLK_SRC_MS);

  CG_Load(ptCG);
}

static bool CG_Input(CG_t* ptCG) {
  uint8_t u8Digit;
  uint8_t u8Input = 0;
  Si5351_MS_t tMS;
  bool bRes;

  if(!digitalRead(CG_PIN_L)) {  // L
    u8Input = 1;
  }
  if(!u8Input) {
    if(!digitalRead(CG_PIN_R)) {  // R
      u8Input = 2;
    }
  }
  if(!u8Input) {
    if(!digitalRead(CG_PIN_A)) {  // A
      u8Input = 3;
    }
  }

  ptCG->u16PreCursor = ptCG->u16Cursor;
  switch(u8Input) {
  case 1:  // L
    if(ptCG->u16Cursor >= CG_UI_PLL && ptCG->u16Cursor < CG_UI_CLK0) {
      if(ptCG->u16Cursor == CG_UI_PLL + 9) {
        ptCG->u16Cursor = CG_UI_DEFAULT;
      } else {
        ptCG->u16Cursor++;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK0 && ptCG->u16Cursor < CG_UI_CLK1) {
      if(ptCG->u16Cursor == CG_UI_CLK0 + 8) {
        ptCG->u16Cursor = CG_UI_PLL;
      } else {
        ptCG->u16Cursor++;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK1 && ptCG->u16Cursor < CG_UI_CLK2) {
      if(ptCG->u16Cursor == CG_UI_CLK1 + 8) {
        ptCG->u16Cursor = CG_UI_CLK0_EN;
      } else {
        ptCG->u16Cursor++;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK2 && ptCG->u16Cursor < CG_UI_CLK0_EN) {
      if(ptCG->u16Cursor == CG_UI_CLK2 + 8) {
        ptCG->u16Cursor = CG_UI_CLK1_EN;
      } else {
        ptCG->u16Cursor++;
      }
    } else if(ptCG->u16Cursor == CG_UI_CLK0_EN) {
      ptCG->u16Cursor = CG_UI_CLK0;
    } else if(ptCG->u16Cursor == CG_UI_CLK1_EN) {
      ptCG->u16Cursor = CG_UI_CLK1;
    } else if(ptCG->u16Cursor == CG_UI_CLK2_EN) {
      ptCG->u16Cursor = CG_UI_CLK2;
    } else if(ptCG->u16Cursor == CG_UI_LOAD) {
      ptCG->u16Cursor = CG_UI_CLK2_EN;
    } else if(ptCG->u16Cursor == CG_UI_SAVE) {
      ptCG->u16Cursor = CG_UI_LOAD;
    } else if(ptCG->u16Cursor == CG_UI_DEFAULT) {
      if(ptCG->bRW) {
        ptCG->u16Cursor = CG_UI_CLK2_EN;
      } else {
        ptCG->u16Cursor = CG_UI_SAVE;
      }
    }
    break;

  case 2:  // R
    if(ptCG->u16Cursor >= CG_UI_PLL && ptCG->u16Cursor < CG_UI_CLK0) {
      if(ptCG->u16Cursor == CG_UI_PLL) {
        u8Digit = CG_GetDigit(ptCG->u32ClkFreq[0]);
        ptCG->u16Cursor = CG_UI_CLK0 + u8Digit;
      } else {
        ptCG->u16Cursor--;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK0 && ptCG->u16Cursor < CG_UI_CLK1) {
      if(ptCG->u16Cursor == CG_UI_CLK0) {
        ptCG->u16Cursor = CG_UI_CLK0_EN;
      } else {
        ptCG->u16Cursor--;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK1 && ptCG->u16Cursor < CG_UI_CLK2) {
      if(ptCG->u16Cursor == CG_UI_CLK1) {
        ptCG->u16Cursor = CG_UI_CLK1_EN;
      } else {
        ptCG->u16Cursor--;
      }
    } else if(ptCG->u16Cursor >= CG_UI_CLK2 && ptCG->u16Cursor < CG_UI_CLK0_EN) {
      if(ptCG->u16Cursor == CG_UI_CLK2) {
        ptCG->u16Cursor = CG_UI_CLK2_EN;
      } else {
        ptCG->u16Cursor--;
      }
    } else if(ptCG->u16Cursor == CG_UI_CLK0_EN) {
      u8Digit = CG_GetDigit(ptCG->u32ClkFreq[1]);
      ptCG->u16Cursor = CG_UI_CLK1 + u8Digit;
    } else if(ptCG->u16Cursor == CG_UI_CLK1_EN) {
      u8Digit = CG_GetDigit(ptCG->u32ClkFreq[2]);
      ptCG->u16Cursor = CG_UI_CLK2 + u8Digit;
    } else if(ptCG->u16Cursor == CG_UI_CLK2_EN) {
      if(ptCG->bRW) {
        ptCG->u16Cursor = CG_UI_DEFAULT;
      } else {
        ptCG->u16Cursor = CG_UI_LOAD;
      }
    } else if(ptCG->u16Cursor == CG_UI_LOAD) {
      ptCG->u16Cursor = CG_UI_SAVE;
    } else if(ptCG->u16Cursor == CG_UI_SAVE) {
      ptCG->u16Cursor = CG_UI_DEFAULT;
    } else if(ptCG->u16Cursor == CG_UI_DEFAULT) {
      u8Digit = CG_GetDigit(ptCG->u32PLLFreq);
      ptCG->u16Cursor = u8Digit;
    }
    break;

  case 3:  // A
    if(ptCG->u16Cursor >= CG_UI_PLL && ptCG->u16Cursor < CG_UI_CLK0) {
      if(ptCG->u16Cursor == CG_UI_PLL + 9) {
        u8Digit = ptCG->u32PLLFreq / 1000000000;
        u8Digit++;
        if(u8Digit >= 2) {
          u8Digit = 0;
        }
        ptCG->u32PLLFreq = 1000000000 * u8Digit + ptCG->u32PLLFreq % 1000000000;
      } else {
        u8Digit = CG_GetDigitValue(ptCG->u32PLLFreq, ptCG->u16Cursor - CG_UI_PLL);
        u8Digit++;
        if(u8Digit >= 10) {
          u8Digit = 0;
        }
        CG_SetDigitValue(&ptCG->u32PLLFreq, ptCG->u16Cursor - CG_UI_PLL, u8Digit);
      }
      CG_Update_PLL(ptCG);
    } else if(ptCG->u16Cursor >= CG_UI_CLK0 && ptCG->u16Cursor < CG_UI_CLK1) {
      u8Digit = CG_GetDigitValue(ptCG->u32ClkFreq[0], ptCG->u16Cursor - CG_UI_CLK0);
      u8Digit++;
      if(u8Digit >= 10) {
        u8Digit = 0;
      }
      CG_SetDigitValue(&ptCG->u32ClkFreq[0], ptCG->u16Cursor - CG_UI_CLK0, u8Digit);
      CG_Update_Clk(ptCG, SI5351_CLK0);
    } else if(ptCG->u16Cursor >= CG_UI_CLK1 && ptCG->u16Cursor < CG_UI_CLK2) {
      u8Digit = CG_GetDigitValue(ptCG->u32ClkFreq[1], ptCG->u16Cursor - CG_UI_CLK1);
      u8Digit++;
      if(u8Digit >= 10) {
        u8Digit = 0;
      }
      CG_SetDigitValue(&ptCG->u32ClkFreq[1], ptCG->u16Cursor - CG_UI_CLK1, u8Digit);
      CG_Update_Clk(ptCG, SI5351_CLK1);
    } else if(ptCG->u16Cursor >= CG_UI_CLK2 && ptCG->u16Cursor < CG_UI_CLK0_EN) {
      u8Digit = CG_GetDigitValue(ptCG->u32ClkFreq[2], ptCG->u16Cursor - CG_UI_CLK2);
      u8Digit++;
      if(u8Digit >= 10) {
        u8Digit = 0;
      }
      CG_SetDigitValue(&ptCG->u32ClkFreq[2], ptCG->u16Cursor - CG_UI_CLK2, u8Digit);
      CG_Update_Clk(ptCG, SI5351_CLK2);
    } else if(ptCG->u16Cursor == CG_UI_CLK0_EN) {
      if(ptCG->bValidPLL && ptCG->bValidClk[0]) {
        if(ptCG->bClk[0]) {
          ptCG->bClk[0] = false;
        } else {
          ptCG->bClk[0] = true;
        }
      } else {
        ptCG->bClk[0] = false;
      }
      CG_Update_Clk(ptCG, SI5351_CLK0);
    } else if(ptCG->u16Cursor == CG_UI_CLK1_EN) {
      if(ptCG->bValidPLL && ptCG->bValidClk[1]) {
        if(ptCG->bClk[1]) {
          ptCG->bClk[1] = false;
        } else {
          ptCG->bClk[1] = true;
        }
      } else {
        ptCG->bClk[1] = false;
      }
      CG_Update_Clk(ptCG, SI5351_CLK1);
    } else if(ptCG->u16Cursor == CG_UI_CLK2_EN) {
      if(ptCG->bValidPLL && ptCG->bValidClk[2]) {
        if(ptCG->bClk[2]) {
          ptCG->bClk[2] = false;
        } else {
          ptCG->bClk[2] = true;
        }
      } else {
        ptCG->bClk[2] = false;
      }
      CG_Update_Clk(ptCG, SI5351_CLK2);
    } else if(ptCG->u16Cursor == CG_UI_LOAD) {
      CG_Load(ptCG);
    } else if(ptCG->u16Cursor == CG_UI_SAVE) {
      CG_Save(ptCG);
    } else if(ptCG->u16Cursor == CG_UI_DEFAULT) {
      CG_SetDefault(ptCG);
    }
    break;
  }

  return (u8Input > 0) ? true : false;
}

void setup() {
  CG_Initialize(&g_tCG);
  CG_Update(&g_tCG);
  g_oDisplay.refresh();
  g_bFirst = false;
}

void loop() {
  if(g_u32CountToSleep < CG_COUNTTOSLEEP) {
    if(!g_tCG.bConst) {
      if(CG_Input(&g_tCG)) {
        CG_Update(&g_tCG);
        g_oDisplay.refresh();
        g_u32CountToSleep = 0;
      }
    }
    g_u32CountToSleep++;
  } else if(g_u32CountToSleep == CG_COUNTTOSLEEP) {
    g_oDisplay.sleep();
    g_u32CountToSleep++;
  } else {
    if(!digitalRead(CG_PIN_A)) {  // A
      g_oDisplay.wakeup();
      g_u32CountToSleep = 0;
    }
  }
  delay(100);
}
