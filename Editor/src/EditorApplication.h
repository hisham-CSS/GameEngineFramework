class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

    void HandleShaderError(unsigned int shader);

    void Cleanup();

    void ProcessInput(GLFWwindow* window);

private:
    GLFWwindow* window;

};