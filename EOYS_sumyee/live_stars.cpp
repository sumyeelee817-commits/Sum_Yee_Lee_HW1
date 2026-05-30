// SPDX-FileCopyrightText: 2025 AlloSphere Research Group <allosphere@ucsb.edu>
#include "al/app/al_DistributedApp.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/graphics/al_VAOMesh.hpp"
#include "al/io/al_File.hpp"
#include "Gamma/SamplePlayer.h"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
#include <fstream>
#include <iostream>

using namespace al;

struct WorldState {
  double time{0.};
};

struct AlloApp : DistributedAppWithState<WorldState> {
  VAOMesh quad;
  
  // 1. Replaced the two shaders and FBO variables with a single unified shader
  ShaderProgram *starsShader; 

  gam::SamplePlayer<float, gam::ipl::Linear, gam::phsInc::OneShot> player;
  ParameterPose camera{"camera", ""};

  void checkGLError(const std::string& location) {
      GLenum err;
      while((err = glGetError()) != GL_NO_ERROR) {
          std::cerr << ">> OPENGL ERROR at [" << location << "]: " << err << std::endl;
      }
  }

  bool load(ShaderProgram *p, std::string fragFileName) {
    SearchPaths sp;
    sp.addSearchPath(".", false);
    sp.addAppPaths();
    sp.addRelativePath("..", true);
    
    auto vertex = sp.find("vertex.glsl");
    auto fragment = sp.find(fragFileName);

    if (!vertex.valid()) {
        std::cerr << "ERROR: Could not find 'vertex.glsl' in your directory!" << std::endl;
        return false;
    }
    if (!fragment.valid()) {
        std::cerr << "ERROR: Could not find '" << fragFileName << "' in your directory!" << std::endl;
        return false;
    }

    std::cout << "Attempting to compile: " << fragFileName << "..." << std::endl;
    bool success = p->compile(slurp(vertex.filepath()), slurp(fragment.filepath()));
    
    if (!success) {
        std::cerr << ">>> FATAL: " << fragFileName << " failed to compile! See GLSL errors above." << std::endl;
    } else {
        std::cout << ">>> SUCCESS: " << fragFileName << " compiled perfectly." << std::endl;
    }
    return success;
  }

  void onInit() override {
    auto cuttleboneDomain = CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) {
      std::cerr << "ERROR: Could not start Cuttlebone. Quitting." << std::endl;
      quit();
    }
    parameterServer() << camera;
  }

  void onCreate() override {
    // 2. Initialize and load only the single unified star shader
    starsShader = new ShaderProgram();
    
    if (!load(starsShader, "star.glsl")) quit();

    SearchPaths sp;
    sp.addSearchPath(".", false);
    sp.addAppPaths();
    sp.addRelativePath("..", true);
    auto audioFile = sp.find("sound.wav");
    if (audioFile.valid()) {
        player.load(audioFile.filepath().c_str());
        std::cout << "Loaded audio from: " << audioFile.filepath() << std::endl;
    } 
    player.pos(0);

    lens().near(0.1).far(25).fovy(80);
    nav().pos(0.966193, 2.1846, -36);
    nav().quat() = al::Quatd(-0.613572, 0, 0, 0.789638);
    
    quad.primitive(Mesh::TRIANGLE_STRIP);
    quad.vertex(-1.f, -1.f, 0);
    quad.vertex(1.f, -1.f, 0);
    quad.vertex(-1.f, 1.f, 0);
    quad.vertex(1.f, 1.f, 0);
    quad.texCoord(0, 0);
    quad.texCoord(1, 0);
    quad.texCoord(0, 1);
    quad.texCoord(1, 1);
    quad.update();

    state().time = 0.f;
  }

  void onAnimate(double dt) override {
    if(isPrimary()) {
      state().time += dt;
      camera.set(nav());
    } else {
      nav().set(camera.get());
    }
  }

  void onSound(AudioIOData &io) override {
    while (io()) {
      float sample = player(); 
      io.out(0) = sample; 
      io.out(1) = sample; 
    }
  }

  void onDraw(Graphics &g) override {
    // 3. Stripped out all the complex FBO setup and multi-pass logic
    g.depthTesting(false); 
    g.blending(false); 
    g.clearColor(0, 0, 0, 1);
    g.clear();

    // 4. Render the unified shader in a single clean pass
    starsShader->use();
    starsShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / (float)height());
    starsShader->uniform("iTime", (float)state().time); 
    starsShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
    starsShader->uniform("foc_len", g.lens().focalLength());
    starsShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
    starsShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
    starsShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));

    quad.draw();
  }

  bool onKeyDown(const Keyboard &k) override {
    if (k.key() == 'p') {
        std::cout << "\n// --- COPY THESE LINES INTO onCreate() ---" << std::endl;
        std::cout << "nav().pos(" << nav().pos().x << ", " << nav().pos().y << ", " << nav().pos().z << ");" << std::endl;
        std::cout << "nav().quat() = al::Quatd(" << nav().quat().w << ", " << nav().quat().x << ", " << nav().quat().y << ", " << nav().quat().z << ");" << std::endl;
        std::cout << "// ----------------------------------------\n" << std::endl;
    }
    return true;
  }
  
  std::string slurp(std::string fileName) {
    std::fstream file(fileName);
    if (!file.is_open()) return "";
    std::string returnValue = "";
    while (file.good()) {
      std::string line;
      getline(file, line);
      returnValue += line + "\n";
    }
    return returnValue;
  }
};

int main() {
  AlloApp app;
  app.dimensions(600, 400);
  app.configureAudio(48000, 512, 2, 0);
  app.start();
}