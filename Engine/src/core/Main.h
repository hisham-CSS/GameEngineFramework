#pragma once

#ifdef _WIN32

extern MyCoreEngine::Application* MyCoreEngine::CreateApplication();

int main(int argc, char** argv)
{
	auto app = MyCoreEngine::CreateApplication();
	app->Run();
	delete app;
}

#endif