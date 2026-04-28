
#include "al/app/al_App.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

#include <fstream>
#include <vector>
using namespace std;

Vec3f randomVec3f(float scale) {
  return Vec3f(rnd::uniformS(), rnd::uniformS(), rnd::uniformS()) * scale;
}
string slurp(string fileName);  // forward declaration


struct AlloApp : App {
  Parameter pointSize{"/pointSize", "", 2.0, 1.0, 10.0};
  Parameter timeStep{"/timeStep", "", 0.1, 0.01, 0.6};
  Parameter dragFactor{"/dragFactor", "", 0.1, 0.0, 0.9};
  // 1. ADD THE PARAMETERS HERE
  Parameter springK{"/springK", "", 0.5, 0.0, 2.0};
  Parameter chargeQ{"/chargeQ", "", 0.05, 0.0, 0.5};

  ShaderProgram pointShader;
  Mesh mesh;
  vector<Vec3f> velocity;
  vector<Vec3f> force;
  vector<float> mass;

  void onInit() override {
    auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
    auto &gui = GUIdomain->newGUI();
    gui.add(pointSize);
    gui.add(timeStep);
    gui.add(dragFactor);
    // 2. REGISTER THEM SO THEY APPEAR
    gui.add(springK);
    gui.add(chargeQ);
  }

  void onCreate() override {
    pointShader.compile(slurp("../point-vertex.glsl"),
                        slurp("../point-fragment.glsl"),
                        slurp("../point-geometry.glsl"));

    auto randomColor = []() { return HSV(rnd::uniform(), 1.0f, 1.0f); };

    mesh.primitive(Mesh::POINTS);
    for (int _ = 0; _ < 500; _++) {
      mesh.vertex(randomVec3f(5));
      mesh.color(randomColor());
      float m = 3 + rnd::normal() / 2;
      if (m < 0.5) m = 0.5;
      mass.push_back(m);
      mesh.texCoord(pow(m, 1.0f / 3), 0);
      velocity.push_back(randomVec3f(0.1));
      force.push_back(Vec3f(0,0,0));
    }
    nav().pos(0, 0, 10);
  }

  bool freeze = false;
  void onAnimate(double dt) override {
    if (freeze) return;

    float targetRadius = 3.0f; 

    // --- 3. SPRING FORCE ---
    for (int i = 0; i < velocity.size(); i++) {
      auto& pos = mesh.vertices()[i];
      float currentDist = pos.mag();
      float displacement = currentDist - targetRadius;
      if (currentDist > 0.001f) {
          Vec3f direction = pos.normalized();
          force[i] -= direction * (displacement * springK);
      }
    }

    // --- 4. REPULSION FORCE (Coulomb's Law) ---
    for (int i = 0; i < mesh.vertices().size(); ++i) {
      for (int j = i + 1; j < mesh.vertices().size(); ++j) {
        Vec3f diff = mesh.vertices()[i] - mesh.vertices()[j];
        float dist = diff.mag();
        if (dist > 0.01f) {
            float pushStrength = (chargeQ * chargeQ) / (dist * dist);
            Vec3f pushForce = diff.normalized() * pushStrength;
            force[i] += pushForce;
            force[j] -= pushForce;
        }
      }
    }

    // Viscous drag
    for (int i = 0; i < velocity.size(); i++) {
      force[i] += - velocity[i] * dragFactor;
    }

    // Numerical Integration
    vector<Vec3f> &position(mesh.vertices());
    for (int i = 0; i < velocity.size(); i++) {
      velocity[i] += force[i] / mass[i] * timeStep;
      position[i] += velocity[i] * timeStep;
    }

    // clear all accelerations 
    for (auto &a : force) a.set(0);
  } // 

  bool onKeyDown(const Keyboard &k) override {
    if (k.key() == ' ') {
      freeze = !freeze;
    }

    if (k.key() == '1') {
      for (int i = 0; i < velocity.size(); i++) {
        force[i] += randomVec3f(1);
      }
    }
    return true;
  }

  void onDraw(Graphics &g) override {
    g.clear(0.3);
    g.shader(pointShader);
    g.shader().uniform("pointSize", pointSize / 100);
    g.blending(true);
    g.blendTrans();
    g.depthTesting(true);
    g.draw(mesh);
  }
}; 

int main() {
  AlloApp app;
  app.configureAudio(48000, 512, 2, 0);
  app.start();
}

string slurp(string fileName) {
  fstream file(fileName);
  string returnValue = "";
  while (file.good()) {
    string line;
    getline(file, line);
    returnValue += line + "\n";
  }
  return returnValue;
}
