#pragma once
#include "Core.h"

namespace MyCoreEngine
{
	class ENGINE_API Application
	{
	public:
		Application();
		virtual ~Application();
		virtual void Run();
		virtual void SomeFunc();
		virtual void NewFunc();
	};

	//defined in other projects
	Application* CreateApplication();
}