// Include-only body; .h suffix avoids separate compilation in the sketch.
// The shared compatibility layer supplies Arduboy2/Playtune replacements and
// the program-memory macros used below.

const unsigned char PROGMEM sprites[] =
{
// hidden
0xff, 0x55, 0xab, 0x55, 0xab, 0x55, 0xab, 0x55, 
0xab, 0x55, 0xab, 0x55, 0xab, 0x55, 0xab, 0x00, 
0x7f, 0x55, 0x2a, 0x55, 0x2a, 0x55, 0x2a, 0x55, 
0x2a, 0x55, 0x2a, 0x55, 0x2a, 0x55, 0x2a, 0x00, 
// revealed
0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 
0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 
0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
// mine
0x00, 0xfe, 0x7e, 0x66, 0x06, 0x0e, 0x0e, 0x02, 
0x02, 0x0e, 0x0e, 0x06, 0x66, 0x7e, 0xfe, 0xfe, 
0x00, 0xff, 0xfe, 0xe6, 0xe0, 0xf0, 0xf0, 0xc0, 
0xc0, 0xf0, 0xf0, 0xe0, 0xe6, 0xfe, 0xff, 0xff, 
// flag
0xff, 0x55, 0xab, 0x55, 0xff, 0x05, 0x07, 0x05, 
0x07, 0x05, 0x07, 0x05, 0xff, 0x55, 0xab, 0x00, 
0x7f, 0x55, 0x2a, 0x55, 0x3f, 0x60, 0x3f, 0x55, 
0x2b, 0x55, 0x2b, 0x55, 0x2b, 0x55, 0x2a, 0x00,
};

const byte winScore[] PROGMEM = {
  0x90, 60, 
  0, 255, 
  0x90, 64, 
  0, 255, 
  0x90, 67, 
  0, 255, 
  0x80, 
  0xF0
};

const byte loseScore[] PROGMEM = {
  0x90, 57, 
  0, 255, 
  0x90, 53, 
  0, 255, 
  0x90, 50, 
  0, 255, 
  0x80, 
  0xF0
};

struct Square {
  bool isMine: 1;
  bool isRevealed: 1;
  bool isFlagged: 1;

  uint8_t neighbouringMines: 4;
};

static_assert(sizeof(Square) == 1, "Square should fit inside a byte.");

enum class GameState: uint8_t {
  Playing,
  Winning,
  Losing
};

struct CommonState {
    Arduboy2 arduboy;
    ArduboyPlaytune tunes;
    GameState gameState = GameState::Playing;
    Square grid[9][9] = {};
    int offsetX = 0;
    int offsetY = 0;
    int markerX = 0;
    int markerY = 0;
    bool firstClick = true;
    uint16_t pauseEnded = 0;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define tunes (g_common->tunes)
#define gameState (g_common->gameState)
#define grid (g_common->grid)
#define offsetX (g_common->offsetX)
#define offsetY (g_common->offsetY)
#define markerX (g_common->markerX)
#define markerY (g_common->markerY)
#define firstClick (g_common->firstClick)
#define pauseEnded (g_common->pauseEnded)

void setup() {
  arduboy.begin();

  tunes.initChannel(PIN_SPEAKER_1);
  tunes.initChannel(PIN_SPEAKER_2);
  
  arduboy.clear();
}

const unsigned char* getSprite(int x, int y) {
  if (grid[x][y].isFlagged) {
    return sprites + 32 * 3;
  } else if (!grid[x][y].isRevealed) {
    return sprites + 32 * 0;
  } else if (grid[x][y].isMine) {
    return sprites + 32 * 2;
  }
  return sprites + 32 * 1;
}

void resetGame() {
  gameState = GameState::Playing;
  memset(grid, 0, sizeof(grid));
  firstClick = true;
  
  offsetX = 0;
  offsetY = 0;
  markerX = 0;
  markerY = 0;

  pauseEnded = arduboy.frameCount + 2;
}

void moveMarker() {
  if(arduboy.justPressed(LEFT_BUTTON))  markerX--;
  if(arduboy.justPressed(RIGHT_BUTTON)) markerX++;
  if(arduboy.justPressed(UP_BUTTON))    markerY--;
  if(arduboy.justPressed(DOWN_BUTTON))  markerY++;

  // clamp cursor to grid
  if (markerX < 0)  markerX = 0;
  if (markerY < 0)  markerY = 0;
  if (markerX >= 9) markerX = 8;
  if (markerY >= 9) markerY = 8;
}

bool isOutside(int x, int y) {
  return x < 0 || y < 0 || x >= 9 || y>= 9;
}

void revealNeighbours(int x, int y) {
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      int x2 = x + i;
      int y2 = y + j;
      
      if (i == 0 && j == 0 || isOutside(x2, y2) || grid[x2][y2].isMine || grid[x2][y2].isFlagged) continue;

      if (!grid[x2][y2].isRevealed) {
        grid[x2][y2].isRevealed = true;

        if (grid[x2][y2].neighbouringMines == 0) {
          revealNeighbours(x2, y2);
        }
      }
    }
  }
}

void addMineCountAround(int x, int y) {
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      int x2 = x + i;
      int y2 = y + j;
      
      if (i == 0 && j == 0 || isOutside(x2, y2)) continue;

      grid[x2][y2].neighbouringMines++;
    }
  }
}

void spawnMines() {
  int mines = 0;

  while (mines < 10) {
    int x = random(0, 9);
    int y = random(0, 9);
    
    if ((x == markerX && y == markerY) || grid[x][y].isMine) {
      continue;
    }

    grid[x][y].isMine = true;
    mines++;

    addMineCountAround(x, y);
  }
}

int countHidden() {
  int count = 0;
  for (int x = 0; x < 9; x++) {
    for (int y = 0; y < 9; y++) {
      if (!grid[x][y].isRevealed) {
        count++;
      }
    }
  }
  return count;
}

void clickA() {
  if (firstClick) {
    firstClick = false;
    arduboy.initRandomSeed();
    spawnMines(); 
  }

  if (!grid[markerX][markerY].isRevealed && !grid[markerX][markerY].isFlagged) {
    grid[markerX][markerY].isRevealed = true;

    if (grid[markerX][markerY].isMine) {
      gameState = GameState::Losing;
      pauseEnded = arduboy.frameCount + 60 * 2;
      tunes.playScore(loseScore);
      return;
    }
    
    if (grid[markerX][markerY].neighbouringMines == 0) {
      revealNeighbours(markerX, markerY);
    }

    if (countHidden() == 10) {
      gameState = GameState::Winning;
      pauseEnded = arduboy.frameCount + 60 * 2;
      tunes.playScore(winScore);
      return;
    }
  }
}

void clickB() {
  if (!grid[markerX][markerY].isRevealed) grid[markerX][markerY].isFlagged = !grid[markerX][markerY].isFlagged;
}

void gameLoop() {
  moveMarker();

  if(arduboy.justReleased(A_BUTTON)) {
    clickA();
  }
  else if(arduboy.justReleased(B_BUTTON)) {
    clickB();
  }

  arduboy.clear();

  arduboy.setTextColor(BLACK);
  arduboy.setTextBackground(WHITE);

  if (offsetX + markerX*16 + 15 >= 128) offsetX--;
  if (offsetX + markerX*16 < 0) offsetX++;
  if (offsetY + markerY*16 + 15 >= 64) offsetY--;
  if (offsetY + markerY*16 < 0) offsetY++;

  for (int x = 0; x < 9; x++) {
    for (int y = 0; y < 9; y++) {
      int pixelX = offsetX + x*16;
      int pixelY = offsetY + y*16;
      
      if (pixelX + 16 < 0 || pixelX >= 128 || pixelY + 16 < 0 || pixelY >= 64) {
        continue;
      }
      
      int spriteNo = (x + y) % 4;
      arduboy.drawBitmap(pixelX, pixelY, getSprite(x,y), 16, 16, WHITE);

      if (grid[x][y].isRevealed && !grid[x][y].isMine && grid[x][y].neighbouringMines > 0) {
        arduboy.setCursor(pixelX + 6, pixelY + 5);
        arduboy.print(grid[x][y].neighbouringMines);
      }
    }
  }

  arduboy.drawRect(offsetX + markerX*16, offsetY + markerY*16, 16, 16, WHITE);
  arduboy.drawRect(offsetX + (markerX*16) + 1, offsetY + (markerY*16) + 1, 14, 14, BLACK);
  
  arduboy.display();
}

void winningLoop() {
  if(arduboy.justReleased(A_BUTTON)) resetGame();
  
  arduboy.clear();

  arduboy.setTextColor(WHITE);
  arduboy.setTextBackground(BLACK);

  arduboy.setCursor(10, 10);

  arduboy.print("You won!");
  
  arduboy.display();
}

void losingLoop() {
  if(arduboy.justReleased(A_BUTTON)) resetGame();
  
  arduboy.clear();

  arduboy.setTextColor(WHITE);
  arduboy.setTextBackground(BLACK);

  arduboy.setCursor(10, 10);

  arduboy.print("You lost!");
  
  arduboy.display();
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();

  if (arduboy.frameCount < pauseEnded) {
    return;
  }
  
  if (gameState == GameState::Playing) {
    gameLoop();
  } else if (gameState == GameState::Winning) {
    winningLoop();
  } else if (gameState == GameState::Losing) {
    losingLoop();
  }

}
