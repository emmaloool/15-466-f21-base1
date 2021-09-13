#include "ShrimpMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/string_cast.hpp>

//for loading/saving PNGs, reading/writing chunk info
#include "load_save_png.hpp"

#include <random>
#include <assert.h>


PPU466::Palette get_palette(glm::uvec2 size, std::vector< glm::u8vec4 > data) {    
    // Build dummy palette to grab four colors
    PPU466::Palette palette = {glm::u8vec4(0), glm::u8vec4(0), glm::u8vec4(0), glm::u8vec4(0)};
    
    glm::u8vec4 temp_color;
    int32_t seen_color_inds = 1;    // assume the first color is transparent, so we read only up to 3 colors
    int32_t i = 0;
    while ((seen_color_inds < 4) && (i < size.x * size.y)) {
        temp_color = data[i];

        // Check colors seen so far, setting colors if they're new
        if (temp_color == palette[0] || temp_color == palette[1] 
         || temp_color == palette[2] || temp_color == palette[3]) {
            i++;
            continue;
        }
        if (temp_color != palette[seen_color_inds]) {
            palette[seen_color_inds] = temp_color;
            seen_color_inds++;
        }
        i++;
    }

    return palette;
}


PPU466::Tile set_tilebits(int32_t tile_row, int32_t tile_col,
                     PPU466::Palette palette, 
                     glm::uvec2 size, std::vector< glm::u8vec4 > &data) {
    // Zero-fill tile
    PPU466::Tile tile;
    tile.bit0.fill(0);
    tile.bit1.fill(0);

    // Fill pixel in the canonical row-major order (lower-left -> bottom-right) 
    glm::u8vec4 pix_color;
    for (int32_t pix_y = 0; pix_y < 8; pix_y++) {
        for (int32_t pix_x = 0; pix_x < 8; pix_x++) {
            pix_color = data[((tile_row + pix_y) * size.x) + (tile_col + pix_x)];
            // Set color bit according to palette
            for (int8_t pal_ind = 0; pal_ind < 4; pal_ind++) {
                if (pix_color == palette[pal_ind]) {
                    // If palette color at IND matches, set corresponding high/low bits
                    // in the tile, at the column index pix_x
                    tile.bit0[pix_y] |=  ((pal_ind & 0x1) << pix_x);
                    tile.bit1[pix_y] |= (((pal_ind & 0x2) << pix_x) >> 1); // need to shift higher bit back down before setting
                }
            }
        }
    }    
    return tile;
}


void ShrimpMode::set_sprite_tiles(glm::uvec2 sprite_size, 
                      std::vector< glm::u8vec4 > &sprite_data,
                      PPU466::Palette &sprite_palette,
                      uint8_t &tile_ind) {
    //Sprites should be of size 16x16 pixels => 2x2 tiles
    assert(((sprite_size.x / 8) == sprite_tile_dim) && ((sprite_size.y / 8) == sprite_tile_dim));

    // Set tile bits for the 2x2 tiles of the sprite:
    // Break up 16x16 image into 4 8x8 tiles
    for (int32_t tile_y = 0; tile_y < sprite_tile_dim; tile_y++) {
        for (int32_t tile_x = 0; tile_x < sprite_tile_dim; tile_x++) {
            // Tiles start offset 8
            int32_t tile_row_start = tile_y * 8;    
            int32_t tile_col_start = tile_x * 8;

            // Set color bits for tile directly
            PPU466::Tile result = 
                    set_tilebits(tile_row_start, tile_col_start, sprite_palette, sprite_size, sprite_data);

            ppu.tile_table[tile_ind].bit0 = result.bit0;
            ppu.tile_table[tile_ind].bit1 = result.bit1;
            tile_ind++;
        }
    }
}


ShrimpMode::ShrimpMode() {
    // -------------------------- PPU Housekeeping --------------------------

    // Clear the PPU palette table?
    for (auto &palette : ppu.palette_table) {
        palette[0] = glm::u8vec4(0);
		palette[1] = glm::u8vec4(0);
		palette[2] = glm::u8vec4(0);
		palette[3] = glm::u8vec4(0);
    }

    // Also clear the tile table bits?
    for (auto &tile : ppu.tile_table) {
        tile.bit0 = {
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
        };

        tile.bit1 = {
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
            0b00000000,
        };
    }

    // As we make sprites, helps to track which tiles/palettes are occupied
    uint8_t palette_ind = 4;
    uint8_t tile_ind = 0;

    // -------------------------- Load PNGs -------------------------- 
    // The following asset pipeline routine to convert a PNG into a sprite
    // (steps include creating a color palette and setting tiles) is loosely 
    // inspired by https://github.com/riyuki15/15-466-f20-base1/blob/master/PlayMode.cpp

    auto configure_sprite = [this](const char* filename, 
                                    SpriteType type, bool consumed,
                                    uint8_t &palette_ind, uint8_t &tile_ind, uint8_t sprite_ind,
                                    uint8_t x, uint8_t y) {
        // Load sprite
        glm::uvec2 size;
        std::vector< glm::u8vec4 > data;
        load_png(filename, &size, &data, LowerLeftOrigin);

        // Create metadata tracking sprite
        sprite_info.emplace_back(SpriteInfo());
        sprite_info.back().type = type;
        sprite_info.back().consumed = consumed;
        sprite_info.back().palette_index = palette_ind;
        sprite_info.back().start_tile_index = tile_ind;

        // Get sprite palette
        ppu.palette_table[palette_ind] = get_palette(size, data);

        // Set sprite tiles
        set_sprite_tiles(size, data, ppu.palette_table[palette_ind], tile_ind);

        // Set sprite position
        ppu.sprites[sprite_ind].x = x;
        ppu.sprites[sprite_ind].y = y;
        
        // Only one palette is used per sprite for this game
        palette_ind++;
    };
    configure_sprite("images/flamingo.png", Flamingo, false, palette_ind, tile_ind, 0, 0, 0);

    // // Load player 
    // glm::uvec2 player_size;
    // std::vector< glm::u8vec4 > player_data;
    // load_png("images/flamingo.png", &player_size, &player_data, LowerLeftOrigin);

    // // Create metadata tracking sprite
    // sprite_info.emplace_back(SpriteInfo());
    // sprite_info.back().type = Flamingo;
    // sprite_info.back().palette_index = palette_ind;
    // sprite_info.back().start_tile_index = tile_ind;

    // // Get sprite palette
    // ppu.palette_table[palette_ind] = get_palette(player_size, player_data);

    // // Set sprite tiles
    // set_sprite_tiles(player_size, player_data, ppu.palette_table[palette_ind], tile_ind);

    // // Only one palette created for flamingo 
    // palette_ind++;


    std::cout << "Flamingo: tile start = " << unsigned(sprite_info.back().start_tile_index ) 
                << ", palette index = " << unsigned(sprite_info.back().palette_index) << std::endl;
}

ShrimpMode::~ShrimpMode() {
}

bool ShrimpMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_DOWN) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void ShrimpMode::update(float elapsed) {

	constexpr float PlayerSpeed = 30.0f;
	if (left.pressed) player_at.x -= PlayerSpeed * elapsed;
	if (right.pressed) player_at.x += PlayerSpeed * elapsed;
	if (down.pressed) player_at.y -= PlayerSpeed * elapsed;
	if (up.pressed) player_at.y += PlayerSpeed * elapsed;

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

    
}

void ShrimpMode::draw(glm::uvec2 const &drawable_size) {
    // background: a little baby flamingo pink
    ppu.background_color = glm::u8vec4( 0xfc, 0xc5, 0xfa, 0xff );

	//--- set ppu state based on game state ---
    SpriteInfo flamingo_info = sprite_info[0];

    int32_t sprite_i, row_offset, col_offset;
    for (int32_t r = 0; r < sprite_tile_dim; r++) {
        for (int32_t c = 0; c < sprite_tile_dim; c++) {
            sprite_i = (r * sprite_tile_dim) + c;
            row_offset = r * 8; // offset for y
            col_offset = c * 8; // offset for x
            ppu.sprites[sprite_i].x = int32_t(player_at.x) + col_offset;
            ppu.sprites[sprite_i].y = int32_t(player_at.y) + row_offset;
            ppu.sprites[sprite_i].index = flamingo_info.start_tile_index + sprite_i;
            ppu.sprites[sprite_i].attributes = flamingo_info.palette_index;
        }
    }

	//--- actually draw ---
	ppu.draw(drawable_size);
}
