// Game:      Montana 2
// Copyright: 2026 Frank van de Ven
// Licence:   MIT
 
 MontanaArduboy arduboy;                              // creates the shared engine object
// ------------------------------------------------------------------------------------------

// variabelen
 #define wachttijd 2000                               // tijd dat startscherm zichtbaar 
 #define wachttijd2 1200                              // tijd voor verwijderen azen
 #define wachttijd3 1200                              // tijd voor wissen kaarten 
 #define wachttijd4 600                               // tijd voor verschijnen eind scherm
 #define wachttijd5 300                               // snelheid knipperen hint kaart

 unsigned long wacht;                                 // voor vertragingen 
 bool knipper = 0;                                    // laat kaart wel (1) / niet (0) zien
 byte snel = 10;                                      // snelheid knipperen kaart
 byte stok[52];                                       // de stok waar alle kaarten inzitten
 byte spel[13][4];                                    // alle kaarten in 4 rijen op tafel
 byte x,y;                                            // teller voor loeps
 byte Cx,Cy;                                          // locatie cursor
 byte game = 0;                                       // stage machine begin bij 0
 byte card;                                           // declareer back-up variabele
 byte hint = 2;                                       // bij hint= 1 werkt A knop voor hints
 byte eind = 2;                                       // bij eind= 1 stopt spel na 3 keer
 byte delen = 3;                                      // max aantal keer opnieuw delen
 byte tip = 0;                                        // 1 = kaart niet te verplaatsen
 byte introPhase = 0;                                 // frame-state for the blocking intro
 bool introCredit = false;
 byte optionPhase = 0;                                // options screen sub-state
 byte losePhase = 0;                                  // lost screen sub-state
 byte winPhase = 0;                                   // win screen sub-state
 byte hintVerder = 0;                                 // hint: wait for A release/press
 bool hintKnipper = false;
 unsigned long hintWacht = 0;

// Arduino's IDE generates prototypes for the sketch; this single translation
// unit needs them explicitly.
void introFrame(void);
void beginWaarde(void);
void schudden(void);
void stokNaarSpel(void);
void zoekAzen(void);
void kaartenOpDisplay(void);
void toetsen(void);
void controle(void);
void opnieuwSchudden1(void);
void opnieuwSchudden2(void);
void WisKaartenUitStok(void);
void verplaats(void);
void optionsFrame(void);
void lostFrame(void);
void winFrame(void);
void hintFrame(void);
 
// ------------------------------------------------------------------------------------------

void setup() {
 arduboy.begin();
 arduboy.setFrameRate(30);
 wacht = millis();                                    // start the intro timer here
}

// ------------------------------------------------------------------------------------------

void loop() {
 if (!(arduboy.nextFrame())) {return;}
 if (arduboy.frameCount % snel == 0) {knipper = knipper ^ 1;}
 arduboy.clear();
 switch (game) {
 case 0:                                              // intro screens (non-blocking)
  introFrame();
  break;
 case 1:                                              // start a new deal
  arduboy.initRandomSeed();
  beginWaarde();
  schudden();
  stokNaarSpel();
  wacht = millis();
  game = 2;
  kaartenOpDisplay();
  break;
 case 2:                                              // remove aces after the pause
  if (millis() - wacht >= wachttijd2) {
   zoekAzen();
   stokNaarSpel();
   while (Cx < 12 && spel[Cx][Cy] == 98) {Cx++;}
   game = 3;
  }
  kaartenOpDisplay();
  break;
 case 3:                                              // input and win check
  toetsen();
  controle();
  kaartenOpDisplay();
  break;
 case 4:                                              // prepare a reshuffle
  if (eind == 1) {
   delen--;
   if (delen == 0) {losePhase = 0; wacht = millis(); game = 8; break;}
  }
  opnieuwSchudden1();
  WisKaartenUitStok();
  kaartenOpDisplay();
  break;
 case 5:                                              // finish the reshuffle after the pause
  if (millis() - wacht >= wachttijd3) {opnieuwSchudden2();}
  kaartenOpDisplay();
  break;
 case 6:                                              // move the selected card
  verplaats();
  if (tip == 1 && hint == 1) {
   hintVerder = 0; hintKnipper = 0; hintWacht = millis(); knipper = 0;
   game = 10;
  }
  kaartenOpDisplay();
  break;
 case 7:                                              // options screen
  optionsFrame();
  break;
 case 8:                                              // shuffle limit reached
  lostFrame();
  break;
 case 9:                                              // won
  winFrame();
  break;
 case 10:                                             // hint display
  hintFrame();
  break;
 }
 arduboy.display();
}

// ------------------------------------------------------------------------------------------

// laat als hint alle kaarten knipperen die gelegd kunnen worden
void drawHintHighlights() {
 byte x1, y1;
 for (y1 = 0; y1 < 4; y1++) {
  if (spel[0][y1] == 98) {
   for (y = 0; y < 4; y++) {
    for (x = 1; x < 13; x++) {
     if (spel[x][y] == 1 || spel[x][y] == 14 ||
         spel[x][y] == 27 || spel[x][y] == 40) {
      Sprites::drawOverwrite(x*10, y*16, kaart, 17);
      Sprites::drawOverwrite(x*10, y*16+8, kaart, 18);
     }
    }
   }
  }
 }
 for (y1 = 0; y1 < 4; y1++) {
  for (x1 = 1; x1 < 13; x1++) {
   if (spel[x1][y1] == 98) {
    card = spel[x1-1][y1];
    if (card != 12 && card != 25 && card != 38 && card != 51) {
     card++;
     for (y = 0; y < 4; y++) {
      for (x = 0; x < 13; x++) {
       if (spel[x][y] == card) {
        Sprites::drawOverwrite(x*10, y*16, kaart, 17);
        Sprites::drawOverwrite(x*10, y*16+8, kaart, 18);
       }
      }
     }
    }
   }
  }
 }
}

void hintFrame() {
 if (millis() - hintWacht >= wachttijd5) {
  hintWacht = millis();
  hintKnipper = !hintKnipper;
 }
 knipper = 0;                                        // keep every card visible
 kaartenOpDisplay();
 if (hintKnipper) {drawHintHighlights();}
 if (hintVerder == 0 && arduboy.pressed(A_BUTTON) == false) {hintVerder = 1;}
 if (hintVerder == 1 && arduboy.pressed(A_BUTTON) == true) {
  hintVerder = 0;
  game = 3;
 }
}

// ------------------------------------------------------------------------------------------

// print tekst teveel pogingen en start nieuw spel
void drawDecorativeGrid() {
 for (y = 0; y < 4; y++) {
  for (x = 0; x < 13; x++) {
   Sprites::drawOverwrite(x*10, y*16, kaart, x);
   Sprites::drawOverwrite(x*10, y*16+8, kaart, y+13);
  }
 }
}

void lostFrame() {
 drawDecorativeGrid();
 if (losePhase == 0) {
  if (millis() - wacht >= wachttijd4) {losePhase = 1;}
  return;
 }
 arduboy.fillRect(12, 18, 104, 27, BLACK);
 arduboy.setCursor(19, 22);
 arduboy.println("To may attempts");
 arduboy.setCursor(38, 34);
 arduboy.println("New game?");
 if (arduboy.pressed(A_BUTTON) || arduboy.pressed(B_BUTTON)) {
  losePhase = 0;
  game = 1;
 }
}

// ------------------------------------------------------------------------------------------

// laat de spelopties zien en laat de speler een keuze maken
// hint (1= A knop geeft hints, 0= nee)  eind (1= spel stopt na 3 keer, 0= spel loopt door)
void drawHintOptions() {
 arduboy.fillRect(12, 18, 104, 27, BLACK);
 arduboy.setCursor(19, 22); arduboy.println("Hint");
 arduboy.setCursor(46, 22); arduboy.println("(A");
 arduboy.setCursor(62, 22); arduboy.println("button)?");
 arduboy.setCursor(35, 34); arduboy.println("A:");
 arduboy.setCursor(47, 34); arduboy.println("Yes B:");
 arduboy.setCursor(83, 34); arduboy.println("No");
}

void drawShuffleOptions() {
 arduboy.fillRect(12, 18, 104, 27, BLACK);
 arduboy.setCursor(24, 22); arduboy.println("Shuffle");
 arduboy.setCursor(69, 22); arduboy.println("limit?");
 arduboy.setCursor(35, 34); arduboy.println("A:");
 arduboy.setCursor(47, 34); arduboy.println("Yes B:");
 arduboy.setCursor(83, 34); arduboy.println("No");
}

void optionsFrame() {
 drawDecorativeGrid();
 if (optionPhase == 0) {
  drawHintOptions();
  if (!arduboy.pressed(A_BUTTON) && !arduboy.pressed(B_BUTTON)) {optionPhase = 1;}
 } else if (optionPhase == 1) {
  drawHintOptions();
  if (arduboy.pressed(A_BUTTON)) {hint = 1; optionPhase = 2;}
  else if (arduboy.pressed(B_BUTTON)) {hint = 0; optionPhase = 2;}
 } else if (optionPhase == 2) {
  drawShuffleOptions();
  if (!arduboy.pressed(A_BUTTON) && !arduboy.pressed(B_BUTTON)) {optionPhase = 3;}
 } else {
  drawShuffleOptions();
  if (arduboy.pressed(A_BUTTON)) {eind = 1; game = 1;}
  else if (arduboy.pressed(B_BUTTON)) {eind = 0; game = 1;}
 }
}

// ------------------------------------------------------------------------------------------

// wis de kaarten die goed liggen uit de stok
 void WisKaartenUitStok() {
 for (byte tel = 0; tel < 52; tel++){                 // doorloop alle kaarten in de stok
  stok[tel] = tel;                                    // zet oplopende kaart nr in de stok
 }
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  card = spel[0][y];                                  // sla linker kaart op
  if (card==1 || card==14 || card==27 || card==40) {  // ligt er een 2?
   for (x = 0; x < 13; x++) {                         // doorloop alle kolommen met kaarten
    if (spel[x][y] != 99) {                           // ligt hier GEEN kaart goed?
     stok[card + x] = 99;                             // wis dan deze kaart uit de stok
    }
   }
  }
 }
}
// ------------------------------------------------------------------------------------------

// controleer of het spel is uitgespeeld
void controle() {
 byte aantal = 0;                                     // telt aantal kaarten dat goed ligt
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  card = spel[0][y];                                  // back-up kaart onder cursor
  if (card==1 || card==14 || card==27 || card==40) {  // ligt er een 2?
   for (x = 0; x < 13; x++) {                         // doorloop alle kolommen met kaarten
   if (spel[x][y] == card) {aantal++;}                // ligt deze kaart goed
   card++;                                            // volgende kaart
   }
  }
 }
 if (aantal == 48) {                                  // all cards in place
  knipper = 0;
  winPhase = 0;
  wacht = millis();
  game = 9;
 }
}
// ------------------------------------------------------------------------------------------

// print tekst gewonnen en start nieuw spel
void winFrame() {
 knipper = 0;
 kaartenOpDisplay();
 if (winPhase == 0) {
  if (millis() - wacht >= wachttijd4) {winPhase = 1;}
  return;
 }
 arduboy.fillRect(12, 18, 94, 27, BLACK);
 arduboy.setCursor(14, 22); arduboy.println("Congratulations");
 arduboy.setCursor(32, 34); arduboy.println("New game?");
 if (arduboy.pressed(A_BUTTON) || arduboy.pressed(B_BUTTON)) {
  winPhase = 0;
  game = 1;
 }
}

// ------------------------------------------------------------------------------------------

// wis alle fouten kaarten die fout liggen
void opnieuwSchudden1(){
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  card = spel[0][y];                                  // back-up kaart onder cursor
  if (card==1 || card==14 || card==27 || card==40) {  // ligt er een 2?
   for (x = 1; x < 13; x++) {                         // doorloop alle kolommen met kaarten
    card++;                                           // volgende kaart
    if (spel[x][y] != card) {                         // is kaart op tafel != volgende kaart
     card = 100;                                      // schakel kaart teller uit
     spel[x][y] = 99;                                 // er liggen geen kaarten meer goed
    }
   }
  wacht = millis();                                   // laad wachttijd met huidige tijd 
  }
 else {                                               // ligt er geen 2 vooraan
  for (x = 0; x < 13; x++) {                          // doorloop rij met kaarten
   spel[x][y] = 99;                                   // er liggen geen kaarten meer goed
  }
 }
 }
 game = 5;                                            // ga naar kaarten schudden / plaatsen
}
// ------------------------------------------------------------------------------------------

// schud de kaarten die fout liggen opnieuw en plaats geschudde kaarten in open ruimtes
void opnieuwSchudden2() {
 schudden();                                          // schud de kaarten in de stok
 stokNaarSpel();                                      // verdeel kaarten uit stok in spel[]
 Cx = 0; Cy = 0;                                      // verplaats cursor naar links boven
 while (Cx < 12 && spel[Cx][Cy] == 98) {Cx++;}        // schuif cursor op als er een aas ligt
 wacht = millis();                                    // laad wachttijd met huidige tijd
 game = 2;                                            // naar de stap 2
}
// ------------------------------------------------------------------------------------------

// verplaats de kaart onder de cursor positie en leg hem op zijn plaats
void verplaats() {
 tip = 1;                                             // reset controle hints geven
 card = spel[Cx][Cy];                                 // back-up kaart onder cursor
 if (card==1 || card==14 || card==27 || card==40) {   // ligt er een 2?
  for (y = 0; y < 4; y++){                            // doorloop alle kaarten 1e kolom
   if (spel[0][y] == 98) {                            // is dit een vrije plaats
    spel[0][y] = card;                                // verplaats kaart naar nieuwe positie
    spel[Cx][Cy] = 98;                                // wis kaart op oude positie
    y = 5;                                            // breek loep af
    tip = 0;                                          // reset controle hints geven
   }
  }
 }
 else {
  for (y = 0; y < 4; y++) {                           // doorloop alle rijen met kaarten
   for (x = 0; x < 12; x++) {                         // doorloop alle kolommen met kaarten
    if (spel[x][y] == card - 1) {                     // zoek de kaart die voor cursor komt
      if (spel[x + 1][y] == 98) {                     // is de plaats hierachter vrij?
        spel[x + 1][y] = card;                        // verplaats kaart naar nieuwe positie
        spel[Cx][Cy] = 98;                            // wis kaart op oude positie
        x = 13; y = 4;                                // breek loep af
        tip = 0;                                      // reset controle hints geven
      }
    }
   }
  }
 }
 game = 3;                                            // ga terug naar invoer toetsen
}
// ------------------------------------------------------------------------------------------

// lees de toetsen, verplaats cursor en pas game status aan
void toetsen() {
 if (arduboy.justPressed(A_BUTTON)) {game = 6;}       // knop A: verplaats kaart
 if (arduboy.justPressed(B_BUTTON)) {game = 4;}       // knop b: opnieuw schudden
 if (arduboy.justPressed(UP_BUTTON)) {                // verplaats cursor omhoog
  if (Cy != 0) {Cy--;}                                // sta je niet boven verlaag y met 1
 }
 if (arduboy.justPressed(DOWN_BUTTON)) {              // verplaats cursor omlaag
  if (Cy != 3) {Cy++;}                                // sta je niet onder verhoog y met 1
 }
 if (arduboy.justPressed(LEFT_BUTTON)) {              // verplaats cursor naar links
  if (Cx != 0) {Cx--;}                                // sta je niet links verlaag x met 1
  }
 if (arduboy.justPressed(RIGHT_BUTTON)) {             // verplaats cursor naar rechts
  if (Cx != 12) {Cx++;}                               // sta je niet rechts verhoog x met 1
 }
}
// ------------------------------------------------------------------------------------------

// haal de kaarten uit de stok[] en verdeel ze over het spel[] array
void stokNaarSpel() {
 byte k3 = 0;                                         // declareer variabele
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  for (x = 0; x < 13; x++) {                          // doorloop alle kolommen met kaarten
   if (spel[x][y] == 99) {                            // kijk of op locatie geen kaart ligt
    while (k3 < 52 && stok[k3] == 99) {k3++;}         // ligt kaart al op tafel. volgende
     spel[x][y] = stok[k3];                           // leg kaart uit stop in spel
     k3++;                                            // volgende kaart uit de stok
   }
  }
 }
}
// ------------------------------------------------------------------------------------------

// zet de kaarten uit "spel[]" op het display
void kaartenOpDisplay() {
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  for (x = 0; x < 13; x++) {                          // doorloop alle kolommen met kaarten
   if (spel[x][y] != 98 && spel[x][y] != 99) {        // bij een leeg vak geen kaart printen
    if (!(x == Cx && y == Cy && knipper == 1)) {      // laat kaart onder cursor knipperen
     byte k1 = spel[x][y] % 13;                       // bereken kaart nr uit stok nr
     byte k2 = spel[x][y] / 13 + 13;                  // bereken kaart soort uit stok nr
     Sprites::drawOverwrite(x*10, y*16, kaart, k1);   // zet bovenste deel kaart op display
     Sprites::drawOverwrite(x*10, y*16+8, kaart, k2); // zet onderste deel kaart op display
    }
   }
   if (spel[Cx][Cy] == 98 && Cx == x && Cy == y && knipper ==1) { // cursor op leeg veld?
    Sprites::drawOverwrite(x*10, y*16, kaart, 17);    // zet bovenste deel kaart op display
    Sprites::drawOverwrite(x*10, y*16+8, kaart, 18);  // zet onderste deel kaart op display
   }
  }
 }
}
// ------------------------------------------------------------------------------------------

// zoek alle azen en haal ze uit de het spel[] (worden nr 98)
void zoekAzen() {
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  for (x = 0; x < 13; x++) {                          // doorloop alle kolommen met kaarten
   if (spel[x][y] == 0) {spel[x][y] = 98;}            // verander schoppen aas in nr 98
   if (spel[x][y] == 13) {spel[x][y] = 98;}           // verander klaveren aas in nr 98
   if (spel[x][y] == 26) {spel[x][y] = 98;}           // verander harten aas in nr 98
   if (spel[x][y] == 39) {spel[x][y] = 98;}           // verander ruiten aas in nr 98
  }
 }
}
// ------------------------------------------------------------------------------------------

// Zet bij starten spel de waardes van de array's en variabelen goed
void beginWaarde() {
 // laad de stok met alle kaarten op volgorde
 for (byte tel = 0; tel < 52; tel++){                 // doorloop alle kaarten in de stok
  stok[tel] = tel;                                    // zet oplopende kaart nr in de stok
 }
 for (y = 0; y < 4; y++) {                            // doorloop alle rijen met kaarten
  for (x = 0; x < 13; x++) {                          // doorloop alle kolommen met kaarten
   spel[x][y] = 99;                                   // maak deze locatie leeg
  }
 }
 Cx = 0; Cy = 0;                                      // zet cursor positie linksboven
 delen = 3;                                           // max aantal keer opnieuw delen
}
// ------------------------------------------------------------------------------------------

// schud de kaarten in de stok door elkaar (aangepast Fiher Yates algoritme)
void schudden() {
 byte tijdelijk, arrayNr;                             // declareer variabelen
 for (int nr = 0; nr < 52; nr++){                     // doorloop alle kaarten in de stok
  arrayNr = random(nr,51);                            // kies een getal
  tijdelijk = stok[nr];                               // sla de kaart uit array tijdelijk op
  stok[nr] = stok[arrayNr];                           // wissel de kaart in de stok
  stok[arrayNr] = tijdelijk;                          // wissel de kaart in de stok
 }
}
// ------------------------------------------------------------------------------------------

// zet het startscherm op het display
void introFrame() {
 arduboy.drawCompressed(0, 0, plaatje, WHITE);
 if (introPhase == 0) {
  if (millis() - wacht >= wachttijd) {introPhase = 1;}
  return;
 }
 if (!introCredit) {
  arduboy.fillRect(11, 49, 106, 12, BLACK);
  arduboy.setCursor(13, 51); arduboy.println("A:Play");
  arduboy.setCursor(52, 51); arduboy.println("game");
  arduboy.setCursor(80, 51); arduboy.println("B:Code");
 } else {
  arduboy.fillRect(11, 50, 106, 10, BLACK);
  arduboy.setCursor(22, 51); arduboy.println("Frank");
  arduboy.setCursor(54, 51); arduboy.println("van");
  arduboy.setCursor(75, 51); arduboy.println("de");
  arduboy.setCursor(90, 51); arduboy.println("Ven");
 }
 if (arduboy.pressed(B_BUTTON)) {introCredit = true;}
 if (arduboy.pressed(A_BUTTON)) {
  introCredit = false;
  introPhase = 0;
  optionPhase = 0;
  game = 7;
 }
}

// ------------------------------------------------------------------------------------------
