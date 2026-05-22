// SPDX-FileCopyrightText: 2025 AlloSphere Research Group <allosphere@ucsb.edu>
// SPDX-License-Identifier: BSD-3-Clause
#include "al/app/al_DistributedApp.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_VAOMesh.hpp"
#include "al/io/al_File.hpp"
#include <fstream>

using namespace al;

struct WorldState {
  double time{0.};
};

struct AlloApp : DistributedAppWithState<WorldState> {
  VAOMesh quad;
  ShaderProgram *shader;
  
  ParameterPose camera{"camera", ""};
  Parameter mouseX{"mouseX", "", 0.f};
  Parameter mouseY{"mouseY", "", 0.f};

  Parameter RayMarch{"RayMarch", "", 13.0, 1.0, 50.0};
  Parameter Normal{"Normal", "", 48.0, 1.0, 100.0};
  Parameter speed{"speed", "", 1.0, 0.0, 5.0};
  Parameter weight{"weight", "", 3.0, 0.0, 10.0};
  Parameter waterdepth{"waterdepth", "", 2.1, 0.1, 10.0};
  Parameter phase{"phase", "", 6.0, 0.0, 20.0};
  Parameter dragMult{"dragMult", "", 0.048, 0.0, 0.2};

  bool load(ShaderProgram *p) {
    SearchPaths sp;
    sp.addSearchPath(".", false);
    sp.addAppPaths();
    sp.addRelativePath("..", true);
    auto vertex = sp.find("vertex.glsl");
    if (vertex.valid()) {
      auto fragment = sp.find("fragment.glsl");
      if (fragment.valid()) {
        return p->compile(slurp(vertex.filepath()), slurp(fragment.filepath()));
      }
    }
    return false;
  }

  void onInit() override
  {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(RayMarch);
    gui.add(Normal);
    gui.add(speed);
    gui.add(weight);
    gui.add(waterdepth);
    gui.add(phase);
    gui.add(dragMult);
  }

  void onCreate() override {
    shader = new ShaderProgram();
    assert(load(shader));

    lens().near(0.1).far(25).fovy(45);
    nav().pos(0, 0, 4);

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

    parameterServer() << camera << mouseX << mouseY << RayMarch << Normal << speed << weight << waterdepth << phase << dragMult;
  }

  double t = 0;
  void onAnimate(double dt) override {
    if(isPrimary()) {
      state().time += dt;
    }
    
    t += dt;
    if (t > 0.1) {
      t -= 0.1;
      auto *p = new ShaderProgram();
      if (load(p)) {
        delete shader;
        shader = p;
      }
    }
  }

  void onDraw(Graphics &g) override {
    g.clear();

    shader->use();
    shader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(),
                   (float)width() / height());
    shader->uniform("iTime", state().time);
    shader->uniform("iMouse", mouseX, mouseY);
    shader->uniform("u_RayMarch", (int)RayMarch.get());
    shader->uniform("u_Normal", (int)Normal.get());
    shader->uniform("u_speed", speed.get());
    shader->uniform("u_weight", weight.get());
    shader->uniform("u_phase", phase.get());
    shader->uniform("u_waterdepth", waterdepth.get());
    shader->uniform("u_dragMult", dragMult.get());
    shader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
    shader->uniform("foc_len", g.lens().focalLength());
    shader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
    shader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
    shader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));

    // bypassing graphics class with direct rendering calls
    // no projection involved
    quad.draw();
  }

  bool onMouseMove(const Mouse &m) override {
    mouseX = m.x();
    mouseY = m.y();
    return true;
  }

  std::string slurp(std::string fileName) {
    std::fstream file(fileName);

    // This new part checks if the file actually exists and opened correctly!
    if (!file.is_open()) {
      std::cout << "ERROR: Could not open file: " << fileName << std::endl;
      return "";
    }

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
  app.start();
}
