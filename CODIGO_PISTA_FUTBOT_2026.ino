/*
  NeoTech Stadium - Control de cancha con Arduino Nano
*/

#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <TM1637Display.h>
#include <avr/pgmspace.h>

// CONFIGURACION
const uint32_t DURACION_PARTIDA_SEG = 120UL;
const uint32_t DURACION_PREPARACION_MS = 3000UL;
const uint32_t TIEMPO_MAX_AUDIO_INICIO_MS = 30000UL;
const unsigned long IGNORAR_FIN_PISTA_MS = 1000UL;
const uint8_t PUNTAJE_MAXIMO = 99;
const unsigned long ANTIRREBOTE_MS = 35;

// PINES
const uint8_t PIN_BOTON_LOCAL = A0;
const uint8_t PIN_BOTON_RESET = A1;
const uint8_t PIN_BOTON_START = A2;
const uint8_t PIN_BOTON_PAUSA = A4;
const uint8_t PIN_BOTON_VISITANTE = A3;

const uint8_t PIN_DF_RX_ARDUINO = 4;
const uint8_t PIN_DF_TX_ARDUINO = 5;

const uint8_t PIN_TM_CLK = 7;
const uint8_t PIN_TM_DIO = 8;

const uint8_t PIN_MATRIZ_DIN = 11;
const uint8_t PIN_MATRIZ_CLK = 13;
const uint8_t PIN_MATRIZ_CS = 10;
const uint8_t NUM_MATRICES = 4;

#define TIPO_MATRIZ MD_MAX72XX::FC16_HW

// AUDIOS
const uint8_t AUDIO_INICIO = 1;
const uint8_t AUDIO_PUNTO = 2;
const uint8_t AUDIO_FIN = 3;
const uint8_t AUDIO_AMBIENTACION = 4;
const uint8_t VOLUMEN_DFPLAYER = 10;

// OBJETOS
SoftwareSerial serialDFPlayer(PIN_DF_RX_ARDUINO, PIN_DF_TX_ARDUINO);
DFRobotDFPlayerMini dfPlayer;

bool audioDisponible = false;

enum ModoAudio : uint8_t {
  MODO_AUDIO_SILENCIO,
  MODO_AUDIO_PREPARACION,
  MODO_AUDIO_AMBIENTACION,
  MODO_AUDIO_GOL,
  MODO_AUDIO_FINAL
};

ModoAudio modoAudio = MODO_AUDIO_SILENCIO;

bool audioTerminadoDurantePausa = false;
unsigned long ultimoComandoAudioMs = 0;

TM1637Display displayTiempo(PIN_TM_CLK, PIN_TM_DIO);

MD_Parola matriz = MD_Parola(
  TIPO_MATRIZ,
  PIN_MATRIZ_DIN,
  PIN_MATRIZ_CLK,
  PIN_MATRIZ_CS,
  NUM_MATRICES
);

char textoMatriz[20] = "INICIA EL JUEGO";
bool matrizAnimada = false;

// ESTADO DE PARTIDA
enum EstadoPartida : uint8_t {
  ESPERANDO_INICIO,
  PREPARANDO_PARTIDA,
  PARTIDA_EN_MARCHA,
  PARTIDA_PAUSADA,
  PARTIDA_FINALIZADA
};

EstadoPartida estadoPartida = ESPERANDO_INICIO;

uint8_t puntosLocal = 0;
uint8_t puntosVisitante = 0;

uint32_t segundosRestantes = DURACION_PARTIDA_SEG;

unsigned long ultimoTickCronometro = 0;
unsigned long inicioPreparacionMs = 0;

// BOTONES
struct Boton {
  uint8_t pin;
  bool estadoEstable;
  bool lecturaAnterior;
  unsigned long ultimoCambio;
};

Boton botonLocal = {PIN_BOTON_LOCAL, LOW, LOW, 0};
Boton botonVisitante = {PIN_BOTON_VISITANTE, LOW, LOW, 0};
Boton botonPausa = {PIN_BOTON_PAUSA, LOW, LOW, 0};
Boton botonStart = {PIN_BOTON_START, LOW, LOW, 0};
Boton botonReset = {PIN_BOTON_RESET, LOW, LOW, 0};

void iniciarBoton(Boton &boton) {
  pinMode(boton.pin, INPUT);

  bool lectura = digitalRead(boton.pin);

  boton.estadoEstable = lectura;
  boton.lecturaAnterior = lectura;
  boton.ultimoCambio = millis();
}

bool huboPulsacion(Boton &boton) {
  bool lectura = digitalRead(boton.pin);

  if (lectura != boton.lecturaAnterior) {
    boton.lecturaAnterior = lectura;
    boton.ultimoCambio = millis();
  }

  if ((millis() - boton.ultimoCambio) >= ANTIRREBOTE_MS &&
      lectura != boton.estadoEstable) {

    boton.estadoEstable = lectura;

    if (boton.estadoEstable == HIGH) {
      return true;
    }
  }

  return false;
}

// DISPLAY
void mostrarTiempo() {
  uint16_t minutos = segundosRestantes / 60UL;
  uint8_t segundos = segundosRestantes % 60UL;

  uint16_t valorDisplay = (minutos * 100U) + segundos;

  displayTiempo.showNumberDecEx(
    valorDisplay,
    0b01000000,
    true,
    4,
    0
  );
}

void establecerTextoMatriz(const char *nuevoTexto) {
  uint8_t i = 0;

  while (i < sizeof(textoMatriz) - 1 && nuevoTexto[i]) {
    textoMatriz[i] = nuevoTexto[i];
    i++;
  }

  textoMatriz[i] = '\0';

  matrizAnimada = true;

  matriz.displayClear();

  matriz.displayText(
    textoMatriz,
    PA_CENTER,
    45,
    450,
    PA_SCROLL_LEFT,
    PA_SCROLL_LEFT
  );

  matriz.displayReset();
}

// FUENTE PERSONALIZADA 3x5 EN FLASH
uint8_t filaGlifo3x5(char caracter, uint8_t fila) {

  static const uint8_t glifos[13][5] PROGMEM = {
    {7, 5, 5, 5, 7},
    {2, 6, 2, 2, 7},
    {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7},
    {7, 1, 2, 2, 2},
    {7, 5, 7, 5, 7},
    {7, 5, 7, 1, 7},
    {6, 5, 6, 4, 4},
    {5, 5, 5, 5, 2},
    {7, 4, 7, 1, 7}
  };

  if (fila >= 5) return 0;

  if (caracter >= '0' && caracter <= '9') {
    return pgm_read_byte(&glifos[caracter - '0'][fila]);
  }

  if (caracter == 'P') return pgm_read_byte(&glifos[10][fila]);
  if (caracter == 'V') return pgm_read_byte(&glifos[11][fila]);
  if (caracter == 'S') return pgm_read_byte(&glifos[12][fila]);

  if (caracter == '-') {
    return (fila == 2) ? 7 : 0;
  }

  return 0;
}

void establecerTextoCompacto(const char *nuevoTexto) {

  uint8_t i = 0;

  while (i < sizeof(textoMatriz) - 1 && nuevoTexto[i]) {
    textoMatriz[i] = nuevoTexto[i];
    i++;
  }

  textoMatriz[i] = '\0';

  matrizAnimada = false;
  matriz.displayClear();

  MD_MAX72XX *grafico = matriz.getGraphicObject();

  grafico->clear();

  const uint16_t columnasTotales = NUM_MATRICES * 8U;
  const uint8_t columnasPorCaracter = 4;

  size_t longitud = strlen(textoMatriz);

  if (longitud == 0) return;

  uint16_t ancho = (longitud * 3U) + (longitud - 1U);

  uint16_t columnaInicial = (ancho < columnasTotales)
    ? (columnasTotales - ancho) / 2U
    : 0U;

  for (size_t indice = 0; indice < longitud; indice++) {

    for (uint8_t fila = 0; fila < 5; fila++) {

      uint8_t patron = filaGlifo3x5(textoMatriz[indice], fila);

      for (uint8_t columna = 0; columna < 3; columna++) {

        bool encender = (patron & (1U << (2U - columna))) != 0;

        uint16_t xLogica =
          columnaInicial + (indice * columnasPorCaracter) + columna;

        if (xLogica < columnasTotales) {

          uint16_t columnaPantalla =
            (columnasTotales - 1U) - xLogica;

          grafico->setPoint(
            fila + 1U,
            columnaPantalla,
            encender
          );
        }
      }
    }
  }
}

// CONSTRUCCION DE NUMEROS SIN snprintf()
char *agregarNumero(char *destino, uint8_t numero) {

  if (numero >= 10) {
    *destino++ = '0' + (numero / 10);
    numero %= 10;
  }

  *destino++ = '0' + numero;

  return destino;
}

void mostrarMarcador() {

  char *p = textoMatriz;

  if (estadoPartida == ESPERANDO_INICIO) {
    establecerTextoMatriz("INICIA EL JUEGO");
    return;
  }

  if (estadoPartida == PREPARANDO_PARTIDA) {
    establecerTextoCompacto("P1 VS P2");
    return;
  }

  if (estadoPartida == PARTIDA_PAUSADA ||
      estadoPartida == PARTIDA_FINALIZADA) {

    if (estadoPartida == PARTIDA_PAUSADA) {

      *p++ = 'P';
      *p++ = 'A';
      *p++ = 'U';
      *p++ = 'S';
      *p++ = 'A';
      *p++ = ' ';
      *p++ = 'P';
      *p++ = '1';
      *p++ = ':';

    } else {

      *p++ = 'F';
      *p++ = 'I';
      *p++ = 'N';
      *p++ = ' ';
      *p++ = 'P';
      *p++ = '1';
      *p++ = ':';
    }

    p = agregarNumero(p, puntosLocal);

    *p++ = ' ';
    *p++ = 'P';
    *p++ = '2';
    *p++ = ':';

    p = agregarNumero(p, puntosVisitante);

    *p = '\0';

    establecerTextoMatriz(textoMatriz);

    return;
  }

  p = agregarNumero(p, puntosLocal);

  *p++ = ' ';
  *p++ = '-';
  *p++ = ' ';

  p = agregarNumero(p, puntosVisitante);

  *p = '\0';

  establecerTextoCompacto(textoMatriz);
}

// CONTROL DE AUDIO
void detenerAudio() {

  if (audioDisponible) {

    serialDFPlayer.listen();

    dfPlayer.stop();

    delay(100);
  }

  modoAudio = MODO_AUDIO_SILENCIO;

  audioTerminadoDurantePausa = false;
}

void iniciarAmbientacion() {

  if (!audioDisponible) return;

  serialDFPlayer.listen();

  dfPlayer.stop();

  delay(100);

  dfPlayer.playMp3Folder(AUDIO_AMBIENTACION);

  modoAudio = MODO_AUDIO_AMBIENTACION;

  ultimoComandoAudioMs = millis();

  Serial.println(F("Audio: inicia ambientacion MP3/0004.mp3"));
}

void repetirAmbientacion() {

  if (!audioDisponible) return;

  serialDFPlayer.listen();

  delay(100);

  dfPlayer.playMp3Folder(AUDIO_AMBIENTACION);

  modoAudio = MODO_AUDIO_AMBIENTACION;

  ultimoComandoAudioMs = millis();

  Serial.println(F("Audio: ambientacion en bucle MP3/0004.mp3"));
}

void reproducirSonidoPunto() {

  if (!audioDisponible) {
    Serial.println(F("Audio de gol omitido: DFPlayer no disponible"));
    return;
  }

  serialDFPlayer.listen();

  dfPlayer.stop();

  delay(100);

  dfPlayer.playMp3Folder(AUDIO_PUNTO);

  modoAudio = MODO_AUDIO_GOL;

  audioTerminadoDurantePausa = false;

  ultimoComandoAudioMs = millis();

  Serial.println(F("Audio: reproduce gol MP3/0002.mp3"));
}

void reproducirSonidoFinal() {

  if (!audioDisponible) {
    Serial.println(F("Audio final omitido: DFPlayer no disponible"));
    return;
  }

  serialDFPlayer.listen();

  dfPlayer.stop();

  delay(100);

  dfPlayer.playMp3Folder(AUDIO_FIN);

  modoAudio = MODO_AUDIO_FINAL;

  ultimoComandoAudioMs = millis();

  Serial.println(F("Audio: reproduce final MP3/0003.mp3"));
}

void procesarEventosAudio() {

  if (!audioDisponible) return;

  serialDFPlayer.listen();

  while (dfPlayer.available()) {

    uint8_t tipo = dfPlayer.readType();

    int valor = dfPlayer.read();

    if (tipo == DFPlayerError) {

      Serial.print(F("Error del DFPlayer: "));

      switch (valor) {

        case Busy:
          Serial.println(F("tarjeta SD no encontrada u ocupada"));
          break;

        case Sleeping:
          Serial.println(F("modulo en reposo"));
          break;

        case SerialWrongStack:
          Serial.println(F("respuesta serial incorrecta; revisar RX/TX"));
          break;

        case CheckSumNotMatch:
          Serial.println(F("error de comunicacion/checksum"));
          break;

        case FileIndexOut:
          Serial.println(F("numero de archivo fuera de rango"));
          break;

        case FileMismatch:
          Serial.println(F("no se encontro el archivo solicitado"));
          break;

        default:
          Serial.println(valor);
          break;
      }

      continue;
    }

    if (tipo == DFPlayerCardOnline) {
      Serial.println(F("DFPlayer: tarjeta SD detectada"));
      continue;
    }

    if (tipo != DFPlayerPlayFinished) continue;

    Serial.print(F("DFPlayer: termino pista "));
    Serial.println(valor);

    if ((millis() - ultimoComandoAudioMs) < IGNORAR_FIN_PISTA_MS) {

      Serial.println(F("  (aviso ignorado: demasiado cerca del ultimo comando)"));

      continue;
    }

    switch (modoAudio) {

      case MODO_AUDIO_PREPARACION:

        if (estadoPartida == PREPARANDO_PARTIDA) {
          comenzarJuegoReal();
        }

        break;

      case MODO_AUDIO_AMBIENTACION:
      case MODO_AUDIO_GOL:

        if (estadoPartida == PARTIDA_EN_MARCHA) {
          repetirAmbientacion();
        }
        else if (estadoPartida == PARTIDA_PAUSADA) {
          audioTerminadoDurantePausa = true;
        }

        break;

      case MODO_AUDIO_FINAL:

        modoAudio = MODO_AUDIO_SILENCIO;

        break;

      default:
        break;
    }
  }
}

// LOGICA DEL JUEGO
void iniciarPartida() {

  if (estadoPartida != ESPERANDO_INICIO &&
      estadoPartida != PARTIDA_FINALIZADA) {
    return;
  }

  detenerAudio();

  puntosLocal = 0;
  puntosVisitante = 0;

  segundosRestantes = DURACION_PARTIDA_SEG;

  estadoPartida = PREPARANDO_PARTIDA;

  inicioPreparacionMs = millis();

  audioTerminadoDurantePausa = false;

  mostrarTiempo();
  mostrarMarcador();

  if (audioDisponible) {

    serialDFPlayer.listen();

    dfPlayer.playMp3Folder(AUDIO_INICIO);

    modoAudio = MODO_AUDIO_PREPARACION;

    ultimoComandoAudioMs = millis();

    Serial.println(F("Audio: reproduce inicio MP3/0001.mp3; el juego empieza al terminar"));

  } else {

    Serial.println(F("DFPlayer no disponible: preparacion de 3 segundos sin audio"));
  }
}

void comenzarJuegoReal() {

  if (estadoPartida != PREPARANDO_PARTIDA) return;

  segundosRestantes = DURACION_PARTIDA_SEG;

  estadoPartida = PARTIDA_EN_MARCHA;

  ultimoTickCronometro = millis();

  mostrarTiempo();
  mostrarMarcador();

  iniciarAmbientacion();

  Serial.println(F("Partida iniciada; tiempo de juego: 2 minutos"));
}

void actualizarPreparacion() {

  if (estadoPartida != PREPARANDO_PARTIDA) return;

  unsigned long transcurrido = millis() - inicioPreparacionMs;

  if (!audioDisponible) {

    if (transcurrido >= DURACION_PREPARACION_MS) {
      comenzarJuegoReal();
    }

  } else if (transcurrido >= TIEMPO_MAX_AUDIO_INICIO_MS) {

    Serial.println(F("Aviso: no llego el fin del audio de inicio; se continua por tiempo maximo"));

    comenzarJuegoReal();
  }
}

void reiniciarPartida() {

  detenerAudio();

  puntosLocal = 0;
  puntosVisitante = 0;

  segundosRestantes = DURACION_PARTIDA_SEG;

  estadoPartida = ESPERANDO_INICIO;

  ultimoTickCronometro = millis();

  inicioPreparacionMs = 0;

  mostrarTiempo();
  mostrarMarcador();

  Serial.println(F("Partida reiniciada; pulse el boton FUTBOL para comenzar"));
}

void cambiarPausa() {

  if (estadoPartida == PARTIDA_EN_MARCHA) {

    estadoPartida = PARTIDA_PAUSADA;

    if (audioDisponible && modoAudio != MODO_AUDIO_SILENCIO) {
      dfPlayer.pause();
    }

    mostrarMarcador();

    Serial.println(F("Partida pausada"));

  } else if (estadoPartida == PARTIDA_PAUSADA) {

    estadoPartida = PARTIDA_EN_MARCHA;

    ultimoTickCronometro = millis();

    if (audioDisponible) {

      if (audioTerminadoDurantePausa) {
        iniciarAmbientacion();
      }
      else if (modoAudio != MODO_AUDIO_SILENCIO) {
        dfPlayer.start();
      }
      else {
        iniciarAmbientacion();
      }
    }

    audioTerminadoDurantePausa = false;

    mostrarMarcador();

    Serial.println(F("Partida reanudada"));
  }
}

void anotarPuntoLocal() {

  if (estadoPartida != PARTIDA_EN_MARCHA) return;

  if (puntosLocal < PUNTAJE_MAXIMO) {

    puntosLocal++;

    mostrarMarcador();

    reproducirSonidoPunto();

    Serial.println(F("Punto para P1"));
  }
}

void anotarPuntoVisitante() {

  if (estadoPartida != PARTIDA_EN_MARCHA) return;

  if (puntosVisitante < PUNTAJE_MAXIMO) {

    puntosVisitante++;

    mostrarMarcador();

    reproducirSonidoPunto();

    Serial.println(F("Punto para P2"));
  }
}

void finalizarPartida() {

  estadoPartida = PARTIDA_FINALIZADA;

  segundosRestantes = 0;

  mostrarTiempo();
  mostrarMarcador();

  detenerAudio();

  reproducirSonidoFinal();

  Serial.println(F("Tiempo terminado"));
}

void actualizarCronometro() {

  if (estadoPartida != PARTIDA_EN_MARCHA) return;

  unsigned long ahora = millis();

  while ((ahora - ultimoTickCronometro) >= 1000UL &&
         estadoPartida == PARTIDA_EN_MARCHA) {

    ultimoTickCronometro += 1000UL;

    if (segundosRestantes > 0) {

      segundosRestantes--;

      mostrarTiempo();
    }

    if (segundosRestantes == 0) {
      finalizarPartida();
    }
  }
}

// BOTONES
void procesarBotones() {

  if (huboPulsacion(botonReset)) {
    reiniciarPartida();
  }

  if (huboPulsacion(botonStart)) {
    iniciarPartida();
  }

  if (huboPulsacion(botonPausa)) {
    cambiarPausa();
  }

  if (huboPulsacion(botonLocal)) {
    anotarPuntoLocal();
  }

  if (huboPulsacion(botonVisitante)) {
    anotarPuntoVisitante();
  }
}

// SETUP
void setup() {

  Serial.begin(9600);

  iniciarBoton(botonLocal);
  iniciarBoton(botonVisitante);
  iniciarBoton(botonPausa);
  iniciarBoton(botonStart);
  iniciarBoton(botonReset);

  displayTiempo.setBrightness(5, true);

  matriz.begin();

  matriz.setIntensity(3);

  matriz.displayClear();

  serialDFPlayer.begin(9600);

  serialDFPlayer.listen();

  Serial.println(F("Inicializando DFPlayer Mini; espere unos segundos..."));

  delay(1500);

  if (dfPlayer.begin(serialDFPlayer, true, true)) {

    audioDisponible = true;

    dfPlayer.setTimeOut(500);

    delay(300);

    serialDFPlayer.listen();

    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);

    dfPlayer.enableDAC();

    dfPlayer.EQ(DFPLAYER_EQ_NORMAL);

    dfPlayer.volume(VOLUMEN_DFPLAYER);

    delay(300);

    Serial.println(F("DFPlayer inicializado correctamente"));

    Serial.println(F("Salida seleccionada: microSD; DAC habilitado"));

    Serial.print(F("Volumen configurado: "));

    Serial.println(VOLUMEN_DFPLAYER);

    serialDFPlayer.listen();

    Serial.print(F("Archivos detectados en la tarjeta: "));

    Serial.println(dfPlayer.readFileCounts());

    Serial.println(F("Archivos esperados: /MP3/0001.mp3 a /MP3/0004.mp3"));

  } else {

    audioDisponible = false;

    Serial.println(F("ERROR: no se pudo inicializar el DFPlayer Mini."));

    Serial.println(F("Revise: microSD insertada, FAT32, alimentacion 5V y RX/TX cruzados."));

    Serial.println(F("Arduino D4 debe ir al TX del DFPlayer; D5 al RX mediante resistencia/divisor."));

    Serial.println(F("Matriz y cronometro seguiran funcionando, pero no habra audio."));
  }

  segundosRestantes = DURACION_PARTIDA_SEG;

  mostrarTiempo();

  estadoPartida = ESPERANDO_INICIO;

  mostrarMarcador();

  ultimoTickCronometro = millis();
}

// LOOP PRINCIPAL
void loop() {

  procesarEventosAudio();

  procesarBotones();

  actualizarPreparacion();

  actualizarCronometro();

  if (matrizAnimada && matriz.displayAnimate()) {
    matriz.displayReset();
  }
}