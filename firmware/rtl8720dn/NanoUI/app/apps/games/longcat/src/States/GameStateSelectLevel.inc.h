#include "GameStateSelectLevel.h"

#include "../Game.h"

void GameStateSelectLevel::update(Game &game)
{
	auto &arduboy = game.getArduboy();
	auto &context = game.getGameContext();
	if (this->repeatDelay > 0)
	{
		--this->repeatDelay;
		return;
	}
	if (arduboy.pressed(DOWN_BUTTON))
	{
		if (this->selection > 0)
			this->selection--;
		context.stage = this->selection;
		LevelUtils::copyStaticLevel(game);
		this->repeatDelay = 15;
	}

	else if (arduboy.pressed(UP_BUTTON))
	{
		if (this->selection < maxLevel-1)
			this->selection++;
		context.stage = this->selection;
		LevelUtils::copyStaticLevel(game);
		this->repeatDelay = 15;
	}

	else if (game.justPressed(A_BUTTON))
	{
		
		context.currentSeed = 0;
		context.stage = this->selection;
		this->selection = 0;
		game.setGameMode(GameMode::Linear);
		game.setGameState(GameState::LoadLevel);
	}

	else if (game.justPressed(B_BUTTON))
	{
		this->selection = 0;
		game.setGameState(GameState::CampaignMenu);
	}
}

void GameStateSelectLevel::render(Game &game)
{
	const char *selectStage_s = "Stage:";
	uint8_t length = CatChars::length(selectStage_s);
	CatChars::print(30, 0, false, selectStage_s);
	CatChars::printBCD(length * 8+30, 0, false,this->selection + 1);
	LevelRenderer::renderPreview(5,42,12,game);
	
	//Prev and next:
	CatChars::printBCD(10, 20, false,this->selection);
	LevelRenderer::renderPreview(3,2,32,game);
	CatChars::printBCD(102, 20, false,this->selection + 2);
	LevelRenderer::renderPreview(3,98,32,game);
	//LevelRenderer::renderPlayer(32,8,game);
}