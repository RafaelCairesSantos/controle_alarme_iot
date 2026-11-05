#include <Arduino.h>
#include "AdafruitIO_WiFi.h"
#include <NewPing.h>
#include "secrets.h"

// =========================
// DEFINIÇÃO DOS PINOS
// =========================
const int LED_ALARME   = 13;
const int BUZZER_PIN   = 27;
const int BOTAO_FISICO = 26;

// Pinos do sensor ultrassônico
const int TRIG_PIN = 12;
const int ECHO_PIN = 14;

// =========================
// CONFIGURAÇÃO DO SENSOR
// =========================
const int MAX_DISTANCE = 100; // Distância máxima do sensor (cm)

// Limite de distância para intrusão (variável configurável via Adafruit IO)
int limiteDistancia = 15;

// Controle da última distância publicada
int distanciaAnterior = -1;

NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE);

// =========================
// CONFIGURAÇÃO DO ADAFRUIT IO
// =========================
AdafruitIO_WiFi io(
    IO_USERNAME,
    IO_KEY,
    WIFI_SSID,
    WIFI_PASS
);

// Feeds do Adafruit IO
// (Caso seu feed no Adafruit IO seja "botaoalarme", altere "botao" para "botaoalarme")
AdafruitIO_Feed *feedBotaoAlarme = io.feed("botao");
AdafruitIO_Feed *feedBotao       = feedBotaoAlarme; // Permite usar tanto feedBotao quanto feedBotaoAlarme
AdafruitIO_Feed *feedDistancia   = io.feed("distanciaultrassom");
AdafruitIO_Feed *feedLimite      = io.feed("limitealarme");
AdafruitIO_Feed *feedIntrusao    = io.feed("intrusao");

// =========================
// VARIÁVEIS DE ESTADO
// =========================
bool alarmeAtivo = false;
bool intrusaoAnterior = false;

unsigned long ultimaLeituraSensor = 0;
const unsigned long INTERVALO_SENSOR = 2500; // Intervalo de leitura do sensor (ms)

// Variáveis do botão (Debounce)
byte ultimaLeituraBotao = HIGH;
byte estadobotaoestavel = HIGH;
unsigned long ultimamudancabotao = 0;
const unsigned long TEMPO_DEBOUNCE = 50;

// =========================
// PROTÓTIPOS DAS FUNÇÕES
// =========================
void handleAlarme(AdafruitIO_Data *data);
void handleLimite(AdafruitIO_Data *data);
void verificarbotao();
void verificaSensor();
void ativarAlerta();
void desligarAlerta();

// =========================
// SETUP
// =========================
void setup()
{
  Serial.begin(115200);

  pinMode(LED_ALARME, OUTPUT);
  digitalWrite(LED_ALARME, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(BOTAO_FISICO, INPUT_PULLUP);

  // Registro das callbacks do Adafruit IO
  feedBotaoAlarme->onMessage(handleAlarme);
  feedLimite->onMessage(handleLimite);

  Serial.println();
  Serial.println("Iniciando a ESP...");

  // Conexão com o Adafruit IO
  io.connect();

  while (io.status() < AIO_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println(io.statusText());
  Serial.println("Adafruit IO Conectado!");

  // Solicita os últimos valores salvos nos feeds
  feedBotaoAlarme->get();
  feedLimite->get();

  Serial.println("Sistema de alarme iniciado...");
  delay(2000);
}

// =========================
// LOOP
// =========================
void loop()
{
  io.run();

  verificarbotao();
  verificaSensor();
}

// =========================
// CALLBACK: CONTROLE DO ALARME REMOTO
// =========================
void handleAlarme(AdafruitIO_Data *data)
{
  String valor = data->toString();
  valor.trim();
  valor.toLowerCase();

  if (valor == "true" || valor == "1" || valor == "on")
  {
    Serial.println("Alarme Armado remotamente");
    alarmeAtivo = true;
  }
  else if (valor == "false" || valor == "0" || valor == "off")
  {
    Serial.println("Alarme Desarmado remotamente");
    alarmeAtivo = false;
    desligarAlerta();
  }
  else
  {
    Serial.print("Informação inválida do FEED: ");
    Serial.println(valor);
  }
}

// =========================
// CALLBACK: RECEBER NOVO LIMITE
// =========================
void handleLimite(AdafruitIO_Data *data)
{
  int novoLimite = data->toInt();

  // Aceita somente valores dentro de uma faixa válida
  if (novoLimite >= 5 && novoLimite <= 100)
  {
    limiteDistancia = novoLimite;

    Serial.print("Novo limite: ");
    Serial.print(limiteDistancia);
    Serial.println(" cm");
  }
}

// =========================
// VERIFICA BOTÃO FÍSICO (DEBOUNCE)
// =========================
void verificarbotao()
{
  int leituraatual = digitalRead(BOTAO_FISICO);

  if (leituraatual != ultimaLeituraBotao)
  {
    ultimamudancabotao = millis();
    ultimaLeituraBotao = leituraatual;
  }

  if (millis() - ultimamudancabotao > TEMPO_DEBOUNCE)
  {
    if (leituraatual != estadobotaoestavel)
    {
      estadobotaoestavel = leituraatual;

      // Botão pressionado (INPUT_PULLUP -> nível LOW)
      if (estadobotaoestavel == LOW)
      {
        alarmeAtivo = !alarmeAtivo;
        feedBotaoAlarme->save(String(alarmeAtivo ? "true" : "false"));

        Serial.println(alarmeAtivo ? "Alarme Ativado pelo botão!" : "Alarme Desativado pelo botão!");

        if (!alarmeAtivo)
        {
          desligarAlerta();
        }
      }
    }
  }
}

// =========================
// LEITURA DO SENSOR E INTRUSÃO
// =========================
void verificaSensor()
{
  if (millis() - ultimaLeituraSensor < INTERVALO_SENSOR)
  {
    return;
  }

  ultimaLeituraSensor = millis();

  unsigned int distancia = sonar.ping_cm();
  Serial.print("Distância: ");
  Serial.print(distancia);
  Serial.println(" cm");

  // Publica a distância quando houver alteração
  if (distancia > 0 && distancia != distanciaAnterior)
  {
    feedDistancia->save(distancia);
    distanciaAnterior = distancia;
    Serial.println("Distância enviada ao Adafruit IO.");
  }

  // Regra de intrusão utilizando o limite dinâmico
  bool intrusao = alarmeAtivo && distancia > 0 && distancia < limiteDistancia;

  // Publica no Adafruit IO apenas quando o estado mudar
  if (intrusao != intrusaoAnterior)
  {
    feedIntrusao->save(String(intrusao ? "true" : "false"));
    intrusaoAnterior = intrusao;

    if (intrusao)
    {
      Serial.println("Intrusão detectada! Alarme ativado.");
    }
    else
    {
      Serial.println("Ambiente seguro.");
    }
  }

  // Acionamento físico (LED e Buzzer)
  if (intrusao)
  {
    ativarAlerta();
  }
  else
  {
    desligarAlerta();
  }
}

// =========================
// ATUADORES (LED E BUZZER)
// =========================
void ativarAlerta()
{
  digitalWrite(LED_ALARME, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
}

void desligarAlerta()
{
  digitalWrite(LED_ALARME, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}
