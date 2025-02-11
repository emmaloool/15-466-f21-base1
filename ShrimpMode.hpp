#include "Mode.hpp"
#include "PPU466.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <array>
#include <deque>
#include <algorithm>

struct ShrimpMode : Mode {
	ShrimpMode();
	virtual ~ShrimpMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;


	//player position:
	glm::vec2 player_at = glm::vec2(PPU466::ScreenWidth/2-16.f, 0.0f);

    //shrimp eaten:
    int8_t score = 0;

    //----- helpers? will move later-----
    void set_sprite_tiles(glm::uvec2 sprite_size, 
                      std::vector< glm::u8vec4 > &sprite_data,
                      PPU466::Palette &sprite_palette,
                      uint8_t &tile_ind);


	//----- drawing handled by PPU466 -----

    // There are 4 types of sprites in the game
    enum SpriteType {
        Flamingo = 0,
        Shrimp = 1,
        Plant = 2,
        Medicine = 3
    };

    // Each sprite will consist of 2x2 tiles
    const uint8_t sprite_tile_dim = 2;

    // The hack: each sprite is represented by 2x2=4 smaller NES sprites
    const uint8_t sprites_per_sprite = 4;

    // So it'll make our lives easier to store some information per one of these larger sprites
    struct SpriteInfo {
        SpriteType type;                // what type of sprite is it
        bool consumed = false;          // whether this sprite's been consumed (relevant for Shrimp, Medicine)
        uint8_t palette_index;          // index of sprite's color palette
        uint8_t start_tile_index;       // index into the tiles the sprite starts at
        uint8_t sprite_index;            // index into the sprites the sprite starts at
    };
    std::vector< SpriteInfo > sprite_infos;
    // The player (flamingo) will start at sprite 0

    typedef enum Pink {
        LittlePink = 0,
        SomePink = 1,
        MostPink = 2,
        Sick = 3
    } Pinkness;
    Pinkness how_pink = LittlePink;

    // Organize start indices for each type of sprite
    struct SpriteStarts {
        uint8_t first_palette_ind;
        uint8_t first_tile_ind;
        uint8_t first_sprite_ind;
    };
    SpriteStarts flamingo_start;
    SpriteStarts shrimp_start;
    SpriteStarts plant_start;
    SpriteStarts med_start;

    // const uint8_t other_sprites_start = 1;
    // const uint8_t plant_start = 9;
    // const uint8_t med_start = 14;

	PPU466 ppu;
};
