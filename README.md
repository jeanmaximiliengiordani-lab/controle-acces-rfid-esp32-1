-Contrôle d'accès RFID multi-utilisateurs avec journal en ligne
Formation EIC 3.0 — Électronique & Prototypage Projet N°4 **Étudiant : **GIORDANI JEAN MAXIMILIEN
Description
Version connectée et évolutive d'une serrure à code, avec identification individuelle par badge RFID. Un ESP32 lit le badge présenté, le compare à une liste d'utilisateurs autorisés stockée sur carte SD, affiche le résultat sur un écran OLED, commande une serrure (servomoteur) et enregistre chaque tentative d'accès (identité, date, heure) dans un journal consultable depuis un tableau de bord web hébergé par l'ESP32.
Le projet est simulé sur Wokwi.
Objectifs
    • Identifier chaque utilisateur par son badge RFID (MFRC522).
    • Stocker la liste des badges autorisés sur la carte SD et la rendre modifiable (ajout / suppression).
    • Enregistrer chaque accès avec l'identité, la date et l'heure.
    • Afficher localement le statut (accès autorisé / refusé) sur l'écran OLED.
    • Fournir un journal consultable des tentatives refusées.
Utilisateurs autorisés (par défaut)
Nom	UID du badge
Max	01020304
Colson	A1B2C3D4
Lilie	11223344
Tommy	55667788

Ces utilisateurs sont créés automatiquement dans /badges.txt au premier démarrage si le fichier est absent ou vide. Tout autre badge est refusé.
Code PIN de secours (carte oubliée)
Chaque utilisateur a aussi un code PIN personnel à 4 chiffres, enregistré sur la carte SD dans /pins.txt (format PIN;Nom, créé automatiquement au premier démarrage). Les codes ne sont pas affichés sur le tableau de bord.
    • Saisir les 4 chiffres sur le clavier, puis \# pour valider ; \* pour annuler.
    • Code correct : accès autorisé, porte ouverte 3 secondes, entrée « CODE-PIN » dans le journal.
    • Code faux : accès refusé et tentative journalisée (le code saisi n'est jamais enregistré).
    • Après 3 échecs consécutifs, le clavier est bloqué 30 secondes.
    • Une saisie abandonnée est effacée au bout de 10 secondes.
Composants
Composant	Rôle
ESP32 DevKit	Microcontrôleur, Wi-Fi, serveur web
Lecteur RFID MFRC522	Lecture des badges
Module carte SD	Stockage des badges et du journal
Écran OLED SSD1306 (I2C)	Affichage du statut
Servomoteur	Simulation de la serrure
Clavier 4x4	Saisie du code PIN personnel (secours si carte oubliée)

Schéma de câblage
Lien Wokwi : [https://wokwi.com/projects/476426746344235009]

Composant	Broche du module	Broche ESP32
MFRC522	SDA (SS)	GPIO21
MFRC522	SCK	GPIO18
MFRC522	MOSI	GPIO23
MFRC522	MISO	GPIO19
MFRC522	RST	GPIO22
MFRC522	3.3V / GND	3V3 / GND
Carte SD	CS	GPIO5
Carte SD	SCK / DI / DO	GPIO18 / GPIO23 / GPIO19 (bus SPI partagé)
Carte SD	VCC / GND	3V3 / GND
OLED	SDA	GPIO4
OLED	SCL	GPIO15
OLED	VCC / GND	3V3 / GND
Servo	Signal	GPIO13
Servo	V+ / GND	5V / GND

Le lecteur RFID et la carte SD partagent le même bus SPI ; seules les broches de sélection (SS et CS) diffèrent.
Structure du dépôt
.  
├── README.md           \# ce fichier  
├── sketch.ino          \# code source complet (version Wokwi en ligne)  
├── libraries.txt       \# bibliothèques pour Wokwi en ligne  
├── diagram.json        \# circuit Wokwi  
├── badges.txt          \# exemple de liste de badges (carte SD)  
├── platformio.ini      \# configuration PlatformIO (VS Code)  
├── wokwi.toml          \# configuration du simulateur Wokwi pour VS Code  
├── src/  
│   └── main.cpp        \# même code, version PlatformIO  
└── images/             \# captures d'écran du circuit et du tableau de bord
Fichiers sur la carte SD
    • /badges.txt : un badge par ligne, au format UID;Nom.
    • /pins.txt : codes PIN de secours, au format PIN;Nom (créé automatiquement).
    • /journal.csv : créé automatiquement, au format date\_heure;uid;nom;statut.
Installation
    1. Ouvrir le projet Wokwi (lien ci-dessus) ou en créer un nouveau avec ESP32, MFRC522, module SD, OLED SSD1306 et servo.
    2. Copier sketch.ino, libraries.txt et diagram.json dans le projet.
    3. Dans l'onglet Carte SD, ajouter badges.txt (optionnel : il est créé automatiquement).
    4. Lancer la simulation avec ▶.
Bibliothèques utilisées : MFRC522, Adafruit SSD1306, Adafruit GFX Library, ESP32Servo, Keypad.
Utilisation
    1. Cliquer sur le lecteur MFRC522 dans la simulation pour présenter un badge.
    2. Badge autorisé : l'OLED affiche « AUTORISE » et le nom, le servo ouvre la serrure 3 secondes, l'accès est journalisé.
    3. Badge inconnu : l'OLED affiche « REFUSE » et l'UID, la serrure reste fermée, la tentative est journalisée.
    4. Le moniteur série accepte 3 commandes : j (journal), b (badges), s (statistiques).
Tableau de bord web
Le serveur web de l'ESP32 (port 80) propose :
Page	Contenu
/	Compteurs et 20 derniers accès (rafraîchissement automatique)
/refuses	Journal des tentatives refusées, avec lien « Autoriser »
/badges	Liste des badges, ajout et suppression
/export	Téléchargement du journal au format CSV

Remarque : sur wokwi.com, l'accès au serveur depuis un navigateur nécessite la passerelle IoT Wokwi (abonnement Wokwi Club). Avec l'extension Wokwi pour VS Code (passerelle intégrée, redirection de port définie dans wokwi.toml), le tableau de bord est accessible à l'adresse http://localhost:8180. Le journal reste aussi consultable via le moniteur série (commande j). Sur une vraie carte ESP32, le tableau de bord est accessible à l'adresse IP affichée sur l'OLED.
Simulation dans VS Code
    1. Installer VS Code, l'extension PlatformIO IDE et l'extension Wokwi Simulator.
    2. Dans VS Code : F1 puis Wokwi: Request a New License pour activer la licence gratuite.
    3. Ouvrir le dossier du projet (platformio.ini, wokwi.toml, diagram.json, src/main.cpp).
    4. Compiler avec PlatformIO (Build).
    5. F1 puis Wokwi: Start Simulator, et ouvrir http://localhost:8180 dans le navigateur.
Captures d'écran
 
Auteur
GIORDANI JEAN MAXIMILIEN — Projet N°4 — Formation EIC 3.0
