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
	};

	//defined in other projects
	Application* CreateApplication();
}