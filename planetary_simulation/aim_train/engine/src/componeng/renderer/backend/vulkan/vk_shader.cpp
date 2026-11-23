#include "componeng/renderer/backend/vulkan/vk_shader.hpp"

#include "componeng/renderer/backend/vulkan/vk_render_device.hpp"
#include "componeng/utils/logger.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace componeng::renderer::vulkan {

namespace {

// GLSL (GL style) -> GLSL (Vulkan style) source rewrite

struct Stmt {
  size_t begin = 0;
  size_t end = 0; // one past the terminating ';' or '}'
};

struct Member {
  std::string type;
  std::string name;
  std::string arrayExpr; // text between [ ], empty if not an array
};

struct StructDef {
  std::vector<Member> members;
};

struct Edit {
  size_t begin;
  size_t end;
  std::string text;
};

struct StageInfo {
  std::string src;
  std::map<std::string, StructDef> structs;
  std::map<std::string, int> defines;

  struct Block {
    Stmt stmt;
    std::string name;
    size_t headLength; // chars of the statement before its '{'
  };
  std::vector<Block> blocks;

  std::vector<Member> loose;
  std::vector<Stmt> looseStmts;

  struct SamplerDecl {
    Stmt stmt;
    std::string type;
    std::string name;
  };
  std::vector<SamplerDecl> samplers;

  struct IoDecl {
    Stmt stmt;
    std::string qualifier; // flat / smooth / noperspective / ""
    bool out = false;
    std::string type;
    std::string name;
  };
  std::vector<IoDecl> io;

  struct Attrib {
    int location;
    std::string type;
    std::string name;
  };
  std::vector<Attrib> attribs;
};

std::string stripComments(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n')
        ++i;
    } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
        if (s[i] == '\n')
          out += '\n';
        ++i;
      }
      i += 2;
      out += ' ';
    } else {
      out += s[i++];
    }
  }
  return out;
}

std::string trim(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Top-level statements: everything at brace depth 0, split at ';' (and after a
// function body). Preprocessor lines are skipped.
std::vector<Stmt> scanStatements(const std::string &s) {
  static const std::regex layoutQualifier(R"(layout\s*\([^)]*\))");
  std::vector<Stmt> out;
  const size_t n = s.size();
  size_t i = 0;
  int depth = 0;
  size_t start = std::string::npos;
  while (i < n) {
    const char c = s[i];
    if (depth == 0 && start == std::string::npos) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        ++i;
        continue;
      }
      if (c == '#') {
        while (i < n && s[i] != '\n') {
          if (s[i] == '\\' && i + 1 < n)
            ++i;
          ++i;
        }
        continue;
      }
      start = i;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (--depth == 0) {
        std::string head = s.substr(start, s.find('{', start) - start);
        head = std::regex_replace(head, layoutQualifier, "");
        if (head.find('(') != std::string::npos) { // a function body
          out.push_back({start, i + 1});
          start = std::string::npos;
        }
      }
    } else if (c == ';' && depth == 0) {
      out.push_back({start, i + 1});
      start = std::string::npos;
    }
    ++i;
  }
  return out;
}

// "float a; vec3 b[2];" -> members
std::vector<Member> parseMembers(const std::string &body) {
  static const std::regex decl(R"(^\s*(\w+)\s+(\w+)\s*(?:\[\s*([^\]]*?)\s*\])?\s*$)");
  std::vector<Member> out;
  std::stringstream ss(body);
  std::string part;
  while (std::getline(ss, part, ';')) {
    std::smatch m;
    if (std::regex_match(part, m, decl))
      out.push_back({m[1], m[2], m[3]});
  }
  return out;
}

StageInfo parseStage(std::string text) {
  StageInfo info;
  text = stripComments(text);

  // `#version` becomes 450 (Vulkan GLSL); one is added if missing.
  static const std::regex versionLine(R"(#version[^\n]*)");
  if (std::regex_search(text, versionLine))
    text = std::regex_replace(text, versionLine, "#version 450");
  else
    text = "#version 450\n" + text;
  info.src = text;

  static const std::regex defineRe(R"(#define\s+(\w+)\s+(\d+))");
  for (auto it = std::sregex_iterator(text.begin(), text.end(), defineRe);
       it != std::sregex_iterator(); ++it)
    info.defines[(*it)[1]] = std::stoi((*it)[2]);

  static const std::regex blockRe(
      R"(^(?:layout\s*\(([^)]*)\)\s*)?uniform\s+(\w+)\s*$)");
  static const std::regex looseRe(
      R"(^(?:layout\s*\([^)]*\)\s*)?uniform\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+([\s\S]+)$)");
  static const std::regex attribRe(
      R"(^layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*in\s+(\w+)\s+(\w+)$)");
  static const std::regex ioRe(
      R"(^(flat|smooth|noperspective)?\s*(in|out)\s+(\w+)\s+(\w+)$)");
  static const std::regex declaratorRe(R"(^\s*(\w+)\s*(?:\[\s*([^\]]*?)\s*\])?\s*$)");

  for (const Stmt &st : scanStatements(text)) {
    const std::string full = text.substr(st.begin, st.end - st.begin);
    const size_t brace = full.find('{');
    std::string head =
        trim(brace == std::string::npos ? full.substr(0, full.size() - 1)
                                        : full.substr(0, brace));
    std::smatch m;

    if (head.rfind("struct", 0) == 0 && brace != std::string::npos) {
      const std::string name = trim(head.substr(6));
      const size_t close = full.rfind('}');
      info.structs[name].members =
          parseMembers(full.substr(brace + 1, close - brace - 1));
    } else if (brace != std::string::npos) {
      if (std::regex_match(head, m, blockRe))
        info.blocks.push_back({st, m[2], brace});
    } else if (std::regex_match(head, m, attribRe)) {
      info.attribs.push_back({std::stoi(m[1]), m[2], m[3]});
    } else if (std::regex_match(head, m, looseRe)) {
      const std::string type = m[1];
      const std::string rest = m[2];
      if (type.rfind("sampler", 0) == 0) {
        info.samplers.push_back({st, type, trim(rest)});
      } else {
        std::stringstream ss(rest);
        std::string d;
        while (std::getline(ss, d, ',')) {
          std::smatch dm;
          if (std::regex_match(d, dm, declaratorRe))
            info.loose.push_back({type, dm[1], dm[2]});
        }
        info.looseStmts.push_back(st);
      }
    } else if (std::regex_match(head, m, ioRe)) {
      info.io.push_back({st, m[1], m[2] == "out", m[3], m[4]});
    }
  }
  return info;
}

// std140 layout of the loose uniforms

struct TL {
  uint32_t align = 16;
  uint32_t size = 16;
};

uint32_t roundUp(uint32_t v, uint32_t a) {
  return (v + a - 1) / a * a;
}

bool basicLayout(const std::string &t, TL &out) {
  static const std::map<std::string, TL> table = {
      {"float", {4, 4}},   {"int", {4, 4}},     {"uint", {4, 4}},
      {"bool", {4, 4}},    {"vec2", {8, 8}},    {"ivec2", {8, 8}},
      {"uvec2", {8, 8}},   {"bvec2", {8, 8}},   {"vec3", {16, 12}},
      {"ivec3", {16, 12}}, {"uvec3", {16, 12}}, {"bvec3", {16, 12}},
      {"vec4", {16, 16}},  {"ivec4", {16, 16}}, {"uvec4", {16, 16}},
      {"bvec4", {16, 16}}, {"mat2", {16, 32}},  {"mat3", {16, 48}},
      {"mat4", {16, 64}},
  };
  auto it = table.find(t);
  if (it == table.end())
    return false;
  out = it->second;
  return true;
}

uint32_t arrayCount(const StageInfo &info, const std::string &expr) {
  if (expr.empty())
    return 0;
  if (std::isdigit(static_cast<unsigned char>(expr[0])))
    return static_cast<uint32_t>(std::stoul(expr));
  auto it = info.defines.find(expr);
  if (it != info.defines.end())
    return static_cast<uint32_t>(it->second);
  LOG_ERROR("Vulkan shader: cannot resolve array size '%s'", expr.c_str());
  return 1;
}

TL typeLayout(const StageInfo &info, const std::string &type,
              std::vector<uint32_t> *memberOffsets = nullptr);

TL varLayout(TL element, uint32_t count) {
  if (count == 0)
    return element;
  const uint32_t align = roundUp(element.align, 16);
  return {align, count * roundUp(element.size, align)};
}

TL typeLayout(const StageInfo &info, const std::string &type,
              std::vector<uint32_t> *memberOffsets) {
  TL basic;
  if (basicLayout(type, basic))
    return basic;
  auto it = info.structs.find(type);
  if (it == info.structs.end()) {
    LOG_ERROR("Vulkan shader: unsupported uniform type '%s'", type.c_str());
    return TL{};
  }
  uint32_t offset = 0, maxAlign = 4;
  for (const Member &m : it->second.members) {
    const TL v = varLayout(typeLayout(info, m.type),
                           arrayCount(info, m.arrayExpr));
    offset = roundUp(offset, v.align);
    if (memberOffsets)
      memberOffsets->push_back(offset);
    offset += v.size;
    maxAlign = std::max(maxAlign, v.align);
  }
  const uint32_t align = roundUp(maxAlign, 16);
  return {align, roundUp(offset, align)};
}

void emitLeaves(const StageInfo &info, const std::string &name,
                const std::string &type, uint32_t count, uint32_t base,
                int stage,
                std::unordered_map<std::string, std::array<int32_t, 2>> &out) {
  auto set = [&](const std::string &n, uint32_t off) {
    auto it = out.try_emplace(n, std::array<int32_t, 2>{-1, -1}).first;
    it->second[stage] = static_cast<int32_t>(off);
  };
  const TL element = typeLayout(info, type);
  const uint32_t stride =
      count ? roundUp(element.size, roundUp(element.align, 16)) : 0;
  const bool isStruct = info.structs.count(type) != 0;
  if (count && !isStruct)
    set(name, base); // `foo` addresses `foo[0]`, as in GL
  for (uint32_t k = 0; k < std::max(count, 1u); ++k) {
    const std::string nm = count ? name + "[" + std::to_string(k) + "]" : name;
    const uint32_t off = base + k * stride;
    if (isStruct) {
      std::vector<uint32_t> offsets;
      typeLayout(info, type, &offsets);
      const auto &members = info.structs.at(type).members;
      for (size_t i = 0; i < members.size(); ++i)
        emitLeaves(info, nm + "." + members[i].name, members[i].type,
                   arrayCount(info, members[i].arrayExpr), off + offsets[i],
                   stage, out);
    } else {
      set(nm, off);
    }
  }
}

// Everything the shader object needs to remember about a rewritten program.
struct Reflection {
  std::vector<VulkanShader::BlockInfo> blocks;
  std::array<VulkanShader::LooseBlock, 2> loose;
  std::unordered_map<std::string, std::array<int32_t, 2>> uniforms;
  std::vector<VulkanShader::SamplerInfo> samplers;
  api::VertexLayout instanceLayout;
};

void applyEdits(std::string &s, std::vector<Edit> edits) {
  std::sort(edits.begin(), edits.end(),
            [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
  for (const Edit &e : edits)
    s.replace(e.begin, e.end - e.begin, e.text);
}

std::string replaceAll(std::string s, const std::string &from,
                       const std::string &to) {
  const std::regex re("\\b" + from + "\\b");
  return std::regex_replace(s, re, to);
}

// Rewrites one stage given the global binding/location assignments.
std::string rewriteStage(const StageInfo &info, int stage, // 0 = vertex
                         const std::map<std::string, uint32_t> &blockBinding,
                         uint32_t looseBinding,
                         const std::map<std::string, uint32_t> &samplerBinding,
                         const std::map<std::string, uint32_t> &ioLocation) {
  std::vector<Edit> edits;

  for (const auto &b : info.blocks) {
    edits.push_back(
        {b.stmt.begin, b.stmt.begin + b.headLength,
         "layout(std140, set = 0, binding = " +
             std::to_string(blockBinding.at(b.name)) + ") uniform " + b.name +
             " "});
  }

  if (!info.looseStmts.empty()) {
    std::string block = "layout(std140, set = 0, binding = " +
                        std::to_string(looseBinding) + ") uniform Loose" +
                        (stage == 0 ? "Vertex" : "Fragment") + " {\n";
    for (const Member &m : info.loose)
      block += "  " + m.type + " " + m.name +
               (m.arrayExpr.empty() ? "" : "[" + m.arrayExpr + "]") + ";\n";
    block += "};\n";
    for (size_t i = 0; i < info.looseStmts.size(); ++i) {
      const Stmt &s = info.looseStmts[i];
      edits.push_back(
          {s.begin, s.end, i + 1 == info.looseStmts.size() ? block : ""});
    }
  }

  for (const auto &s : info.samplers) {
    edits.push_back({s.stmt.begin, s.stmt.end,
                     "layout(set = 0, binding = " +
                         std::to_string(samplerBinding.at(s.name)) +
                         ") uniform " + s.type + " " + s.name + ";"});
  }

  uint32_t fragOut = 0;
  for (const auto &d : info.io) {
    uint32_t loc;
    if (stage == 1 && d.out)
      loc = fragOut++;
    else
      loc = ioLocation.at(d.name);
    edits.push_back({d.stmt.begin, d.stmt.end,
                     "layout(location = " + std::to_string(loc) + ") " +
                         (d.qualifier.empty() ? "" : d.qualifier + " ") +
                         (d.out ? "out " : "in ") + d.type + " " + d.name +
                         ";"});
  }

  std::string out = info.src;
  applyEdits(out, std::move(edits));

  out = replaceAll(out, "gl_VertexID", "gl_VertexIndex");
  out = replaceAll(out, "gl_InstanceID", "gl_InstanceIndex");

  if (stage == 0) {
    // GL clip space is z in [-w, w], Vulkan's is [0, w]; fix it up after the
    // shader's own main. (Y is handled by a flipped viewport.)
    static const std::regex mainRe(R"(void\s+main\s*\(\s*(?:void)?\s*\))");
    out = std::regex_replace(out, mainRe, "void engine_gl_main()");
    out += "\nvoid main() {\n  engine_gl_main();\n  gl_Position.z = "
           "(gl_Position.z + gl_Position.w) * 0.5;\n}\n";
  }
  return out;
}

bool prepareSources(const std::string &vsIn, const std::string &fsIn,
                    Reflection &r, std::string &vsOut, std::string &fsOut) {
  const StageInfo stages[2] = {parseStage(vsIn), parseStage(fsIn)};

  uint32_t next = 0;
  std::map<std::string, uint32_t> blockBinding;
  for (const StageInfo &s : stages)
    for (const auto &b : s.blocks)
      if (blockBinding.try_emplace(b.name, next).second) {
        r.blocks.push_back({b.name, next, UINT32_MAX});
        ++next;
      }

  uint32_t looseBinding[2] = {UINT32_MAX, UINT32_MAX};
  for (int i = 0; i < 2; ++i) {
    if (stages[i].loose.empty())
      continue;
    looseBinding[i] = next++;
    r.loose[i].binding = looseBinding[i];
    uint32_t offset = 0;
    for (const Member &m : stages[i].loose) {
      const uint32_t count = arrayCount(stages[i], m.arrayExpr);
      const TL v = varLayout(typeLayout(stages[i], m.type), count);
      offset = roundUp(offset, v.align);
      emitLeaves(stages[i], m.name, m.type, count, offset, i, r.uniforms);
      offset += v.size;
    }
    r.loose[i].size = std::max(roundUp(offset, 16), 16u);
    r.loose[i].data.assign(r.loose[i].size, 0);
  }

  std::map<std::string, uint32_t> samplerBinding;
  for (const StageInfo &s : stages)
    for (const auto &d : s.samplers) {
      if (d.type != "sampler2D" && d.type != "samplerBuffer") {
        LOG_ERROR("Vulkan shader: sampler type '%s' is not supported (%s)",
                  d.type.c_str(), d.name.c_str());
        return false;
      }
      if (samplerBinding.try_emplace(d.name, next).second) {
        r.samplers.push_back({d.name, next, d.type == "samplerBuffer", 0});
        ++next;
      }
    }

  // Varyings: vertex outputs numbered in declaration order, fragment inputs
  // matched to them by name (the two stages may declare them in any order).
  std::map<std::string, uint32_t> ioLocation;
  uint32_t loc = 0;
  for (const auto &d : stages[0].io)
    if (d.out)
      ioLocation[d.name] = loc++;
  for (const auto &d : stages[1].io)
    if (!d.out && !ioLocation.count(d.name))
      ioLocation[d.name] = loc++;
  // Vertex inputs that carry no layout(location) are not supported: GL code in
  // this engine always writes them.
  std::map<std::string, uint32_t> vsIoOnly;
  std::map<std::string, uint32_t> vsLoc;
  for (const auto &d : stages[0].io) {
    if (!d.out) {
      LOG_ERROR("Vulkan shader: vertex input '%s' needs layout(location)",
                d.name.c_str());
      return false;
    }
  }

  vsOut = rewriteStage(stages[0], 0, blockBinding, looseBinding[0],
                       samplerBinding, ioLocation);
  fsOut = rewriteStage(stages[1], 1, blockBinding, looseBinding[1],
                       samplerBinding, ioLocation);

  // Per-instance attribute layout, as GLShader::reflectInstanceLayout does.
  std::vector<StageInfo::Attrib> attribs = stages[0].attribs;
  std::sort(attribs.begin(), attribs.end(),
            [](const auto &a, const auto &b) { return a.location < b.location; });
  uint32_t offset = 0;
  for (const auto &a : attribs) {
    if (a.location < static_cast<int>(api::kFirstInstanceAttribLocation))
      continue;
    uint32_t comps = 0;
    if (a.type == "float")
      comps = 1;
    else if (a.type == "vec2")
      comps = 2;
    else if (a.type == "vec3")
      comps = 3;
    else if (a.type == "vec4")
      comps = 4;
    if (a.type == "mat4") {
      for (int row = 0; row < 4; ++row) {
        r.instanceLayout.attributes.push_back({a.name, offset, 4, false, row});
        offset += 16;
      }
    } else if (comps) {
      r.instanceLayout.attributes.push_back({a.name, offset, comps, false, -1});
      offset += 4 * comps;
    } else {
      LOG_ERROR("Unsupported instance attribute type for '%s'", a.name.c_str());
    }
  }
  r.instanceLayout.stride = offset;
  return true;
}

// glslang

bool compileStage(const std::string &source, EShLanguage lang,
                  std::vector<uint32_t> &spirv, const char *label) {
  static const bool initialised = glslang::InitializeProcess();
  (void)initialised;

  glslang::TShader shader(lang);
  const char *src = source.c_str();
  shader.setStrings(&src, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
  const EShMessages msgs =
      static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(GetDefaultResources(), 450, false, msgs)) {
    LOG_ERROR("Vulkan %s shader failed to compile:\n%s\n%s", label,
              shader.getInfoLog(), shader.getInfoDebugLog());
    return false;
  }
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(msgs)) {
    LOG_ERROR("Vulkan %s shader failed to link:\n%s", label, program.getInfoLog());
    return false;
  }
  spv::SpvBuildLogger logger;
  glslang::SpvOptions options;
  glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &logger, &options);
  return !spirv.empty();
}

VkShaderModule makeModule(VkDevice device, const std::vector<uint32_t> &code) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code.size() * sizeof(uint32_t);
  ci.pCode = code.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(device, &ci, nullptr, &m);
  return m;
}

} // namespace

// VulkanShader

VulkanShader::VulkanShader(VulkanRenderDevice *device,
                           std::shared_ptr<VulkanCore> core, uint64_t id)
    : m_device(device), m_core(std::move(core)), m_id(id) {
}

VulkanShader::~VulkanShader() {
  release();
}

void VulkanShader::loadGLSL(const char *vertexSource, const char *fragmentSource,
                            const char *geometrySource) {
  release();
  if (geometrySource)
    LOG_ERROR("Vulkan backend: geometry shaders are not supported, ignoring");

  Reflection r;
  std::string vs, fs;
  if (!prepareSources(vertexSource, fragmentSource ? fragmentSource : "", r, vs,
                      fs))
    return;

  std::vector<uint32_t> vsSpv, fsSpv;
  if (!compileStage(vs, EShLangVertex, vsSpv, "vertex") ||
      !compileStage(fs, EShLangFragment, fsSpv, "fragment")) {
    if (std::getenv("COMPONENG_VK_DUMP_SHADERS"))
      LOG_ERROR("---- vertex ----\n%s\n---- fragment ----\n%s", vs.c_str(),
                fs.c_str());
    return;
  }
  if (std::getenv("COMPONENG_VK_DUMP_SHADERS"))
    LOG_INFO("---- vertex ----\n%s\n---- fragment ----\n%s", vs.c_str(),
             fs.c_str());

  m_vertex = makeModule(m_core->device, vsSpv);
  m_fragment = makeModule(m_core->device, fsSpv);

  m_blocks = std::move(r.blocks);
  m_loose = std::move(r.loose);
  m_uniforms = std::move(r.uniforms);
  m_samplers = std::move(r.samplers);
  m_instanceLayout = std::move(r.instanceLayout);

  const VkShaderStageFlags both =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  for (const auto &b : m_blocks)
    bindings.push_back({b.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, both,
                        nullptr});
  for (int s = 0; s < 2; ++s)
    if (m_loose[s].binding != UINT32_MAX)
      bindings.push_back({m_loose[s].binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          1,
                          s == 0 ? VK_SHADER_STAGE_VERTEX_BIT
                                 : VK_SHADER_STAGE_FRAGMENT_BIT,
                          nullptr});
  for (const auto &s : m_samplers)
    bindings.push_back({s.binding,
                        s.texelBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                                      : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        1, both, nullptr});

  VkDescriptorSetLayoutCreateInfo li{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = static_cast<uint32_t>(bindings.size());
  li.pBindings = bindings.data();
  vkCreateDescriptorSetLayout(m_core->device, &li, nullptr, &m_setLayout);

  VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pi.setLayoutCount = 1;
  pi.pSetLayouts = &m_setLayout;
  vkCreatePipelineLayout(m_core->device, &pi, nullptr, &m_pipelineLayout);
}

void VulkanShader::loadSPIRV(const char *vertexPath, const char *fragmentPath,
                             const char *geometryPath) {
  // Reflection needs the source, so like the GL backend this takes the GLSL
  // that sits next to a ".spv" name.
  auto stripExt = [](std::string s) {
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".spv") == 0)
      s.erase(s.size() - 4);
    return s;
  };
  auto read = [](const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open())
      LOG_ERROR("Failed to open shader file: %s", path.c_str());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  const std::string v = read(stripExt(vertexPath));
  const std::string f = read(stripExt(fragmentPath));
  if (v.empty() || f.empty())
    return;
  loadGLSL(v.c_str(), f.c_str(), geometryPath ? "" : nullptr);
}

void VulkanShader::use() const {
  m_device->bindShader(this);
}

void VulkanShader::beginGpuTimer() const {
  m_device->beginShaderTimer(m_id);
}

void VulkanShader::endGpuTimer() const {
  m_device->endShaderTimer(m_id);
}

double VulkanShader::lastGpuTimeMs() const {
  return m_device->shaderTimeMs(m_id);
}

void VulkanShader::release() {
  if (m_vertex || m_fragment || m_setLayout || m_pipelineLayout) {
    VulkanCore *core = m_core.get();
    VkShaderModule vs = m_vertex, fs = m_fragment;
    VkDescriptorSetLayout sl = m_setLayout;
    VkPipelineLayout pl = m_pipelineLayout;
    core->defer([core, vs, fs, sl, pl]() {
      if (pl)
        vkDestroyPipelineLayout(core->device, pl, nullptr);
      if (sl)
        vkDestroyDescriptorSetLayout(core->device, sl, nullptr);
      if (vs)
        vkDestroyShaderModule(core->device, vs, nullptr);
      if (fs)
        vkDestroyShaderModule(core->device, fs, nullptr);
    });
  }
  m_vertex = m_fragment = VK_NULL_HANDLE;
  m_setLayout = VK_NULL_HANDLE;
  m_pipelineLayout = VK_NULL_HANDLE;
  m_blocks.clear();
  m_loose = {};
  m_uniforms.clear();
  m_samplers.clear();
  m_instanceLayout = {};
}

void VulkanShader::write(const char *name, const void *src, size_t bytes) const {
  auto it = m_uniforms.find(name);
  if (it == m_uniforms.end())
    return; // not active in this shader: GL ignores it too
  for (int s = 0; s < 2; ++s) {
    const int32_t off = it->second[s];
    if (off >= 0 && off + bytes <= m_loose[s].data.size())
      std::memcpy(m_loose[s].data.data() + off, src, bytes);
  }
}

void VulkanShader::setFloat(const char *name, float value) const {
  write(name, &value, sizeof(value));
}

void VulkanShader::setInteger(const char *name, int value) const {
  for (const SamplerInfo &s : m_samplers) {
    if (s.name == name) {
      s.unit = value;
      return;
    }
  }
  write(name, &value, sizeof(value));
}

void VulkanShader::setVector2f(const char *name, float x, float y) const {
  const float v[2] = {x, y};
  write(name, v, sizeof(v));
}

void VulkanShader::setVector3f(const char *name, float x, float y,
                               float z) const {
  const float v[3] = {x, y, z};
  write(name, v, sizeof(v));
}

void VulkanShader::setVector4f(const char *name, float x, float y, float z,
                               float w) const {
  const float v[4] = {x, y, z, w};
  write(name, v, sizeof(v));
}

void VulkanShader::setMatrix4(const char *name, const float *matrix) const {
  write(name, matrix, 16 * sizeof(float));
}

api::VertexLayout VulkanShader::reflectInstanceLayout() const {
  return m_instanceLayout;
}

bool VulkanShader::bindUniformBlock(const char *blockName, uint32_t binding,
                                    size_t /*expectedSizeBytes*/) {
  for (BlockInfo &b : m_blocks) {
    if (b.name == blockName) {
      b.engineBinding = binding;
      return true;
    }
  }
  return false;
}

std::vector<core::Name> VulkanShader::reflectActiveUniformNames() const {
  std::vector<core::Name> names;
  for (const auto &[name, offsets] : m_uniforms) {
    (void)offsets;
    names.emplace_back(name);
  }
  for (const SamplerInfo &s : m_samplers)
    names.emplace_back(s.name);
  return names;
}

} // namespace componeng::renderer::vulkan
