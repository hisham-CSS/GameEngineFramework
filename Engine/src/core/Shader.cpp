#include <glad/glad.h>
#include "Shader.h"
#include "Core.h"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <utility>

namespace MyCoreEngine
{
    namespace {
        // Insert `defines` (one or more "#define ..." lines) right after the
        // `#version` directive, which GLSL requires to be the first line. A
        // source with no #version gets the preamble at the top.
        std::string injectDefines(const std::string& source, const char* defines) {
            if (!defines || !*defines) return source;
            const std::size_t v = source.find("#version");
            if (v == std::string::npos) return std::string(defines) + "\n" + source;
            const std::size_t eol = source.find('\n', v);
            if (eol == std::string::npos) return source + "\n" + defines + "\n";
            return source.substr(0, eol + 1) + defines + "\n" + source.substr(eol + 1);
        }
    }

    Shader::Shader(const char* vertexPath, const char* fragmentPath)
    {
        build_(vertexPath, fragmentPath, nullptr);
    }

    Shader::Shader(const char* vertexPath, const char* fragmentPath, const char* defines)
    {
        build_(vertexPath, fragmentPath, defines);
    }

    bool Shader::bindUniformBlock(const std::string& blockName, unsigned binding) const
    {
        if (!ID) return false;
        const GLuint index = glGetUniformBlockIndex(ID, blockName.c_str());
        if (index == GL_INVALID_INDEX) return false;
        glUniformBlockBinding(ID, index, binding);
        return true;
    }

    void Shader::build_(const char* vertexPath, const char* fragmentPath, const char* defines)
    {
        // 1. retrieve the vertex/fragment source code from filePath
        std::string vertexCode;
        std::string fragmentCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        // ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // open files
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            // read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            // close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // convert stream into string
            vertexCode = injectDefines(vShaderStream.str(), defines);
            fragmentCode = injectDefines(fShaderStream.str(), defines);
        }
        catch (std::ifstream::failure& e)
        {
            std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << "\n"
                      << "Vertex shader path: " << vertexPath << "\n"
                      << "Fragment shader path: " << fragmentPath << std::endl;
            valid_ = false;
            return; // nothing to compile; ID stays 0
        }
        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();
        // 2. compile shaders
        unsigned int vertex, fragment;
        // vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        // fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        // shader Program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        // delete the shaders as they're linked into our program now and no longer necessary
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    Shader::~Shader()
    {
        if (ID) glDeleteProgram(ID);
    }

    Shader::Shader(Shader&& other) noexcept
        : ID(std::exchange(other.ID, 0u)),
          locations_(std::move(other.locations_)),
          valid_(other.valid_)
    {
    }

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if (this != &other) {
            if (ID) glDeleteProgram(ID);
            ID = std::exchange(other.ID, 0u);
            locations_ = std::move(other.locations_);
            valid_ = other.valid_;
        }
        return *this;
    }

    void Shader::use() const
    {
        glUseProgram(ID);
    }

    int Shader::loc_(const std::string& name) const
    {
        auto it = locations_.find(name);
        if (it != locations_.end()) return it->second;
        const int location = glGetUniformLocation(ID, name.c_str());
        locations_.emplace(name, location);
        return location;
    }

    // utility uniform functions
    // ------------------------------------------------------------------------
    void Shader::setBool(const std::string& name, bool value) const
    {
        glUniform1i(loc_(name), (int)value);
    }
    // ------------------------------------------------------------------------
    void Shader::setInt(const std::string& name, int value) const
    {
        glUniform1i(loc_(name), value);
    }
    // ------------------------------------------------------------------------
    void Shader::setFloat(const std::string& name, float value) const
    {
        glUniform1f(loc_(name), value);
    }
    // ------------------------------------------------------------------------
    void Shader::setVec2(const std::string& name, const glm::vec2& value) const
    {
        glUniform2fv(loc_(name), 1, &value[0]);
    }
    void Shader::setVec2(const std::string& name, float x, float y) const
    {
        glUniform2f(loc_(name), x, y);
    }
    // ------------------------------------------------------------------------
    void Shader::setVec3(const std::string& name, const glm::vec3& value) const
    {
        glUniform3fv(loc_(name), 1, &value[0]);
    }
    void Shader::setVec3(const std::string& name, float x, float y, float z) const
    {
        glUniform3f(loc_(name), x, y, z);
    }
    // ------------------------------------------------------------------------
    void Shader::setVec4(const std::string& name, const glm::vec4& value) const
    {
        glUniform4fv(loc_(name), 1, &value[0]);
    }
    void Shader::setVec4(const std::string& name, float x, float y, float z, float w) const
    {
        glUniform4f(loc_(name), x, y, z, w);
    }
    // ------------------------------------------------------------------------
    void Shader::setMat2(const std::string& name, const glm::mat2& mat) const
    {
        glUniformMatrix2fv(loc_(name), 1, GL_FALSE, &mat[0][0]);
    }
    // ------------------------------------------------------------------------
    void Shader::setMat3(const std::string& name, const glm::mat3& mat) const
    {
        glUniformMatrix3fv(loc_(name), 1, GL_FALSE, &mat[0][0]);
    }
    // ------------------------------------------------------------------------
    void Shader::setMat4(const std::string& name, const glm::mat4& mat) const
    {
        glUniformMatrix4fv(loc_(name), 1, GL_FALSE, &mat[0][0]);
    }

    // utility function for checking shader compilation/linking errors.
    // ------------------------------------------------------------------------
    void Shader::checkCompileErrors(unsigned int shader, std::string type)
    {
        int success;
        char infoLog[1024];
        if (type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
                valid_ = false;
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
                valid_ = false;
            }
        }
    }
}
