# Build the game. On Windows the executable is build/game/frontier.exe.
.PHONY: game intro test check
game:
	python build.py

intro:
	python build.py --intro --output build/intro

test:
	python test.py

check:
	python test.py --integration
