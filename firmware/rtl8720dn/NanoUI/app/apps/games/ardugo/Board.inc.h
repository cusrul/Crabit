
#include "ArduGo.h"
#include "Board.h"
#include "assets.h"
#include "sidebar.h"

/*
 *  File: Board.cpp
 *  Purpose: Implementation of board data and methods.
 *  Author: Don Anderson
 */

void
Board::loop(){
  if(millis() > cursor_millis + 300){
    cursor_millis = millis();
    cursor_color = cursor_color == WHITE ? BLACK : WHITE;
  }
  execButtons();
} 

void 
Board::execButtons(){
  if (wasJustPressed(UP_BUTTON)) cursorUp();
  if (wasJustPressed(DOWN_BUTTON)) cursorDown();
  if (wasJustPressed(LEFT_BUTTON)) cursorLeft();
  if (wasJustPressed(RIGHT_BUTTON)) cursorRight();
 
  if (wasJustPressed(A_BUTTON)){
    if(!checkValid(cursor_row, cursor_col, BLACK_STONE)){
      tunes.tone(150, 100);
    }
    else{
      tunes.tone(2000, 100);
      move(cursor_row, cursor_col, BLACK_STONE);
      row_col_t next;
      if(nextMove(WHITE_STONE, next)){
        move(next.row, next.col, WHITE_STONE);
        setCursor(next.row, next.col);
      }
      cur_sidebar = STATS_SIDEBAR;
    }
  } 
  
  if (wasJustPressed(B_BUTTON)){
    clear();
    cur_sidebar = TITLE_SIDEBAR;
    black_live = black_capt = 0;
    white_live = white_capt = 0;
  }
  
};

void
Board::render(){

  // Draw Background
  drawBitmap(65, 0, board_background);
  
  // Draw Cursor
  if(cursor_color == BLACK)
    arduboy.fillRect(66+7*cursor_col, 57-7*cursor_row, 6, 6, BLACK);

  // Draw Stones
  for(int r=0; r<size; r++){
    for(int c=0; c<size; c++){
      if(isWhite(r,c)){
          arduboy.drawRect(67+7*c, 58-7*r, 4, 4, BLACK);
      }
      else if(isBlack(r,c)){
          arduboy.fillRect(67+7*c, 58-7*r, 4, 4, BLACK);
      }
    }
  }
}

void 
Board::clear(){
  point_t *p = pointsBeg();
  point_t *e = pointsEnd();
  while(p != e) *p++ = 0;
  cursor_row = cursor_col = 4;
}

// App reentry reset: clear() deliberately keeps the upstream restart behavior,
// while this also clears the blink phase and button history owned by Board.
void
Board::resetRuntime(){
  clear();
  cursor_millis = 0;
  cursor_color = BLACK;
  buttons_prev = 0;
}

void 
Board::clearMeta(point_t exclude_mask){
  point_t *p = pointsBeg();
  point_t *e = pointsEnd();
  while(p != e) *p++ &= (META_MASK | exclude_mask);
}





