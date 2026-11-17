/*
  MesaClima - protótipo de acessório de mesa (ESP32 + OLED 128x64)

  - Liga, conecta no Wi-Fi salvo e mostra hora + clima da cidade onde está.
  - Primeira vez / rede nova: cria o Wi-Fi "MesaClima-XXXX"; o portal abre no celular.
  - Localização automática por IP (ipwho.is). Opcional: digitar a cidade no portal.
  - Clima e fuso: Open-Meteo (uso NÃO comercial no plano gratuito).
  - Hora: NTP, com o fuso vindo do Open-Meteo (acerta sozinho em qualquer cidade).

  Botão BOOT (GPIO0):
    toque curto ........ próxima tela
    segurar 3 s ........ abre o portal de configuração (Wi-Fi / cidade)
    segurar 10 s ....... apaga tudo (reset de fábrica)

  Bibliotecas (Gerenciador de Bibliotecas da Arduino IDE):
    U8g2 (olikraus), WiFiManager (tzapu, v2.0.17), ArduinoJson (v7.x)
  Placa: "ESP32 Dev Module".
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>
#include <math.h>

// ======================= CONFIGURAÇÃO =======================
#define USE_SH1106 0              // 0 = SSD1306 (0,96")  |  1 = SH1106 (1,3")
#define BUTTON_PIN 0              // botão BOOT da placa
#define PAGE_MS 5000UL            // tempo de cada tela
#define WEATHER_EVERY_MS (15UL * 60UL * 1000UL)
#define RETRY_EVERY_MS 60000UL
#define LOCATION_EVERY_MS (60UL * 60UL * 1000UL)
#define AUTO_PORTAL_AFTER_MS (10UL * 60UL * 1000UL)  // sem Wi-Fi por 10 min -> abre portal
#define STALE_AFTER_MS (60UL * 60UL * 1000UL)        // dados com mais de 1 h = desatualizados
#define NIGHT_START_HOUR 23       // tela escurece (poupa o OLED)
#define NIGHT_END_HOUR 6
// ============================================================

#if USE_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

// (fica aqui no topo porque a Arduino IDE gera declarações de função antes do resto do código)
enum Icon { ICON_SUN, ICON_MOON, ICON_PARTLY, ICON_CLOUD, ICON_RAIN, ICON_STORM, ICON_SNOW, ICON_FOG };

Preferences prefs;

// ---------- Estado ----------
struct Weather {
  float temp = 0, feels = 0, wind = 0, pressure = 0;
  int humidity = 0, code = 0;
  bool isDay = true;
  int dCode[4] = {0};
  float dMax[4] = {0}, dMin[4] = {0};
  int dRain[4] = {0};
  char dDate[4][11] = {{0}};
  char sunrise0[6] = "--:--", sunset0[6] = "--:--", sunrise1[6] = "--:--";
} wx;

double lat = 0, lon = 0;
String placeName = "";
String manualCity = "";
bool haveLoc = false;

bool tzKnown = false;
int32_t tzOffset = 0;
bool ntpStarted = false;

bool everGood = false;
uint32_t lastGoodMs = 0, lastLocMs = 0, nextFetchMs = 0;
int failCount = 0;

int page = 0;
uint32_t pageStartMs = 0, lastDrawMs = 0, heldMs = 0;
uint32_t offlineSinceMs = 0, lastBeginMs = 0;

WiFiManagerParameter* gCityParam = nullptr;
volatile bool inPortal = false;
volatile bool gGotIp = false;
const char* gFailMsg = nullptr;
String gApName;

const char* DIAS[] = {"domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sábado"};
const char* DIAS_CURTO[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"};
const char* MESES[] = {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"};

// ======================= UTILITÁRIOS =======================
String apName() {
  uint64_t mac = ESP.getEfuseMac();
  char b[24];
  snprintf(b, sizeof b, "MesaClima-%04X", (uint16_t)(mac >> 32));
  return String(b);
}

String urlEncode(const String& s) {
  String out;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      snprintf(buf, sizeof buf, "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

int dowFromDate(const char* s) {  // 0 = domingo
  int y, m, d;
  if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return 0;
  static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + tt[m - 1] + d) % 7;
}

void copyHM(char* dst, const char* iso) {  // "2026-10-13T05:32" -> "05:32"
  if (iso && strlen(iso) >= 16) {
    memcpy(dst, iso + 11, 5);
    dst[5] = 0;
  } else {
    strcpy(dst, "--:--");
  }
}

bool localNow(struct tm& t) {
  if (!tzKnown) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // ainda não sincronizou
  localtime_r(&now, &t);
  return true;
}

// ======================= TELA: BÁSICO =======================
String fitText(String s, int maxW) {
  while (s.length() > 0 && u8g2.getUTF8Width(s.c_str()) > maxW) {
    int i = s.length() - 1;
    while (i > 0 && (((uint8_t)s[i]) & 0xC0) == 0x80) i--;  // não corta no meio de um caractere UTF-8
    s.remove(i);
  }
  return s;
}

void drawCentered(const char* text, int y, int x0 = 0, int w = 128) {
  int tw = u8g2.getUTF8Width(text);
  u8g2.drawUTF8(x0 + (w - tw) / 2, y, text);
}

void drawRight(const char* text, int xRight, int y) {
  u8g2.drawUTF8(xRight - u8g2.getUTF8Width(text), y, text);
}

// Mensagem de até 3 linhas centralizada (sem limpar/enviar o buffer)
void drawStatus(const char* a, const char* b = nullptr, const char* c = nullptr) {
  u8g2.setFont(u8g2_font_6x12_tf);
  const char* L[3] = {a, b, c};
  int n = 0;
  for (int i = 0; i < 3; i++) if (L[i] && *L[i]) n++;
  int y = (64 - n * 16) / 2 + 12;
  for (int i = 0; i < 3; i++) {
    if (L[i] && *L[i]) {
      drawCentered(L[i], y);
      y += 16;
    }
  }
}

void showMessage(const char* a, const char* b = nullptr, const char* c = nullptr) {
  u8g2.clearBuffer();
  drawStatus(a, b, c);
  u8g2.sendBuffer();
}

void showPortalScreen(const String& ap) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  drawCentered("Conecte o celular ao", 12);
  u8g2.setFont(u8g2_font_7x13B_tf);
  drawCentered(ap.c_str(), 29);
  u8g2.setFont(u8g2_font_6x12_tf);
  drawCentered("A página abre sozinha", 46);
  drawCentered("ou acesse 192.168.4.1", 60);
  u8g2.sendBuffer();
}

// ======================= ÍCONES =======================
Icon iconFor(int code, bool day) {
  switch (code) {
    case 0: case 1: return day ? ICON_SUN : ICON_MOON;
    case 2: return day ? ICON_PARTLY : ICON_CLOUD;
    case 3: return ICON_CLOUD;
    case 45: case 48: return ICON_FOG;
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: return ICON_RAIN;
    case 71: case 73: case 75: case 77: case 85: case 86: return ICON_SNOW;
    case 95: case 96: case 99: return ICON_STORM;
    default: return ICON_CLOUD;
  }
}

const char* descFor(int code) {
  switch (code) {
    case 0: return "Céu limpo";
    case 1: return "Quase limpo";
    case 2: return "Parc. nublado";
    case 3: return "Nublado";
    case 45: case 48: return "Neblina";
    case 51: case 53: case 55: case 56: case 57: return "Garoa";
    case 61: case 63: case 65: return "Chuva";
    case 66: case 67: return "Chuva gelada";
    case 71: case 73: case 75: case 77: return "Neve";
    case 80: case 81: case 82: return "Pancadas de chuva";
    case 85: case 86: return "Pancadas de neve";
    case 95: case 96: case 99: return "Tempestade";
    default: return "--";
  }
}

void drawCloud(int cx, int cy, int s, int g = 0) {
  int r1 = s * 20 / 100 + g, r2 = s * 28 / 100 + g, r3 = s * 18 / 100 + g;
  int x1 = cx - s * 22 / 100, y1 = cy + s * 5 / 100;
  int x2 = cx, y2 = cy - s * 8 / 100;
  int x3 = cx + s * 26 / 100, y3 = cy + s * 8 / 100;
  u8g2.drawDisc(x1, y1, r1);
  u8g2.drawDisc(x2, y2, r2);
  u8g2.drawDisc(x3, y3, r3);
  u8g2.drawBox(x1, y1, x3 - x1 + 1, r1 + 1);
}

void drawSunShape(int cx, int cy, int r) {
  u8g2.drawDisc(cx, cy, r);
  int a = r + 2, b = r + 2 + max(2, r * 2 / 3);
  for (int k = 0; k < 8; k++) {
    float ang = k * 0.785398f;
    float c = cosf(ang), sn = sinf(ang);
    u8g2.drawLine(cx + lroundf(c * a), cy + lroundf(sn * a), cx + lroundf(c * b), cy + lroundf(sn * b));
  }
}

void drawIcon(Icon ic, int cx, int cy, int s) {
  switch (ic) {
    case ICON_SUN:
      drawSunShape(cx, cy, s * 22 / 100);
      break;
    case ICON_MOON: {
      int r = s * 32 / 100;
      u8g2.drawDisc(cx, cy, r);
      u8g2.setDrawColor(0);
      u8g2.drawDisc(cx + r * 55 / 100, cy - r * 35 / 100, r * 85 / 100);
      u8g2.setDrawColor(1);
      break;
    }
    case ICON_PARTLY:
      drawSunShape(cx - s * 17 / 100, cy - s * 17 / 100, s * 15 / 100);
      u8g2.setDrawColor(0);
      drawCloud(cx + s * 8 / 100, cy + s * 12 / 100, s * 80 / 100, 2);
      u8g2.setDrawColor(1);
      drawCloud(cx + s * 8 / 100, cy + s * 12 / 100, s * 80 / 100);
      break;
    case ICON_CLOUD:
      drawCloud(cx, cy, s);
      break;
    case ICON_RAIN: {
      drawCloud(cx, cy - s * 12 / 100, s);
      int y0 = cy + s * 14 / 100 + 2;
      for (int i = -1; i <= 1; i++) {
        int x = cx + i * s * 20 / 100;
        u8g2.drawLine(x, y0, x - max(1, s * 5 / 100), y0 + max(3, s * 16 / 100));
      }
      break;
    }
    case ICON_STORM: {
      drawCloud(cx, cy - s * 12 / 100, s);
      int y0 = cy + s * 12 / 100, h = max(6, s * 28 / 100), w = max(2, s * 6 / 100);
      u8g2.drawLine(cx + w, y0, cx - w, y0 + h / 2);
      u8g2.drawLine(cx - w, y0 + h / 2, cx + w, y0 + h / 2);
      u8g2.drawLine(cx + w, y0 + h / 2, cx - w, y0 + h);
      break;
    }
    case ICON_SNOW: {
      drawCloud(cx, cy - s * 12 / 100, s);
      int y0 = cy + s * 14 / 100 + 3, rr = s >= 28 ? 2 : 1;
      for (int i = -1; i <= 1; i++) u8g2.drawDisc(cx + i * s * 20 / 100, y0 + (i == 0 ? 3 : 0), rr);
      break;
    }
    case ICON_FOG: {
      int th = max(1, s / 16);
      for (int i = 0; i < 3; i++) {
        int y = cy - s * 15 / 100 + i * s * 20 / 100;
        int w = (i == 1) ? s * 80 / 100 : s * 60 / 100;
        u8g2.drawBox(cx - w / 2, y, w, th);
      }
      break;
    }
  }
}

// Número grande (o sinal de menos é desenhado à mão; a fonte só tem dígitos)
int drawBigNumber(int x, int baseline, int value) {
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int w = 0;
  if (value < 0) {
    u8g2.drawBox(x, baseline - 14, 8, 3);
    w += 11;
    value = -value;
  }
  char b[8];
  snprintf(b, sizeof b, "%d", value);
  u8g2.drawStr(x + w, baseline, b);
  return w + u8g2.getStrWidth(b);
}

// ======================= TELAS =======================
bool isStale() { return everGood && (uint32_t)(millis() - lastGoodMs) > STALE_AFTER_MS; }

void drawStatusPage() {
  if (WiFi.status() != WL_CONNECTED) {
    drawStatus("Sem Wi-Fi", "Segure o botão 3s", "para configurar");
  } else if (failCount > 0) {
    drawStatus("Sem dados do clima", "Tentando de novo...");
  } else {
    drawStatus("Buscando dados...");
  }
}

void drawHeader() {
  char right[16] = "";
  if (everGood) {
    if (isStale()) strcpy(right, "offline");
    else snprintf(right, sizeof right, "%d°C", (int)lroundf(wx.temp));
  }
  u8g2.setFont(u8g2_font_6x12_tf);
  int rw = right[0] ? u8g2.getUTF8Width(right) : 0;
  String city = placeName.length() ? placeName : String("MesaClima");
  city = fitText(city, 128 - rw - 8);
  u8g2.drawUTF8(0, 10, city.c_str());
  if (rw) drawRight(right, 128, 10);
}

// Tela 0: hora + data
void pageClock() {
  struct tm t;
  if (!localNow(t)) {
    if (WiFi.status() != WL_CONNECTED) drawStatusPage();
    else drawStatus("Acertando a hora...");
    return;
  }
  drawHeader();
  char hh[4], mm[4];
  snprintf(hh, sizeof hh, "%02d", t.tm_hour);
  snprintf(mm, sizeof mm, "%02d", t.tm_min);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int wH = u8g2.getStrWidth(hh), wM = u8g2.getStrWidth(mm), gap = 10;
  int x0 = (128 - (wH + gap + wM)) / 2;
  u8g2.drawStr(x0, 48, hh);
  u8g2.drawStr(x0 + wH + gap, 48, mm);
  if (t.tm_sec % 2 == 0) {  // dois pontos piscando
    int cx = x0 + wH + gap / 2 - 1;
    u8g2.drawBox(cx, 28, 3, 3);
    u8g2.drawBox(cx, 38, 3, 3);
  }
  char date[32];
  snprintf(date, sizeof date, "%s, %d %s", DIAS[t.tm_wday], t.tm_mday, MESES[t.tm_mon]);
  u8g2.setFont(u8g2_font_6x12_tf);
  drawCentered(date, 62);
}

// Tela 1: tempo agora
void pageNow() {
  drawHeader();
  drawIcon(iconFor(wx.code, wx.isDay), 24, 34, 34);
  int w = drawBigNumber(52, 49, (int)lroundf(wx.temp));
  u8g2.drawCircle(52 + w + 4, 28, 2);  // símbolo de grau
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(52 + w + 9, 35, "C");
  u8g2.setFont(u8g2_font_6x12_tf);
  drawCentered(descFor(wx.code), 62);
}

// Tela 2: detalhes (grade 2x3)
void cell(int col, int row, const char* label, const char* value) {
  int x = col * 64 + 4, y = row * 21;
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawUTF8(x, y + 7, label);
  u8g2.setFont(u8g2_font_7x13_tf);
  u8g2.drawUTF8(x, y + 18, value);
}

void pageDetails() {
  char v[24];
  snprintf(v, sizeof v, "%d°C", (int)lroundf(wx.feels));
  cell(0, 0, "Sensação", v);
  snprintf(v, sizeof v, "%d%%", wx.humidity);
  cell(1, 0, "Umidade", v);
  snprintf(v, sizeof v, "%d km/h", (int)lroundf(wx.wind));
  cell(0, 1, "Vento", v);
  snprintf(v, sizeof v, "%d hPa", (int)lroundf(wx.pressure));
  cell(1, 1, "Pressão", v);
  snprintf(v, sizeof v, "%d%%", wx.dRain[0]);
  cell(0, 2, "Chuva hoje", v);

  const char* lbl = "Pôr do sol";
  const char* val = wx.sunset0;
  struct tm t;
  if (localNow(t)) {
    char cur[6];
    snprintf(cur, sizeof cur, "%02d:%02d", t.tm_hour, t.tm_min);
    if (strcmp(cur, wx.sunrise0) < 0) { lbl = "Nascer sol"; val = wx.sunrise0; }
    else if (strcmp(cur, wx.sunset0) < 0) { lbl = "Pôr do sol"; val = wx.sunset0; }
    else { lbl = "Nascer sol"; val = wx.sunrise1; }
  } else if (!wx.isDay) {
    lbl = "Nascer sol";
    val = wx.sunrise1;
  }
  cell(1, 2, lbl, val);

  u8g2.drawVLine(63, 0, 64);
  u8g2.drawHLine(0, 21, 128);
  u8g2.drawHLine(0, 42, 128);
}

// Tela 3: previsão (hoje + 3 dias)
void pageForecast() {
  for (int i = 0; i < 4; i++) {
    int cx = 16 + 32 * i;
    char b[12];
    u8g2.setFont(u8g2_font_6x12_tf);
    drawCentered(i == 0 ? "Hoje" : DIAS_CURTO[dowFromDate(wx.dDate[i])], 11, cx - 16, 32);
    drawIcon(iconFor(wx.dCode[i], true), cx, 27, 22);
    u8g2.setFont(u8g2_font_6x13B_tf);
    snprintf(b, sizeof b, "%d°", (int)lroundf(wx.dMax[i]));
    drawCentered(b, 50, cx - 16, 32);
    u8g2.setFont(u8g2_font_6x12_tf);
    snprintf(b, sizeof b, "%d°", (int)lroundf(wx.dMin[i]));
    drawCentered(b, 62, cx - 16, 32);
    if (i > 0) u8g2.drawVLine(32 * i, 16, 46);
  }
}

void drawFrame() {
  u8g2.clearBuffer();
  if (heldMs >= 10000) drawStatus("Solte o botão para", "APAGAR TUDO", "(reset de fábrica)");
  else if (heldMs >= 3000) drawStatus("Solte o botão para", "configurar Wi-Fi", "e cidade");
  else if (page == 0) pageClock();
  else if (!everGood) drawStatusPage();
  else if (page == 1) pageNow();
  else if (page == 2) pageDetails();
  else pageForecast();
  u8g2.sendBuffer();
}

// ======================= REDE =======================
bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();  // PROTÓTIPO: não valida o certificado (ver README)
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setUserAgent("MesaClima/0.1");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();  // getString trata resposta "chunked"
    DeserializationError err = filter
      ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
      : deserializeJson(doc, payload);
    ok = !err;
  } else {
    // mostra so o servidor (a URL completa contem a cidade ou as coordenadas)
    Serial.printf("HTTP %d em %s\n", code, url.substring(8, url.indexOf('/', 8)).c_str());
  }
  http.end();
  return ok;
}

void startNtp() {
  configTime(tzOffset, 0, "pool.ntp.org", "a.ntp.br", "time.google.com");
  ntpStarted = true;
}

void applyTz(int32_t off) {
  bool changed = !tzKnown || off != tzOffset;
  tzOffset = off;
  tzKnown = true;
  if (changed) {
    prefs.putInt("off", off);
    if (ntpStarted) startNtp();
  }
}

bool geocodeCity(const String& name) {
  JsonDocument doc;
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=pt&name=" + urlEncode(name);
  if (!httpGetJson(url, doc)) return false;
  JsonObject r = doc["results"][0].as<JsonObject>();
  if (r.isNull()) return false;
  lat = r["latitude"] | 0.0;
  lon = r["longitude"] | 0.0;
  placeName = String(r["name"] | "");
  haveLoc = true;
  prefs.putDouble("glat", lat);
  prefs.putDouble("glon", lon);
  prefs.putString("gname", placeName);
  prefs.putString("gfor", name);
  return true;
}

bool ipLocation() {
  JsonDocument filter;
  filter["success"] = true;
  filter["city"] = true;
  filter["latitude"] = true;
  filter["longitude"] = true;
  filter["timezone"]["offset"] = true;
  JsonDocument doc;
  if (!httpGetJson("https://ipwho.is/", doc, &filter)) return false;
  if (!(doc["success"] | false)) return false;
  lat = doc["latitude"] | 0.0;
  lon = doc["longitude"] | 0.0;
  placeName = String(doc["city"] | "");
  haveLoc = true;
  if (!tzKnown && doc["timezone"]["offset"].is<int>()) applyTz(doc["timezone"]["offset"].as<int>());
  return true;
}

bool resolveLocation() {
  if (manualCity.length()) {
    if (prefs.getString("gfor", "") == manualCity && prefs.isKey("glat")) {  // já resolvida antes
      lat = prefs.getDouble("glat", 0);
      lon = prefs.getDouble("glon", 0);
      placeName = prefs.getString("gname", manualCity);
      haveLoc = true;
      return true;
    }
    if (geocodeCity(manualCity)) return true;
    // cidade digitada não encontrada: cai para a localização por IP
  }
  return ipLocation();
}

void copyDaily(JsonObject d, int i) {
  wx.dCode[i] = d["weather_code"][i] | 0;
  wx.dMax[i] = d["temperature_2m_max"][i] | 0.0f;
  wx.dMin[i] = d["temperature_2m_min"][i] | 0.0f;
  wx.dRain[i] = d["precipitation_probability_max"][i] | 0;
  const char* ds = d["time"][i] | "";
  strlcpy(wx.dDate[i], ds, sizeof wx.dDate[i]);
}

bool fetchWeather() {
  char url[512];
  snprintf(url, sizeof url,
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,surface_pressure,wind_speed_10m"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
    "&timezone=auto&forecast_days=4&wind_speed_unit=kmh",
    lat, lon);
  JsonDocument doc;
  if (!httpGetJson(String(url), doc)) return false;
  JsonObject cur = doc["current"].as<JsonObject>();
  JsonObject d = doc["daily"].as<JsonObject>();
  if (cur.isNull() || d.isNull()) return false;

  wx.temp = cur["temperature_2m"] | 0.0f;
  wx.feels = cur["apparent_temperature"] | 0.0f;
  wx.humidity = cur["relative_humidity_2m"] | 0;
  wx.wind = cur["wind_speed_10m"] | 0.0f;
  wx.pressure = cur["surface_pressure"] | 0.0f;
  wx.code = cur["weather_code"] | 0;
  wx.isDay = (cur["is_day"] | 1) == 1;
  for (int i = 0; i < 4; i++) copyDaily(d, i);
  copyHM(wx.sunrise0, d["sunrise"][0] | "");
  copyHM(wx.sunset0, d["sunset"][0] | "");
  copyHM(wx.sunrise1, d["sunrise"][1] | "");
  if (doc["utc_offset_seconds"].is<int>()) applyTz(doc["utc_offset_seconds"].as<int>());
  return true;
}

bool doFetch() {
  bool needLoc = !haveLoc ||
    (manualCity.length() == 0 && (uint32_t)(millis() - lastLocMs) > LOCATION_EVERY_MS);
  if (needLoc) {
    if (resolveLocation()) lastLocMs = millis();
    else if (!haveLoc) { failCount++; return false; }
  }
  if (!fetchWeather()) { failCount++; return false; }
  failCount = 0;
  everGood = true;
  lastGoodMs = millis();
  return true;
}

// ======================= PORTAL / RESET =======================
void saveCityParam() {
  if (!gCityParam) return;
  String c = String(gCityParam->getValue());
  c.trim();
  if (c != manualCity) {
    manualCity = c;
    prefs.putString("mcity", c);
    prefs.remove("gfor");
    haveLoc = false;
  }
}

// Mostra na tela de cada etapa da configuração (só enquanto o portal está aberto)
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (!inPortal) return;
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      showMessage("Celular conectado!", "Abra o navegador em", "http://192.168.4.1");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      if (!gGotIp) showPortalScreen(gApName);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      showMessage("Rede encontrada!", "Entrando...");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      gGotIp = true;
      showMessage("Conectado!", "Configuração salva");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t r = info.wifi_sta_disconnected.reason;
      if (r == 201) gFailMsg = "Rede não encontrada";
      else if (r == 202 || r == 204 || r == 15 || r == 2) gFailMsg = "Senha incorreta?";
      else gFailMsg = "Falha ao conectar";
      break;
    }
    default:
      break;
  }
}

// timeoutSec = 0 -> sem limite (primeira configuração). Sempre reinicia ao terminar.
void runPortal(uint32_t timeoutSec) {
  String ap = apName();
  gApName = ap;
  bool ok = false;
  int attempts = 0;
  inPortal = true;
  while (!ok) {
    gGotIp = false;
    gFailMsg = nullptr;
    WiFiManager wm;
    WiFiManagerParameter pCity("city", "Cidade (opcional, ex: Fortaleza). Vazio = automático", manualCity.c_str(), 40);
    gCityParam = &pCity;
    wm.addParameter(&pCity);
    wm.setTitle("MesaClima");
    wm.setSaveConfigCallback(saveCityParam);
    wm.setSaveParamsCallback(saveCityParam);
    wm.setConfigPortalTimeout(timeoutSec);
    wm.setConnectTimeout(20);
    wm.setBreakAfterConfig(true);
    showPortalScreen(ap);
    ok = wm.startConfigPortal(ap.c_str());  // rede aberta, portal cativo
    if (ok) saveCityParam();
    gCityParam = nullptr;
    if (!ok && !gFailMsg) break;            // acabou o tempo sem ninguém configurar
    if (!ok) {                              // tentou conectar e falhou: avisa e reabre o portal
      showMessage("Não conectou:", gFailMsg, "Tente de novo");
      delay(4000);
      if (++attempts >= 5) break;
    }
  }
  inPortal = false;
  if (ok) showMessage("Conectado!", "Reiniciando...");
  else showMessage("Reiniciando...");
  delay(1000);
  ESP.restart();
}

void factoryReset() {
  showMessage("Apagando", "configurações...");
  prefs.clear();
  WiFi.disconnect(true, true);  // apaga a rede Wi-Fi salva
  delay(800);
  ESP.restart();
}

void handleButton(uint32_t now) {
  static uint32_t pressStart = 0;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down) {
    if (!pressStart) pressStart = now;
    heldMs = now - pressStart;
  } else if (pressStart) {
    uint32_t held = now - pressStart;
    pressStart = 0;
    heldMs = 0;
    if (held >= 10000) factoryReset();
    else if (held >= 3000) runPortal(180);
    else if (held >= 40) {  // toque curto: próxima tela
      page = (page + 1) % 4;
      pageStartMs = now;
    }
  }
}

// ======================= SETUP / LOOP =======================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setContrast(200);

  prefs.begin("mesa", false);
  manualCity = prefs.getString("mcity", "");
  tzKnown = prefs.isKey("off");
  tzOffset = prefs.getInt("off", 0);

  showMessage("MesaClima", "iniciando...");
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  delay(200);

  if (WiFi.SSID().length() == 0) {
    runPortal(0);  // primeira vez: configurar Wi-Fi (reinicia ao terminar)
  } else {
    showMessage("Conectando ao", "Wi-Fi...");
    WiFi.begin();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(250);
  }
  pageStartMs = millis();
}

void loop() {
  uint32_t now = millis();
  handleButton(now);

  if (WiFi.status() == WL_CONNECTED) {
    offlineSinceMs = 0;
    if (!ntpStarted) startNtp();
    if ((int32_t)(now - nextFetchMs) >= 0) {
      nextFetchMs = millis() + (doFetch() ? WEATHER_EVERY_MS : RETRY_EVERY_MS);
    }
  } else {
    if (!offlineSinceMs) offlineSinceMs = now ? now : 1;
    if ((uint32_t)(now - lastBeginMs) > 30000UL) {  // tenta reconectar a cada 30 s
      WiFi.begin();
      lastBeginMs = now;
    }
    if ((uint32_t)(now - offlineSinceMs) > AUTO_PORTAL_AFTER_MS) runPortal(180);
    nextFetchMs = now;  // busca assim que a rede voltar
  }

  if ((uint32_t)(now - pageStartMs) >= PAGE_MS) {
    page = (page + 1) % 4;
    pageStartMs = now;
  }

  if ((uint32_t)(now - lastDrawMs) >= 250) {
    lastDrawMs = now;
    static uint8_t lastContrast = 255;
    struct tm t;
    uint8_t c = 200;
    if (localNow(t) && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR)) c = 8;
    if (c != lastContrast) {
      u8g2.setContrast(c);
      lastContrast = c;
    }
    drawFrame();
  }
}
