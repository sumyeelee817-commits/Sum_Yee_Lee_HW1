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
            // We only steer if we are a safe distance away to prevent the "spinning glitch"
            float distToLove = (love.pos() - me.pos()).mag();
            if (distToLove > 0.2) {
                me.faceToward(love.pos(), 0.3); 
            }

            // 2. THE "SOFT BOUNDARY" (Instead of friction)
            // If the agent wanders too far from the center, we forcefully 
            // steer it back. This keeps them all in a "bubble" around (0,0,0).
            float distFromCenter = me.pos().mag();
            if (distFromCenter > 7.0) {
                Vec3d toCenter = -me.pos();
                toCenter.normalize();
                // We nudge the position directly back toward the center
                me.pos() += toCenter * 0.05; 
            }

            // 3. SEPARATION (Personal Space)
            // --- TASK: MAINTAIN DISTANCE (Separation) ---
            Vec3d nudgeAway;
            int neighborCount = 0;

        for (int j = 0; j < agents.size(); j++) {
            if (i == j) continue; // Don't check against yourself

            // 1. Find distance to the other agent
            Vec3d diff = me.pos() - agents[j].pos();
            float dist = diff.mag();

            // 2. Threshold Check (The "Too Close" zone)
            // Adjust this value (e.g., 0.4 to 1.0) to change their personal space
            float threshold = 0.6; 

            if (dist < threshold && dist > 0.0001) {
                // 3. Calculate Nudge: The closer they are, the harder we push away
                // Normalizing 'diff' gives us the direction away from the neighbor
                nudgeAway += (diff / dist) * (threshold - dist);
                neighborCount++;
            }
        }

        // 4. Apply the nudge to the position
        // The 0.2 is the "strength" of the personal space boundary
        if (neighborCount > 0) {
            me.pos() += nudgeAway * 0.2;
        }
            // 4. FORWARD MOVEMENT
        
        me.moveF(0.8); 
            
            // Apply the physics step
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
