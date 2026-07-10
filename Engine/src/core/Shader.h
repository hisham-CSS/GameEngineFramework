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
        ~Shader();

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
        // cached glGetUniformLocation (driver lookups are expensive per frame)
        int loc_(const std::string& name) const;

        // utility function for checking shader compilation/linking errors.
        // ------------------------------------------------------------------------
        void checkCompileErrors(unsigned int shader, std::string type);

        mutable std::unordered_map<std::string, int> locations_;
        bool valid_ = true;
    };
}
