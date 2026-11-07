// BitPet - tamagotchi de Bitcoin para ESP32 CYD (2.8" 320x240 horizontal)
// Privacidade: SSID, senha e endereco ficam so na flash (NVS) do aparelho.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>   // biblioteca: XPT2046_Touchscreen (Paul Stoffregen)
#include <LittleFS.h>
#include <ESPmDNS.h>
#include "sprites.h"                // personagem padrao (gerado a partir da folha de sprites)
#include "page.h"                   // pagina de configuracao (PIN + Wi-Fi + sprites)

#define ROTATION 3        // 1 = horizontal; se ficar de cabeca para baixo, use 3
#define SX 80             // area animada do pet (sprite) na tela
#define SY 44
#define SW 160
#define SH 156
#define FRAME_MS 150

enum Mood { FED, HAPPY, NORMAL, HUNGRY, DYING };
static const char* MOOD_NAME[] = {"Alimentado", "Feliz", "Normal", "Com fome", "Quase morto"};
static const uint16_t MOOD_COLOR[] = {0x07E0, 0xFFE0, 0x5DFF, 0xFD20, 0xF800};
static const char* MOOD_MSG[] = {"Alimentado pela alta. Sem FOMO!", "Mercado animado. Tudo bem!", "Tudo calmo por aqui...", "Com fome... cade a alta?", "Quase morto... HODL!"};

struct Theme { uint16_t bg, ground, rain, body, belly, spot, line, bulb; };
static const Theme TH[5] = {
  //  fundo   chao    codigo  corpo   barriga manchas contorno bulbo
  {0x0161, 0x0821, 0x07E0, 0x5E55, 0xCF9B, 0x2C4E, 0x0944, 0x262B},   // FED
  {0x0120, 0x0821, 0x05E0, 0x5E55, 0xCF9B, 0x2C4E, 0x0944, 0x262B},   // HAPPY
  {0x0100, 0x0821, 0x07F3, 0x5E55, 0xCF9B, 0x2C4E, 0x0944, 0x262B},   // NORMAL
  {0x2100, 0x1081, 0xFD20, 0x6D52, 0xB6B9, 0x3BCC, 0x0944, 0x8CC5},   // HUNGRY
  {0x2000, 0x1000, 0xF800, 0x8CD2, 0xBE17, 0x5B2C, 0x18C3, 0x52AA},   // DYING
};

TFT_eSPI tft;
TFT_eSprite spr(&tft);   // pet 160x156
TFT_eSprite mon(&tft);   // telas dos monitores 64x56
// Alvo de desenho com deslocamento vertical: a tela real (dy=0) ou uma faixa da captura de tela.
struct Cv {
  TFT_eSPI* t; int dy;
  uint16_t fg = 0xFFFF; uint8_t datum = TL_DATUM;
  void fillRect(int x, int y, int w, int h, uint16_t c) { t->fillRect(x, y - dy, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { t->fillRoundRect(x, y - dy, w, h, r, c); }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { t->drawRoundRect(x, y - dy, w, h, r, c); }
  void fillCircle(int x, int y, int r, uint16_t c) { t->fillCircle(x, y - dy, r, c); }
  int drawString(const String& s, int x, int y, int f) {
    if (f == 1 && t != &tft) {                       // fonte pequena nas faixas da captura: desenha letra a letra (5x7 em celula 6x8)
      int w = 6 * s.length(), px = x, py = y - dy;
      if (datum == MC_DATUM) { px -= w / 2; py -= 4; }
      for (unsigned i = 0; i < s.length(); i++) ((TFT_eSprite*)t)->drawChar(px + 6 * i, py, s[i], fg, fg, 1);
      return w;
    }
    return t->drawString(s, x, y - dy, f);
  }
  int textWidth(const String& s, int f) { return t->textWidth(s, f); }
  void setTextDatum(uint8_t d) { datum = d; t->setTextDatum(d); }
  void setTextColor(uint16_t c) { fg = c; t->setTextColor(c); }
  void setTextSize(uint8_t z) { t->setTextSize(z); }
  void pushMon(int x, int y) { if (t == &tft) mon.pushSprite(x, y); else mon.pushToSprite((TFT_eSprite*)t, x, y - dy); }
};
static Cv SCR{&tft, 0};
static bool gMask = false;   // captura com saldo e IP censurados
// Toque da CYD (XPT2046 em barramento SPI proprio). Se o seu User_Setup usar USE_HSPI_PORT, troque HSPI por VSPI.
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(33, 36);   // CS=33, IRQ=36
uint32_t petUntil = 0;
uint32_t previewUntil = 0;       // ate quando mostra a previa de outra animacao
int previewMood = -1;            // humor em previa (toque duplo, alterna)
uint32_t pendingTap = 0;         // toque simples aguardando confirmar que nao e duplo
bool ipShowReq = false, ipActive = false;   // tocar na barra do topo mostra o endereco (IP) do aparelho
uint32_t ipUntil = 0;
Preferences prefs;
WebServer web(80);
DNSServer dns;

String cfgSsid, cfgPass, cfgAddr;
double btcUsd = 0, chg24 = 0, balBtc = 0;
int pendingTx = 0;
bool haveData = false, lastFetchOk = true, invertColors = true;
uint32_t lastFetch = 0, lastFrame = 0;
int frame = 0;
Mood curMood = NORMAL;

// ---------- Humor (pela variacao de 24h) ----------
Mood moodFromChange(double c) {
  if (c >= 5.0)  return FED;      // alta extrema: se autoalimenta
  if (c >= 1.5)  return HAPPY;
  if (c > -1.5)  return NORMAL;
  if (c > -5.0)  return HUNGRY;
  return DYING;                   // baixa extrema: fome maxima
}

// ---------- Helpers de desenho ----------
static uint16_t fade(uint16_t c, int s) {   // s: 0..255
  int r = ((c >> 11) & 31) * s / 255, g = ((c >> 5) & 63) * s / 255, b = (c & 31) * s / 255;
  return (r << 11) | (g << 5) | b;
}

static void heart(TFT_eSPI& g, int x, int y, uint16_t c, int s) {
  static const uint8_t H[4] = {0b11011, 0b11111, 0b01110, 0b00100};
  for (int r = 0; r < 4; r++) for (int k = 0; k < 5; k++)
    if (H[r] & (1 << (4 - k))) g.fillRect(x + k * s, y + r * s, s, s, c);
}

static void sparkle(int x, int y, uint16_t c) { spr.drawFastHLine(x - 3, y, 7, c); spr.drawFastVLine(x, y - 3, 7, c); }

// ---------- Quarto hacker (desenhado em qualquer alvo, com deslocamento) ----------
static TFT_eSPI* gT = &tft;
static int gX0 = 0, gY0 = 0;
static void FR(int x, int y, int w, int h, uint16_t c) { gT->fillRect(x - gX0, y - gY0, w, h, c); }

static void drawRoom(Mood m, TFT_eSPI* t, int x0, int y0) {
  gT = t; gX0 = x0; gY0 = y0;
  const Theme& T = TH[m];
  // parede
  FR(0, 0, 320, 128, T.bg);
  for (int x = 40; x < 320; x += 80) FR(x, 0, 1, 128, fade(T.rain, 40));
  // letreiro neon
  gT->setTextDatum(MC_DATUM);
  gT->setTextColor(fade(T.rain, 90)); gT->drawString(">_ BITPET", 161 - x0, 55 - y0, 2);
  gT->setTextColor(T.rain);           gT->drawString(">_ BITPET", 160 - x0, 54 - y0, 2);
  // monitores (as telas sao animadas a parte)
  const int mx[2] = {6, 242};
  for (int i = 0; i < 2; i++) {
    int x = mx[i];
    FR(x, 44, 72, 64, 0x2104); FR(x + 4, 48, 64, 56, 0x0000);
    FR(x + 32, 108, 8, 20, 0x2104); FR(x + 20, 124, 32, 4, 0x2104);
  }
  // mesa
  FR(0, 128, 320, 22, 0x3943); FR(0, 128, 320, 2, 0x5A05);
  FR(0, 150, 320, 40, 0x28E2);
  FR(16, 156, 56, 30, 0x2104); FR(40, 168, 8, 2, 0x7BEF);
  FR(244, 156, 36, 30, 0x2104); FR(258, 168, 8, 2, 0x7BEF);
  // teclado
  FR(10, 132, 62, 12, 0x18C3);
  for (int r = 0; r < 3; r++) for (int k = 0; k < 9; k++) FR(12 + k * 6, 134 + r * 3, 4, 2, 0x4208);
  // caneca
  FR(292, 114, 12, 14, 0xC618); FR(304, 118, 3, 6, 0xC618); FR(294, 116, 8, 2, 0x6B4D);
  // rack de servidor
  FR(284, 150, 26, 40, 0x18C3);
  for (int i = 0; i < 4; i++) FR(287, 154 + i * 9, 20, 7, 0x0841);
  // chao
  FR(0, 190, 320, 50, 0x1082);
  for (int y = 204; y < 240; y += 14) FR(0, y, 320, 1, 0x0841);
}

// ---------- Partes animadas do quarto (desenhadas direto na tela) ----------
void animBits(Mood m, int f, Cv& g = SCR) {
  const Theme& T = TH[m];
  // monitor esquerdo: codigo rolando
  mon.fillSprite(0x0000);
  mon.setTextDatum(MC_DATUM);
  if (m == DYING) {
    mon.setTextColor(0xF800);
    if (f % 6 < 4) { mon.drawString("FATAL", 32, 16, 2); mon.drawString("ERROR", 32, 36, 2); }
  } else {
    int div = (m == HUNGRY) ? 6 : 3, base = f / div, sub = (f % div) * 8 / div;
    for (int i = 0; i < 8; i++) {
      uint32_t h = (uint32_t)(base + i) * 2654435761u;
      int x = 3 + ((h >> 5) % 3) * 6, y = 2 + i * 8 - sub;
      for (int k = 0; k < 3; k++) {
        int w = 5 + ((h >> (8 + k * 4)) % 12);
        if (x + w > 62) w = 62 - x;
        if (w > 0) mon.fillRect(x, y, w, 3, k == 0 ? T.rain : fade(T.rain, 150));
        x += w + 3;
      }
    }
    if (f % 4 < 2) mon.fillRect(3, 50, 5, 3, T.rain);
  }
  g.pushMon(10, 48);

  // monitor direito: grafico de candles
  mon.fillSprite(0x0000);
  for (int y = 14; y < 56; y += 14) mon.drawFastHLine(0, y, 64, fade(T.rain, 35));
  float tr = (m == FED) ? 1.0f : (m == HAPPY ? 0.5f : (m == NORMAL ? 0.0f : (m == HUNGRY ? -0.5f : -1.0f)));
  int prevY = 0;
  for (int i = 0; i < 10; i++) {
    uint32_t h = (uint32_t)(f / 6 + i) * 2246822519u; h ^= h >> 13;
    int y = 28 - (int)(tr * (i - 5) * 2.6f) + (int)(h % 13) - 6;
    int bh = 3 + (h >> 9) % 6;
    uint16_t c = (i > 0 && y < prevY) ? 0x07E0 : 0xF800;
    mon.drawFastVLine(5 + i * 6, y - bh / 2 - 2, bh + 4, c);
    mon.fillRect(4 + i * 6, y - bh / 2, 3, bh, c);
    prevY = y;
  }
  mon.setTextDatum(TL_DATUM); mon.setTextColor(0xFD20); mon.drawString("BTC", 2, 2, 1);
  g.pushMon(246, 48);

  // luz RGB do teclado
  static const uint16_t RGB[6] = {0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x07FF, 0xF81F};
  for (int s = 0; s < 6; s++) g.fillRect(10 + s * 10, 145, 10, 2, m == DYING ? (f % 4 < 2 ? 0x8000 : 0x2000) : RGB[(s + f / 2) % 6]);
  // LEDs do rack
  for (int i = 0; i < 4; i++) {
    uint32_t h = (uint32_t)(f / 2 + i) * 2654435761u;
    uint16_t c1 = (m == DYING) ? ((f % 4 < 2) ? 0xF800 : 0x2000) : (((h >> 7) % 5 == 0) ? 0xFD20 : 0x07E0);
    uint16_t c2 = ((h >> 11) % 2) ? 0x07FF : 0x0841;
    g.fillRect(289, 157 + i * 9, 3, 3, c1);
    g.fillRect(295, 157 + i * 9, 3, 3, c2);
  }
}

// ---------- Pet animado (sprite 160x156 sobre o quarto) ----------
// Desenha um quadro RLE (indice, repeticoes). Indice 0 = transparente.
// Personagem personalizado (enviado pela pagina): /spr.bin no LittleFS
//   "BPS1" + paleta 255 x u16 + 20 x {w,h,len u32} + dados RLE (20 quadros, 4 por humor)
static bool customSpr = false;
static uint16_t cPal[256];
struct CEnt { uint8_t w, h; uint32_t off, len; };
static CEnt cEnt[20];
static SprFrame cLoaded[4];
static uint8_t* cBuf[4] = {0, 0, 0, 0};
static int cLoadedMood = -1;

static void freeCustom() { for (int i = 0; i < 4; i++) { if (cBuf[i]) free(cBuf[i]); cBuf[i] = 0; } cLoadedMood = -1; }

static bool loadSprIndex() {
  customSpr = false; freeCustom();
  File f = LittleFS.open("/spr.bin", "r");
  if (!f) return false;
  uint8_t mg[4]; uint8_t hd[20 * 6];
  bool ok = f.read(mg, 4) == 4 && !memcmp(mg, "BPS1", 4)
         && f.read((uint8_t*)&cPal[1], 510) == 510 && f.read(hd, sizeof hd) == (int)sizeof hd;
  if (ok) {
    uint32_t off = 4 + 510 + sizeof hd;
    for (int i = 0; i < 20 && ok; i++) {
      cEnt[i].w = hd[i * 6]; cEnt[i].h = hd[i * 6 + 1];
      memcpy(&cEnt[i].len, hd + i * 6 + 2, 4);
      cEnt[i].off = off; off += cEnt[i].len;
      if (!cEnt[i].w || cEnt[i].w > SW || !cEnt[i].h || cEnt[i].h > 150 || cEnt[i].len > 40000 || (cEnt[i].len & 1)) ok = false;
    }
    if (off != f.size()) ok = false;
  }
  f.close();
  customSpr = ok; cPal[0] = 0;
  return ok;
}

static void ensureMood(int m) {
  if (!customSpr || cLoadedMood == m) return;
  freeCustom();
  File f = LittleFS.open("/spr.bin", "r");
  bool ok = (bool)f;
  for (int i = 0; i < 4 && ok; i++) {
    CEnt& e = cEnt[m * 4 + i];
    cBuf[i] = (uint8_t*)malloc(e.len);
    if (!cBuf[i] || !f.seek(e.off) || f.read(cBuf[i], e.len) != (int)e.len) { ok = false; break; }
    cLoaded[i] = {e.w, e.h, cBuf[i], e.len};
  }
  if (f) f.close();
  if (ok) cLoadedMood = m; else { freeCustom(); customSpr = false; }   // falhou: volta ao padrao
}

static const SprFrame& getFrame(int m, int fi) { return customSpr ? cLoaded[fi] : SPR[m][fi]; }

static void drawSprite(const SprFrame& s, int x0, int y0) {
  const uint16_t* pal = customSpr ? cPal : SPR_PAL;
  int x = 0, y = 0;
  for (uint32_t i = 0; i + 1 < s.len; i += 2) {
    uint8_t idx = pgm_read_byte(s.data + i);
    int n = pgm_read_byte(s.data + i + 1);
    while (n > 0) {
      int run = min(n, (int)s.w - x);
      if (idx) spr.drawFastHLine(x0 + x, y0 + y, run, pal[idx]);
      x += run; n -= run;
      if (x >= s.w) { x = 0; y++; }
    }
  }
}

void drawScene(Mood m, int f) {
  drawRoom(m, &spr, SX, SY);

  bool petting = millis() < petUntil;       // carinho: ele fica feliz, qualquer que seja o humor
  bool preview = !petting && millis() < previewUntil && previewMood >= 0;
  Mood em = preview ? (Mood)previewMood : m;   // humor mostrado (previa troca o humor real)
  Mood sm = petting ? HAPPY : em;           // humor do sprite
  int cx = SW / 2, jump = 0, fi = (f / 2) % 4;   // 4 quadros por humor
  switch (sm) {
    case FED:    { static const int8_t J[8] = {0, -4, -8, -9, -8, -4, 0, 0}; jump = J[f % 8]; break; }
    case HAPPY:  if (!petting) cx += (int)(8 * sinf(f * 0.15f)); jump = (f % 2) ? (petting ? -5 : -3) : 0; break;
    case NORMAL: cx += (int)(4 * sinf(f * 0.1f)); fi = (f / 4) % 4; break;
    case HUNGRY: cx += (f % 2) ? 2 : -2; break;
    case DYING:  cx += (int)(2 * sinf(f * 0.25f)); jump = 2; fi = (f / 3) % 4; break;
  }
  ensureMood(sm);
  const SprFrame& s = getFrame(sm, fi);
  int y0 = 152 + jump - s.h, x0 = cx - s.w / 2;

  spr.fillEllipse(cx, 153, 44 - abs(jump) / 2, 4, 0x0000);   // sombra
  drawSprite(s, x0, y0);

  // efeitos
  if (petting) {                                              // coracoes subindo
    for (int k = 0; k < 4; k++) {
      int hy = y0 + 50 - ((f * 4 + k * 22) % 96), hx = cx + ((k % 2) ? 38 : -52) + (k * 7) % 11;
      if (hy > 0) heart(spr, hx, hy, 0xFC76, 3);
    }
  } else switch (em) {
    case FED: {
      int ph = f % 10;
      if (ph < 8) {                                           // moeda caindo na boca
        int my = y0 + s.h * 40 / 100, cy = -10 + ph * (my + 10) / 8;
        spr.fillCircle(cx, cy, 7, 0xFD20); spr.drawCircle(cx, cy, 7, 0xBBC0); spr.fillRect(cx - 1, cy - 4, 2, 8, 0xBBC0);
      }
      sparkle(10, y0 + 20, ((f + 0) % 4 < 2) ? 0x07E0 : TH[em].bg);
      sparkle(150, y0 + 50, ((f + 1) % 4 < 2) ? 0xFFFF : TH[em].bg);
      sparkle(145, y0 + 10, ((f + 2) % 4 < 2) ? 0x07E0 : TH[em].bg);
      sparkle(8, y0 + 90, ((f + 3) % 4 < 2) ? 0xFFFF : TH[em].bg);
      break;
    }
    case HAPPY: {
      int hy = y0 + 40 - (f % 12) * 4;
      if (hy > 0) heart(spr, (f % 24 < 12) ? cx + 40 : cx - 52, hy, 0xFC76, 3);
      break;
    }
    case HUNGRY: {
      if (f % 6 < 4) {                                        // balao: pensa em moeda
        spr.fillCircle(cx + 18, y0 + 12, 2, 0xFFFF); spr.fillCircle(cx + 24, y0 + 3, 3, 0xFFFF);
        spr.fillRoundRect(cx + 26, 1, 44, 26, 8, 0xFFFF);
        spr.fillCircle(cx + 48, 14, 8, 0xFD20); spr.drawCircle(cx + 48, 14, 8, 0xBBC0);
        spr.fillRect(cx + 47, 9, 2, 10, 0xBBC0);
      }
      break;
    }
    case DYING: {
      int gy = y0 + 12 - (f % 16) * 5, gx = cx + 24 + (int)(10 * sinf(f * 0.5f));   // alminha subindo
      if (gy > 8) { spr.fillRoundRect(gx - 8, gy - 8, 16, 16, 6, 0xCF1F); spr.fillRect(gx - 4, gy - 3, 2, 3, 0x18C3); spr.fillRect(gx + 2, gy - 3, 2, 3, 0x18C3); }
      break;
    }
    default: break;
  }
  spr.pushSprite(SX, SY);
}

// ---------- Toque: 1 toque = carinho, 2 toques = previa de outra animacao ----------
void pollTouch() {
  static uint32_t last = 0, lastTap = 0;
  static bool down = false;
  uint32_t now = millis();
  if (pendingTap && now - pendingTap > 380) { petUntil = now + 2500; pendingTap = 0; }   // era toque simples
  if (now - last < 40) return;
  last = now;
  bool t = ts.tirqTouched() && ts.touched();
  if (!t) { down = false; return; }
  if (down) return;                                   // so conta o inicio do toque
  down = true;
  TS_Point p = ts.getPoint();
  int x = map(p.x, 200, 3700, 0, 320), y = map(p.y, 240, 3800, 0, 240);
  if (y < 40) { ipShowReq = true; return; }                // barra do topo: mostra o IP
  if (!(x > 40 && x < 280 && y > 40 && y < 210)) return;   // area do pet
  if (pendingTap && now - pendingTap <= 380) {        // segundo toque: previa, alterna os 5 humores
    pendingTap = 0;
    previewMood = (previewMood + 1) % 5;
    if (previewMood == (int)curMood) previewMood = (previewMood + 1) % 5;
    previewUntil = now + 3500; petUntil = 0;
  } else pendingTap = now;
}

// ---------- HUD (sobre o quarto, fora da area animada) ----------
static String dots(unsigned long long v) {
  char b[24]; snprintf(b, sizeof b, "%llu", v);
  String s = b;
  for (int i = (int)s.length() - 3; i > 0; i -= 3) s = s.substring(0, i) + "." + s.substring(i);
  return s;
}

void drawHUD(Mood m, Cv& g = SCR) {
  // barra de saciedade (pela variacao de 24h)
  int pct = haveData ? constrain((int)(50 + chg24 * 5), 0, 100) : 50;
  uint16_t pc = pct > 66 ? 0x07E0 : (pct > 33 ? 0xFFE0 : 0xF800);
  g.fillRoundRect(10, 3, 300, 15, 5, 0x0841);
  g.drawRoundRect(10, 3, 300, 15, 5, MOOD_COLOR[m]);
  int w = 296 * pct / 100;
  if (w > 8) g.fillRoundRect(12, 5, w, 11, 3, pc);
  g.setTextDatum(MC_DATUM); g.setTextColor(pct >= 50 ? 0x0000 : 0xFFFF);
  g.drawString(String(pct) + "%", 160, 11, 1);

  // mensagem do humor
  String msg = haveData ? String(MOOD_MSG[m]) : String("Conectando ao mercado...");
  int tw = g.textWidth(msg, 2) + 16, bx = 160 - tw / 2;
  g.fillRoundRect(bx, 21, tw, 18, 6, 0x0000); g.drawRoundRect(bx, 21, tw, 18, 6, MOOD_COLOR[m]);
  g.setTextColor(MOOD_COLOR[m]); g.drawString(msg, 160, 30, 2);

  // caixa do preco
  g.fillRoundRect(196, 204, 118, 34, 5, 0x0000); g.drawRoundRect(196, 204, 118, 34, 5, 0xFFE0);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(0xFD20); g.drawString("BTC/USD", 202, 207, 1);
  if (haveData) {
    g.setTextColor(chg24 >= 0 ? 0x07E0 : 0xF800);
    g.drawString(String(chg24 >= 0 ? "+" : "") + String(chg24, 2) + "% 24h", 250, 207, 1);
  }
  g.setTextColor(0xFFFF);
  g.drawString(haveData && btcUsd > 0 ? "$" + dots((unsigned long long)btcUsd) : String("--"), 202, 213, 4);

  // caixa do saldo
  char bb[24]; snprintf(bb, sizeof bb, "%.8f", balBtc);
  String bal = haveData ? String(bb) + " BTC" : String("carregando...");
  if (!lastFetchOk) bal = "sem conexao";
  else if (gMask && haveData) bal = "*.******** BTC";
  int bw = min(g.textWidth(bal, 2) + 34, 186);
  g.fillRoundRect(6, 204, bw, 34, 5, 0x0000); g.drawRoundRect(6, 204, bw, 34, 5, 0xFFE0);
  g.fillCircle(16, 214, 4, !lastFetchOk ? 0xF800 : (pendingTx ? 0xFFE0 : 0x07E0));
  g.setTextColor(0xFFFF); g.drawString(bal, 26, 207, 2);
  if (haveData) {
    g.setTextColor(0xBDF7);
    g.drawString(gMask ? String("~ $*****") : "~ $" + String(balBtc * btcUsd, 2), 12, 225, 1);
    if (pendingTx) { g.setTextColor(0xFFE0); g.drawString(String(pendingTx) + " tx pendente(s)", 82, 225, 1); }
  }
}

void drawUI() {
  curMood = haveData ? moodFromChange(chg24) : NORMAL;
  drawRoom(curMood, &tft, 0, 0);
  drawHUD(curMood);
}

void showMsg(const String& a, const String& b = "") {
  tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(a, 160, 100, 4); tft.setTextColor(0xBDF7, TFT_BLACK); tft.drawString(b, 160, 140, 2);
}

// ---------- Rede / dados ----------
bool httpGetJson(const String& url, JsonDocument& doc) {
  WiFiClientSecure c; c.setInsecure();   // apenas dados publicos; nenhum segredo trafega
  HTTPClient h; h.setTimeout(10000);
  h.setUserAgent("BitPet/1.0 (ESP32)");
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!h.begin(c, url)) { Serial.println("http begin falhou"); return false; }
  int code = h.GET();
  bool ok = false;
  if (code == 200) {
    String body = h.getString();
    DeserializationError e = deserializeJson(doc, body);
    ok = !e;
    if (e) Serial.println(String("json erro: ") + e.c_str());
  } else {
    Serial.println(String("HTTP ") + code + " em " + url);
  }
  h.end();
  return ok;
}

bool fetchData() {
  bool okPrice = false, okBal = false;
  {
    JsonDocument d;
    if (httpGetJson("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true", d)) {
      btcUsd = d["bitcoin"]["usd"] | btcUsd;
      chg24 = d["bitcoin"]["usd_24h_change"] | chg24;
      okPrice = btcUsd > 0;
    }
  }
  {
    JsonDocument d;
    if (httpGetJson("https://mempool.space/api/address/" + cfgAddr, d)) {
      double conf = (double)(d["chain_stats"]["funded_txo_sum"] | 0LL) - (double)(d["chain_stats"]["spent_txo_sum"] | 0LL);
      double mem  = (double)(d["mempool_stats"]["funded_txo_sum"] | 0LL) - (double)(d["mempool_stats"]["spent_txo_sum"] | 0LL);
      balBtc = (conf + mem) / 1e8;
      pendingTx = d["mempool_stats"]["tx_count"] | 0;
      okBal = true;
    }
  }
  if (okPrice || okBal) haveData = true;
  return okPrice && okBal;
}

// ---------- Acesso por PIN + pagina de configuracao ----------
// O PIN aleatorio so aparece na tela do aparelho. Sem ele, nada pode ser alterado.
bool portalMode = false, pinActive = false, pinShowReq = false;
String pinCode, authToken;
uint32_t pinUntil = 0, lastPinGen = 0, lockUntil = 0, tokenUntil = 0;
int pinFails = 0;
bool upBad = false; File upFile; uint32_t upSize = 0;

bool validAddr(const String& a) {
  if (a.length() < 26 || a.length() > 90) return false;
  for (size_t i = 0; i < a.length(); i++) if (!isalnum((unsigned char)a[i])) return false;
  return a.startsWith("bc1") || a.startsWith("1") || a.startsWith("3");
}

static bool authed() {
  String t = web.arg("t");
  return authToken.length() && t == authToken && millis() < tokenUntil;
}

static String ipText(bool mask) {
  if (mask) return "192.168.xxx.xxx";
  return portalMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

static void drawPinOverlay(Cv& g = SCR, const String& pin = pinCode, bool mask = false) {
  g.fillRoundRect(40, 60, 240, 120, 10, 0x0000);
  g.drawRoundRect(40, 60, 240, 120, 10, 0xFFE0); g.drawRoundRect(41, 61, 238, 118, 9, 0xFFE0);
  g.setTextDatum(MC_DATUM);
  g.setTextColor(0xFFE0); g.drawString("PIN de acesso", 160, 78, 2);
  g.setTextColor(0xFFFF); g.setTextSize(2); g.drawString(pin, 160, 115, 4); g.setTextSize(1);
  g.setTextColor(0xBDF7); g.drawString("http://" + ipText(mask), 160, 150, 2);
  g.drawString("vale por 2 minutos", 160, 168, 1);
}

static void drawIpOverlay(Cv& g = SCR, bool mask = false) {
  g.fillRoundRect(40, 70, 240, 100, 10, 0x0000);
  g.drawRoundRect(40, 70, 240, 100, 10, 0x5DFF); g.drawRoundRect(41, 71, 238, 98, 9, 0x5DFF);
  g.setTextDatum(MC_DATUM);
  g.setTextColor(0x5DFF); g.drawString("Configurar pelo navegador", 160, 90, 2);
  g.setTextColor(0xFFFF); g.drawString(ipText(mask), 160, 122, 4);
  g.setTextColor(0xBDF7); g.drawString("http://bitpet.local (ou o IP acima)", 160, 154, 2);
}

static void pinTick() {
  if (ipShowReq) { ipShowReq = false; if (!pinActive) { ipActive = true; ipUntil = millis() + 10000; drawIpOverlay(); } }
  if (ipActive && millis() > ipUntil) { ipActive = false; if (!pinActive) drawUI(); }
  if (pinShowReq) { pinShowReq = false; if (pinCode.length()) { pinActive = true; ipActive = false; drawPinOverlay(); } }
  if (pinActive && (pinCode.isEmpty() || millis() > pinUntil)) {
    pinActive = false; pinCode = "";
    if (portalMode) showMsg("Configure no celular", "Wi-Fi BitPet-Setup > http://192.168.4.1");
    else drawUI();
  }
}

static String randHex(int n) { String r; for (int i = 0; i < n; i++) r += String((unsigned)(esp_random() & 15), HEX); return r; }

static void setupRoutes() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", PAGE); });
  web.on("/pin", HTTP_POST, [] {
    if (millis() < lockUntil) { web.send(429, "text/plain", "Bloqueado por tentativas erradas. Aguarde 1 minuto."); return; }
    if (lastPinGen && millis() - lastPinGen < 8000) { web.send(429, "text/plain", "Aguarde alguns segundos."); return; }
    lastPinGen = millis();
    char b[8]; snprintf(b, sizeof b, "%06u", (unsigned)(esp_random() % 1000000));
    pinCode = b; pinUntil = millis() + 120000; pinFails = 0; pinShowReq = true;
    web.send(200, "text/plain", "ok");
  });
  web.on("/auth", HTTP_POST, [] {
    if (millis() < lockUntil) { web.send(429, "text/plain", "Bloqueado por tentativas erradas. Aguarde 1 minuto."); return; }
    if (pinCode.isEmpty() || millis() > pinUntil) { web.send(403, "text/plain", "Gere um PIN primeiro."); return; }
    if (web.arg("p") == pinCode) {
      authToken = randHex(24); tokenUntil = millis() + 600000; pinCode = "";
      web.send(200, "text/plain", authToken);
    } else {
      if (++pinFails >= 3) { lockUntil = millis() + 60000; pinCode = ""; pinFails = 0; web.send(403, "text/plain", "PIN errado 3 vezes. Bloqueado por 1 minuto."); }
      else web.send(403, "text/plain", "PIN incorreto.");
    }
  });
  web.on("/info", HTTP_GET, [] {
    if (!authed()) { web.send(403, "text/plain", "Sem acesso"); return; }
    String s = cfgSsid, a = cfgAddr; s.replace("\"", "'"); s.replace("\\", "/");
    web.send(200, "application/json", "{\"ssid\":\"" + s + "\",\"addr\":\"" + a + "\"}");
  });
  web.on("/screen", HTTP_GET, [] {   // captura da tela em BMP 24 bits (320x240), so com PIN. Redesenha em faixas na RAM.
    if (!authed()) { web.send(403, "text/plain", "Sem acesso"); return; }
    const int W = 320, H = 240, BH = 24;
    bool mask = web.arg("m") != "0"; String ov = web.arg("o");
    TFT_eSprite st(&tft); st.setColorDepth(16);
    if (!st.createSprite(W, BH)) { web.send(500, "text/plain", "Sem memoria"); return; }
    uint32_t rowBytes = W * 3, total = 54 + rowBytes * H, imgSize = rowBytes * H;
    uint8_t hd[54] = {'B', 'M'};
    memcpy(hd + 2, &total, 4); hd[10] = 54; hd[14] = 40;
    memcpy(hd + 18, &W, 4); memcpy(hd + 22, &H, 4); hd[26] = 1; hd[28] = 24; memcpy(hd + 34, &imgSize, 4);
    web.setContentLength(total);
    web.send(200, "image/bmp", "");
    web.sendContent((const char*)hd, 54);
    static uint8_t row[W * 3];
    Cv c{&st, 0};
    gMask = mask;
    for (int yb = H - BH; yb >= 0; yb -= BH) {          // BMP grava de baixo para cima
      c.dy = yb;
      drawRoom(curMood, &st, 0, yb);
      animBits(curMood, frame, c);
      spr.pushToSprite(&st, SX, SY - yb);               // pet do ultimo quadro
      drawHUD(curMood, c);
      if (ov == "ip") drawIpOverlay(c, mask);
      else if (ov == "pin") drawPinOverlay(c, "123456", mask);
      for (int y = BH - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
          uint16_t k = st.readPixel(x, y);
          uint8_t r5 = k >> 11, g6 = (k >> 5) & 63, b5 = k & 31;
          row[x * 3] = (b5 << 3) | (b5 >> 2); row[x * 3 + 1] = (g6 << 2) | (g6 >> 4); row[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
        }
        web.sendContent((const char*)row, rowBytes);
      }
    }
    gMask = false; st.deleteSprite();
  });
  web.on("/save", HTTP_POST, [] {
    if (!authed()) { web.send(403, "text/plain", "Sem acesso"); return; }
    String ss = web.arg("s"), pw = web.arg("p"), a = web.arg("a"); a.trim();
    if (ss.isEmpty() || !validAddr(a)) { web.send(400, "text/plain", "Endereco invalido"); return; }
    if (pw.isEmpty() && ss == cfgSsid) pw = cfgPass;
    prefs.begin("bitpet", false);
    prefs.putString("ssid", ss); prefs.putString("pass", pw); prefs.putString("addr", a);
    prefs.end();
    web.send(200, "text/plain", "ok");
    delay(800); ESP.restart();
  });
  web.on("/sprite/reset", HTTP_POST, [] {
    if (!authed()) { web.send(403, "text/plain", "Sem acesso"); return; }
    freeCustom(); customSpr = false; LittleFS.remove("/spr.bin");
    web.send(200, "text/plain", "ok");
  });
  web.on("/sprite", HTTP_POST, [] {
    if (upFile) upFile.close();
    if (upBad) { LittleFS.remove("/spr.tmp"); web.send(upSize ? 413 : 403, "text/plain", upSize ? "Arquivo grande demais" : "Sem acesso"); return; }
    LittleFS.remove("/spr.bin"); LittleFS.rename("/spr.tmp", "/spr.bin");
    if (loadSprIndex()) web.send(200, "text/plain", "ok");
    else { LittleFS.remove("/spr.bin"); web.send(400, "text/plain", "Arquivo de sprite invalido"); }
  }, [] {
    HTTPUpload& u = web.upload();
    if (u.status == UPLOAD_FILE_START) {
      upBad = !authed(); upSize = 0; freeCustom();
      if (!upBad) upFile = LittleFS.open("/spr.tmp", "w");
      if (!upBad && !upFile) upBad = true;
    } else if (u.status == UPLOAD_FILE_WRITE && !upBad) {
      upSize += u.currentSize;
      if (upSize > 900000 || upFile.write(u.buf, u.currentSize) != u.currentSize) { upBad = true; upFile.close(); }
    } else if (u.status == UPLOAD_FILE_END) { if (upFile) upFile.close(); }
  });
  web.onNotFound([] { if (portalMode) web.send_P(200, "text/html", PAGE); else web.send(404, "text/plain", "404"); });   // captive portal
  web.begin();
}

void runPortal() {
  portalMode = true;
  showMsg("Configure no celular", "Wi-Fi BitPet-Setup > http://192.168.4.1");
  WiFi.mode(WIFI_AP); WiFi.softAP("BitPet-Setup");
  dns.start(53, "*", WiFi.softAPIP());
  setupRoutes();
  for (;;) { dns.processNextRequest(); web.handleClient(); pinTick(); delay(2); }
}

bool connectWifi() {
  WiFi.mode(WIFI_STA); WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

// ---------- Menu de boot: teste de cores / reset ----------
// BOOT curto = inverte as cores (salvo). BOOT segurado 2s = apaga Wi-Fi e endereco.
// (nao segure BOOT ao ligar: isso coloca a placa em modo de gravacao)
void drawColorTest() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 107, 110, TFT_RED);
  tft.fillRect(107, 0, 106, 110, TFT_GREEN);
  tft.fillRect(213, 0, 107, 110, TFT_BLUE);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Vermelho / Verde / Azul + fundo preto?", 160, 128, 2);
  tft.drawString("Cores erradas: aperte BOOT (rapido)", 160, 158, 2);
  tft.drawString("Apagar Wi-Fi e endereco: segure BOOT 2s", 160, 180, 2);
}

void bootMenu() {
  drawColorTest();
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {   // menu de cores: 3 s (cada aperto de BOOT reinicia a contagem)
    if (digitalRead(0) == LOW) {
      uint32_t p = millis();
      while (digitalRead(0) == LOW && millis() - p < 2000) delay(10);
      prefs.begin("bitpet", false);
      if (digitalRead(0) == LOW) {                        // longo: apaga config
        prefs.clear(); prefs.putBool("inv", invertColors); prefs.end();
        cfgSsid = ""; cfgAddr = "";
        showMsg("Config apagada"); delay(1500);
        while (digitalRead(0) == LOW) delay(10);
        return;
      }
      invertColors = !invertColors;                        // curto: inverte
      prefs.putBool("inv", invertColors); prefs.end();
      tft.invertDisplay(invertColors);
      t0 = millis();
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  prefs.begin("bitpet", true);
  invertColors = prefs.getBool("inv", true);
  prefs.end();

  tft.init(); tft.setRotation(ROTATION);
  tft.invertDisplay(invertColors);
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  spr.setColorDepth(16);
  if (!spr.createSprite(SW, SH)) Serial.println("sem memoria para o sprite!");
  mon.setColorDepth(16); mon.createSprite(64, 56);
  if (!LittleFS.begin(true)) Serial.println("LittleFS falhou");
  loadSprIndex();
  touchSPI.begin(25, 39, 32, 33);   // CLK, MISO, MOSI, CS
  ts.begin(touchSPI); ts.setRotation(ROTATION);

  bootMenu();

  prefs.begin("bitpet", true);
  cfgSsid = prefs.getString("ssid", ""); cfgPass = prefs.getString("pass", ""); cfgAddr = prefs.getString("addr", "");
  prefs.end();
  if (cfgSsid.isEmpty() || cfgAddr.isEmpty()) runPortal();
  showMsg("Conectando...", cfgSsid);
  if (!connectWifi()) runPortal();   // Wi-Fi errado => reabre configuracao
  showMsg("Conectado! IP: " + WiFi.localIP().toString(), "Toque na barra do topo para rever o IP");
  delay(5000);
  tft.fillScreen(TFT_BLACK);
  setupRoutes(); MDNS.begin("bitpet");
  drawUI();
  lastFetchOk = fetchData(); drawUI(); lastFetch = millis();
}

void loop() {
  web.handleClient(); pinTick();
  if (!pinActive) pollTouch();
  if (millis() - lastFetch > 60000) {
    lastFetch = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    lastFetchOk = fetchData(); drawUI();
    if (pinActive) pinShowReq = true;
    else if (ipActive) drawIpOverlay();
  }
  if (!pinActive && !ipActive && millis() - lastFrame > FRAME_MS) {
    lastFrame = millis(); frame++;
    drawScene(curMood, frame);
    animBits(curMood, frame);
  }
}
