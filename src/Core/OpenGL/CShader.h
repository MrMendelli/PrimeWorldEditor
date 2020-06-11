#ifndef CSHADER_H
#define CSHADER_H

#include <Common/TString.h>
#include <GL/glew.h>

class CShader
{
    bool mVertexShaderExists = false;
    bool mPixelShaderExists = false;
    bool mProgramExists = false;
    GLuint mVertexShader = 0;
    GLuint mPixelShader = 0;
    GLuint mProgram = 0;

    GLuint mMVPBlockIndex = 0;
    GLuint mVertexBlockIndex = 0;
    GLuint mPixelBlockIndex = 0;
    GLuint mLightBlockIndex = 0;
    GLuint mBoneTransformBlockIndex = 0;

    // Cached uniform locations
    GLint mTextureUniforms[8] = {};
    GLint mNumLightsUniform = 0;

    static int smNumShaders;
    static CShader* spCurrentShader;

public:
    CShader();
    CShader(const char* pkVertexSource, const char* pkPixelSource);
    ~CShader();
    bool CompileVertexSource(const char* pkSource);
    bool CompilePixelSource(const char* pkSource);
    bool LinkShaders();
    bool IsValidProgram();
    GLuint GetProgramID();
    GLuint GetUniformLocation(const char* pkUniform);
    GLuint GetUniformBlockIndex(const char* pkUniformBlock);
    void UniformBlockBinding(GLuint BlockIndex, GLuint BlockBinding);
    void SetTextureUniforms(uint32 NumTextures);
    void SetNumLights(uint32 NumLights);
    void SetCurrent();

    // Static
    static CShader* FromResourceFile(const TString& rkShaderName);
    static CShader* CurrentShader();
    static void KillCachedShader();

    static int NumShaders() { return smNumShaders; }

private:
    void CacheCommonUniforms();
    void DumpShaderSource(GLuint Shader, const TString& rkOut);
};

#endif // CSHADER_H
