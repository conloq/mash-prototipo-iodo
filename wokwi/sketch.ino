// Protótipo da máquina do teste de iodo (MASH / Conloq)
// Roda no Wokwi: https://wokwi.com/projects/new/esp32
//
// A máquina tem tela LCD 20x4, botão ANALISAR, LED branco de iluminação,
// LEDs de resultado e buzzer. A "câmera" da máquina, nesta simulação, é o
// celular com a página web/index.html: ao apertar ANALISAR a máquina pede
// a leitura por MQTT, o celular analisa a gota com OpenCV.js e devolve o
// resultado, que a máquina mostra na tela.
//
// Não usa bibliotecas externas (o plano gratuito do Wokwi não permite):
// o MQTT é o cliente que já vem no ESP-IDF e o LCD é controlado direto
// pelo I2C (módulo PCF8574, endereço 0x27).

#include <WiFi.h>
#include <Wire.h>
#include "esp_idf_version.h"
#include "mqtt_client.h"

// Rede simulada do Wokwi (tem acesso à internet)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// Broker MQTT público. O código abaixo tem que ser o mesmo
// digitado no campo "Código da máquina" da página do celular.
const char* MQTT_URI = "mqtt://broker.hivemq.com:1883";
const char* CODIGO = "demo01";

const int PIN_BOTAO = 4;          // botão ANALISAR (liga no GND)
const int PIN_LED_LUZ = 2;        // anel de LED branco que ilumina a amostra
const int PIN_LED_VERDE = 18;     // sem amido: conversão concluída
const int PIN_LED_VERMELHO = 19;  // amido presente
const int PIN_LED_AMARELO = 5;    // leitura inconclusiva
const int PIN_BUZZER = 23;

const unsigned long TIMEOUT_MS = 15000;    // espera pela câmera
const unsigned long RESULTADO_MS = 12000;  // tempo do resultado na tela

// ---------- LCD 20x4 por I2C, sem biblioteca ----------
// PCF8574: P0=RS, P1=RW, P2=E, P3=luz de fundo, P4..P7=D4..D7
const uint8_t LCD_END = 0x27;
const uint8_t LCD_LUZ = 0x08, LCD_E = 0x04, LCD_RS = 0x01;

void lcdPulso(uint8_t v) {
  Wire.beginTransmission(LCD_END);
  Wire.write(v | LCD_LUZ | LCD_E);
  Wire.endTransmission();
  delayMicroseconds(1);
  Wire.beginTransmission(LCD_END);
  Wire.write((v | LCD_LUZ) & ~LCD_E);
  Wire.endTransmission();
  delayMicroseconds(50);
}

void lcdEnviar(uint8_t valor, uint8_t modo) {
  lcdPulso((valor & 0xF0) | modo);
  lcdPulso(((valor << 4) & 0xF0) | modo);
}

void lcdComando(uint8_t c) {
  lcdEnviar(c, 0);
  if (c <= 0x02) delay(2);
}

void lcdIniciar() {
  Wire.begin(21, 22);
  delay(50);
  lcdPulso(0x30); delay(5);
  lcdPulso(0x30); delayMicroseconds(150);
  lcdPulso(0x30);
  lcdPulso(0x20);     // modo 4 bits
  lcdComando(0x28);   // 2 linhas lógicas, fonte 5x8
  lcdComando(0x0C);   // tela ligada, sem cursor
  lcdComando(0x06);   // avança o cursor
  lcdComando(0x01);   // limpa
}

// Escreve uma linha inteira do LCD, completando com espaços.
void linha(int n, const String& texto) {
  const uint8_t inicio[] = {0x00, 0x40, 0x14, 0x54};
  lcdComando(0x80 | inicio[n]);
  String t = texto.substring(0, 20);
  while (t.length() < 20) t += ' ';
  for (unsigned int i = 0; i < t.length(); i++) lcdEnviar(t[i], LCD_RS);
}

// ---------- MQTT (cliente do ESP-IDF) ----------
esp_mqtt_client_handle_t mqtt = nullptr;
volatile bool mqttConectado = false;
volatile bool chegouResultado = false;
volatile bool mudouConexao = false;
char resultado[96];

String topicoPedido;
String topicoResultado;
String topicoStatus;

// Roda em outra tarefa: só copia os dados e levanta flags para o loop().
void eventoMqtt(void* args, esp_event_base_t base, int32_t id, void* dados) {
  esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)dados;
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      esp_mqtt_client_subscribe(mqtt, topicoResultado.c_str(), 0);
      esp_mqtt_client_publish(mqtt, topicoStatus.c_str(), "online", 0, 0, 1);
      mqttConectado = true;
      mudouConexao = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConectado = false;
      mudouConexao = true;
      break;
    case MQTT_EVENT_DATA: {
      char topico[96];  // o tópico do evento não termina em '\0'
      int t = min(ev->topic_len, (int)sizeof(topico) - 1);
      memcpy(topico, ev->topic, t);
      topico[t] = '\0';
      if (topicoResultado == topico) {
        int n = min(ev->data_len, (int)sizeof(resultado) - 1);
        memcpy(resultado, ev->data, n);
        resultado[n] = '\0';
        chegouResultado = true;
      }
      break;
    }
    default:
      break;
  }
}

void iniciarMqtt() {
  esp_mqtt_client_config_t cfg = {};
#if ESP_IDF_VERSION_MAJOR >= 5
  cfg.broker.address.uri = MQTT_URI;
  cfg.session.last_will.topic = topicoStatus.c_str();
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.retain = 1;
#else
  cfg.uri = MQTT_URI;
  cfg.lwt_topic = topicoStatus.c_str();
  cfg.lwt_msg = "offline";
  cfg.lwt_retain = 1;
#endif
  mqtt = esp_mqtt_client_init(&cfg);
  esp_mqtt_client_register_event(mqtt, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, eventoMqtt, nullptr);
  esp_mqtt_client_start(mqtt);  // reconecta sozinho se cair
}

// ---------- Máquina ----------
enum Estado { OCIOSO, AGUARDANDO, RESULTADO };
Estado estado = OCIOSO;
unsigned long inicioEstado = 0;
int ultimoSegundo = -1;
int numeroPedido = 0;
bool botaoAnterior = HIGH;

void apagarLedsResultado() {
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_VERMELHO, LOW);
  digitalWrite(PIN_LED_AMARELO, LOW);
}

void bip(int vezes, int frequencia) {
  for (int i = 0; i < vezes; i++) {
    tone(PIN_BUZZER, frequencia, 120);
    delay(180);
  }
  noTone(PIN_BUZZER);
}

void telaOciosa() {
  estado = OCIOSO;
  digitalWrite(PIN_LED_LUZ, LOW);
  apagarLedsResultado();
  linha(0, "MASH Teste de Iodo");
  linha(1, "Pingue o iodo e");
  linha(2, "aperte ANALISAR");
  linha(3, String("WiFi:") + (WiFi.status() == WL_CONNECTED ? "OK " : "-- ") +
               " Camera:" + (mqttConectado ? "OK" : "--"));
}

void pedirAnalise() {
  if (!mqttConectado) {
    linha(0, "Sem conexao");
    linha(1, "Verifique a rede");
    linha(2, "");
    linha(3, "");
    bip(2, 300);
    delay(1500);
    telaOciosa();
    return;
  }
  numeroPedido++;
  estado = AGUARDANDO;
  inicioEstado = millis();
  ultimoSegundo = -1;
  chegouResultado = false;
  apagarLedsResultado();
  digitalWrite(PIN_LED_LUZ, HIGH);  // acende a iluminação para a foto
  linha(0, "Analisando...");
  linha(1, "Lendo a cor da gota");
  linha(2, "");
  esp_mqtt_client_publish(mqtt, topicoPedido.c_str(), String(numeroPedido).c_str(), 0, 0, 0);
  Serial.printf("Pedido %d enviado\n", numeroPedido);
  bip(1, 1000);
}

// Resultado chega como "CLASSE;h=..;s=..;v=..;n=.."
void mostrarResultado(const String& msg) {
  int fim = msg.indexOf(';');
  String classe = fim >= 0 ? msg.substring(0, fim) : msg;

  estado = RESULTADO;
  inicioEstado = millis();
  digitalWrite(PIN_LED_LUZ, LOW);
  apagarLedsResultado();

  if (classe == "SEM_AMIDO") {
    digitalWrite(PIN_LED_VERDE, HIGH);
    linha(0, "SEM AMIDO");
    linha(1, "Conversao concluida");
    linha(2, "Pode seguir");
    linha(3, "Luz: OK");
    bip(1, 1500);
  } else if (classe == "AMIDO_PRESENTE") {
    digitalWrite(PIN_LED_VERMELHO, HIGH);
    linha(0, "AMIDO PRESENTE");
    linha(1, "Manter a rampa");
    linha(2, "Repetir o teste");
    linha(3, "Luz: OK");
    bip(3, 600);
  } else if (classe == "INCONCLUSIVO" && msg.indexOf(";m=luz;") >= 0) {
    digitalWrite(PIN_LED_AMARELO, HIGH);
    linha(0, "POUCA LUZ");
    linha(1, "Acenda a luz ou");
    linha(2, "confira se o anel");
    linha(3, "esta sobre o branco");
    bip(2, 400);
  } else if (classe == "INCONCLUSIVO" && msg.indexOf(";m=cor;") >= 0) {
    digitalWrite(PIN_LED_AMARELO, HIGH);
    linha(0, "COR NAO RECONHECIDA");
    linha(1, "Nao e azul/escura");
    linha(2, "nem amarela/ambar");
    linha(3, "Repita a leitura");
    bip(2, 400);
  } else if (classe == "ERRO") {
    // A página respondeu, mas não conseguiu analisar (OpenCV ou câmera)
    digitalWrite(PIN_LED_AMARELO, HIGH);
    bool opencv = msg.indexOf("opencv") >= 0;
    linha(0, "CAMERA NAO PRONTA");
    linha(1, opencv ? "Visao ainda" : "Camera desligada");
    linha(2, opencv ? "carregando" : "no celular");
    linha(3, "Tente de novo");
    bip(2, 400);
  } else {
    digitalWrite(PIN_LED_AMARELO, HIGH);
    linha(0, msg.indexOf(";m=gota;") >= 0 ? "NENHUMA GOTA" : "NAO FOI POSSIVEL LER");
    linha(1, "Centralize a gota");
    linha(2, "no circulo e tente");
    linha(3, "de novo");
    bip(2, 400);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_LED_LUZ, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  String base = String("conloq-mash-proto/") + CODIGO + "/";
  topicoPedido = base + "pedido";
  topicoResultado = base + "resultado";
  topicoStatus = base + "status";

  lcdIniciar();
  linha(0, "MASH Teste de Iodo");
  linha(1, "Conectando WiFi...");
  linha(2, String("Codigo: ") + CODIGO);
  linha(3, "");

  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  Serial.println("WiFi conectado");

  iniciarMqtt();
  telaOciosa();
}

void loop() {
  if (mudouConexao) {
    mudouConexao = false;
    Serial.println(mqttConectado ? "MQTT conectado" : "MQTT desconectado");
    if (estado == OCIOSO) telaOciosa();
  }

  if (chegouResultado) {
    chegouResultado = false;
    Serial.printf("Resultado: %s\n", resultado);
    if (estado == AGUARDANDO) mostrarResultado(String(resultado));
  }

  // Botão com borda de descida e um debounce simples
  bool botao = digitalRead(PIN_BOTAO);
  if (botaoAnterior == HIGH && botao == LOW && estado != AGUARDANDO) {
    delay(30);
    if (digitalRead(PIN_BOTAO) == LOW) pedirAnalise();
  }
  botaoAnterior = botao;

  if (estado == AGUARDANDO) {
    unsigned long passou = millis() - inicioEstado;
    int restante = passou >= TIMEOUT_MS ? 0 : (TIMEOUT_MS - passou) / 1000;
    if (restante != ultimoSegundo) {
      ultimoSegundo = restante;
      linha(3, "Aguardando camera " + String(restante) + "s");
    }
    if (passou > TIMEOUT_MS) {
      digitalWrite(PIN_LED_LUZ, LOW);
      linha(0, "Camera nao respondeu");
      linha(1, "Abra a pagina no");
      linha(2, "celular e confira");
      linha(3, String("o codigo: ") + CODIGO);
      bip(2, 300);
      estado = RESULTADO;
      inicioEstado = millis();
    }
  } else if (estado == RESULTADO && millis() - inicioEstado > RESULTADO_MS) {
    telaOciosa();
  }

  delay(10);
}
