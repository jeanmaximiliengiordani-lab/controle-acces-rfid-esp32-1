#include <Arduino.h>

/*
  PROJET N°4 - Controle d'acces RFID multi-utilisateurs avec journal en ligne
  Formation EIC 3.0 - Electronique & Prototypage
  Materiel : ESP32, MFRC522, servomoteur, ecran OLED SSD1306, module carte SD

  Fichiers sur la carte SD :
    /badges.txt   -> un badge par ligne :  UID;Nom      (ex: 01020304;Jean Dupont)
    /journal.csv  -> cree automatiquement : date_heure;uid;nom;statut
*/
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <Keypad.h>

// ---------- Broches ----------
#define RFID_SS   21
#define RFID_RST  22
#define SD_CS     5
#define SERVO_PIN 13
#define OLED_SDA  4
#define OLED_SCL  15
// SPI partage (RFID + SD) : SCK=18, MISO=19, MOSI=23

// ---------- Wi-Fi (reseau Wokwi) ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

#define BADGES_FILE "/badges.txt"
#define LOG_FILE    "/journal.csv"

#define SERVO_FERME 0
#define SERVO_OUVERT 90
#define DUREE_OUVERTURE_MS 3000

MFRC522 rfid(RFID_SS, RFID_RST);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
Servo servo;
WebServer server(80);

// ---------- Clavier 4x4 (code PIN personnel) ----------
// Ordre des fils du clavier : R1 R2 R3 R4 puis C1 C2 C3 C4
byte rowPins[4] = {17, 16, 14, 27};
byte colPins[4] = {26, 25, 33, 32};
char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

#define PINS_FILE "/pins.txt"
#define LONGUEUR_PIN 4
#define ESSAIS_MAX 3
#define DUREE_BLOCAGE_MS 30000
#define DELAI_SAISIE_MS 10000

String saisie = "";
unsigned long derniereTouche = 0;
unsigned long blocageJusqua = 0;
int echecsPin = 0;

unsigned long fermeturePorteA = 0;
unsigned long retourEcranA = 0;
int nbAutorises = 0;
int nbRefuses = 0;
bool sdOk = false;

// =====================================================
//  Utilitaires
// =====================================================
String nowStr() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return "heure-inconnue";
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

String clean(String s) {
  s.replace(";", " ");
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.trim();
  return s;
}

String uidToStr() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// =====================================================
//  Ecran OLED
// =====================================================
void afficher(String titre, String detail = "") {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 8);
  oled.println(titre);
  oled.setTextSize(1);
  oled.setCursor(0, 40);
  oled.println(detail);
  oled.display();
}

void ecranRepos() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 4);
  oled.println("CONTROLE D'ACCES");
  oled.setCursor(0, 24);
  oled.println("Approchez votre badge");
  oled.setCursor(0, 48);
  if (WiFi.status() == WL_CONNECTED) oled.println(WiFi.localIP());
  else oled.println("Wi-Fi : non connecte");
  oled.display();
}

// =====================================================
//  Carte SD : badges et journal
// =====================================================
bool trouverBadge(const String& uid, String& nom) {
  File f = SD.open(BADGES_FILE);
  if (!f) return false;
  bool trouve = false;
  while (f.available()) {
    String ligne = f.readStringUntil('\n');
    ligne.trim();
    int p = ligne.indexOf(';');
    if (p <= 0) continue;
    if (ligne.substring(0, p).equalsIgnoreCase(uid)) {
      nom = ligne.substring(p + 1);
      trouve = true;
      break;
    }
  }
  f.close();
  return trouve;
}

void ecrireJournal(const String& uid, const String& nom, const String& statut) {
  bool existe = SD.exists(LOG_FILE);
  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Erreur : impossible d'ecrire le journal");
    return;
  }
  if (!existe) f.println("date_heure;uid;nom;statut");
  f.println(nowStr() + ";" + uid + ";" + nom + ";" + statut);
  f.close();
}

void compterJournal() {
  nbAutorises = 0;
  nbRefuses = 0;
  File f = SD.open(LOG_FILE);
  if (!f) return;
  f.readStringUntil('\n');  // en-tete
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (l.indexOf(";AUTORISE") >= 0) nbAutorises++;
    else if (l.indexOf(";REFUSE") >= 0) nbRefuses++;
  }
  f.close();
}

// Utilisateurs autorises crees automatiquement si /badges.txt est absent ou vide
struct Utilisateur {
  const char* uid;
  const char* nom;
  const char* pin;   // code PIN de secours (carte oubliee)
};
const Utilisateur UTILISATEURS_INITIAUX[] = {
  {"01020304", "Max",    "1357"},   // UID par defaut du badge de test Wokwi
  {"A1B2C3D4", "Colson", "2468"},
  {"11223344", "Lilie",  "3690"},
  {"55667788", "Tommy",  "4812"}
};

void initialiserBadges() {
  bool vide = true;
  File f = SD.open(BADGES_FILE);
  if (f) {
    vide = (f.size() == 0);
    f.close();
  }
  if (!vide) return;

  File w = SD.open(BADGES_FILE, FILE_WRITE);
  if (!w) {
    Serial.println("Erreur : impossible de creer badges.txt");
    return;
  }
  for (const Utilisateur& u : UTILISATEURS_INITIAUX) {
    w.println(String(u.uid) + ";" + u.nom);
  }
  w.close();
  Serial.println("badges.txt cree avec les utilisateurs initiaux");
}

void ouvrirPorte();  // declaration anticipee

// Codes PIN : fichier /pins.txt (une ligne par utilisateur : PIN;Nom)
void initialiserPins() {
  bool vide = true;
  File f = SD.open(PINS_FILE);
  if (f) {
    vide = (f.size() == 0);
    f.close();
  }
  if (!vide) return;

  File w = SD.open(PINS_FILE, FILE_WRITE);
  if (!w) {
    Serial.println("Erreur : impossible de creer pins.txt");
    return;
  }
  for (const Utilisateur& u : UTILISATEURS_INITIAUX) {
    w.println(String(u.pin) + ";" + u.nom);
  }
  w.close();
  Serial.println("pins.txt cree avec les codes initiaux");
}

bool trouverPin(const String& pin, String& nom) {
  File f = SD.open(PINS_FILE);
  if (!f) return false;
  bool trouve = false;
  while (f.available()) {
    String ligne = f.readStringUntil('\n');
    ligne.trim();
    int p = ligne.indexOf(';');
    if (p <= 0) continue;
    if (ligne.substring(0, p) == pin) {
      nom = ligne.substring(p + 1);
      trouve = true;
      break;
    }
  }
  f.close();
  return trouve;
}

// =====================================================
//  Commandes via le moniteur serie (j, b, s)
// =====================================================
void afficherFichier(const char* chemin) {
  File f = SD.open(chemin);
  if (!f) {
    Serial.println("(fichier absent ou vide)");
    return;
  }
  while (f.available()) Serial.write(f.read());
  f.close();
}

void commandesSerie() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'j') {
    Serial.println("--- JOURNAL D'ACCES ---");
    afficherFichier(LOG_FILE);
  } else if (c == 'b') {
    Serial.println("--- BADGES AUTORISES ---");
    afficherFichier(BADGES_FILE);
  } else if (c == 's') {
    Serial.printf("Autorises : %d | Refuses : %d\n", nbAutorises, nbRefuses);
  } else if (c == 'o') {
    Serial.println("Test servo : ouverture manuelle");
    ouvrirPorte();
  } else if (c == 'f') {
    servo.write(SERVO_FERME);
    fermeturePorteA = 0;
    Serial.println("Test servo : fermeture manuelle");
  }
}

// =====================================================
//  Porte et clavier (code PIN)
// =====================================================
void ouvrirPorte() {
  servo.write(SERVO_OUVERT);
  fermeturePorteA = millis() + DUREE_OUVERTURE_MS;
  Serial.println("Porte OUVERTE (servo a 90 degres)");
}

void afficherSaisie() {
  String etoiles = "";
  for (unsigned int i = 0; i < saisie.length(); i++) etoiles += "*";
  afficher("CODE", etoiles + "\n# valider  * annuler");
}

void validerPin() {
  String nom = "";
  bool ok = (saisie.length() == LONGUEUR_PIN) && trouverPin(saisie, nom);
  saisie = "";

  if (ok) {
    echecsPin = 0;
    nbAutorises++;
    ecrireJournal("CODE-PIN", nom, "AUTORISE");
    afficher("AUTORISE", nom + " (code)");
    ouvrirPorte();
    Serial.println("ACCES AUTORISE par code PIN : " + nom);
    retourEcranA = millis() + DUREE_OUVERTURE_MS;
  } else {
    echecsPin++;
    nbRefuses++;
    ecrireJournal("CLAVIER", "Inconnu", "REFUSE");  // le code saisi n'est jamais enregistre
    Serial.println("CODE PIN FAUX (" + String(echecsPin) + "/" + String(ESSAIS_MAX) + ")");
    if (echecsPin >= ESSAIS_MAX) {
      blocageJusqua = millis() + DUREE_BLOCAGE_MS;
      retourEcranA = 0;
      afficher("BLOQUE", "Clavier bloque 30 s");
    } else {
      afficher("CODE FAUX", String(echecsPin) + "/" + String(ESSAIS_MAX) + " essais");
      retourEcranA = millis() + DUREE_OUVERTURE_MS;
    }
  }
}

void gererClavier() {
  unsigned long maintenant = millis();

  // Clavier bloque apres trop d'essais
  if (blocageJusqua) {
    if (maintenant < blocageJusqua) {
      keypad.getKey();  // touches ignorees
      return;
    }
    blocageJusqua = 0;
    echecsPin = 0;
    ecranRepos();
  }

  // Saisie abandonnee
  if (saisie.length() > 0 && maintenant - derniereTouche > DELAI_SAISIE_MS) {
    saisie = "";
    ecranRepos();
  }

  char k = keypad.getKey();
  if (!k) return;
  derniereTouche = maintenant;

  if (k >= '0' && k <= '9') {
    if (saisie.length() < LONGUEUR_PIN) saisie += k;
    retourEcranA = 0;
    afficherSaisie();
  } else if (k == '*') {
    saisie = "";
    ecranRepos();
  } else if (k == '#') {
    validerPin();
  }
  // les touches A, B, C, D sont ignorees
}

// =====================================================
//  Traitement d'un badge
// =====================================================
void traiterBadge() {
  String uid = uidToStr();
  String nom = "";
  bool autorise = trouverBadge(uid, nom);

  if (autorise) {
    nbAutorises++;
    ecrireJournal(uid, nom, "AUTORISE");
    afficher("AUTORISE", nom);
    ouvrirPorte();
    Serial.println("ACCES AUTORISE : " + nom + " (" + uid + ")");
  } else {
    nbRefuses++;
    ecrireJournal(uid, "Inconnu", "REFUSE");
    afficher("REFUSE", "UID : " + uid);
    Serial.println("ACCES REFUSE : " + uid);
  }
  retourEcranA = millis() + DUREE_OUVERTURE_MS;
}

// =====================================================
//  Tableau de bord web
// =====================================================
String page(const String& titre, const String& corps, bool rafraichir = false) {
  String h = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  if (rafraichir) h += "<meta http-equiv='refresh' content='5'>";
  h += "<title>" + titre + "</title><style>";
  h += R"(
    body{font-family:Arial,sans-serif;margin:0;background:#f2f5f9;color:#222}
    header{background:#1f3a5f;color:#fff;padding:14px 20px}
    header h1{margin:0 0 8px;font-size:22px}
    nav a{color:#fff;margin-right:16px;text-decoration:none;font-weight:bold}
    main{padding:20px;max-width:900px;margin:auto}
    .cartes{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:20px}
    .carte{flex:1;min-width:150px;background:#fff;border-radius:8px;padding:14px;box-shadow:0 1px 4px #0002;text-align:center}
    .carte b{display:block;font-size:30px}
    table{width:100%;border-collapse:collapse;background:#fff;box-shadow:0 1px 4px #0002}
    th,td{padding:8px 10px;border-bottom:1px solid #e3e3e3;text-align:left}
    th{background:#e8eef6}
    .ok{color:#1a7f37;font-weight:bold}.ko{color:#c62828;font-weight:bold}
    input,button{padding:8px;margin:4px 4px 4px 0}
    button{background:#1f3a5f;color:#fff;border:0;border-radius:4px;cursor:pointer}
    a.del{color:#c62828}
  )";
  h += "</style></head><body><header><h1>Access Control - RFID</h1><nav>";
  h += "<a href='/'>Tableau de bord</a><a href='/refuses'>Acces refuses</a>";
  h += "<a href='/badges'>Badges</a><a href='/export'>Export CSV</a></nav></header>";
  h += "<main>" + corps + "</main></body></html>";
  return h;
}

// Tableau du journal (les plus recents en premier)
String tableJournal(bool seulementRefuses, int maxLignes) {
  if (maxLignes > 30) maxLignes = 30;
  String lignes[30];
  int total = 0;

  File f = SD.open(LOG_FILE);
  if (f) {
    f.readStringUntil('\n');  // en-tete
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (l.length() == 0) continue;
      if (seulementRefuses && l.indexOf(";REFUSE") < 0) continue;
      lignes[total % maxLignes] = l;
      total++;
    }
    f.close();
  }

  String h = "<table><tr><th>Date / heure</th><th>UID</th><th>Nom</th><th>Statut</th>";
  if (seulementRefuses) h += "<th></th>";
  h += "</tr>";
  int n = (total < maxLignes) ? total : maxLignes;
  for (int i = 0; i < n; i++) {
    String l = lignes[(total - 1 - i) % maxLignes];
    int p1 = l.indexOf(';');
    int p2 = l.indexOf(';', p1 + 1);
    int p3 = l.indexOf(';', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) continue;
    String date = l.substring(0, p1);
    String uid = l.substring(p1 + 1, p2);
    String nom = l.substring(p2 + 1, p3);
    String statut = l.substring(p3 + 1);
    String classe = statut.startsWith("AUTORISE") ? "ok" : "ko";
    h += "<tr><td>" + esc(date) + "</td><td>" + esc(uid) + "</td><td>" + esc(nom);
    h += "</td><td class='" + classe + "'>" + esc(statut) + "</td>";
    if (seulementRefuses) {
      if (uid == "CLAVIER") h += "<td>(code PIN)</td>";
      else h += "<td><a href='/badges?uid=" + esc(uid) + "'>Autoriser</a></td>";
    }
    h += "</tr>";
  }
  h += "</table>";
  if (n == 0) h += "<p>Aucune entree dans le journal.</p>";
  return h;
}

void pageAccueil() {
  String c = "<div class='cartes'>";
  c += "<div class='carte'>Acces autorises<b class='ok'>" + String(nbAutorises) + "</b></div>";
  c += "<div class='carte'>Acces refuses<b class='ko'>" + String(nbRefuses) + "</b></div>";
  c += "<div class='carte'>Total<b>" + String(nbAutorises + nbRefuses) + "</b></div></div>";
  c += "<h2>Journal d'acces (20 derniers)</h2>" + tableJournal(false, 20);
  server.send(200, "text/html", page("Tableau de bord", c, true));
}

void pageRefuses() {
  String c = "<h2>Tentatives d'acces refusees</h2>" + tableJournal(true, 30);
  server.send(200, "text/html", page("Acces refuses", c, true));
}

void pageBadges() {
  String c = "<h2>Badges autorises</h2><table><tr><th>UID</th><th>Nom</th><th></th></tr>";
  int n = 0;
  File f = SD.open(BADGES_FILE);
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      int p = l.indexOf(';');
      if (p <= 0) continue;
      String uid = l.substring(0, p);
      String nom = l.substring(p + 1);
      c += "<tr><td>" + esc(uid) + "</td><td>" + esc(nom) + "</td>";
      c += "<td><a class='del' href='/del?uid=" + esc(uid) + "'>Supprimer</a></td></tr>";
      n++;
    }
    f.close();
  }
  c += "</table>";
  if (n == 0) c += "<p>Aucun badge enregistre.</p>";
  c += "<h2>Ajouter un badge</h2><form action='/add' method='get'>";
  c += "<input name='uid' placeholder='UID (ex : 01020304)' value='" + esc(server.arg("uid")) + "' required>";
  c += "<input name='nom' placeholder='Nom complet' required>";
  c += "<button type='submit'>Ajouter</button></form>";
  server.send(200, "text/html", page("Badges", c));
}

void redirigerBadges() {
  server.sendHeader("Location", "/badges");
  server.send(303);
}

void actionAjouter() {
  String uid = clean(server.arg("uid"));
  uid.toUpperCase();
  String nom = clean(server.arg("nom"));
  String dejaLa;
  if (uid.length() > 0 && nom.length() > 0 && !trouverBadge(uid, dejaLa)) {
    File f = SD.open(BADGES_FILE, FILE_APPEND);
    if (f) {
      f.println(uid + ";" + nom);
      f.close();
    }
  }
  redirigerBadges();
}

void actionSupprimer() {
  String uid = clean(server.arg("uid"));
  String garde = "";
  File f = SD.open(BADGES_FILE);
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      int p = l.indexOf(';');
      if (p <= 0) continue;
      if (!l.substring(0, p).equalsIgnoreCase(uid)) garde += l + "\n";
    }
    f.close();
  }
  SD.remove(BADGES_FILE);
  File w = SD.open(BADGES_FILE, FILE_WRITE);
  if (w) {
    w.print(garde);
    w.close();
  }
  redirigerBadges();
}

void exporterCsv() {
  File f = SD.open(LOG_FILE);
  if (!f) {
    server.send(404, "text/plain", "Journal vide");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=journal_acces.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

// =====================================================
//  setup / loop
// =====================================================
void connecterWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  Serial.print("Connexion Wi-Fi");
  unsigned long debut = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - debut < 15000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connecte !");
    Serial.print("Adresse IP : ");
    Serial.println(WiFi.localIP());
    // Heure de Port-au-Prince (UTC-5, heure d'ete comme les Etats-Unis)
    configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  } else {
    Serial.println(" echec (le controle d'acces fonctionne quand meme)");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== Controle d'acces RFID ===");

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED : ERREUR");
  afficher("DEMARRAGE", "Veuillez patienter");

  SPI.begin(18, 19, 23);
  rfid.PCD_Init();

  sdOk = SD.begin(SD_CS);
  Serial.println(sdOk ? "SD : OK" : "SD : ERREUR");
  if (sdOk) {
    initialiserBadges();
    initialiserPins();
    compterJournal();
  } else {
    afficher("ERREUR SD", "Verifier le module");
  }

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(SERVO_FERME);

  connecterWifi();

  server.on("/", pageAccueil);
  server.on("/refuses", pageRefuses);
  server.on("/badges", pageBadges);
  server.on("/add", actionAjouter);
  server.on("/del", actionSupprimer);
  server.on("/export", exporterCsv);
  server.begin();
  Serial.println("Serveur web demarre (port 80)");
  Serial.println("Commandes serie : j = journal | b = badges | s = stats | o = ouvrir servo | f = fermer servo");

  ecranRepos();
}

void loop() {
  server.handleClient();
  commandesSerie();
  gererClavier();

  // Refermer la porte apres le delai
  if (fermeturePorteA && millis() > fermeturePorteA) {
    servo.write(SERVO_FERME);
    fermeturePorteA = 0;
    Serial.println("Porte FERMEE (servo a 0 degre)");
  }
  // Revenir a l'ecran d'attente
  if (retourEcranA && millis() > retourEcranA) {
    ecranRepos();
    retourEcranA = 0;
  }

  // Lecture d'un badge
  if (sdOk && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    traiterBadge();
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}
