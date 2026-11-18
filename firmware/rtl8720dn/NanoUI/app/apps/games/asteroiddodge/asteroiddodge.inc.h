// AsteroidDodge.ino
// Copyright (c) 2026 James Collins
// Licensed freely under the MIT License. See LICENSE in the project root for
// details.



// Screen dimensions
constexpr uint8_t SCREEN_W = 128;
constexpr uint8_t SCREEN_H = 64;

// Player
constexpr int16_t PLAYER_SPEED = 1;
constexpr uint8_t PLAYER_W = 5;
constexpr uint8_t PLAYER_H = 5;

// Fixed seed to ensure players all get the same level
constexpr uint16_t LEVEL_SEED = 42;

// EEPROM layout
constexpr int EEPROM_HIGHSCORE_ADDR = 0xAD;
constexpr int EEPROM_MAGIC_ADDR = EEPROM_HIGHSCORE_ADDR + sizeof(uint16_t);
constexpr uint8_t EEPROM_MAGIC = 0xAD;

// Asteroids info
constexpr uint8_t MAX_ASTEROIDS = 16;
constexpr uint8_t ASTEROID_W = 4;
constexpr uint8_t ASTEROID_H = 2;
constexpr uint8_t SPAWN_PERIOD = 15; // frames between spawns (0.25s)

// Game states
enum GameState : uint8_t {
    STATE_TITLE,
    STATE_ABOUT,
    STATE_PLAY,
    STATE_DEAD,
    STATE_GAMEOVER,
};
//Player position
struct Player {
    int16_t x, y;
};
//Asteroid State
struct Asteroid {
    int16_t x, y;
    int16_t vx;
    bool active;
};
// Descending tones when hit by asteroid
const uint16_t hitTones[] PROGMEM = {500, 80, 250, 130, TONES_END};

// Player death sound
const uint16_t deathTones[] PROGMEM = {120, 50,  90, 50, 120, 50,  90,
                                       50,  120, 50, 90, 50,  70,  50,
                                       90,  50,  70, 80, 50,  120, TONES_END};

// Read-only screen text. Named namespace-scope arrays keep these bytes in a
// section named after the mangled symbol (`.rodata._ZN...AsteroidDodgeSandbox`),
// which the split linker attributes to this port with its code; anonymous
// literals merge into .rodata.str1.1 and lose the namespace. `used` keeps the
// objects emitted, and every array below is printed by the title/about/game-over
// screens.
__attribute__((used)) const char titleText[] = "AsteroidDodge";
__attribute__((used)) const char playLabel[] = "Play";
__attribute__((used)) const char hiLabel[] = "Hi:";
__attribute__((used)) const char aboutLabel[] = "About";
__attribute__((used)) const char muteLabel[] = "Mute:";
__attribute__((used)) const char byText[] = "by James Collins";
__attribute__((used)) const char githubText[] = "github.com/jbc2001/";
__attribute__((used)) const char projectText[] = "AsteroidDodge-Arduboy";
__attribute__((used)) const char continueText[] = "press any to continue";
__attribute__((used)) const char gameOverText[] = "GAME OVER";
__attribute__((used)) const char scoreLabel[] = "Score:";

// On-demand game state (see port.h). Placed after every type it uses.
struct GameStateHolder {
    Arduboy2 arduboy;
    ArduboyTones sound;
    GameState state = STATE_TITLE;
    Player player;
    Asteroid asteroids[MAX_ASTEROIDS];
    uint8_t menuSelection = 0;
    uint16_t score = 0;
    uint16_t highScore = 0;
    uint16_t frameCount = 0;
    uint8_t spawnTimer = 0;
    uint8_t deathTimer = 0;
    uint8_t lives = 3;
    uint8_t invincTimer = 0;
};
static GameStateHolder *g_game = nullptr;
#define arduboy (g_game->arduboy)
#define sound (g_game->sound)
#define state (g_game->state)
#define player (g_game->player)
#define asteroids (g_game->asteroids)
#define menuSelection (g_game->menuSelection)
#define score (g_game->score)
#define highScore (g_game->highScore)
#define frameCount (g_game->frameCount)
#define spawnTimer (g_game->spawnTimer)
#define deathTimer (g_game->deathTimer)
#define lives (g_game->lives)
#define invincTimer (g_game->invincTimer)

// Hides all asteroids and resets death timer
void ResetAsteroids() {
    for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
        asteroids[i].active = false;
    }
    deathTimer = 0;
}

// Spawns next asteroid at the left edge with random y and speed
void SpawnAsteroid() {
    for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) {
            asteroids[i].active = true;
            asteroids[i].x = -ASTEROID_W;
            asteroids[i].y = random(0, SCREEN_H - ASTEROID_H);
            asteroids[i].vx = random(2, 4);
            return;
        }
    }
}

//checks if player collides with an asteroid
bool PlayerHits(const Asteroid &a) {
    return (player.x < a.x + ASTEROID_W && player.x + PLAYER_W > a.x &&
            player.y < a.y + ASTEROID_H && player.y + PLAYER_H > a.y);
}

// Resets game variables to start a new game
void ResetGame() {
    randomSeed(LEVEL_SEED);
    player.x = SCREEN_W - PLAYER_W - 20;
    player.y = SCREEN_H / 2;
    score = 0;
    frameCount = 0;
    spawnTimer = 0;
    lives = 3;
    invincTimer = 0;
    ResetAsteroids();
}

// Handles menu navigation for title screen
void UpdateTitle() {
    if (arduboy.justPressed(UP_BUTTON) && menuSelection > 0) {
        menuSelection--;
        sound.tone(600, 20);
    }
    if (arduboy.justPressed(DOWN_BUTTON) && menuSelection < 2) {
        menuSelection++;
        sound.tone(600, 20);
    }
    if (arduboy.justPressed(A_BUTTON)) {
        if (menuSelection == 0) {
            sound.tone(1000, 40);
            ResetGame();
            state = STATE_PLAY;
        } else if (menuSelection == 1) {
            sound.tone(1000, 40);
            state = STATE_ABOUT;
        } else {
            sound.tone(1000, 40);
            arduboy.audio.toggle();
            arduboy.audio.saveOnOff();
        }
    }
}

// Draws title screen with static asteroids and menu options
void DrawTitle() {
    // Static background asteroids
    arduboy.fillRect(2, 2, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(20, 0, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(55, 0, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(90, 0, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(118, 3, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(0, 20, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(25, 15, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(85, 13, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(110, 20, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(7, 33, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(3, 44, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(14, 26, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(22, 38, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(16, 51, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(115, 29, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(120, 41, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(113, 54, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(96, 26, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(104, 42, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(95, 54, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(10, 57, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(35, 61, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(72, 61, ASTEROID_W, ASTEROID_H, WHITE);
    arduboy.fillRect(121, 61, ASTEROID_W, ASTEROID_H, WHITE);

    // Title
    arduboy.setCursor(29, 4);
    arduboy.print(titleText);

    //Menu drawing with selection indicator
    if (menuSelection == 0) {
        arduboy.setCursor(36, 20);
        arduboy.print('>');
    }
    arduboy.setCursor(48, 20);
    arduboy.print(playLabel);

    arduboy.setCursor(48, 30);
    arduboy.print(hiLabel);
    arduboy.print(highScore);

    if (menuSelection == 1) {
        arduboy.setCursor(36, 40);
        arduboy.print('>');
    }
    arduboy.setCursor(48, 40);
    arduboy.print(aboutLabel);

    if (menuSelection == 2) {
        arduboy.setCursor(36, 52);
        arduboy.print('>');
    }
    arduboy.setCursor(48, 52);
    arduboy.print(muteLabel);

    // Mute setting, crossed box = on
    arduboy.drawRect(84, 52, 7, 7, WHITE);
    if (!arduboy.audio.enabled()) {
        arduboy.drawLine(84, 52, 90, 58, WHITE);
        arduboy.drawLine(90, 52, 84, 58, WHITE);
    }
}

// Handles input for about screen
void UpdateAbout() {
    if (arduboy.justPressed(A_BUTTON | B_BUTTON | UP_BUTTON | DOWN_BUTTON |
                            LEFT_BUTTON | RIGHT_BUTTON)) {
        sound.tone(1000, 40);
        state = STATE_TITLE;
    }
}

// Draws about screen with credits and github link
void DrawAbout() {
    arduboy.setCursor(16, 6);
    arduboy.print(byText);
    arduboy.setCursor(7, 22);
    arduboy.print(githubText);
    arduboy.setCursor(1, 32);
    arduboy.print(projectText);
    arduboy.setCursor(1, 50);
    arduboy.print(continueText);
}

// Main game loop
void UpdatePlay() {
    // Player movement
    if (arduboy.pressed(LEFT_BUTTON)) {
        player.x -= PLAYER_SPEED;
    }
    if (arduboy.pressed(RIGHT_BUTTON)) {
        player.x += PLAYER_SPEED;
    }
    if (arduboy.pressed(UP_BUTTON)) {
        player.y -= PLAYER_SPEED;
    }
    if (arduboy.pressed(DOWN_BUTTON)) {
        player.y += PLAYER_SPEED;
    }

    // Keep player on screen
    if (player.x < 0) {
        player.x = 0;
    }
    if (player.x > SCREEN_W - PLAYER_W) {
        player.x = SCREEN_W - PLAYER_W;
    }
    if (player.y < 0) {
        player.y = 0;
    }
    if (player.y > SCREEN_H - PLAYER_H) {
        player.y = SCREEN_H - PLAYER_H;
    }

    // Score tracks seconds survived
    frameCount++;
    score = frameCount / 60;

    // Spawn new asteroid every SPAWN_PERIOD frames
    if (++spawnTimer >= SPAWN_PERIOD) {
        spawnTimer = 0;
        SpawnAsteroid();
    }

    // Decrease invincibility timer if active
    if (invincTimer > 0) {
        invincTimer--;
    }

    // Move asteroids and check for collisions
    for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
        // Skip inactive asteroids
        if (!asteroids[i].active) {
            continue;
        }

        // Move asteroid
        asteroids[i].x += asteroids[i].vx;

        // Deactivate if off screen
        if (asteroids[i].x > SCREEN_W) {
            asteroids[i].active = false;
            continue;
        }

        // Check collision with player if not invincible
        if (invincTimer == 0 && PlayerHits(asteroids[i])) {
            asteroids[i].active = false;
            lives--;
            // Handle death
            if (lives == 0) {
                if (score > highScore) {
                    highScore = score;
                    // Save new high score to EEPROM
                    EEPROM.put(EEPROM_HIGHSCORE_ADDR, highScore);
                }
                // Play death tones
                sound.tones(deathTones);
                deathTimer = 90;
            } else {
                // Play hit tones
                sound.tones(hitTones);
                deathTimer = 30;
            }
            state = STATE_DEAD;
            return;
        }
    }
}

// Draws player, asteroids, score, and lives
void DrawPlay() {
    // Draw ship, blinking every 4 frames while invincible
    if (invincTimer == 0 || (invincTimer % 8) < 4) {
        arduboy.drawTriangle(player.x + PLAYER_W - 1, player.y,
                             player.x + PLAYER_W - 1, player.y + PLAYER_H - 1,
                             player.x, player.y + (PLAYER_H / 2), WHITE);
    }

    // Draw asteroids
    for (uint8_t i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) {
            continue;
        }
        arduboy.fillRect(asteroids[i].x, asteroids[i].y, ASTEROID_W, ASTEROID_H,
                         WHITE);
    }

    // Draw score
    arduboy.setCursor(0, 0);
    arduboy.print(score);

    // Draw life bars
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t barX = SCREEN_W - 9 - i * 9;
        if (i < lives) {
            arduboy.fillRect(barX, 1, 7, 4, WHITE);
        } else {
            arduboy.drawRect(barX, 1, 7, 4, WHITE);
        }
    }
}

// Handles death timer and transitions to game over or respawn
void UpdateDead() {
    if (deathTimer > 0) {
        deathTimer--;
    }

    if (deathTimer == 0) {
        if (lives == 0) {
            state = STATE_GAMEOVER;
        } else {
            // Respawn with 1 second of invincibility
            invincTimer = 60;
            state = STATE_PLAY;
        }
    }
}

//Draws hit and death flashes over play screen
void DrawDead() {
    if (lives == 0) {
        // Death flashes
        if ((deathTimer % 8) < 4) {
            arduboy.fillScreen(WHITE);
        } else {
            DrawPlay();
        }
    } else {
        // Respawn flashes
        if ((deathTimer % 10) < 5) {
            arduboy.fillScreen(WHITE);
        } else {
            DrawPlay();
        }
    }
}

// handles input on game over screen to return to title
void UpdateGameOver() {
    if (arduboy.justPressed(A_BUTTON | B_BUTTON | UP_BUTTON | DOWN_BUTTON |
                            LEFT_BUTTON | RIGHT_BUTTON)) {
        sound.tone(1000, 40);
        state = STATE_TITLE;
    }
}

// Draws game over screen with final score and prompt to return to title
void DrawGameOver() {
    arduboy.setCursor(37, 10);
    arduboy.print(gameOverText);
    arduboy.setCursor(46, 26);
    arduboy.print(scoreLabel);
    uint8_t digits;
    if (score < 10) {
        digits = 1;
    } else if (score < 100) {
        digits = 2;
    } else {
        digits = 3;
    }
    arduboy.setCursor((128 - digits * 6) / 2, 36);
    arduboy.print(score);
    arduboy.setCursor(1, 52);
    arduboy.print(continueText);
}

// Initializes Arduboy, loads high score from EEPROM, and sets up title screen
void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);

    // Uses a magic byte to check if EEPROM is initialized, if not sets high score to 0
    uint8_t magic;
    EEPROM.get(EEPROM_MAGIC_ADDR, magic);
    if (magic != EEPROM_MAGIC) {
        highScore = 0;
        EEPROM.put(EEPROM_HIGHSCORE_ADDR, highScore);
        EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    } else {
        EEPROM.get(EEPROM_HIGHSCORE_ADDR, highScore);
    }
}

// Main loop
void loop() {
    // Wait till ready for next frame
    if (!arduboy.nextFrame()) {
        return;
    }
    arduboy.pollButtons();
    arduboy.clear();

    // Update and draw based on current game state
    switch (state) {
    case STATE_TITLE:
        UpdateTitle();
        DrawTitle();
        break;
    case STATE_ABOUT:
        UpdateAbout();
        DrawAbout();
        break;
    case STATE_PLAY:
        UpdatePlay();
        DrawPlay();
        break;
    case STATE_DEAD:
        UpdateDead();
        DrawDead();
        break;
    case STATE_GAMEOVER:
        UpdateGameOver();
        DrawGameOver();
        break;
    }

    // Display the frame
    arduboy.display();
}
