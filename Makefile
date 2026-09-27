# GNU Make 4.x. Windows: run from an x64 Native Tools Command Prompt.
.DEFAULT_GOAL := all
VULKAN ?= 0
CONFIG ?= debug
BUILD ?= build/$(CONFIG)-vk$(VULKAN)
PROJECT_HEADERS := $(wildcard src/*.h src/*/*.h src/*/*/*.h src/*/*/*/*.h third_party/entt/src/entt/*.hpp third_party/entt/src/entt/*/*.hpp third_party/entt/src/entt/*/*/*.hpp third_party/lua/*.h third_party/glfw/include/GLFW/*.h third_party/picojson/*.h)
ENGINE_SRC := $(filter-out src/main.cpp,$(wildcard src/*.cpp src/async/*.cpp src/components/*.cpp src/core/*.cpp src/ecs/*.cpp src/ecs/core/*.cpp src/ecs/jobs/*.cpp src/ecs/rendering/*.cpp src/functional/*.cpp src/interaction/*.cpp src/logging/*.cpp src/automation/*.cpp src/editor/*.cpp src/project/*.cpp src/rendering/*.cpp src/runtime/*.cpp src/scripting/*.cpp src/structs/*.cpp src/tooling/*.cpp src/util/*.cpp src/vulcan/*.cpp))
LUA_SRC := $(filter-out third_party/lua/lua.c third_party/lua/onelua.c third_party/lua/ltests.c,$(wildcard third_party/lua/*.c))
GLFW_COMMON := context init input monitor platform vulkan window egl_context osmesa_context null_init null_monitor null_window null_joystick
ECS_TEST_SRC := src/test/ecs_tests.cpp $(wildcard src/test/ecs/*.cpp) src/test/rendering/shader_tests.cpp src/test/rendering/render_system_integration_tests.cpp
LUA_TEST_SRC := $(wildcard src/test/scripting/*.cpp)
PROJECT_TEST_SRC := $(filter-out src/test/project/runtime_tests.cpp,$(wildcard src/test/project/*.cpp))
RUNTIME_TEST_SRC := $(wildcard src/test/project/runtime_tests.cpp)
EDITOR_TEST_SRC := $(wildcard src/test/editor/*.cpp)
WORKFLOW_TEST_SRC := $(wildcard src/test/tooling/*.cpp src/test/automation/*.cpp)
INCLUDES := -Isrc -Ithird_party/entt/src -Ithird_party/lua -Ithird_party/glfw/include
DEFINES := -DENTT_USE_ATOMIC

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /C
CXX := cl
CC := cl
OBJEXT := obj
EXE := .exe
GLFW_PLATFORM := win32_module win32_time win32_thread win32_init win32_joystick win32_monitor win32_window wgl_context
GLFW_DEFINE := /D_GLFW_WIN32 /D_CRT_SECURE_NO_WARNINGS
CXXFLAGS := /nologo /std:c++latest /EHsc /W4 /permissive- /wd4100 /wd4189 /FS
CFLAGS := /nologo /std:c11 /W3 /D_CRT_SECURE_NO_WARNINGS
ifeq ($(CONFIG),release)
CXXFLAGS += /O2 /DNDEBUG
CFLAGS += /O2 /DNDEBUG
else
CXXFLAGS += /Od /Z7
CFLAGS += /Od /Z7
endif
LDLIBS := user32.lib gdi32.lib shell32.lib
MKDIR = if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
CREATE_BUILD = if not exist "$(subst /,\,$@)" mkdir "$(subst /,\,$@)"
SAVE_OPTIONS = fc /B "$(subst /,\,$@)" "$(subst /,\,$@.tmp)" >nul 2>&1 || copy /Y "$(subst /,\,$@.tmp)" "$(subst /,\,$@)" >nul
REMOVE_OPTIONS = del "$(subst /,\,$@.tmp)"
COMPILE_CPP = $(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) /Fd"$(BUILD)/compile.pdb" /c "$<" /Fo"$@"
COMPILE_C = $(CC) $(CFLAGS) $(INCLUDES) /Fd"$(BUILD)/c-compile.pdb" /FS /c "$<" /Fo"$@"
LINK = $(CXX) /nologo /Fe"$@" $(filter %.$(OBJEXT),$^) /link /DEBUG /PDB:"$@.pdb" $(LDLIBS)
RUN = $(subst /,\,$<)
ifeq ($(VULKAN),1)
ifndef VULKAN_SDK
$(error Set VULKAN_SDK to your Vulkan SDK directory when VULKAN=1)
endif
INCLUDES += -I"$(VULKAN_SDK)/Include"
LDLIBS += "$(VULKAN_SDK)/Lib/vulkan-1.lib"
GLSLC ?= "$(VULKAN_SDK)/Bin/glslc.exe"
endif
else
OBJEXT := o
EXE :=
GLFW_PLATFORM := posix_module posix_time posix_thread posix_poll linux_joystick x11_init x11_monitor x11_window xkb_unicode glx_context
GLFW_DEFINE := -D_GLFW_X11 -D_DEFAULT_SOURCE
CXXFLAGS := -std=c++23 -Wall -Wextra -Wno-unused-parameter -pthread -MMD -MP
CFLAGS := -std=c11 -Wall -D_DEFAULT_SOURCE -MMD -MP
ifeq ($(CONFIG),release)
CXXFLAGS += -O2 -DNDEBUG
CFLAGS += -O2 -DNDEBUG
else
CXXFLAGS += -O0 -g
CFLAGS += -O0 -g
endif
LDLIBS := -pthread -ldl -lm -lX11
MKDIR = mkdir -p "$(dir $@)"
CREATE_BUILD = mkdir -p "$@"
SAVE_OPTIONS = cmp -s "$@" "$@.tmp" || cp "$@.tmp" "$@"
REMOVE_OPTIONS = rm -f "$@.tmp"
COMPILE_CPP = $(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) -c "$<" -o "$@"
COMPILE_C = $(CC) $(CFLAGS) $(INCLUDES) -c "$<" -o "$@"
LINK = $(CXX) -o "$@" $(filter %.$(OBJEXT),$^) $(LDLIBS)
RUN = ./$<
ifeq ($(VULKAN),1)
LDLIBS += -lvulkan
endif
GLSLC ?= glslc
endif
ifeq ($(VULKAN),1)
DEFINES += -DCPP_GAME_ENGINE_USE_VULKAN=1
endif

GLFW_SRC := $(addprefix third_party/glfw/src/,$(addsuffix .c,$(GLFW_COMMON) $(GLFW_PLATFORM)))
objects = $(addprefix $(BUILD)/obj/,$(addsuffix .$(OBJEXT),$(basename $(1))))
COMMON_OBJ := $(call objects,$(ENGINE_SRC) $(LUA_SRC) $(GLFW_SRC))
ENGINE := $(BUILD)/bin/CPPGameEngine$(EXE)
ECS_TEST := $(BUILD)/bin/EcsTests$(EXE)
LUA_TEST := $(BUILD)/bin/LuaTests$(EXE)
ENGINE_TEST := $(BUILD)/bin/EngineTests$(EXE)
PROJECT_TEST := $(BUILD)/bin/ProjectTests$(EXE)
RUNTIME_TEST := $(BUILD)/bin/RuntimeTests$(EXE)
EDITOR_TEST := $(BUILD)/bin/EditorTests$(EXE)
WORKFLOW_TEST := $(BUILD)/bin/WorkflowTests$(EXE)
SMOKE := $(BUILD)/bin/VulkanSmoke$(EXE)
SHADERS := $(BUILD)/shaders/triangle.vert.spv $(BUILD)/shaders/triangle.frag.spv $(BUILD)/shaders/mesh.vert.spv $(BUILD)/shaders/mesh_textured.vert.spv $(BUILD)/shaders/mesh_textured.frag.spv
.PHONY: all test shaders smoke smoke-validation run clean
.PHONY: FORCE
FORCE:
$(BUILD):
	@$(CREATE_BUILD)
$(BUILD)/build-options.txt: FORCE | $(BUILD)
	$(file >$@.tmp,$(CXX) $(CXXFLAGS) $(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) $(GLFW_DEFINE) $(LDLIBS))
	@$(SAVE_OPTIONS)
	@$(REMOVE_OPTIONS)
all: $(ENGINE) $(if $(filter 1,$(VULKAN)),$(SHADERS))
$(ENGINE): $(call objects,src/main.cpp) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(ECS_TEST): $(call objects,$(ECS_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(LUA_TEST): $(call objects,$(LUA_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(ENGINE_TEST): $(call objects,src/test/core/engine_scripting_tests.cpp) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(PROJECT_TEST): $(call objects,$(PROJECT_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(RUNTIME_TEST): $(call objects,$(RUNTIME_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(EDITOR_TEST): $(call objects,$(EDITOR_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(WORKFLOW_TEST): $(call objects,$(WORKFLOW_TEST_SRC)) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(SMOKE): $(call objects,src/test/rendering/vulkan_smoke.cpp) $(COMMON_OBJ)
	@$(MKDIR)
	$(LINK)
$(BUILD)/obj/%.$(OBJEXT): %.cpp $(PROJECT_HEADERS) Makefile $(BUILD)/build-options.txt
	@$(MKDIR)
	$(COMPILE_CPP)
$(BUILD)/obj/third_party/lua/%.$(OBJEXT): third_party/lua/%.c $(wildcard third_party/lua/*.h) Makefile $(BUILD)/build-options.txt
	@$(MKDIR)
	$(COMPILE_C)
$(BUILD)/obj/third_party/glfw/src/%.$(OBJEXT): third_party/glfw/src/%.c $(wildcard third_party/glfw/src/*.h third_party/glfw/include/GLFW/*.h) Makefile $(BUILD)/build-options.txt
	@$(MKDIR)
	$(COMPILE_C) $(GLFW_DEFINE)
$(BUILD)/shaders/%.spv: assets/shaders/%
	@$(MKDIR)
	$(GLSLC) "$<" -o "$@"
shaders: $(SHADERS)
test: $(ECS_TEST) $(LUA_TEST) $(ENGINE_TEST) $(PROJECT_TEST) $(RUNTIME_TEST) $(EDITOR_TEST) $(WORKFLOW_TEST)
	"$(ECS_TEST)"
	"$(LUA_TEST)"
	"$(ENGINE_TEST)"
	"$(PROJECT_TEST)"
	"$(RUNTIME_TEST)"
	"$(EDITOR_TEST)"
	"$(WORKFLOW_TEST)"
smoke: $(SMOKE) $(SHADERS)
	"$(SMOKE)" "$(BUILD)/shaders"
smoke-validation: $(SMOKE) $(SHADERS)
	"$(SMOKE)" "$(BUILD)/shaders" --validation
run: all
	"$(ENGINE)" run examples/first-project/project.json $(if $(filter 1,$(VULKAN)),--shaders "$(BUILD)/shaders",--headless --ticks 120)
# Deliberately do not recursively delete a configurable path. Remove build/ manually.
clean:
	@echo Remove the build directory to clean all configurations.
TEST_OBJ := $(call objects,$(ECS_TEST_SRC) $(LUA_TEST_SRC) $(PROJECT_TEST_SRC) $(RUNTIME_TEST_SRC) $(EDITOR_TEST_SRC) $(WORKFLOW_TEST_SRC) src/main.cpp src/test/core/engine_scripting_tests.cpp src/test/rendering/vulkan_smoke.cpp)
-include $(COMMON_OBJ:.$(OBJEXT)=.d) $(TEST_OBJ:.$(OBJEXT)=.d)
