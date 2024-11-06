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
	};

	//defined in other projects
	Application* CreateApplication();
}