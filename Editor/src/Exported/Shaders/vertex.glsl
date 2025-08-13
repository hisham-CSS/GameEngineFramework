#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout(location = 3) in mat4 iModel;  // uses 3,4,5,6

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform int uUseInstancing;           // 0 = off (default), 1 = use iModel

void main()
{
    TexCoords = aTexCoords;    
    // find the place where you build world position with 'model'
    mat4 M = (uUseInstancing != 0) ? iModel : model;

    // replace any uses of 'model' in the VS with 'M'
    vec4 worldPos = M * vec4(aPos, 1.0);
    vec3 worldN   = mat3(transpose(inverse(M))) * aNormal;

    // and your existing line that builds gl_Position should use 'M' too:
    gl_Position = projection * view * worldPos;  // if you already compute worldPos, you’re good
}