// SPDX-FileCopyrightText: 2026 AlloSphere Research Group <allosphere@ucsb.edu>
#include "al/app/al_DistributedApp.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_VAOMesh.hpp"
#include "al/io/al_File.hpp"
#include "al/graphics/al_Texture.hpp" 
#include "al/graphics/al_FBO.hpp"     
#include "Gamma/SamplePlayer.h"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <sstream>

// The blueprint for a single "frame" of your performance
struct FrameData {
    double time;
    int scene;
    al::Vec3d pos;
    al::Quatd quat;
    // Your GUI Parameters
    float speed, waterdepth, phase, dragMult, weight, rayMarch, normal;
};

using namespace al;

// --- UPGRADED WORLD STATE ---
// Everything in here is instantly synced across the entire AlloSphere network
struct WorldState {
  double time{0.};
  int currentScene{0};         // Tracks which shader to draw
  double sceneStartTime{0.};   // Tracks exactly when you pressed '9'
};

struct AlloApp : DistributedAppWithState<WorldState> {
  VAOMesh quad;
  
  ShaderProgram *doorOpenShader;
  ShaderProgram *doorCloseShader;
  ShaderProgram *oceanShader;
  ShaderProgram *starsBufferAShader;
  ShaderProgram *starsImageShader;
  
  FBO fbo;
  Texture tex[2];
  int currentTex = 0;

  gam::SamplePlayer<float, gam::ipl::Linear, gam::phsInc::OneShot> player;
  
  ParameterPose camera{"camera", ""};
  al::Vec3d prevPos;
  Parameter mouseX{"mouseX", "", 0.f};
  Parameter mouseY{"mouseY", "", 0.f};
  Parameter RayMarch{"RayMarch", "", 13.0, 1.0, 50.0};
  Parameter Normal{"Normal", "", 48.0, 1.0, 100.0};
  Parameter speed{"speed", "", 1.0, 0.0, 5.0};
  Parameter weight{"weight", "", 3.0, 0.0, 10.0};
  Parameter waterdepth{"waterdepth", "", 2.1, 0.1, 10.0};
  Parameter phase{"phase", "", 6.0, 0.0, 20.0};
  Parameter dragMult{"dragMult", "", 0.048, 0.0, 0.2};

  // --- FLIGHT RECORDER VARIABLES ---
  std::vector<FrameData> flightPath;
  bool isRecording = false;
  bool isPlaying = false;
  double performanceStartTime = 0.0;
  size_t playbackIndex = 0;

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

    if (vertex.valid() && fragment.valid()) {
        bool success = p->compile(slurp(vertex.filepath()), slurp(fragment.filepath()));
        if(success) std::cout << ">>> SUCCESS: Compiled " << fragFileName << std::endl;
        else std::cerr << ">>> FATAL ERROR: Failed to compile " << fragFileName << std::endl;
        return success;
    }
    std::cerr << ">>> FATAL ERROR: Could not find " << fragFileName << std::endl;
    return false;
  }

  void saveFlightPath() {
      std::ofstream file("performance_data.txt");
      for (auto& frame : flightPath) {
          file << frame.time << " " << frame.scene << " "
               << frame.pos.x << " " << frame.pos.y << " " << frame.pos.z << " "
               << frame.quat.w << " " << frame.quat.x << " " << frame.quat.y << " " << frame.quat.z << " "
               << frame.speed << " " << frame.waterdepth << " " << frame.phase << " "
               << frame.dragMult << " " << frame.weight << " " << frame.rayMarch << " " << frame.normal << "\n";
      }
      std::cout << ">>> Saved " << flightPath.size() << " frames to performance_data.txt\n";
  }

  // --- BULLETPROOF: Load flight path (Skips bad frames!) ---
  void loadFlightPath(std::string filename = "performance_data.txt") {
      std::ifstream file(filename);
      if (!file.is_open()) {
          std::cout << ">>> No existing flight path found at " << filename << " <<<" << std::endl;
          return;
      }
      
      flightPath.clear();
      std::string line;
      int lineNumber = 0;
      int skippedFrames = 0;
      
      // Read the file ONE line at a time
      while (std::getline(file, line)) {
          lineNumber++;
          std::stringstream ss(line); // Create a mini-stream just for this line
          
          double t;
          int sc;
          double px, py, pz, qw, qx, qy, qz;
          float s, wd, ph, dm, w, rm, n;
          
          // Try to extract all 16 numbers from this specific line
          if (ss >> t >> sc >> px >> py >> pz >> qw >> qx >> qy >> qz >> s >> wd >> ph >> dm >> w >> rm >> n) {
              FrameData frame;
              frame.time = t;
              frame.scene = sc;
              
              // Fixed: Access pos and quat directly instead of using cameraPose
              frame.pos = al::Vec3d(px, py, pz);
              frame.quat = al::Quatd(qw, qx, qy, qz);
              
              frame.speed = s;
              frame.waterdepth = wd;
              frame.phase = ph;
              frame.dragMult = dm;
              frame.weight = w;
              frame.rayMarch = rm;
              frame.normal = n;
              
              flightPath.push_back(frame);
          } else {
              // If the math was corrupted (NaN) or missing, skip the frame and keep going!
              skippedFrames++;
              std::cerr << ">>> WARNING: Skipped corrupted frame at line " << lineNumber << " <<<" << std::endl;
          }
      }
      
      file.close();
      std::cout << ">>> SUCCESS: Loaded " << flightPath.size() << " frames. (Skipped " << skippedFrames << " bad frames) <<<" << std::endl;
  }

  void onInit() override {
    auto cuttleboneDomain = CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) { quit(); }
    
    if(isPrimary()) {
        auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
        auto &gui = GUIdomain->newGUI();
        gui.add(RayMarch); gui.add(Normal); gui.add(speed); gui.add(weight);
        gui.add(waterdepth); gui.add(phase); gui.add(dragMult);
    }
    parameterServer() << camera << mouseX << mouseY << RayMarch << Normal << speed << weight << waterdepth << phase << dragMult;
  }

  void onCreate() override {
    doorOpenShader = new ShaderProgram();
    doorCloseShader = new ShaderProgram();
    oceanShader = new ShaderProgram();
    starsBufferAShader = new ShaderProgram();
    starsImageShader = new ShaderProgram();
    
    if(!load(doorOpenShader, "door.glsl")) quit();
    if(!load(doorCloseShader, "door_close.glsl")) quit();
    if(!load(oceanShader, "fragment.glsl")) quit();
    if(!load(starsBufferAShader, "bufferA.glsl")) quit();
    if(!load(starsImageShader, "image.glsl")) quit();

    fbo.create();

    SearchPaths sp;
    sp.addSearchPath(".", false);
    sp.addAppPaths();
    sp.addRelativePath("..", true);
    auto audioFile = sp.find("sound.wav");
    if (audioFile.valid()) {
        player.load(audioFile.filepath().c_str());
        std::cout << "Loaded audio: " << audioFile.filepath() << std::endl;
    } 
    player.pos(0);

    lens().near(0.1).far(25).fovy(40);
    quad.primitive(Mesh::TRIANGLE_STRIP);
    quad.vertex(-1.f, -1.f, 0); quad.vertex(1.f, -1.f, 0);
    quad.vertex(-1.f, 1.f, 0);  quad.vertex(1.f, 1.f, 0);
    quad.texCoord(0, 0); quad.texCoord(1, 0);
    quad.texCoord(0, 1); quad.texCoord(1, 1);
    quad.update();

    // Set default values for the start of the program
    state().time = 0.f;
    state().currentScene = 0;
    state().sceneStartTime = 0.f;
    
    // Lock in the camera for Scene 0 (Door Open)
    lens().fovy(40);
    nav().pos(1.3, 1.7, -5);
    nav().quat() = al::Quatd(1, 0, 0, 0);

    prevPos = nav().pos();
  }

  void onAnimate(double dt) override {
    if(isPrimary()) {
      state().time += dt;

      if (isPlaying && flightPath.size() > 0) {
          // --- PLAYBACK MODE ---
          double currentTime = state().time - performanceStartTime;
          
          // Fast-forward to the current frame based on time
          while (playbackIndex < flightPath.size() - 1 && flightPath[playbackIndex + 1].time < currentTime) {
              playbackIndex++;
          }
          
          // Force the system state to match the recorded frame
          FrameData f = flightPath[playbackIndex];
          nav().pos() = f.pos;
          nav().quat() = f.quat;
          
          if (state().currentScene != f.scene) {
              state().currentScene = f.scene;
              state().sceneStartTime = state().time; // Reset shader clock for the new scene
          }
          
          speed.set(f.speed);
          waterdepth.set(f.waterdepth);
          phase.set(f.phase);
          dragMult.set(f.dragMult);
          weight.set(f.weight);
          RayMarch.set(f.rayMarch);
          Normal.set(f.normal);
          
          if (playbackIndex >= flightPath.size() - 1) {
              isPlaying = false;
              std::cout << ">>> Playback Finished!\n";
          }
      } else {
          // --- LIVE CONTROL & RECORDING MODE ---
          al::Vec3d frameMovement = nav().pos() - prevPos;
          double xSpeed = 0.1; 
          double ySpeed = 0.05; 
          double zSpeed = 0.1; 
          
          nav().pos() = prevPos + al::Vec3d(
              frameMovement.x * xSpeed, 
              frameMovement.y * ySpeed, 
              frameMovement.z * zSpeed
          );
          prevPos = nav().pos();

          double lookSpeed = 0.25; 
          nav().spin() *= lookSpeed;
          nav().step(dt);

          if (isRecording) {
              FrameData f;
              f.time = state().time - performanceStartTime; // Store relative time starting from 0.0
              f.scene = state().currentScene;
              f.pos = nav().pos();
              f.quat = nav().quat();
              f.speed = speed.get();
              f.waterdepth = waterdepth.get();
              f.phase = phase.get();
              f.dragMult = dragMult.get();
              f.weight = weight.get();
              f.rayMarch = RayMarch.get();
              f.normal = Normal.get();
              flightPath.push_back(f);
          }
      }
      camera.set(nav());
    } else {
      nav().set(camera.get());
    }
  }

  // --- NEW KEYBOARD TRIGGER SYSTEM ---
  bool onKeyDown(const Keyboard &k) override {
    if (k.key() == 'p') {
        std::cout << "\n// --- SAVED CAMERA COORDS ---" << std::endl;
        std::cout << "nav().pos(" << nav().pos().x << ", " << nav().pos().y << ", " << nav().pos().z << ");" << std::endl;
        std::cout << "nav().quat() = al::Quatd(" << nav().quat().w << ", " << nav().quat().x << ", " << nav().quat().y << ", " << nav().quat().z << ");" << std::endl;
    }
    // Press 'r' to start/stop recording
    if (k.key() == 'r') {
        if (!isRecording) {
            std::cout << "\n>>> RECORDING STARTED...\n";
            flightPath.clear();
            performanceStartTime = state().time;
            isRecording = true;
            isPlaying = false;
            player.pos(0);
        } else {
            isRecording = false;
            std::cout << ">>> RECORDING STOPPED.\n";
            saveFlightPath(); // Automatically writes the file
        }
    }
    
    // Press 'l' to load the file and play it back
    if (k.key() == 'l') {
        loadFlightPath();
        if (flightPath.size() > 0) {
            isPlaying = true;
            isRecording = false;
            performanceStartTime = state().time;
            playbackIndex = 0;
            std::cout << "\n>>> PLAYBACK STARTED...\n";
            player.pos(0);
        }
    }

    if (k.key() == '9') {
        if(isPrimary()) {
            // ---> CHANGED TO % 5: Now cycles through 0, 1, 2, 3, 4, then back to 0!
            state().currentScene = (state().currentScene + 1) % 5;
            
            state().sceneStartTime = state().time;
            
            // Snap the camera to the perfect position for the new scene
            if (state().currentScene == 0) { // 0: Door Open
              lens().fovy(40); nav().pos(1.3, 1.7, -3.58262); nav().quat() = al::Quatd(1, 0, 0, 0); 
            } 
            else if (state().currentScene == 1) { // 1: Ocean (First time)
              lens().fovy(80); nav().pos(-21.71, 5.67836, -218.727); nav().quat() = al::Quatd(-0.937772, -0.00161208, -0.346776, 0.0181049);
            } 
            else if (state().currentScene == 2) { // 2: Stars
              lens().fovy(80); nav().pos(0.966193, 2.1846, -36); nav().quat() = al::Quatd(-0.613572, 0, 0, 0.789638);
            } 
            else if (state().currentScene == 3) { // 3: Ocean (Return trip!)
              lens().fovy(80); nav().pos(-21.6952, 5.58952, -218.689); nav().quat() = al::Quatd(-0.707998, 0.622268, -0.225784, -0.246056);
            } 
            else if (state().currentScene == 4) { // 4: Door Close
              lens().fovy(40); nav().pos(1.3, 1.7, -5); nav().quat() = al::Quatd(1, 0, 0, 0);
            }
            prevPos = nav().pos();
            std::cout << ">>> Switched to Scene: " << state().currentScene << std::endl;
        }
    }
    return true;
  }

  void onSound(AudioIOData &io) override {
    while (io()) {
      float left = player.read(0);
      float right = player(1); 

      if (io.channelsOut() > 0) io.out(0) = left;
      if (io.channelsOut() > 1) io.out(1) = right; 

      // Safe routing for the AlloSphere (Only writes if channels 34 and 55 exist)
      if (io.channelsOut() > 34) io.out(34) = right;
      if (io.channelsOut() > 55) io.out(55) = (right + left) / 2.0f;

      //io.out(0) = left; 
      //io.out(34) = right;
      //io.out(55) = (right + left) / 2.0f;
    }
  }

  void onDraw(Graphics &g) override {
    g.depthTesting(false); 
    g.blending(false); 
    g.clearColor(0, 0, 0, 1);
    g.clear();

    if (tex[0].width() != fbWidth() || tex[0].height() != fbHeight()) {
        tex[0].create2D(fbWidth(), fbHeight(), Texture::RGBA16F, Texture::RGBA, Texture::FLOAT);
        tex[0].filter(Texture::LINEAR); tex[0].wrap(Texture::CLAMP_TO_EDGE);
        tex[1].create2D(fbWidth(), fbHeight(), Texture::RGBA16F, Texture::RGBA, Texture::FLOAT);
        tex[1].filter(Texture::LINEAR); tex[1].wrap(Texture::CLAMP_TO_EDGE);
        fbo.bind();
        fbo.attachTexture2D(tex[0]); g.clear();
        fbo.attachTexture2D(tex[1]); g.clear();
        fbo.unbind();
    }
    
    // --- RESET THE LOCAL SHADER CLOCK ---
    // Subtracts the time you pressed '9' from the master clock
    float shaderTime = state().time - state().sceneStartTime;

    // --- RENDER SCENES ---
    if (state().currentScene == 0) { // DOOR OPEN
        doorOpenShader->use();
        doorOpenShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / height());
        doorOpenShader->uniform("iTime", shaderTime);
        doorOpenShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
        doorOpenShader->uniform("foc_len", g.lens().focalLength());
        doorOpenShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
        doorOpenShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
        doorOpenShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));
        quad.draw();
    } 
    else if (state().currentScene == 1 || state().currentScene == 3) {// OCEAN
        oceanShader->use();
        oceanShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / height());
        oceanShader->uniform("iTime", shaderTime);
        oceanShader->uniform("iMouse", mouseX, mouseY);
        oceanShader->uniform("u_RayMarch", (int)RayMarch.get());
        oceanShader->uniform("u_Normal", (int)Normal.get());
        oceanShader->uniform("u_speed", speed.get());
        oceanShader->uniform("u_weight", weight.get());
        oceanShader->uniform("u_phase", phase.get());
        oceanShader->uniform("u_waterdepth", waterdepth.get());
        oceanShader->uniform("u_dragMult", dragMult.get());
        oceanShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
        oceanShader->uniform("foc_len", g.lens().focalLength());
        oceanShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
        oceanShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
        oceanShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));
        quad.draw();
    }
    else if (state().currentScene == 2) { // STARS (Two Passes)
        int nextTex = (currentTex + 1) % 2; 
        fbo.bind(); 
        fbo.attachTexture2D(tex[nextTex]); 
        starsBufferAShader->use();
        starsBufferAShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / (float)height());
        starsBufferAShader->uniform("iTime", shaderTime); 
        starsBufferAShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
        starsBufferAShader->uniform("foc_len", g.lens().focalLength());
        starsBufferAShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
        starsBufferAShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
        starsBufferAShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));
        tex[currentTex].bind(0);
        starsBufferAShader->uniform("texFeedback", 0);
        quad.draw();
        tex[currentTex].unbind(0);
        fbo.unbind(); 
        currentTex = nextTex; 

        g.clear(); 
        starsImageShader->use();
        starsImageShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / (float)height());
        starsImageShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
        starsImageShader->uniform("foc_len", g.lens().focalLength());
        starsImageShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
        starsImageShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
        starsImageShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));
        tex[currentTex].bind(0);
        starsImageShader->uniform("texMain", 0);
        quad.draw();
        tex[currentTex].unbind(0);
    }
    else if (state().currentScene == 4) { // DOOR CLOSE
        doorCloseShader->use();
        doorCloseShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(), (float)width() / height());
        doorCloseShader->uniform("iTime", shaderTime);
        doorCloseShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
        doorCloseShader->uniform("foc_len", g.lens().focalLength());
        doorCloseShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(g.projMatrix()));
        doorCloseShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(g.viewMatrix()));
        doorCloseShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));
        quad.draw();
    }
  }

  bool onMouseMove(const Mouse &m) override {
    if(isPrimary()) { mouseX = m.x(); mouseY = m.y(); }  
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