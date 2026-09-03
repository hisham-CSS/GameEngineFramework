#pragma once
#include "Core.h"
#include <glm/glm.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

namespace MyCoreEngine
{
    class ENGINE_API Shader {
    public:
        // constructor generates the shader on the fly
        Shader(const char* vertexPath, const char* fragmentPath);
        // The same two files with a preprocessor preamble injected after each
        // stage's `#version` line -- "#define SKINNED 1" is how ONE vertex.glsl
        // yields the static and the skinned program of a pass (ROADMAP M3.2e),
        // so the two can never disagree about anything but the skin.
        Shader(const char* vertexPath, const char* fragmentPath, const char* defines);
        ~Shader();

        // Route a named uniform block to a binding point (a std140 UBO bound
        // with glBindBufferBase at the same index). False if the program has
        // no such block -- a static program asked for `uBones`, say.
        bool bindUniformBlock(const std::string& blockName, unsigned binding) const;

        // GL program handles can't be shared; allow moves, forbid copies
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        unsigned int ID = 0;

        // false when a file failed to read or a stage failed to compile/link
        bool isValid() const { return valid_; }

        void use() const;
        // utility uniform functions (locations cached per name)
        // ------------------------------------------------------------------------
        void setBool(const std::string& name, bool value) const;
        // ------------------------------------------------------------------------
        void setInt(const std::string& name, int value) const;
        // ------------------------------------------------------------------------
        void setFloat(const std::string& name, float value) const;
        // ------------------------------------------------------------------------
        void setVec2(const std::string& name, const glm::vec2& value) const;
        void setVec2(const std::string& name, float x, float y) const;
        // ------------------------------------------------------------------------
        void setVec3(const std::string& name, const glm::vec3& value) const;
        void setVec3(const std::string& name, float x, float y, float z) const;
        // ------------------------------------------------------------------------
        void setVec4(const std::string& name, const glm::vec4& value) const;
        void setVec4(const std::string& name, float x, float y, float z, float w) const;
        // ------------------------------------------------------------------------
        void setMat2(const std::string& name, const glm::mat2& mat) const;
        // ------------------------------------------------------------------------
        void setMat3(const std::string& name, const glm::mat3& mat) const;
        // ------------------------------------------------------------------------
        void setMat4(const std::string& name, const glm::mat4& mat) const;
    private:
        void build_(const char* vertexPath, const char* fragmentPath, const char* defines);
        // cached glGetUniformLocation (driver lookups are expensive per frame)
        int loc_(const std::string& name) const;

        // utility function for checking shader compilation/linking errors.
        // ------------------------------------------------------------------------
        void checkCompileErrors(unsigned int shader, std::string type);

        mutable std::unordered_map<std::string, int> locations_;
        bool valid_ = true;
    };
}
