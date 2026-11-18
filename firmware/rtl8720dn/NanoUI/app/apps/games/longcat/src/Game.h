#pragma once


#include "States/GameState.h"

#include "GameContext/GameContext.h"

#include "GameMode/GameMode.h"

#include "States.h"

#include "Utils/SaveUtil.h"

class Game
{
public:
  void setup();
  void loop();

private:
  GameMode gameMode{GameMode::Fixed};
  GameState gameState{GameState::SplashScreen};
  GameContext gameContext;

private:
  Arduboy2 arduboy;
  uint8_t lastButtons = 0;
  uint8_t edgeButtons = 0;
  GameStateSplashScreen splashScreenState;
  GameStateMainMenu mainMenuState;
  GameStateSelectLevel selectLevelState;
  GameStateSelectSeed selectSeedState;
  GameStateLoadLevel loadLevelState;
  GameStateGamePlay gamePlayState;
  GameStateSimulate simulateState;
  GameStateCampaignMenu campaignMenuState;
  GameStateRandomMenu randomMenuState;
  GameStateCredits creditsState;
  GameStateOptionsMenu optionsMenuState;
  GameStateEndScreen endScreenState;

public:
  void clickSound()
  {
    ArduboyTones::tone(100, 20);
  }

  void setGameMode(GameMode gameMode)
  {
    this->gameMode = gameMode;
  }

  GameMode getGameMode() const
  {
    return this->gameMode;
  }

  void setGameState(GameState gameState)
  {
    this->gameState = gameState;
  }

  GameState getGameState() const
  {
    return this->gameState;
  }

  GameContext &getGameContext()
  {
    return this->gameContext;
  }

  const GameContext &getGameContext() const
  {
    return this->gameContext;
  }

  Arduboy2 &getArduboy()
  {
    return this->arduboy;
  }

  const Arduboy2 &getArduboy() const
  {
    return this->arduboy;
  }

  // One action per physical PAT tap. The BW16 adapter stretches a tap pulse
  // over a few frames so pressed()/justPressed() can both see it; upstream's
  // AVR build saw a one-frame justPressed and would not repeat menu actions.
  bool justPressed(uint8_t mask) const
  {
    return (this->edgeButtons & mask) != 0;
  }

  // Re-arms every transient object on app entry/unmount. The private RAM
  // EEPROM save (lastStage/randomDifficulty) is intentionally not touched.
  void resetState()
  {
    this->gameMode = GameMode::Fixed;
    this->gameState = GameState::SplashScreen;
    this->gameContext = GameContext();
    this->splashScreenState = GameStateSplashScreen();
    this->mainMenuState = GameStateMainMenu();
    this->selectLevelState = GameStateSelectLevel();
    this->selectSeedState = GameStateSelectSeed();
    this->loadLevelState = GameStateLoadLevel();
    this->gamePlayState = GameStateGamePlay();
    this->simulateState = GameStateSimulate();
    this->campaignMenuState = GameStateCampaignMenu();
    this->randomMenuState = GameStateRandomMenu();
    this->creditsState = GameStateCredits();
    this->optionsMenuState = GameStateOptionsMenu();
    this->endScreenState = GameStateEndScreen();
    this->lastButtons = 0;
    this->edgeButtons = 0;
  }
};
