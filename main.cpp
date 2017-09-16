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

#define SPRITE_NUM 79
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
#define CENTER 0
#define LEFT 1
#define RIGHT 2

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
//--- object ---
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


	struct player {
		glm::vec2 position = glm::vec2(0.5f, 0.0f);
		bool carrying = false;
		int in_hand = NONE;
	};

	struct movable {
		glm::vec2 position;
		bool show;
		bool carried = false;
		bool can_interact;
		bool used = false;
	};
	

	bool should_quit = false;
	
	bool escaped = false;
	int current_map = CENTER;
	
	player P1;
	
	//movable 
	
	
	
	
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
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_RIGHT) {
				P1.position.x += 0.2f;
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_UP) {
				P1.position.y += 0.2f;
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_LEFT) {
				P1.position.x -= 0.2f;
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_DOWN) {
				P1.position.y -= 0.2f;
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_z) {
				
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
			static SpriteInfo background = load_sprite("center");
			//rect(glm::vec2(0.0f, 0.0f), glm::vec2(camera.radius.x, camera.radius.y), background.min_uv, background.max_uv, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
			
			//Draw a sprite "player" at position (5.0, 2.0):
			static SpriteInfo player_sp = load_sprite("player1"); //TODO: hoist
			//printf("%s %f %f %f %f\n", player.name, player.min_uv.x, player.min_uv.y, player.max_uv.x, player.max_uv.y);
			
			
			
			
			if(current_map == CENTER) {
				SpriteInfo background = load_sprite("center");
				
				
			} else if (current_map == LEFT) {
				SpriteInfo background = load_sprite("left");
				
				
				
			} else if (current_map == RIGHT) {
				SpriteInfo background = load_sprite("right");
				
				
				
			}
			rect(glm::vec2(0.0f, 0.0f), glm::vec2(camera.radius.x, camera.radius.y), background.min_uv, background.max_uv, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
			draw_sprite(player_sp, P1.position, 0.0f);

			


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
