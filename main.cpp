#include "load_save_png.hpp"
#include "GL.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game1: Make and Escape";
		glm::uvec2 size = glm::uvec2(800, 600);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//texture:
	GLuint tex = 0;
	glm::uvec2 tex_size = glm::uvec2(0,0);

	{ //load texture 'tex':
		std::vector< uint32_t > data;
		if (!load_png("map.png", &tex_size.x, &tex_size.y, &data, LowerLeftOrigin)) {
			std::cerr << "Failed to load texture." << std::endl;
			exit(1);
		}
		//create a texture object:
		glGenTextures(1, &tex);
		//bind texture object to GL_TEXTURE_2D:
		glBindTexture(GL_TEXTURE_2D, tex);
		//upload texture data from data:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_size.x, tex_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);
		//set texture sampling parameters:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_TexCoord = 0;
	GLuint program_Color = 0;
	GLuint program_mvp = 0;
	GLuint program_tex = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"in vec4 Position;\n"
			"in vec2 TexCoord;\n"
			"in vec4 Color;\n"
			"out vec2 texCoord;\n"
			"out vec4 color;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	color = Color;\n"
			"	texCoord = TexCoord;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform sampler2D tex;\n"
			"in vec4 color;\n"
			"in vec2 texCoord;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	fragColor = texture(tex, texCoord) * color;\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_TexCoord = glGetAttribLocation(program, "TexCoord");
		if (program_TexCoord == -1U) throw std::runtime_error("no attribute named TexCoord");
		program_Color = glGetAttribLocation(program, "Color");
		if (program_Color == -1U) throw std::runtime_error("no attribute named Color");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_tex = glGetUniformLocation(program, "tex");
		if (program_tex == -1U) throw std::runtime_error("no uniform named tex");
	}

	//vertex buffer:
	GLuint buffer = 0;
	{ //create vertex buffer
		glGenBuffers(1, &buffer);
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
	}

	struct Vertex {
		Vertex(glm::vec2 const &Position_, glm::vec2 const &TexCoord_, glm::u8vec4 const &Color_) :
			Position(Position_), TexCoord(TexCoord_), Color(Color_) { }
		glm::vec2 Position;
		glm::vec2 TexCoord;
		glm::u8vec4 Color;
	};
	static_assert(sizeof(Vertex) == 20, "Vertex is nicely packed.");

	//vertex array object:
	GLuint vao = 0;
	{ //create vao and set up binding:
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glVertexAttribPointer(program_Position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0);
		glVertexAttribPointer(program_TexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + sizeof(glm::vec2));
		glVertexAttribPointer(program_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + sizeof(glm::vec2) + sizeof(glm::vec2));
		glEnableVertexAttribArray(program_Position);
		glEnableVertexAttribArray(program_TexCoord);
		glEnableVertexAttribArray(program_Color);
	}

	//------------ sprite info ------------
	struct SpriteInfo {
		char name[20];
		glm::vec2 min_uv = glm::vec2(4.0f / 500.0f, 115.0f / 240.f);
		glm::vec2 max_uv = glm::vec2(163.0f / 500.0f, 234.0f / 240.0f);
		glm::vec2 rad = glm::vec2(13.3f, 9.975f);
	};

#define SPRITE_NUM 80
#define TEXTURE_MAP_SIZE_X 481
#define TEXTURE_MAP_SIZE_Y 199
	//read the sprite data from file
	SpriteInfo sprite_list[SPRITE_NUM];
	glm::vec2 screen_size;
	{
		std::ifstream fin ("spriteBin.bin", std::ifstream::binary);
		for(int i=0;i<SPRITE_NUM;i++) {;
			fin.read(reinterpret_cast<char*>(&sprite_list[i].name), sizeof(char) * 20);
			//reference to https://stackoverflow.com/questions/19614581/reading-floating-numbers-from-bin-file-continuosly-and-outputting-in-console-win
			fin.read(reinterpret_cast<char*>(&(sprite_list[i].min_uv.x)), sizeof(float));
			fin.read(reinterpret_cast<char*>(&(sprite_list[i].max_uv.y)), sizeof(float));
			fin.read(reinterpret_cast<char*>(&(sprite_list[i].max_uv.x)), sizeof(float));
			fin.read(reinterpret_cast<char*>(&(sprite_list[i].min_uv.y)), sizeof(float));
			sprite_list[i].min_uv.y = TEXTURE_MAP_SIZE_Y - sprite_list[i].min_uv.y;
			sprite_list[i].max_uv.y = TEXTURE_MAP_SIZE_Y - sprite_list[i].max_uv.y;
			if(i==0) {
				screen_size = glm::vec2(sprite_list[i].max_uv.x - sprite_list[i].min_uv.x, sprite_list[i].max_uv.y - sprite_list[i].min_uv.y);
			}
			sprite_list[i].rad.x *= (sprite_list[i].max_uv.x - sprite_list[i].min_uv.x) / screen_size.x;
			sprite_list[i].rad.y *= (sprite_list[i].max_uv.y - sprite_list[i].min_uv.y) / screen_size.y;
			sprite_list[i].min_uv.x /= TEXTURE_MAP_SIZE_X;
			sprite_list[i].min_uv.y /= TEXTURE_MAP_SIZE_Y;
			sprite_list[i].max_uv.x /= TEXTURE_MAP_SIZE_X;
			sprite_list[i].max_uv.y /= TEXTURE_MAP_SIZE_Y;
			//printf("%s %f %f %f %f\n", sprite_list[i].name.c_str(), sprite_list[i].min_uv.x, sprite_list[i].min_uv.y, sprite_list[i].max_uv.x, sprite_list[i].max_uv.y);
		}
		fin.close();
	}

	auto load_sprite = [&](std::string const &name) -> SpriteInfo {
		SpriteInfo info;
		//TODO: look up sprite name in table of sprite infos
		int i;
		char n[20];
		for(i=0;i<(int)name.length();i++) {
			n[i] = name[i];
		}
		for(i=(int)name.length();i<20;i++) {
			n[i] = '\0';
		}
		for(i=0; i<SPRITE_NUM;i++) {
			if(strncmp(sprite_list[i].name, n, 20)==0) {
				return sprite_list[i];
			}
		}
		printf("cannot find sprite %s", name.c_str());
		exit(1);
		return info;
	};


	//------------ game state ------------

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		glm::vec2 at = glm::vec2(0.0f, 0.0f);
		glm::vec2 radius = glm::vec2(10.0f, 10.0f);
	} camera;
	//correct radius for aspect ratio:
	camera.radius.x = camera.radius.y * (float(config.size.x) / float(config.size.y));

	//------------ game loop ------------

//--- background ---
#define BACKGROUND_CENTER 0
#define BACKGROUND_LEFT 1
#define BACKGROUND_RIGHT 2

#define NONE 0
//--- material ---
#define BOARD 1
#define ROPE 2
#define PICK_AXE_HEAD 3
#define STICK 4
#define ROD 5
#define KNIFE 6
//--- tool ---
#define BRIDGE 7
#define PICK_AXE 8
#define LONG_KNIFE 9
//--- item ---
#define CRYSTAL 10
#define COIN 11
#define APPLE 12
#define ROCK 13
#define KEY 14
//--- landmark ---
#define GATE 15
#define WORK_BENCH 16
#define PILLAR_RIGHT 17
#define PILLAR_UP 18
#define PILLAR_LEFT 19
#define PILLAR_DOWN 20
#define PILLAR_CENTER 21
#define TREE 22
#define POND 23
#define PLACE_BRIDGE 24
#define SCALE 25
#define MAP 26
#define HOLE 27

//--- player direction ---
#define RIGHT 0
#define UP 1
#define LEFT 2
#define DOWN 3


	struct player {
		glm::vec2 position = glm::vec2(6.0f, 0.0f);
		bool carrying = false;
		int in_hand = NONE;
		int direction = RIGHT;
		bool walking = false;
		bool walk_leg = true;
	};

	class movable {
	public:
		glm::vec2 position;
		glm::vec2 rad;
		bool show;
		bool carried = false;
		bool can_interact = false;
		bool used = false;
		bool touched = false;
		movable(float x, float y, float r_x, float r_y, bool sh, bool interact) {
			position = glm::vec2(x, y);
			rad = glm::vec2(r_x, r_y);
			show = sh;
			can_interact = interact;
		}
		bool touches(player p) {
			if(std::abs(p.position.x-position.x)<=rad.x && std::abs(p.position.y-position.y)<=rad.y) {
				return true;
			} else {
				return false;
			}
		}
	};
	
	class landmark {
	public:
		glm::vec2 position;
		glm::vec2 rad;
		bool show = true;
		bool can_interact = true;
		bool touched = false;
		landmark(float x, float y, float r_x, float r_y) {
			position = glm::vec2(x, y);
			rad = glm::vec2(r_x, r_y);
		}
		bool touches(player p) {
			if(std::abs(p.position.x-position.x)<=rad.x && std::abs(p.position.y-position.y)<=rad.y) {
				return true;
			} else {
				return false;
			}
		}
	};
	
	class highlight {
	public:
		glm::vec2 position_p;
		bool show_p;
		highlight(glm::vec2 position, bool touched) {
			position_p = position;
			show_p = touched;
		}
		void refresh(glm::vec2 position, bool touched) {
			position_p = position;
			show_p = touched;
			return;
		}
	};

	bool should_quit = false;
	
	bool escaped = false;
	int current_map = BACKGROUND_CENTER;
	bool interact = false;
	
	//--- objects ---
	player P1;
	//--movables--
	std::vector< movable > movables;
	movable board(-8.0f, 4.0f, 2.0f, 2.0f, true, true);
	movable rope(-5.0f, -2.0f, 2.0f, 2.0f, true, true);
	movable pick_axe_head(7.0f, -3.2f, 2.0f, 2.0f, true, true);
	movable stick(9.0f, -3.0f, 2.0f, 2.0f, true, true);
	movable rod(9.0f, -2.0f, 2.0f, 2.0f, true, true);
	movable knife(-3.0f, -5.0f, 2.0f, 2.0f, true, true);
	movable bridge(4.0f, -5.0f, 2.0f, 2.0f, false, false);
	movable pick_axe(4.0f, -5.0f, 2.0f, 2.0f, false, false);
	movable long_knife(4.0f, -5.0f, 2.0f, 2.0f, false, false);
	movable crystal(-4.6f, 2.3f, 2.0f, 2.0f, true, false);
	movable coin(6.0f, -4.0f, 2.0f, 2.0f, false, false);
	movable apple(5.7f, 8.3f, 2.0f, 2.0f, true, false);
	movable rock(-4.0f, 5.0f, 2.0f, 2.0f, false, true);
	movable key(0.0f, 2.0f, 2.0f, 2.0f, false, false);	
	movables.push_back(board);
	movables.push_back(rope);
	movables.push_back(pick_axe_head);
	movables.push_back(stick);
	movables.push_back(rod);
	movables.push_back(knife);
	movables.push_back(bridge);
	movables.push_back(pick_axe);
	movables.push_back(long_knife);
	movables.push_back(crystal);
	movables.push_back(coin);
	movables.push_back(apple);
	movables.push_back(rock);
	movables.push_back(key);
	
	//--landmarks--
	std::vector< landmark > landmarks;
	landmark gate(0.07f, 7.33f, 2.0f, 2.0f);
	landmark work_bench(9.25f, -8.8f, 4.0f, 2.5f);
	landmark pillar_right(4.0f, 1.0f, 2.0f, 2.0f);
	landmark pillar_up(0.0f, 3.0f, 2.0f, 2.0f);
	landmark pillar_left(-4.0f, 1.0f, 2.0f, 2.0f);
	landmark pillar_down(0.0f, -3.0f, 2.0f, 2.0f);
	landmark pillar_center(0.0f, 1.0f, 2.0f, 2.0f);
	landmark tree(8.0f, 3.6f, 2.0f, 1.8f);
	landmark pond(6.0f, 1.5f, 2.0f, 1.5f);
	landmark bridge_place(4.0f, 1.5f, 2.0f, 2.0f);
	landmark scale(-7.0f, 5.0f, 1.5f, 2.0f);
	landmark map(6.0f, 5.4f, 3.0f, 0.3f);
	landmark hole(6.0f, -4.0f, 2.0f, 2.0f);
	landmarks.push_back(gate);
	landmarks.push_back(work_bench);
	landmarks.push_back(pillar_right);
	landmarks.push_back(pillar_up);
	landmarks.push_back(pillar_left);
	landmarks.push_back(pillar_down);
	landmarks.push_back(pillar_center);
	landmarks.push_back(tree);
	landmarks.push_back(pond);
	landmarks.push_back(bridge_place);
	landmarks.push_back(scale);
	landmarks.push_back(map);
	landmarks.push_back(hole);
	
	//--highlights--
	/*std::vector< highlight > highlights;
	highlight h_board(board.position, board.touched);
	highlight h_rope(rope.position, rope.touched);
	highlight h_pick_axe_head(pick_axe_head.position, pick_axe_head.touched);
	highlight h_stick(stick.position, stick.touched);
	highlight h_rod(rod.position, rod.touched);
	highlight h_knife(knife.position, knife.touched);
	highlight h_bridge(bridge.position, bridge.touched);
	highlight h_pick_axe(pick_axe.position, pick_axe.touched);
	highlight h_long_knife(long_knife.position, long_knife.touched);
	highlight h_crystal(crystal.position, crystal.touched);
	highlight h_coin(coin.position, coin.touched);
	highlight h_apple(apple.position, apple.touched);
	highlight h_rock(rock.position, rock.touched);
	highlight h_key(key.position, key.touched);
	highlight h_gate(gate.position, gate.touched);
	highlight h_workBench(work_bench.position, work_bench.touched);
	highlight h_pillar_right(pillar_right.position, pillar_right.touched);
	highlight h_pillar_up(pillar_up.position, pillar_up.touched);
	highlight h_pillar_left(pillar_left.position, pillar_left.touched);
	highlight h_pillar_down(pillar_down.position, pillar_down.touched);
	highlight h_pillar_center(pillar_center.position, pillar_center.touched);
	highlight h_tree(tree.position, tree.touched);
	highlight h_pond(pond.position, pond.touched);
	highlight h_bridgePlace(bridge_place.position, bridge_place.touched);
	highlight h_scale(scale.position, scale.touched);
	highlight h_map(map.position, map.touched);
	highlight h_hole(hole.position, hole.touched);
	highlights.push_back(h_board);
	highlights.push_back(h_rope);
	highlights.push_back(h_pick_axe_head);
	highlights.push_back(h_stick);
	highlights.push_back(h_rod);
	highlights.push_back(h_knife);
	highlights.push_back(h_bridge);
	highlights.push_back(h_pick_axe);
	highlights.push_back(h_long_knife);
	highlights.push_back(h_crystal);
	highlights.push_back(h_coin);
	highlights.push_back(h_apple);
	highlights.push_back(h_rock);
	highlights.push_back(h_key);
	highlights.push_back(h_gate);
	highlights.push_back(h_workBench);
	highlights.push_back(h_pillar_right);
	highlights.push_back(h_pillar_up);
	highlights.push_back(h_pillar_left);
	highlights.push_back(h_pillar_down);
	highlights.push_back(h_pillar_center);
	highlights.push_back(h_tree);
	highlights.push_back(h_pond);
	highlights.push_back(h_bridgePlace);
	highlights.push_back(h_scale);
	highlights.push_back(h_map);
	highlights.push_back(h_hole);*/
	
	//--- sprites ---
	static SpriteInfo background = load_sprite("center");
	static SpriteInfo player_sp = load_sprite("player1");
	static SpriteInfo board_sp = load_sprite("board");
	static SpriteInfo rope_sp = load_sprite("rope");
	static SpriteInfo pick_axe_head_sp = load_sprite("pickAxeHead");
	static SpriteInfo stick_sp = load_sprite("stick");
	static SpriteInfo rod_sp = load_sprite("rod");
	static SpriteInfo knife_sp = load_sprite("knife");
	static SpriteInfo bridge_sp = load_sprite("bridge");
	static SpriteInfo pick_axe_sp = load_sprite("pickAxe");
	static SpriteInfo long_knife_sp = load_sprite("longKnife");
	static SpriteInfo crystal_sp = load_sprite("crystal");
	static SpriteInfo coin_sp = load_sprite("coin");
	static SpriteInfo apple_sp = load_sprite("apple");
	static SpriteInfo rock_sp = load_sprite("rock");
	static SpriteInfo key_sp = load_sprite("key");
	static SpriteInfo gate_sp = load_sprite("gate");
	static SpriteInfo hole_sp = load_sprite("hole");
	static SpriteInfo scale_sp = load_sprite("scaleBalanced");
	static SpriteInfo message_sp = load_sprite("message");
	static SpriteInfo h_board_sp = load_sprite("h_board");
	static SpriteInfo h_rope_sp = load_sprite("h_rope");
	static SpriteInfo h_pick_axe_head_sp = load_sprite("h_pickAxeHead");
	static SpriteInfo h_stick_sp = load_sprite("h_stick");
	static SpriteInfo h_rod_sp = load_sprite("h_rod");
	static SpriteInfo h_knife_sp = load_sprite("h_knife");
	static SpriteInfo h_bridge_sp = load_sprite("h_bridge");
	static SpriteInfo h_pick_axe_sp = load_sprite("h_pickAxe");
	static SpriteInfo h_long_knife_sp = load_sprite("h_longKnife");
	static SpriteInfo h_crystal_sp = load_sprite("h_crystal");
	static SpriteInfo h_coin_sp = load_sprite("h_coin");
	static SpriteInfo h_apple_sp = load_sprite("h_apple");
	static SpriteInfo h_rock_sp = load_sprite("h_rock");
	static SpriteInfo h_key_sp = load_sprite("h_key");
	static SpriteInfo h_gate_sp = load_sprite("h_gate");
	static SpriteInfo h_hole_sp = load_sprite("h_hole");
	static SpriteInfo h_scale_sp = load_sprite("h_scaleBalanced");
	static SpriteInfo h_bridgePlace_sp = load_sprite("h_bridgePlace");
	static SpriteInfo h_work_bench_sp = load_sprite("h_workBench");	
	
	//==================================================================================================================
	
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
				mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
				mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
			} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			} else {
				if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_RIGHT) {
					P1.direction = RIGHT;
					P1.walking = true;
					P1.walk_leg = !P1.walk_leg;
					if(P1.position.x<12.6f && (current_map==BACKGROUND_CENTER || current_map==BACKGROUND_LEFT)) {
						P1.position.x += 0.3f;
					} else if (P1.position.x<11.2f && current_map==BACKGROUND_RIGHT) {
						P1.position.x += 0.3f;
					}
					interact = false;
				}
				if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_UP) {
					P1.direction = UP;
					P1.walking = true;
					P1.walk_leg = !P1.walk_leg;
					if(P1.position.y<5.4f) {
						P1.position.y += 0.3f;
					}
					interact = false;
				}
				if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_LEFT) {
					P1.direction = LEFT;
					P1.walking = true;
					P1.walk_leg = !P1.walk_leg;
					if(P1.position.x>-12.6f && (current_map==BACKGROUND_CENTER || current_map==BACKGROUND_RIGHT)) {
						P1.position.x -= 0.3f;
					} else if (P1.position.x>-11.2f && current_map==BACKGROUND_LEFT) {
						P1.position.x -= 0.3f;
					}
					interact = false;
				}
				if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_DOWN) {
					P1.direction = DOWN;
					P1.walking = true;
					P1.walk_leg = !P1.walk_leg;
					if(P1.position.y>-8.6f) {
						P1.position.y -= 0.3f;
					}
					interact = false;
				}
				if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_z) {
					interact = true;
				}
			}
		}
		if (should_quit) break;
		
		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
		previous_time = current_time;

		{ //update game state:
			(void)elapsed;
		}

		//draw output:
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			std::vector< Vertex > verts;

			//helper: add rectangle to verts:
			auto rect = [&verts](glm::vec2 const &at, glm::vec2 const &rad, glm::vec2 const &uv_min, glm::vec2 const &uv_max, glm::u8vec4 const &tint) {
				verts.emplace_back(at + glm::vec2(-rad.x,-rad.y), glm::vec2(uv_min.x, uv_min.y), tint);
				verts.emplace_back(verts.back());
				verts.emplace_back(at + glm::vec2(-rad.x, rad.y), glm::vec2(uv_min.x, uv_max.y), tint);
				verts.emplace_back(at + glm::vec2( rad.x,-rad.y), glm::vec2(uv_max.x, uv_min.y), tint);
				verts.emplace_back(at + glm::vec2( rad.x, rad.y), glm::vec2(uv_max.x, uv_max.y), tint);
				verts.emplace_back(verts.back());
			};

			auto draw_sprite = [&verts](SpriteInfo const &sprite, glm::vec2 const &at, float angle = 0.0f) {
				glm::vec2 min_uv = sprite.min_uv;
				glm::vec2 max_uv = sprite.max_uv;
				glm::vec2 rad = sprite.rad;
				glm::u8vec4 tint = glm::u8vec4(0xff, 0xff, 0xff, 0xff);
				glm::vec2 right = glm::vec2(std::cos(angle), std::sin(angle));
				glm::vec2 up = glm::vec2(-right.y, right.x);

				verts.emplace_back(at + right * -rad.x + up * -rad.y, glm::vec2(min_uv.x, min_uv.y), tint);
				verts.emplace_back(verts.back());
				verts.emplace_back(at + right * -rad.x + up * rad.y, glm::vec2(min_uv.x, max_uv.y), tint);
				verts.emplace_back(at + right *  rad.x + up * -rad.y, glm::vec2(max_uv.x, min_uv.y), tint);
				verts.emplace_back(at + right *  rad.x + up *  rad.y, glm::vec2(max_uv.x, max_uv.y), tint);
				verts.emplace_back(verts.back());
			};
			
			
			
			// background behavior in each map
			if(current_map == BACKGROUND_CENTER) {
				background = load_sprite("center");
				if(P1.position.x>=12.2f && P1.direction==RIGHT) {
					current_map = BACKGROUND_RIGHT;
					P1.position.x = -12.2f;
				} else if(P1.position.x<=-12.2f && P1.direction==LEFT) {
					current_map = BACKGROUND_LEFT;
					P1.position.x = 12.2f;
				}								
			} else if (current_map == BACKGROUND_LEFT) {
				background = load_sprite("left");
				if(P1.position.x>=12.2f && P1.direction==RIGHT) {
					current_map = BACKGROUND_CENTER;
					P1.position.x = -12.2f;
				}		
			} else if (current_map == BACKGROUND_RIGHT) {
				background = load_sprite("right");
				if(P1.position.x<=-12.2f && P1.direction==LEFT) {
					current_map = BACKGROUND_CENTER;
					P1.position.x = 12.2f;
				}		
			}
			rect(glm::vec2(0.0f, 0.0f), glm::vec2(camera.radius.x, camera.radius.y), background.min_uv, background.max_uv, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
			
			// landmark behavior in each map
			if(current_map == BACKGROUND_CENTER) {
				if(work_bench.touches(P1)) {
					draw_sprite(h_work_bench_sp, work_bench.position, 0.0f);
					if(interact) {
						if(P1.carrying) {
							if(P1.in_hand==BOARD) {
								board.carried = false;
								board.used = true;
								if(rope.used) {
									bridge.show = true;
									bridge.can_interact = true;
									bridge_place.can_interact = true;
								}
							} else if(P1.in_hand==ROPE) {
								rope.carried = false;
								rope.used = true;
								if(board.used) {
									bridge.show = true;
									bridge.can_interact = true;
									bridge_place.can_interact = true;
								}
							} else if(P1.in_hand==PICK_AXE_HEAD) {
								pick_axe_head.carried = false;
								pick_axe_head.used = true;
								if(stick.used) {
									pick_axe.show = true;
									pick_axe.can_interact = true;
									hole.can_interact = true;
								}
							} else if(P1.in_hand==STICK) {
								stick.carried = false;
								stick.used = true;
								if(pick_axe_head.used) {
									pick_axe.show = true;
									pick_axe.can_interact = true;
									hole.can_interact = true;
								}
							} else if(P1.in_hand==ROD) {
								rod.carried = false;
								rod.used = true;
								if(knife.used) {
									long_knife.show = true;
									long_knife.can_interact = true;
									apple.can_interact = true;
								}
							} else if(P1.in_hand==KNIFE) {
								knife.carried = false;
								knife.used = true;
								if(rod.used) {
									long_knife.show = true;
									long_knife.can_interact = true;
									apple.can_interact = true;
								}
							}
							P1.carrying = false;
							P1.in_hand = NONE;
						} else {
							// show message
						}
					}
				}
				if(gate.show) {
					draw_sprite(gate_sp, gate.position, 0.0f);
					if(gate.can_interact && gate.touches(P1)) {
						draw_sprite(h_gate_sp, gate.position, 0.0f);
						// show message
					}
				}
			} else if (current_map == BACKGROUND_LEFT) {
					
			} else if (current_map == BACKGROUND_RIGHT) {
					
			}
			
			//movable behavior in each map
			if (current_map == BACKGROUND_CENTER) {
				if(board.show) {
					draw_sprite(board_sp, board.position, 0.0f);
					if(board.can_interact && board.touches(P1)) {
						draw_sprite(h_board_sp, board.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = BOARD;
							board.show = false;
							board.can_interact = false;
							board.carried = true;
						}
					}
				}
				if(pick_axe_head.show) {
					draw_sprite(pick_axe_head_sp, pick_axe_head.position, 0.0f);
					if(pick_axe_head.can_interact && pick_axe_head.touches(P1)) {
						draw_sprite(h_pick_axe_head_sp, pick_axe_head.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = PICK_AXE_HEAD;
							pick_axe_head.show = false;
							pick_axe_head.can_interact = false;
							pick_axe_head.carried = true;
						}
					}
				}
				if(bridge.show) {
					draw_sprite(bridge_sp, bridge.position, 0.0f);
					if(bridge.can_interact && bridge.touches(P1)) {
						draw_sprite(h_bridge_sp, bridge.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = BRIDGE;
							bridge.show = false;
							bridge.can_interact = false;
							bridge.carried = true;
						}
					}
				}
				if(pick_axe.show) {
					draw_sprite(pick_axe_sp, pick_axe.position, 0.0f);
					if(pick_axe.can_interact && pick_axe.touches(P1)) {
						draw_sprite(h_pick_axe_sp, pick_axe.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = PICK_AXE;
							pick_axe.show = false;
							pick_axe.can_interact = false;
							pick_axe.carried = true;
						}
					}
				}
				if(long_knife.show) {
					draw_sprite(long_knife_sp, long_knife.position, 0.0f);
					if(long_knife.can_interact && long_knife.touches(P1)) {
						draw_sprite(h_long_knife_sp, long_knife.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = LONG_KNIFE;
							long_knife.show = false;
							long_knife.can_interact = false;
							long_knife.carried = true;
						}
					}
				}
			} else if (current_map == BACKGROUND_LEFT) {
				if(stick.show) {
					draw_sprite(stick_sp, stick.position, 0.0f);
					if(stick.can_interact && stick.touches(P1)) {
						draw_sprite(h_stick_sp, stick.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = STICK;
							stick.show = false;
							stick.can_interact = false;
							stick.carried = true;
						}
					}
				}
				if(rope.show) {
					draw_sprite(rope_sp, rope.position, 0.0f);
					if(rope.can_interact && rope.touches(P1)) {
						draw_sprite(h_rope_sp, rope.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = ROPE;
							rope.show = false;
							rope.can_interact = false;
							rope.carried = true;
						}
					}
				}
				if(crystal.show) {
					draw_sprite(crystal_sp, crystal.position, 0.0f);
					if(crystal.can_interact && crystal.touches(P1)) {
						draw_sprite(h_crystal_sp, crystal.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = CRYSTAL;
							crystal.show = false;
							crystal.can_interact = false;
							crystal.carried = true;
						}
					}
				}
				if(apple.show) {
					draw_sprite(apple_sp, apple.position, 0.0f);
					if(apple.can_interact && apple.touches(P1)) {
						draw_sprite(h_apple_sp, apple.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = APPLE;
							apple.show = false;
							apple.can_interact = false;
							apple.carried = true;
						}
					}
				}
				
			} else if (current_map == BACKGROUND_RIGHT) {
				if(rod.show) {
					draw_sprite(rod_sp, rod.position, 0.0f);
					if(rod.can_interact && rod.touches(P1)) {
						draw_sprite(h_rod_sp, rod.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = ROD;
							rod.show = false;
							rod.can_interact = false;
							rod.carried = true;
						}
					}
				}
				if(knife.show) {
					draw_sprite(knife_sp, knife.position, 0.0f);
					if(knife.can_interact && knife.touches(P1)) {
						draw_sprite(h_knife_sp, knife.position, 0.0f);
						if(interact && !P1.carrying) {
							P1.carrying = true;
							P1.in_hand = KNIFE;
							knife.show = false;
							knife.can_interact = false;
							knife.carried = true;
						}
					}
				}
				if(rock.can_interact && rock.touches(P1)) {
					draw_sprite(h_rock_sp, rock.position, 0.0f);
					if(interact && !P1.carrying) {
						P1.carrying = true;
						P1.in_hand = ROCK;
						rock.show = true;
						rock.can_interact = false;
						rock.carried = true;
					}
				}
				if(rock.show) {
					draw_sprite(rock_sp, rock.position, 0.0f);
				}
			}
			for (auto &movable : movables) {
				if(movable.carried==true) {
					movable.position = P1.position;
				}
			}
			
			//determine the sprite of the player
			if(P1.carrying==NONE) {
				if(P1.walk_leg) {
					player_sp = load_sprite("player1");
				}
				else {
					player_sp = load_sprite("player2");
				}
			} else {
				if(P1.walk_leg) {
					player_sp = load_sprite("playerCarry1");
				}
				else {
					player_sp = load_sprite("playerCarry2");
				}
			}
			draw_sprite(player_sp, P1.position, 0.0f);

//==================================================================================================================
			glBindBuffer(GL_ARRAY_BUFFER, buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * verts.size(), &verts[0], GL_STREAM_DRAW);

			glUseProgram(program);
			glUniform1i(program_tex, 0);
			glm::vec2 scale = 1.0f / camera.radius;
			glm::vec2 offset = scale * -camera.at;
			glm::mat4 mvp = glm::mat4(
				glm::vec4(scale.x, 0.0f, 0.0f, 0.0f),
				glm::vec4(0.0f, scale.y, 0.0f, 0.0f),
				glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
				glm::vec4(offset.x, offset.y, 0.0f, 1.0f)
			);
			glUniformMatrix4fv(program_mvp, 1, GL_FALSE, glm::value_ptr(mvp));

			glBindTexture(GL_TEXTURE_2D, tex);
			glBindVertexArray(vao);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, verts.size());
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}
