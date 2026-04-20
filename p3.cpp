#include <iostream>
#include <vector>
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float rs() { return rnd::uniformS(); }

struct MyApp : public App {
    ParameterInt N{"/N", "", 50, 2, 200};
    ParameterColor color{"/color"};
    
    // Configuration
    float thresholdT = 2.5;    // Neighborhood radius
    float personalSpace = 0.4; // Separation distance
    float worldSize = 5.0;     // Boundary for wrap-around
    int maxK = 8;              // Max neighbors to consider for performance

    Light light;
    Material material;
    Mesh mesh;

    struct Agent : Nav {
        // Inherits pos, quat, and movement methods
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
            a.pos(Vec3d(rs(), rs(), rs()) * worldSize);
            a.quat(Quatd(rs(), rs(), rs(), rs()).normalize());
        }
    }

    void onAnimate(double dt) override {
        if (N != lastN) {
            lastN = N;
            reset(N);
        }

        for (int i = 0; i < agents.size(); i++) {
            auto& me = agents[i];

            Vec3d avgHeading(0, 0, 0);
            Vec3d avgPosition(0, 0, 0);
            Vec3d separationNudge(0, 0, 0);
            int neighborCount = 0;

            // Find neighbors
            for (int j = 0; j < agents.size(); j++) {
                if (i == j) continue;
                if (neighborCount >= maxK) break; 

                Vec3d diff = agents[j].pos() - me.pos();
                float dist = diff.mag();

                if (dist < thresholdT && dist > 0.0001) {
                    avgHeading += agents[j].uf();
                    avgPosition += agents[j].pos();

                    if (dist < personalSpace) {
                        // Push away more aggressively the closer they are
                        separationNudge -= diff.normalized() * (personalSpace - dist);
                    }
                    neighborCount++;
                }
            }

            // Apply Flocking Rules
            if (neighborCount > 0) {
                avgHeading /= neighborCount;
                avgPosition /= neighborCount;

                // 1. ALIGNMENT (Match heading)
                me.faceToward(me.pos() + avgHeading, 0.04);

                // 2. COHESION (Too far from center? Nudge back)
                Vec3d toCenter = (avgPosition - me.pos());
                me.pos() += toCenter * 0.015;

                // 3. SEPARATION (Too close? Nudge away)
                me.pos() += separationNudge * 0.15;
            }

            // 4. RANDOM WANDER (The "Organic" touch)
            // Gently rotate the quat by a tiny random amount each frame
            me.turnU(rs() * 0.02);
            me.turnR(rs() * 0.02);

            // 5. MOVEMENT
            me.moveF(0.6); 
            me.step(dt);

            // 6. WRAP AROUND (Teleport to opposite side)
            Vec3d p = me.pos();
            for (int axis = 0; axis < 3; axis++) {
                if (p[axis] > worldSize) p[axis] -= (worldSize * 2);
                if (p[axis] < -worldSize) p[axis] += (worldSize * 2);
            }
            me.pos(p);
        }
    }

    void onCreate() override {
        // Using a cone so we can see which way they are facing
        addCone(mesh);
        mesh.scale(1, 0.2, 1);
        mesh.scale(0.15);
        mesh.generateNormals();

        nav().pos(0, 0, 15);
        light.pos(-2, 7, 5);
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
