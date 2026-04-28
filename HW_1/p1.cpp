#include <iostream>
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float rs() { return rnd::uniformS(); }

struct MyApp : public App {
    ParameterInt N{"/N", "", 10, 2, 100};
    ParameterColor color{"/color"};

    Light light;
    Material material;
    Mesh mesh;

    // Use al::Nav as the basis for each agent
    struct Agent : Nav {
        int loveIndex = -1; 
    };

    std::vector<Agent> agents; 
    int lastN = 0;

    void onInit() override {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();
        gui.add(N);
        gui.add(color);
    }

    void reset(int n) {
        agents.clear();
        agents.resize(n);
        for (int i = 0; i < n; i++) {
            auto& a = agents[i];
            
            a.pos(Vec3d(rs(), rs(), rs()) * 1.0);
            a.quat(Quatd(rs(), rs(), rs(), rs()).normalize());
            
    

            int target = rnd::uniformi(n - 1);
            if (target >= i) target++; 
            a.loveIndex = target;
        }
    }

    void onAnimate(double dt) override {
        if (N != lastN) {
            lastN = N;
            reset(N);
        }

        for (int i = 0; i < agents.size(); i++) {
            auto& me = agents[i];
            auto& love = agents[me.loveIndex];
            
            // 1. CHASING LOGIC
            
            float distToLove = (love.pos() - me.pos()).mag();
            if (distToLove > 0.2) {
                me.faceToward(love.pos(), 0.3); 
            }

            // 2. THE "SOFT BOUNDARY" not going too fast, it will be all in the center
        
            float distFromCenter = me.pos().mag();
            if (distFromCenter > 7.0) {
                Vec3d toCenter = -me.pos();
                toCenter.normalize();
                
                me.pos() += toCenter * 0.05; 
            }

            // 3. SEPARATION (Personal Space)
            Vec3d separationPush;
            for (int j = 0; j < agents.size(); j++) {
                if (i == j) continue;
                Vec3d diff = me.pos() - agents[j].pos();
                float dist = diff.mag();
                // If they are closer than 0.5 units, push them apart
                if (dist < 0.5 && dist > 0.0001) {
                    separationPush += (diff / dist) * (0.5 - dist);
                }
            }
            me.pos() += separationPush * 0.1; 

            // 4. FORWARD MOVEMENT
        
            me.moveF(0.8); 
            
            me.step(dt);
        }
    }
        void onCreate() override {
        addCone(mesh);
        mesh.scale(1, 0.2, 1);
        mesh.scale(0.2);
        mesh.generateNormals();

        nav().pos(0, 0, 6);
        light.pos(-2, 7, 0);
  
        nav().faceToward(Vec3d(0, 0, 0));

    
    }
    void onDraw(Graphics& g) override {
        g.clear(color);
        g.lighting(true);
        g.light(light);
        
        material.specular(light.diffuse() * 0.2);
        material.shininess(50);
        g.material(material);

        for (auto& a : agents) {
            g.pushMatrix();
            g.translate(a.pos());
            g.rotate(a.quat());
            g.draw(mesh);
            g.popMatrix();
        }
    }
};

int main() { MyApp().start(); }
