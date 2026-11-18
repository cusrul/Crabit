#pragma once

// Included once by samegame_app.h, after the platform/standard headers.
// Each former translation unit keeps its own private names, while all state
// remains under SameGameSandbox rather than leaking into other game ports.
namespace SameGameSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "MyArduboy.inc.h"
#include "common.inc.h"
#include "menu.inc.h"

namespace Logo {
#include "logo.inc.h"
}
namespace Title {
#include "title.inc.h"
}
namespace Game {
#include "game.inc.h"
}
#undef circulate
namespace Editor {
#include "editor.inc.h"
}

void initLogo() { Logo::initLogo(); }
MODE_T updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
MODE_T updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
MODE_T updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }
void initEditor() { Editor::initEditor(); }
MODE_T updateEditor() { return Editor::updateEditor(); }
void drawEditor() { Editor::drawEditor(); }

#include "samegame.inc.h"

void resetGameState() {
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. RAM EEPROM
    // intentionally survives visits in this engine, but never power loss.
    memset(&record, 0, sizeof(record));
    padX = padY = padRepeatCount = 0;
    lastScore = 0;
    isInvalid = isRecordDirty = false;
    recordState = RECORD_NOT_READ;
    eepAddr = 0;
    menuX = menuY = menuW = menuH = menuItemCount = menuItemPos = 0;
    isFramed = isControlSound = isInvalidMenu = false;
    memset(menuItemAry, 0, sizeof(menuItemAry));
    Logo::counter = 0;
    Logo::signalOn = false;
    Title::state = Title::STATE_INIT;
    Game::state = Game::STATE_PLAYING;
    memset(&Game::field, 0, sizeof(Game::field));
    memset(&Game::lastField, 0, sizeof(Game::lastField));
    memset(Game::linkedFlag, 0, sizeof(Game::linkedFlag));
    Game::displayScore = 0;
    Game::counter = Game::linkedTarget = Game::linkedCount = 0;
    Game::isTouched = Game::isPerfect = Game::isHiscore = Game::isBackable = false;
    Game::lastX = Game::lastY = 0;
    Editor::state = Editor::STATE_SELECTING;
    memset(Editor::backup, 0, sizeof(Editor::backup));
    Editor::counter = Editor::targetObject = Editor::cursorX = Editor::cursorY = Editor::drawColor = 0;
    Editor::lastX = Editor::lastY = 0;
    mode = MODE_LOGO;
}
} // namespace SameGameSandbox

// Do not change macro meanings for later games in the shared sketch TU.
#undef FPS
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef OBJECT_TYPES
#undef IMG_OBJECT_W
#undef IMG_OBJECT_H
#undef PERFECT_BONUS
#undef dprint
#undef dprintln
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef IMG_SOUND_W
#undef IMG_SOUNDM_W
#undef MENU_COUNT_MAX
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef IMG_TITLE_W
#undef IMG_TITLE_H
#undef IMG_SUBTITLE_W
#undef IMG_SUBTITLE_H
#undef ONE_HOUR
#undef FIELD_W
#undef FIELD_H
#undef GRID_SIZE
#undef ERASING_ANIM
#undef RESULT_ANIM
#undef circulate
#undef getField
#undef getLinkedFlag
#undef getOffsetX
#undef IMG_ERASER_W
#undef IMG_ERASER_H
#undef IMG_CURSOR_W
#undef IMG_CURSOR_H
#undef IMG_CURSOR_ID_CLEAR
#undef IMG_OVER_W
#undef IMG_OVER_H
#undef IMG_PERFECT_W
#undef IMG_PERFECT_H
#undef IMG_DIGIT_W
#undef IMG_DIGIT_H
#undef IMG_BUTTONS_W
#undef IMG_BUTTONS_H
#undef NO_COLOR
#undef calcBit
#undef getPattern
#undef setPattern
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
