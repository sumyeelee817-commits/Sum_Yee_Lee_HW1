#include <iostream>
#include <vector>
#include <algorithm>
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/math/al_Random.hpp"

using namespace al;

float rs() { return rnd::uniformS(); }

struct TriggerSpot {
    Vec3f pos;
    bool active = true;
};

struct MyApp : public App {
    ParameterInt N{"/N", "", 50, 2, 200};
    ParameterColor color{"/color"};
    
    float thresholdT = 2.5;
    float personalSpace = 0.5; 
    float worldSize = 6.0;

    Light light;
    Mesh agentMesh, predatorMesh;

    enum PredState { INACTIVE, HUNTING, RETREATING };

    struct Agent : Nav {
        bool alive = true;
    };

    struct Predator : Nav {
        PredState state = INACTIVE;
        int currentSpotIndex = -1; 
    };

    std::vector<Agent> agents;
    Predator predator;
    std::vector<TriggerSpot> triggerSpots;
    int lastN = 0;

    void onInit() override {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();
        gui.add(N);
        gui.add(color);

        std::vector<Vec3f> positions = {{4, 4, 0}, {-4, 4, 0}, {4, -4, 0}, {-4, -4, 0}};
        for (auto p : positions) triggerSpots.push_back({p, true});
    }

    void resetFlock(int n) {
        agents.clear();
        for (int i = 0; i < n; i++) {
            Agent a;
            a.pos(Vec3d(rs(), rs(), rs()) * (worldSize * 0.5));
            a.quat(Quatd(rs(), rs(), rs(), rs()).normalize());
            agents.push_back(a);
        }
    }

    void onAnimate(double dt) override {
        if (N != lastN) { lastN = N; resetFlock(N); }

        // --- PREDATOR LOGIC ---
        if (predator.state == INACTIVE) {
            for (auto& a : agents) {
                for (int i = 0; i < triggerSpots.size(); i++) {
                    if (triggerSpots[i].active && (a.pos() - triggerSpots[i].pos).mag() < 1.2) {
                        predator.state = HUNTING;
                        predator.currentSpotIndex = i;
                        triggerSpots[i].active = false; // Disable spot immediately
                        predator.pos(triggerSpots[i].pos);
                        break;
                    }
                }
                if (predator.state == HUNTING) break;
            }
        } 
        else if (predator.state == HUNTING) {
            float closestDist = 999.0;
            Vec3d targetPreyPos(0, 0, 0);
            for (auto& a : agents) {
                float d = (a.pos() - predator.pos()).mag();
                if (d < closestDist) { closestDist = d; targetPreyPos = a.pos(); }
            }

            if (agents.empty() || closestDist > 6.5) {
                predator.state = RETREATING;
            } else {
                predator.faceToward(targetPreyPos, 0.15);
                predator.moveF(1.4);
                predator.step(dt);

                agents.erase(std::remove_if(agents.begin(), agents.end(), 
                    [&](const Agent& a) {
                        return (a.pos() - predator.pos()).mag() < 0.6; 
                    }), agents.end());
            }
        } 
        else if (predator.state == RETREATING) {
            Vec3d target = triggerSpots[predator.currentSpotIndex].pos;
            predator.faceToward(target, 0.1);
            predator.moveF(1.0);
            predator.step(dt);

            if ((predator.pos() - target).mag() < 0.5) {
                predator.state = INACTIVE;
                triggerSpots[predator.currentSpotIndex].active = true; // Re-enable spot
            }
        }

        // --- FLOCK LOGIC ---
        float currentFlockSpeed = (predator.state == HUNTING) ? 1.3 : 0.6;

        for (auto& me : agents) {
            Vec3d avgHeading(0,0,0), avgPos(0,0,0), sep(0,0,0);
            int count = 0;

            for (auto& other : agents) {
                if (&me == &other) continue;
                Vec3d diff = other.pos() - me.pos();
                float d = diff.mag();
                if (d > 0 && d < thresholdT) {
                    avgHeading += other.uf();
                    avgPos += other.pos();
                    if (d < personalSpace) sep -= diff.normalized() * (personalSpace - d);
                    count++;
                }
            }

            if (count > 0) {
                me.faceToward(me.pos() + (avgHeading/count), 0.05);
                me.pos() += ((avgPos/count) - me.pos()) * 0.02;
                me.pos() += sep * 0.15;
            }

            // AVOIDANCE: Only active when being hunted
            if (predator.state == HUNTING) {
                Vec3d away = me.pos() - predator.pos();
                if (away.mag() < 5.0) me.faceToward(me.pos() + away, 0.25);

                for (auto& s : triggerSpots) {
                    Vec3d awaySpot = me.pos() - s.pos;
                    if (awaySpot.mag() < 3.0) me.faceToward(me.pos() + awaySpot, 0.15);
                }
            }

            contain(me, 0.1);
            me.turnU(rs() * 0.02);
            me.moveF(currentFlockSpeed);
            me.step(dt);
        }
    }

    void contain(Nav& n, float strength) {
        if (n.pos().mag() > worldSize) n.faceToward(Vec3d(0,0,0), strength);
    }

    void onCreate() override {
        addCone(agentMesh);
        agentMesh.scale(0.15, 0.05, 0.15);
        agentMesh.generateNormals();

        addSphere(predatorMesh, 0.5); 
        predatorMesh.generateNormals();

        nav().pos(0, 0, 20);
        light.pos(5, 5, 5);
    }

    void onDraw(Graphics& g) override {
        g.clear(color);
        g.lighting(true);
        g.light(light);

        for (auto& s : triggerSpots) {
            g.pushMatrix();
            g.translate(s.pos);
            // Red if ready to trigger, faint gray if predator is out or returning
            s.active ? g.color(1, 0, 0, 0.2) : g.color(0.5, 0.5, 0.5, 0.05); 
            g.draw(agentMesh); 
            g.popMatrix();
        }

        for (auto& a : agents) {
            (predator.state == HUNTING) ? g.color(0.5, 1, 1) : g.color(1, 1, 1);
            g.pushMatrix();
            g.translate(a.pos());
            g.rotate(a.quat());
            g.draw(agentMesh);
            g.popMatrix();
        }

        if (predator.state != INACTIVE) {
            predator.state == HUNTING ? g.color(1, 0, 0) : g.color(0.3, 0.3, 0.3);
            g.pushMatrix();
            g.translate(predator.pos());
            g.rotate(predator.quat());
            g.draw(predatorMesh);
            g.popMatrix();
        }
    }
};

int main() { MyApp().start(); }