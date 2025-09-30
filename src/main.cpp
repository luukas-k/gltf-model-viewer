#include <print>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/gl.h>
#include <nlohmann/json.hpp>
#include <fstream>

enum struct image_format : uint32_t {
	unknown = 0,
	rgba,
};

struct image_specification {
	image_format format{ image_format::unknown };
	int width{ 0 }, height{ 0 };

	bool operator==(const image_specification &other) const {
		return format == other.format && width == other.width && height == other.height;
	}
};

template <>
struct std::hash<image_specification> {
	std::size_t operator()(const image_specification &k) const {
		return ((hash<uint32_t>()((uint32_t)k.format) ^ (hash<int>()(k.width) << 1)) >> 1) ^ (hash<int>()(k.height) << 1);
	}
};

struct image {
	image_specification specification;
	std::vector<uint8_t> data;
};

struct material {
	glm::vec4 base_color;
	int base_color_index, base_color_layer;
	int pad0, pad1;
	int normal_index, normal_layer;
	int pad2, pad3;
};

struct vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 tangent;
};

struct submesh {
	size_t first_vertex, first_index;
	uint32_t draw_count;
	uint32_t material;
};

struct mesh {
	uint32_t vao{};
	uint32_t vbo{}, ibo{};
	std::vector<submesh> submeshes;
};

struct draw_elements_indirect_command {
	uint32_t count;
	uint32_t instanceCount;
	uint32_t firstIndex;
	int32_t baseVertex;
	uint32_t baseInstance;
};



int main(int argc, const char *argv[]) {
	glfwInit();

	glfwWindowHint(GLFW_SAMPLES, 16);

	GLFWwindow *handle = glfwCreateWindow(1280, 720, "Title", nullptr, nullptr);
	glfwMakeContextCurrent(handle);
	gladLoadGL((GLADloadfunc)glfwGetProcAddress);

	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback([](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam) {
		std::println("({}) {}", id, (const char *)message);
	}, nullptr);

	std::filesystem::path gltf_path = "assets/sponza/Sponza.gltf";
	std::ifstream gltf_fs(gltf_path);
	if (!gltf_fs) {
		std::println("Failed to open '{}'.", gltf_path.string());
		return -1;
	}

	auto gltf = nlohmann::json::parse(gltf_fs);
	std::println("Generator: {}", (std::string)gltf["asset"]["generator"]);

	std::vector<image> images;
	for (auto &img_def : gltf["images"]) {
		std::filesystem::path image_path = gltf_path.parent_path() / img_def["uri"];
		std::string image_path_str = image_path.string();

		int w{}, h{}, c{};
		stbi_uc *data = stbi_load(image_path_str.c_str(), &w, &h, &c, 4);

		images.push_back(image{
			.specification = {
				.format = image_format::rgba,
				.width = w,
				.height = h
			},
			.data = std::vector<uint8_t>(data, data + w * h * 4 * sizeof(uint8_t))
						 });

		stbi_image_free(data);
	}

	std::unordered_map<image_specification, std::vector<size_t>> spec_to_image;
	for (size_t i = 0; i < images.size(); i++) {
		auto &img = images.at(i);
		spec_to_image[img.specification].push_back(i);
	}

	std::vector<uint32_t> array_textures;
	std::unordered_map<uint32_t, std::pair<size_t, uint32_t>> image_to_array_tex; // image -> (array_tex_ind, array_tex_layer)
	for (auto &[spec, image_indices] : spec_to_image) {
		uint32_t tex{};
		glGenTextures(1, &tex);
		assert(spec.format == image_format::rgba);
		glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, spec.width, spec.height, (GLsizei)image_indices.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		for (uint32_t layer = 0; layer < image_indices.size(); layer++) {
			auto img_i = image_indices.at(layer);
			auto &img = images.at(img_i);
			
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, spec.width, spec.height, 1, GL_RGBA, GL_UNSIGNED_BYTE, img.data.data());
		
			image_to_array_tex[img_i] = { array_textures.size(), layer };
		}

		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

		array_textures.push_back(tex);
	}

	std::vector<material> materials;
	for (auto &mat_def : gltf["materials"]) {
		auto &pbr = mat_def["pbrMetallicRoughness"];

		glm::vec4 base_color = pbr.contains("baseColorFactor") ? 
			glm::vec4{pbr["baseColorFactor"][0], pbr["baseColorFactor"][1], pbr["baseColorFactor"][2], pbr["baseColorFactor"][3]} :
			glm::vec4{0, 0, 0, 1};

		int32_t base_color_index{-1}, base_color_layer{-1};
		if (pbr.contains("baseColorTexture")) {
			auto &tex_def = gltf["textures"][(int)pbr["baseColorTexture"]["index"]];
			std::pair<size_t, uint32_t> base_color_texture = image_to_array_tex[tex_def["source"]];
			base_color_index = (int32_t)base_color_texture.first;
			base_color_layer = (int32_t)base_color_texture.second;
		}

		int32_t normal_index{-1}, normal_layer{-1};
		if (mat_def.contains("normalTexture")) {
			auto &tex_def = gltf["textures"][(int)mat_def["normalTexture"]["index"]];
			std::pair<size_t, uint32_t> normal_texture = image_to_array_tex[tex_def["source"]];
			normal_index = (int32_t)normal_texture.first;
			normal_layer = (int32_t)normal_texture.second;
		}

		materials.push_back(material{
			.base_color = base_color,
			.base_color_index = base_color_index, 
			.base_color_layer = base_color_layer,
			.normal_index = normal_index, 
			.normal_layer = normal_layer,
							});
	}

	std::vector<std::vector<char>> buffers;
	for (auto &buffer_def : gltf["buffers"]) {
		std::filesystem::path buffer_path = gltf_path.parent_path() / buffer_def["uri"];
		std::ifstream buffer_fs(buffer_path, std::ios::binary);
		if (!buffer_fs) {
			std::println("Failed to open buffer '{}'.", buffer_path.string());
			return -1;
		}

		buffers.emplace_back(std::istreambuf_iterator<char>(buffer_fs), std::istreambuf_iterator<char>());
	}

	auto type_components = [](std::string type) -> uint32_t {
		if (type == "SCALAR") return 1;
		if (type == "VEC2") return 2;
		if (type == "VEC3") return 3;
		if (type == "VEC4") return 4;
		assert(false);
		return 0;
	};
	
	std::vector<mesh> meshes;

	for (auto &mesh_def : gltf["meshes"]) {
		std::vector<vertex> mesh_vertices;
		std::vector<uint32_t> mesh_indices;
		std::vector<submesh> sub_meshes;

		for (auto &prim_def : mesh_def["primitives"]) {
			auto &pos_accessor = gltf["accessors"][(int)prim_def["attributes"]["POSITION"]];
			auto &pos_buffer_view = gltf["bufferViews"][(int)pos_accessor["bufferView"]];
			auto &pos_buffer = buffers[(int)pos_buffer_view["buffer"]];

			std::span<char> pos_buffer_data = 
				std::span(pos_buffer)
				.subspan(pos_buffer_view["byteOffset"], pos_buffer_view["byteLength"])
				.subspan(pos_accessor["byteOffset"]);

			auto &norm_accessor = gltf["accessors"][(int)prim_def["attributes"]["NORMAL"]];
			auto &norm_buffer_view = gltf["bufferViews"][(int)norm_accessor["bufferView"]];
			auto &norm_buffer = buffers[(int)norm_buffer_view["buffer"]];

			std::span<char> norm_buffer_data =
				std::span(norm_buffer)
				.subspan(norm_buffer_view["byteOffset"], norm_buffer_view["byteLength"])
				.subspan(norm_accessor["byteOffset"]);

			auto &tex_coord_accessor = gltf["accessors"][(int)prim_def["attributes"]["TEXCOORD_0"]];
			auto &tex_coord_buffer_view = gltf["bufferViews"][(int)tex_coord_accessor["bufferView"]];
			auto &tex_coord_buffer = buffers[(int)tex_coord_buffer_view["buffer"]];

			std::span<char> tex_coord_buffer_data =
				std::span(tex_coord_buffer)
				.subspan(tex_coord_buffer_view["byteOffset"], tex_coord_buffer_view["byteLength"])
				.subspan(tex_coord_accessor["byteOffset"]);

			std::vector<vertex> vertices;

			for (size_t i = 0; i < pos_accessor["count"]; i++) {
				vertex v{};

				v.position = *(glm::vec3*)pos_buffer_data.subspan(i * sizeof(glm::vec3), sizeof(glm::vec3)).data();
				v.normal = *(glm::vec3*)norm_buffer_data.subspan(i * sizeof(glm::vec3), sizeof(glm::vec3)).data();
				v.uv = *(glm::vec2*)tex_coord_buffer_data.subspan(i * sizeof(glm::vec2), sizeof(glm::vec2)).data();

				if (prim_def["attributes"].contains("TANGENT")) {
					auto &tangent_accessor = gltf["accessors"][(int)prim_def["attributes"]["TANGENT"]];
					auto &tangent_buffer_view = gltf["bufferViews"][(int)tangent_accessor["bufferView"]];
					auto &tangent_buffer = buffers[(int)tangent_buffer_view["buffer"]];

					std::span<char> tangent_buffer_data =
						std::span(tangent_buffer)
						.subspan(tangent_buffer_view["byteOffset"], tangent_buffer_view["byteLength"])
						.subspan(tangent_accessor["byteOffset"]);

					v.tangent = *(glm::vec4*)tangent_buffer_data.subspan(i * sizeof(glm::vec4), sizeof(glm::vec4)).data();
				}

				vertices.push_back(v);
			}

			auto &ind_accessor = gltf["accessors"][(int)prim_def["indices"]];
			auto &ind_buffer_view = gltf["bufferViews"][(int)ind_accessor["bufferView"]];
			auto &ind_buffer = buffers[(int)ind_buffer_view["buffer"]];

			std::span<char> ind_buffer_data =
				std::span(ind_buffer)
				.subspan(ind_buffer_view["byteOffset"], ind_buffer_view["byteLength"])
				.subspan(ind_accessor["byteOffset"]);

			std::vector<uint32_t> indices;

			for (size_t i = 0; i < ind_accessor["count"]; i++) {
				uint32_t v = 0;
				if (ind_accessor["componentType"] == GL_UNSIGNED_INT) {
					v = *(uint32_t*)ind_buffer_data.subspan(i * sizeof(uint32_t), sizeof(uint32_t)).data();
				}
				else if (ind_accessor["componentType"] == GL_UNSIGNED_SHORT) {
					v = (uint32_t)*(uint16_t *)ind_buffer_data.subspan(i * sizeof(uint16_t), sizeof(uint16_t)).data();
				}
				else {
					assert(false);
				}
				indices.push_back(v);
			}

			sub_meshes.push_back(submesh{ 
				.first_vertex = mesh_vertices.size(), 
				.first_index = mesh_indices.size(), 
				.draw_count = (uint32_t)indices.size(),
				.material = prim_def["material"]
								 });
			mesh_vertices.append_range(vertices);
			mesh_indices.append_range(indices);
		}

		uint32_t vao{}, vbo{}, ibo{};
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		
		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, mesh_vertices.size() * sizeof(vertex), mesh_vertices.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(vertex), (const void *)(intptr_t)offsetof(vertex, position));

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, false, sizeof(vertex), (const void *)(intptr_t)offsetof(vertex, normal));

		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, false, sizeof(vertex), (const void *)(intptr_t)offsetof(vertex, uv));

		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, false, sizeof(vertex), (const void *)(intptr_t)offsetof(vertex, tangent));

		glGenBuffers(1, &ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_indices.size() * sizeof(uint32_t), mesh_indices.data(), GL_STATIC_DRAW);

		glBindVertexArray(0);

		meshes.push_back(mesh{
			.vao = vao,
			.vbo = vbo,
			.ibo = ibo,
			.submeshes = sub_meshes,
						 });
	}
	
	struct node {
		glm::vec3 position{ 0,0,0 };
		glm::vec3 scale{ 1,1,1 };
		std::optional<uint32_t> mesh;
	};
	std::vector<node> nodes;
	for (auto &node_def : gltf["nodes"]) {
		glm::vec3 pos =
			node_def.contains("translation") ?
			glm::vec3{ node_def["translation"][0], node_def["translation"][1], node_def["translation"][2] } :
			glm::vec3{ 0.f, 0.f, 0.f };

		glm::vec3 scale =
			node_def.contains("scale") ?
			glm::vec3{ node_def["scale"][0], node_def["scale"][1], node_def["scale"][2] } :
			glm::vec3{ 1.f, 1.f, 1.f };

		std::optional<uint32_t> mesh;
		if (node_def.contains("mesh")) {
			mesh = (uint32_t)node_def["mesh"];
		}

		nodes.push_back(node{
			.position = pos,
			.scale = scale,
			.mesh = mesh,
						});
	}


	const char *vs_src = R"GLSL(#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;

uniform mat4 sys_proj = mat4(1);
uniform mat4 sys_view = mat4(1);
uniform mat4 sys_model = mat4(1);

out vec3 fPos;
out vec3 fNormal;
out vec2 fUV;
out flat int fMaterial;
out mat3 fTBN;

void main(){
	gl_Position = sys_proj * sys_view * sys_model * vec4(aPos, 1);
	fPos = (sys_model * vec4(aPos, 1)).xyz;
	fNormal = mat3(transpose(inverse(sys_model))) * aNormal;
	fUV = aUV;
	fMaterial = gl_DrawID;
	
	vec3 T = normalize(vec3(sys_model * vec4(aTangent.xyz,   0.0)));
	vec3 B = normalize(vec3(sys_model * vec4(cross(aTangent.xyz, aNormal) * aTangent.w, 0.0)));
	vec3 N = normalize(vec3(sys_model * vec4(aNormal,    0.0)));
	fTBN = mat3(T, B, N);
}

)GLSL";
	const char *fs_src = R"GLSL(#version 460 core

in vec3 fPos;
in vec3 fNormal;
in vec2 fUV;
in flat int fMaterial;
in mat3 fTBN;

uniform sampler2DArray sys_textures[16];

struct material {
	vec4 base_color;
	ivec4 base_color_texture;
	ivec4 normal_texture;
};

layout(std430, binding = 0) buffer materials_buffer {
	material materials[];
};

out vec4 rColor;

vec4 get_base_color(material mat, vec2 uv) {
	if (mat.base_color_texture.x == 0) {
		return texture(sys_textures[0], vec3(uv, mat.base_color_texture.y));
	}
	else if (mat.base_color_texture.x == 1) {
		return texture(sys_textures[1], vec3(uv, mat.base_color_texture.y));
	}
	else if (mat.base_color_texture.x == 2) {
		return texture(sys_textures[2], vec3(uv, mat.base_color_texture.y));
	}
	else {
		return mat.base_color;
	}
}

vec3 get_normal(material mat, vec2 uv) {
	if (mat.normal_texture.x == 0) {
		return fTBN * (texture(sys_textures[0], vec3(uv, mat.normal_texture.y)).xyz * 2 - vec3(1));
	}
	else if (mat.normal_texture.x == 1) {
		return fTBN * (texture(sys_textures[1], vec3(uv, mat.normal_texture.y)).xyz * 2 - vec3(1));
	}
	else if (mat.normal_texture.x == 2) {
		return fTBN * (texture(sys_textures[2], vec3(uv, mat.normal_texture.y)).xyz * 2 - vec3(1));
	}
	else {
		return fNormal;
	}
}

void main(){
	material mat = materials[fMaterial];

	vec3 light_color = vec3(1);
	vec3 light_pos = vec3(0, 5, 0);
	vec3 light_dir = normalize(light_pos - fPos);

	vec4 base_color = get_base_color(mat, fUV);

	// Ambient
	float ambient_strength = 0.1;
	vec3 ambient = light_color * ambient_strength;

	// Diffuse
	// vec3 norm = normalize(fNormal);
	vec3 norm = normalize(get_normal(mat, fUV));
	float diffuse_strength = max(dot(norm, light_dir), 0.0);
	vec3 diffuse = light_color * diffuse_strength;

	if (base_color.a < 0.5) {
		discard;
	}

	rColor = vec4((ambient + diffuse) * base_color.rgb, base_color.a);
	// rColor = base_color;
	// rColor = vec4(vec3(dot(fNormal, light_dir)), 1);
	// rColor = vec4(norm, 1);
}

)GLSL";

	uint32_t 
		program = glCreateProgram(),
		vs = glCreateShader(GL_VERTEX_SHADER),
		fs = glCreateShader(GL_FRAGMENT_SHADER);

	glShaderSource(vs, 1, &vs_src, nullptr);
	glShaderSource(fs, 1, &fs_src, nullptr);

	glCompileShader(vs);
	glCompileShader(fs);

	glAttachShader(program, vs);
	glAttachShader(program, fs);

	glLinkProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	int len{};
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
	if (len > 0) {
		char *log = new char[len + 1] {0};
		glGetProgramInfoLog(program, len + 1, nullptr, log);
		std::println("{}", (const char *)log);
		delete[] log;
	}

	glm::vec3 camera_pos{ 0.f, 0.f, 0.f };
	glm::vec3 camera_rot{ 0.f, 0.f, 0.f };

	glm::dvec2 cursor_pos{};
	glfwGetCursorPos(handle, &cursor_pos.x, &cursor_pos.y);

	double last_update = glfwGetTime();
	while (!glfwWindowShouldClose(handle)) {
		double current_update = glfwGetTime();
		float dt = (float)(current_update - last_update);
		last_update = current_update;

		glfwPollEvents();

		// Updating
		glm::dvec2 cur_cursor_pos{};
		glfwGetCursorPos(handle, &cur_cursor_pos.x, &cur_cursor_pos.y);
		glm::dvec2 cursor_delta = (glfwGetInputMode(handle, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) ? cursor_pos - cur_cursor_pos : glm::dvec2{0, 0};
		cursor_pos = cur_cursor_pos;

		if (glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
			glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		if (glfwGetKey(handle, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}

		glm::vec3 mv_input{
			(glfwGetKey(handle, GLFW_KEY_A) == GLFW_PRESS) ? -1.f : 0.f + 
			(glfwGetKey(handle, GLFW_KEY_D) == GLFW_PRESS) ?  1.f : 0.f,
			(glfwGetKey(handle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? -1.f : 0.f +
			(glfwGetKey(handle, GLFW_KEY_SPACE) == GLFW_PRESS) ? 1.f : 0.f,
			(glfwGetKey(handle, GLFW_KEY_W) == GLFW_PRESS) ? -1.f : 0.f +
			(glfwGetKey(handle, GLFW_KEY_S) == GLFW_PRESS) ? 1.f : 0.f
		};
		glm::vec3 mv_dir{
			mv_input.z * sinf(camera_rot.y) + mv_input.x * cosf(camera_rot.y),
			mv_input.y,
			mv_input.z *cosf(camera_rot.y) - mv_input.x * sinf(camera_rot.y),
		};
		camera_pos += 5.f * mv_dir * dt;

		float dy = ((glfwGetKey(handle, GLFW_KEY_UP) ? 1.f : 0.f) + (glfwGetKey(handle, GLFW_KEY_DOWN) ? -1.f : 0.f)) * dt + cursor_delta.y;
		float dx = ((glfwGetKey(handle, GLFW_KEY_RIGHT) ? -1.f : 0.f) + (glfwGetKey(handle, GLFW_KEY_LEFT) ? 1.f : 0.f)) * dt + cursor_delta.x;
		float sens = 0.01f;
		camera_rot.x = glm::clamp(camera_rot.x + sens * dy, -3.1415f / 2.f, 3.1415f / 2.f);
		camera_rot.y = glm::mod(camera_rot.y + sens * dx, 2.f * 3.1415f);

		// Rendering

		int w{}, h{};
		glfwGetFramebufferSize(handle, &w, &h);
		float ar = (float)w / h;
		
		glViewport(0, 0, w, h);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(1.f, 1.f, 1.f, 1.f);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		glUseProgram(program);

		glm::mat4
			proj = glm::perspective(glm::radians(80.f), ar, 0.1f, 1000.f),
			view =
				glm::rotate(glm::mat4(1), -camera_rot.z, { 0.f, 0.f, 1.f }) *
				glm::rotate(glm::mat4(1), -camera_rot.x, { 1.f, 0.f, 0.f }) *
				glm::rotate(glm::mat4(1), -camera_rot.y, { 0.f, 1.f, 0.f }) *
				glm::translate(glm::mat4(1), -camera_pos)
			;

		glUniformMatrix4fv(glGetUniformLocation(program, "sys_proj"), 1, false, &proj[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(program, "sys_view"), 1, false, &view[0][0]);

		for (auto &n : nodes) {
			if (!n.mesh) {
				continue;
			}
			auto &m = meshes.at(*n.mesh);

			glm::mat4 model =
				glm::translate(glm::mat4(1), n.position) *
				glm::scale(glm::mat4(1), n.scale);

			glUniformMatrix4fv(glGetUniformLocation(program, "sys_model"), 1, false, &model[0][0]);

			glBindVertexArray(m.vao);
			std::vector<draw_elements_indirect_command> cmds;
			std::vector<material> mats;
			std::vector<uint32_t> bind_textures;
			for (auto &sm : m.submeshes) {
				cmds.push_back(draw_elements_indirect_command{
							   .count = sm.draw_count,
							   .instanceCount = 1,
							   .firstIndex = (uint32_t)sm.first_index,
							   .baseVertex = (int32_t)sm.first_vertex,
							   .baseInstance = 0,
							   });

				auto &mat = materials.at(sm.material);

				int32_t base_index = -1;
				if (mat.base_color_index != -1) {
					auto &base_at = array_textures.at(mat.base_color_index);
					auto loc = std::find(bind_textures.begin(), bind_textures.end(), base_at);
					if (loc == bind_textures.end()) {
						base_index = bind_textures.size();
						bind_textures.push_back(base_at);
					}
					else {
						base_index = (uint32_t)std::distance(bind_textures.begin(), loc);
					}
				}

				int32_t norm_index = -1;
				if (mat.normal_index != -1) {
					auto &norm_at = array_textures.at(mat.normal_index);
					auto norm_loc = std::find(bind_textures.begin(), bind_textures.end(), norm_at);
					if (norm_loc == bind_textures.end()) {
						norm_index = bind_textures.size();
						bind_textures.push_back(norm_at);
					}
					else {
						norm_index = (uint32_t)std::distance(bind_textures.begin(), norm_loc);
					}
				}

				mats.push_back(material{
					.base_color = mat.base_color,
					.base_color_index = base_index,
					.base_color_layer = mat.base_color_layer,
					.normal_index = norm_index,
					.normal_layer = mat.normal_layer
							   });
			}

			for (uint32_t i = 0; i < bind_textures.size(); i++) {
				glActiveTexture(GL_TEXTURE0 + i);
				glBindTexture(GL_TEXTURE_2D_ARRAY, bind_textures[i]);
				auto name = std::format("sys_textures[{}]", i);
				glUniform1i(glGetUniformLocation(program, name.c_str()), i);
			}

			uint32_t mat_buffer;
			glGenBuffers(1, &mat_buffer);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, mat_buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, mats.size() * sizeof(material), mats.data(), GL_STATIC_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mat_buffer);

			glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, cmds.data(), cmds.size(), sizeof(draw_elements_indirect_command));
		
			glDeleteBuffers(1, &mat_buffer);
		}

		glfwSwapBuffers(handle);
	}

	glfwDestroyWindow(handle);
	glfwTerminate();
	return 0;
}