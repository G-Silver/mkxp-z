/*
** globalstate.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "globalstate.h"

#include "util.h"
#include "filesystem.h"
#include "graphics.h"
#include "input.h"
#include "audio.h"
#include "glstate.h"
#include "shader.h"
#include "texpool.h"
#include "font.h"
#include "eventthread.h"
#include "gl-util.h"
#include "global-ibo.h"
#include "binding.h"

#include <QFile>

#include <unistd.h>

#include <QDebug>

GlobalState *GlobalState::instance = 0;
static GlobalIBO *globalIBO = 0;

#define GAME_ARCHIVE "Game.rgssad"

struct GlobalStatePrivate
{
	void *bindingData;
	SDL_Window *sdlWindow;
	Scene *screen;

	FileSystem fileSystem;

	EventThread &eThread;
	RGSSThreadData &rtData;
	Config &config;

	Graphics graphics;
	Input input;
	Audio audio;

	GLState _glState;

	SpriteShader spriteShader;
	TransShader transShader;
	SimpleTransShader sTransShader;
	HueShader hueShader;
	BltShader bltShader;

	TexPool texPool;
	FontPool fontPool;

	Font *defaultFont;

	Tex::ID globalTex;
	int globalTexW, globalTexH;

	TexFBO gpTexFBO;

	unsigned int stampCounter;

	GlobalStatePrivate(RGSSThreadData *threadData)
	    : bindingData(0),
	      sdlWindow(threadData->window),
	      fileSystem(threadData->argv0),
	      eThread(*threadData->ethread),
	      rtData(*threadData),
	      config(threadData->config),
	      graphics(threadData),
	      stampCounter(0)
	{
		if (!config.gameFolder.isEmpty())
		{
			int unused = chdir(config.gameFolder.constData());
			(void) unused;
			fileSystem.addPath(config.gameFolder.constData());
		}

		// FIXME find out correct archive filename
		QByteArray archPath = threadData->config.gameFolder + "/" GAME_ARCHIVE;

		if (QFile::exists(archPath.constData()))
			fileSystem.addPath(archPath.constData());

		for (int i = 0; i < config.rtps.count(); ++i)
			fileSystem.addPath(config.rtps[i].constData());

		globalTexW = 128;
		globalTexH = 64;

		globalTex = Tex::gen();
		Tex::bind(globalTex);
		Tex::setRepeat(false);
		Tex::setSmooth(false);
		Tex::allocEmpty(globalTexW, globalTexH);

		TexFBO::init(gpTexFBO);
		/* Reuse starting values */
		TexFBO::allocEmpty(gpTexFBO, globalTexW, globalTexH);
		TexFBO::linkFBO(gpTexFBO);
	}

	~GlobalStatePrivate()
	{
		Tex::del(globalTex);
		TexFBO::fini(gpTexFBO);
	}
};

void GlobalState::initInstance(RGSSThreadData *threadData)
{
	globalIBO = new GlobalIBO();
	globalIBO->ensureSize(1);

	GlobalState *state = new GlobalState(threadData);

	GlobalState::instance = state;

	state->p->defaultFont = new Font();
}

void GlobalState::finiInstance()
{
	delete GlobalState::instance->p->defaultFont;

	delete GlobalState::instance;

	delete globalIBO;
}

void GlobalState::setScreen(Scene &screen)
{
	p->screen = &screen;
}

#define GSATT(type, lower) \
	type GlobalState :: lower() \
	{ \
		return p->lower; \
	}

GSATT(void*, bindingData)
GSATT(SDL_Window*, sdlWindow)
GSATT(Scene*, screen)
GSATT(FileSystem&, fileSystem)
GSATT(EventThread&, eThread)
GSATT(RGSSThreadData&, rtData)
GSATT(Config&, config)
GSATT(Graphics&, graphics)
GSATT(Input&, input)
GSATT(Audio&, audio)
GSATT(GLState&, _glState)
GSATT(SpriteShader&, spriteShader)
GSATT(TransShader&, transShader)
GSATT(SimpleTransShader&, sTransShader)
GSATT(HueShader&, hueShader)
GSATT(BltShader&, bltShader)
GSATT(TexPool&, texPool)
GSATT(FontPool&, fontPool)

void GlobalState::setBindingData(void *data)
{
	p->bindingData = data;
}

void GlobalState::ensureQuadIBO(int minSize)
{
	globalIBO->ensureSize(minSize);
}

void GlobalState::bindQuadIBO()
{
	IBO::bind(globalIBO->ibo);
}

void GlobalState::bindTex()
{
	Tex::bind(p->globalTex);
	Tex::allocEmpty(p->globalTexW, p->globalTexH);
	Tex::bindMatrix(p->globalTexW, p->globalTexH);
}

void GlobalState::ensureTexSize(int minW, int minH, Vec2 &currentSizeOut)
{
	if (minW > p->globalTexW)
		p->globalTexW = findNextPow2(minW);

	if (minH > p->globalTexH)
		p->globalTexH = findNextPow2(minH);

	currentSizeOut = Vec2(p->globalTexW, p->globalTexH);
}

TexFBO &GlobalState::gpTexFBO(int minW, int minH)
{
	bool needResize = false;

	if (minW > p->gpTexFBO.width)
	{
		p->gpTexFBO.width = findNextPow2(minW);
		needResize = true;
	}

	if (minH > p->gpTexFBO.height)
	{
		p->gpTexFBO.height = findNextPow2(minH);
		needResize = true;
	}

	if (needResize)
	{
		Tex::bind(p->gpTexFBO.tex);
		Tex::allocEmpty(p->gpTexFBO.width, p->gpTexFBO.height);
	}

	return p->gpTexFBO;
}

void GlobalState::checkShutdown()
{
	if (!p->rtData.rqTerm)
		return;

	p->rtData.rqTermAck = true;
	p->texPool.disable();
	scriptBinding->terminate();
}

Font &GlobalState::defaultFont()
{
	return *p->defaultFont;
}

unsigned int GlobalState::genTimeStamp()
{
	return p->stampCounter++;
}

GlobalState::GlobalState(RGSSThreadData *threadData)
{
	p = new GlobalStatePrivate(threadData);
	p->screen = p->graphics.getScreen();
}

GlobalState::~GlobalState()
{
	delete p;
}