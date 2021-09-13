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

    // Also clear the background??
    for (uint32_t bg_ind = 0; bg_ind < PPU466::BackgroundWidth * PPU466::BackgroundHeight; bg_ind++) {
        // tile table index = 255 (the last tile), palette index = 0 (the first palette)
        ppu.background[bg_ind] = 255; 
    }

    // -------------------------- Load PNGs -------------------------- 
    // As we make sprites, helps to track which tiles/palettes are occupied
    uint8_t palette_ind = 1;
    uint8_t tile_ind = 0;
    uint8_t sprite_ind = 0;

    // The following asset pipeline routine and its subroutines to convert a PNG into a sprite
    // (steps include creating a color palette and setting tiles) is loosely 
    // inspired by https://github.com/riyuki15/15-466-f20-base1/blob/master/PlayMode.cpp
    auto configure_sprite = [this](const char* filename, 
                                    SpriteType type, bool consumed,
                                    uint8_t palette_ind, uint8_t &tile_ind, uint8_t &sprite_ind,
                                    uint8_t x, uint8_t y) {
        // Load sprite
        glm::uvec2 size;
        std::vector< glm::u8vec4 > data;
        load_png(filename, &size, &data, LowerLeftOrigin);

        // Create metadata tracking sprite
        sprite_infos.emplace_back(SpriteInfo());
        sprite_infos.back().type = type;
        sprite_infos.back().consumed = consumed;
        sprite_infos.back().palette_index = palette_ind;
        sprite_infos.back().start_tile_index = tile_ind;
        sprite_infos.back().sprite_index = sprite_ind;

        // Get sprite palette
        ppu.palette_table[palette_ind] = get_palette(size, data);

        // Set sprite tiles
        set_sprite_tiles(size, data, ppu.palette_table[palette_ind], tile_ind);

        // Initialize sprite attributes
        // Most sprites will stay static in one location
        ppu.sprites[sprite_ind].x = x;
        ppu.sprites[sprite_ind].y = y;
        ppu.sprites[sprite_ind].index = sprite_infos.back().start_tile_index;
        ppu.sprites[sprite_ind].attributes = palette_ind;

        // 4 smaller sprites make up our sprite
        sprite_ind += sprites_per_sprite;
    };

    // Sets x,y,tile,palette information for a newly added sprite's subtiles given the starting x,y of the sprite
    auto fix_sprite_tiles = [this](uint8_t start_x, uint8_t start_y) {
        for (int32_t r = 0; r < sprite_tile_dim; r++) {
            for (int32_t c = 0; c < sprite_tile_dim; c++) {
                uint32_t sprite_i = sprite_infos.back().sprite_index + (r * sprite_tile_dim) + c;
                uint8_t row_offset = r * 8; // offset for y
                uint8_t col_offset = c * 8; // offset for x
                ppu.sprites[sprite_i].x = start_x + col_offset;
                ppu.sprites[sprite_i].y = start_y + row_offset;
                ppu.sprites[sprite_i].index = sprite_infos.back().start_tile_index + (r * sprite_tile_dim) + c;
                ppu.sprites[sprite_i].attributes = sprite_infos.back().palette_index;
            }
        }
    };

    // --------- Create flamingo (player sprite)
    configure_sprite("images/flamingo_most_pink.png", Flamingo, false, palette_ind, tile_ind, sprite_ind, 0, 0);
    // TODO: import other images?
    for (auto i = 0; i < 4; i++) {
        std::cout << "Most pink palette" << std::endl;
        std::cout << glm::to_string(ppu.palette_table[palette_ind][i]) << std::endl;
    }


    // Only one palette is used per type of sprite for this game
    palette_ind++;

    // a complicated way of generating shrimp "evenly" between 4 quadrants in the image
    std::vector<uint8_t>coordinates;
    {
        // Shrimp
        coordinates.push_back(16);
        coordinates.push_back(10);

        coordinates.push_back(200);
        coordinates.push_back(73);

        coordinates.push_back(100);
        coordinates.push_back(223);

        coordinates.push_back(143);
        coordinates.push_back(166);

        coordinates.push_back(73);
        coordinates.push_back(100);

        coordinates.push_back(156);
        coordinates.push_back(100);

        coordinates.push_back(34);
        coordinates.push_back(140);

        coordinates.push_back(175);
        coordinates.push_back(30);

     
        // plants
        coordinates.push_back(120);
        coordinates.push_back(60);

        coordinates.push_back(23);
        coordinates.push_back(178);

        coordinates.push_back(223);
        coordinates.push_back(145);

        coordinates.push_back(45);
        coordinates.push_back(45);

        coordinates.push_back(120);
        coordinates.push_back(120);

        // Potion
        coordinates.push_back(234);
        coordinates.push_back(220);
    }
    uint8_t sprite_ct = 0;

    // --------- Create shrimp
    for (uint8_t shrimp_ct = sprite_ct; shrimp_ct < 8; shrimp_ct++) {
        uint8_t shrimp_x = coordinates[2 * shrimp_ct];
        uint8_t shrimp_y = coordinates[(2 * shrimp_ct) + 1];

        std::string shrimp_png;
        uint8_t shrimp_result = shrimp_ct / 2;
        if (shrimp_result == 0)      shrimp_png = "images/shrimp_top.png";
        else if (shrimp_result == 1) shrimp_png = "images/shrimp_bottom.png";
        else if (shrimp_result == 2) shrimp_png = "images/shrimp_left.png";
        else                         shrimp_png = "images/shrimp_right.png";
    
        configure_sprite(shrimp_png.c_str(), Shrimp, false, palette_ind, tile_ind, sprite_ind, shrimp_x, shrimp_y);
        fix_sprite_tiles(shrimp_x, shrimp_y);

    }
    sprite_ct += 8;
    palette_ind++;

    // --------- Create plants
    for (uint8_t plant_ct = sprite_ct; plant_ct < (sprite_ct + 5); plant_ct++) {
        uint8_t plant_x = coordinates[2 * plant_ct];
        uint8_t plant_y = coordinates[(2 * plant_ct) + 1];
    
        configure_sprite("images/plant.png", Plant, false, palette_ind, tile_ind, sprite_ind, plant_x, plant_y);
        fix_sprite_tiles(plant_x, plant_y);
    }
    sprite_ct += 5;
    palette_ind++;

    // --------- Create medicine
    uint8_t med_x = coordinates[2 * sprite_ct];
    uint8_t med_y = coordinates[(2 * sprite_ct) + 1];
    configure_sprite("images/pepto.png", Medicine, false, palette_ind, tile_ind, sprite_ind, med_x, med_y);
    fix_sprite_tiles(med_x, med_y);
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

	constexpr float PlayerSpeed = 50.0f;
	if (left.pressed) player_at.x -= PlayerSpeed * elapsed;
	if (right.pressed) player_at.x += PlayerSpeed * elapsed;
	if (down.pressed) player_at.y -= PlayerSpeed * elapsed;
	if (up.pressed) player_at.y += PlayerSpeed * elapsed;

    // Update position to clip within screen boundaries
    if (player_at.x < 0) player_at.x = 0;
    if (player_at.y < 0) player_at.y = 0;
    if ((player_at.x + 16) >= PPU466::ScreenWidth)  player_at.x = PPU466::ScreenWidth - 16;
    if ((player_at.y + 16) >= PPU466::ScreenHeight) player_at.y = PPU466::ScreenHeight - 16;

    // Handle collisions with scene
    for (uint8_t i = other_sprites_start; i < sprite_infos.size(); i++) {
        SpriteInfo &sinfo = sprite_infos[i];
        std::string type;
        if (sinfo.type == Shrimp) type = "Shrimp";
        else if (sinfo.type == Plant) type = "Plant";
        else if (sinfo.type == Medicine) type = "Medicine";
        PPU466::Sprite sprite = ppu.sprites[sinfo.sprite_index];

        glm::vec2 min = glm::max(glm::vec2(sprite.x, sprite.y), glm::vec2(player_at.x, player_at.y));
        glm::vec2 max = glm::min(glm::vec2(sprite.x, sprite.y) + 16.f, glm::vec2(player_at.x, player_at.y) + 16.f);
		if (min.x > max.x || min.y > max.y) continue; //if no overlap, no collision:

        // std::cout << "Handling collision with sprite " << unsigned(i) << ", " 
        //     <<  type << ", start (x,y) = " << unsigned(sprite.x) << "," << unsigned(sprite.y) << std::endl;

        // Handle collision based on type
        if (sinfo.type == Shrimp) {
            if (sinfo.consumed) break;      // Do nothing if consumed
            score++;
            sinfo.consumed = true;
        } 
        else if (sinfo.type == Plant) {     // Don't walk over plants, just undo step
            if (left.pressed)       player_at.x += PlayerSpeed * elapsed;
            else if (right.pressed) player_at.x -= PlayerSpeed * elapsed;
            else if (down.pressed)  player_at.y += PlayerSpeed * elapsed;
            else if (up.pressed)    player_at.y -= PlayerSpeed * elapsed;
        }
        else if (sinfo.type == Medicine) {
            for (uint8_t s = other_sprites_start; s < plant_start; s++) {
                SpriteInfo &shrimp_info = sprite_infos[s];
                shrimp_info.consumed = false;
            }
            score = 0;
        }  
    }
    //reset button press counters:
    left.downs = 0;
    right.downs = 0;
    up.downs = 0;
    down.downs = 0;
}

void ShrimpMode::draw(glm::uvec2 const &drawable_size) {

    // background: a little baby flamingo pink
    ppu.background_color = glm::u8vec4( 0xAD, 0xD8, 0xE6, 0xff );

	//--- set ppu state based on game state ---



    // Draw directly with the x,y saved into sprite
    // Remaining sprites
    uint32_t sprite_i, row_offset, col_offset, big_sprite_i;
    for (big_sprite_i = 1; big_sprite_i < sprite_infos.size(); big_sprite_i++) {
        SpriteInfo sprite_info = sprite_infos[big_sprite_i];
        for (int32_t r = 0; r < sprite_tile_dim; r++) {
            for (int32_t c = 0; c < sprite_tile_dim; c++) {
                // "Hide" sprite if consumed, by changing its color palette
                sprite_i = sprite_info.sprite_index + (r * sprite_tile_dim) + c;
                if (sprite_info.consumed) ppu.sprites[sprite_i].attributes = 0;
                else                      ppu.sprites[sprite_i].attributes = sprite_info.palette_index;
            }
        }

    }

    // ----- Flamingo (rendered last so it'll draw on top)
    SpriteInfo flamingo_info = sprite_infos[0];

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
