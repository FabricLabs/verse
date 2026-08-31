#include <SDL2/SDL_main.h>

extern int verse_real_main(void);

int SDL_main(int argc, char* argv[])
{
	(void)argc; (void)argv;
	return verse_real_main();
}
