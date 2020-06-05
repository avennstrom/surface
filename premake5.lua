solution "surface"
	platforms { "Win64" }
	configurations { "Debug", "Release", "Retail" }

project "surface"
	language "C++"
	kind "ConsoleApp"
	targetname "surface"
	targetdir "bin"
	objdir "build/%{cfg.shortname}"
	targetsuffix "_%{cfg.buildcfg}"
	architecture "x86_64"
	debugdir "%{cfg.targetdir}"
	cppdialect "C++latest"

	links {
		"noise",
		"vulkan-1",
	}

	files {
		"src/*.hpp",
		"src/*.cpp",
		"src/shaders/*.vert",
		"src/shaders/*.frag",
		"src/shaders/*.comp",
		"src/shaders/*.glsl",
		"src/tracy/TracyClient.cpp",
		"src/FastNoiseSIMD/*.hpp",
		"src/FastNoiseSIMD/*.cpp",
	}

	local vulkanSdkPath = os.getenv('VK_SDK_PATH')

	includedirs {
		"src",
		"extlib/libnoise/include",
		"extlib/glm",
		vulkanSdkPath .. "/Include",
	}

	libdirs {
		vulkanSdkPath .. "/Lib",
	}

	defines {
		"NOMINMAX",
		"_CRT_SECURE_NO_WARNINGS",
		"WIN32_LEAN_AND_MEAN",
	}

	flags {
		"FatalWarnings",
	}

	warnings "Extra"

	prebuildcommands {
		--'mkdir "%{cfg.targetdir}/shaders"',
	}

	buildoptions {
		"/wd4324",
	}

	filter "files:**.glsl"
		buildaction "None"

	-- Compute Shaders
	filter "files:**.comp"
		buildmessage "Building compute shader %{file.name}"
		buildcommands {
			'glslangValidator -V -o "%{cfg.targetdir}/shaders/%{file.basename}_cs" %{file.path}',
		}
		buildoutputs "%{cfg.targetdir}/shaders/%{file.basename}_cs"
		buildinputs {
			"%{cfg.projectdir}/src/shaders/utils.glsl",
		}

	-- Vertex Shaders
	filter "files:**.vert"
		buildmessage "Building vertex shader %{file.name}"
		buildcommands {
			'glslangValidator -V -o "%{cfg.targetdir}/shaders/%{file.basename}_vs" %{file.path}',
		}
		buildoutputs "%{cfg.targetdir}/shaders/%{file.basename}_vs"
		buildinputs {
			"%{cfg.projectdir}/src/shaders/%{file.basename}.glsl",
			"%{cfg.projectdir}/src/shaders/utils.glsl",
		}
		

	-- Pixel Shaders
	filter "files:**.frag"
		buildmessage "Building pixel shader %{file.name}"
		buildcommands {
			'glslangValidator -V -o "%{cfg.targetdir}/shaders/%{file.basename}_ps" %{file.path}',
		}
		buildoutputs "%{cfg.targetdir}/shaders/%{file.basename}_ps"
		buildinputs {
			"%{cfg.projectdir}/src/shaders/%{file.basename}.glsl",
			"%{cfg.projectdir}/src/shaders/utils.glsl",
		}

	filter "files:FastNoiseSIMD_avx2.cpp"
		buildoptions { "/arch:AVX" }

	filter "configurations:Debug"
		defines { 
			"_DEBUG",
			"CONFIG_DEBUG",
		}
		libdirs {
			"extlib/libnoise/lib/%{cfg.platform}/Debug/",
		}
		optimize "Off"
		symbols "On"

	filter "configurations:not Debug"
		defines { "NDEBUG" }
		libdirs {
			"extlib/libnoise/lib/%{cfg.platform}/Release/",
		}

	filter "configurations:Release"
		defines {
			"CONFIG_RELEASE",
		}
		optimize "On"
		symbols "On"

	filter "configurations:Retail"
		defines {
			"CONFIG_RETAIL",
		}
		kind "WindowedApp"
		optimize "Full"
		symbols "Off"

	filter "configurations:not Retail"
		defines {
			"TRACY_ENABLE",
			"TRACY_ON_DEMAND",
		}