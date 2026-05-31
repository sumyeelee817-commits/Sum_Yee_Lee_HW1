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
#include <sstream>
#include "al/app/al_AppRecorder.hpp"

// Native EQR Vertex Shader
const std::string eqrVert = R"(
#version 400
layout (location = 0) in vec3 position;
layout (location = 2) in vec2 texcoord;
out vec2 v_uv;
void main() {
    gl_Position = vec4(position.xy, -1.0, 1.0);
    v_uv = texcoord;
}
)";

// Native EQR Fragment Shader
const std::string eqrFrag = R"(
#version 400
in vec2 v_uv;
uniform samplerCube cubemap;
layout (location = 0) out vec4 fragColor;
#define PI 3.14159265359
void main() {
    float phi = (1.0 - v_uv.y) * PI; 
    float theta = v_uv.x * 2.0 * PI; 
    vec3 dir;
    dir.x = sin(phi) * sin(theta);
    dir.y = cos(phi);
    dir.z = sin(phi) * cos(theta);
    fragColor = texture(cubemap, dir);
}
)";

const char* errstr(int result) {
switch(result) {
case GL_INVALID_ENUM:         return "An unacceptable value is specified for an enumerated argument...\n";
case GL_INVALID_VALUE:        return "A numeric argument is out of range...\n";
case GL_INVALID_OPERATION:    return "The specified operation is not allowed in the current state...\n";
case GL_STACK_OVERFLOW:       return "This command would cause a stack overflow...\n";
case GL_STACK_UNDERFLOW:      return "This command would cause a stack underflow...\n";
case GL_OUT_OF_MEMORY:        return "There is not enough memory left to execute the command...\n";
default: return "\n";
}
return "\n";
}

void glcheck() {
  if (auto result = glGetError() != GL_NO_ERROR) {
    printf("ERROR: %s", errstr(result));
    fflush(stdout);
    exit(1);
  }
}

// Lightweight Frame Blueprint (No Ocean Parameters)
struct FrameData {
    double time;
    int scene;
    al::Vec3d pos;
    al::Quatd quat;
};

using namespace al;

struct WorldState {
  double time{0.};
  int currentScene{0};         
  double sceneStartTime{0.};   
};

struct AlloApp : DistributedAppWithState<WorldState> {
  // --- EQR VIDEO VARIABLES ---
  al::AppRecorder rec;
  al::FBO cubemapFbo;
  al::Texture cubemapTex;
  al::RBO cubemapRbo;
  al::ShaderProgram eqrShader;

  al::Matrix4f getCubemapViewMatrix(int face, al::Vec3d pos) {
      al::Vec3f center, up;
      switch(face) {
          case 0: center = pos + al::Vec3d( 1,  0,  0); up = al::Vec3d(0, -1,  0); break; // Right
          case 1: center = pos + al::Vec3d(-1,  0,  0); up = al::Vec3d(0, -1,  0); break; // Left
          case 2: center = pos + al::Vec3d( 0,  1,  0); up = al::Vec3d(0,  0,  1); break; // Top
          case 3: center = pos + al::Vec3d( 0, -1,  0); up = al::Vec3d(0,  0, -1); break; // Bottom
          case 4: center = pos + al::Vec3d( 0,  0,  1); up = al::Vec3d(0, -1,  0); break; // Back
          case 5: center = pos + al::Vec3d( 0,  0, -1); up = al::Vec3d(0, -1,  0); break; // Front
      }
      return al::Matrix4f::lookAt(pos, center, up);
  }

  VAOMesh quad;
  ShaderProgram *doorOpenShader;
  ShaderProgram *doorCloseShader;

  gam::SamplePlayer<float, gam::ipl::Linear, gam::phsInc::OneShot> player;
  
  ParameterPose camera{"camera", ""};
  al::Vec3d prevPos;
  Parameter mouseX{"mouseX", "", 0.f};
  Parameter mouseY{"mouseY", "", 0.f};

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
    auto vertex = sp.find("door_vertex.glsl");
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
      std::ofstream file("performance_data_doors.txt");
      for (auto& frame : flightPath) {
          file << frame.time << " " << frame.scene << " "
               << frame.pos.x << " " << frame.pos.y << " " << frame.pos.z << " "
               << frame.quat.w << " " << frame.quat.x << " " << frame.quat.y << " " << frame.quat.z << "\n";
      }
      std::cout << ">>> Saved " << flightPath.size() << " frames to performance_data_doors.txt\n";
  }

  void loadFlightPath(std::string filename = "performance_data_doors.txt") {
      std::ifstream file(filename);
      if (!file.is_open()) {
          std::cout << ">>> No existing flight path found at " << filename << " <<<" << std::endl;
          return;
      }
      
      flightPath.clear();
      std::string line;
      int lineNumber = 0;
      int skippedFrames = 0;
      
      while (std::getline(file, line)) {
          lineNumber++;
          std::stringstream ss(line); 
          
          double t;
          int sc;
          double px, py, pz, qw, qx, qy, qz;
          
          if (ss >> t >> sc >> px >> py >> pz >> qw >> qx >> qy >> qz) {
              FrameData frame;
              frame.time = t;
              frame.scene = sc;
              frame.pos = al::Vec3d(px, py, pz);
              frame.quat = al::Quatd(qw, qx, qy, qz);
              flightPath.push_back(frame);
          } else {
              skippedFrames++;
          }
      }
      file.close();
      std::cout << ">>> SUCCESS: Loaded " << flightPath.size() << " frames.\n";
  }

  void onInit() override {
    auto cuttleboneDomain = CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) { quit(); }
    parameterServer() << camera << mouseX << mouseY;
  }

  void onCreate() override {
    eqrShader.compile(eqrVert, eqrFrag);
    
    cubemapTex.filter(Texture::LINEAR);
    // Lowered to 1024 for much faster offline testing
    cubemapTex.createCubemap(1024); 
    cubemapRbo.resize(1024, 1024);
    
    cubemapFbo.bind();
    cubemapFbo.attachCubemapFace(cubemapTex, GL_TEXTURE_CUBE_MAP_POSITIVE_X);
    cubemapFbo.attachRBO(cubemapRbo);
    cubemapFbo.unbind();

    doorOpenShader = new ShaderProgram();
    doorCloseShader = new ShaderProgram();
    
    if(!load(doorOpenShader, "door.glsl")) quit();
    if(!load(doorCloseShader, "door_close.glsl")) quit();

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

    state().time = 0.f;
    state().currentScene = 0;
    state().sceneStartTime = 0.f;
    
    lens().fovy(40);
    nav().pos(1.3, 1.7, -5);
    nav().quat() = al::Quatd(1, 0, 0, 0);

    prevPos = nav().pos();
  }

  void onAnimate(double dt) override {
    if(isPrimary()) {
      state().time += dt;

      if (isPlaying && flightPath.size() > 0) {
          double currentTime = state().time - performanceStartTime;
          
          while (playbackIndex < flightPath.size() - 1 && flightPath[playbackIndex + 1].time < currentTime) {
              playbackIndex++;
          }
          
          FrameData f = flightPath[playbackIndex];
          nav().pos() = f.pos;
          nav().quat() = f.quat;
          
          if (state().currentScene != f.scene) {
              state().currentScene = f.scene;
              state().sceneStartTime = state().time; 
          }
          
          if (playbackIndex >= flightPath.size() - 1) {
              isPlaying = false;
              std::cout << ">>> Playback Finished!\n";
          }
      } else {
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
              f.time = state().time - performanceStartTime;
              f.scene = state().currentScene;
              f.pos = nav().pos();
              f.quat = nav().quat();
              flightPath.push_back(f);
          }
      }
      camera.set(nav());
    } else {
      nav().set(camera.get());
    }
  }

  bool onKeyDown(const Keyboard &k) override {
    if (k.key() == 'v') {
        std::cout << "\n>>> OFFLINE EQR VIDEO CAPTURE STARTED...\n";
        rec.connectApp(this);
        rec.startRecordingOffline(); 
    }
    
    if (k.key() == 's') {
        std::cout << "\n>>> OFFLINE VIDEO CAPTURE STOPPED. Compiling MP4...\n";
        rec.stopRecording();
    }

    if (k.key() == 'p') {
        std::cout << "\n// --- SAVED CAMERA COORDS ---" << std::endl;
        std::cout << "nav().pos(" << nav().pos().x << ", " << nav().pos().y << ", " << nav().pos().z << ");" << std::endl;
        std::cout << "nav().quat() = al::Quatd(" << nav().quat().w << ", " << nav().quat().x << ", " << nav().quat().y << ", " << nav().quat().z << ");" << std::endl;
    }

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
            saveFlightPath(); 
        }
    }
    
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
            // Simplify toggle to only 2 scenes: 0 (Open) and 1 (Close)
            state().currentScene = (state().currentScene + 1) % 2;
            state().sceneStartTime = state().time;
            
            lens().fovy(40); 
            nav().pos(1.3, 1.7, -5); 
            nav().quat() = al::Quatd(1, 0, 0, 0); 
            
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

      if (io.channelsOut() > 34) io.out(34) = right;
      if (io.channelsOut() > 55) io.out(55) = (right + left) / 2.0f;
    }
  }

  void onDraw(Graphics &g) override {
    float shaderTime = state().time - state().sceneStartTime;

    float safe_foc_len = g.lens().focalLength();
    if (safe_foc_len <= 0.0001f) safe_foc_len = 1.0f; 

    // Scene Router
    ShaderProgram* activeShader = doorOpenShader;
    if (state().currentScene == 0) activeShader = doorOpenShader;
    else if (state().currentScene == 1) activeShader = doorCloseShader;

    cubemapFbo.bind();
    Matrix4f proj = Matrix4f::perspective(90.0, 1.0, 0.1, 100.0);
    
    for(int i = 0; i < 6; i++) {
        cubemapFbo.attachCubemapFace(cubemapTex, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i);
        g.viewport(0, 0, 1024, 1024); 
        g.clearColor(0, 0, 0, 1);
        g.clear();
        g.depthTesting(false); 
        g.blending(false); 

        Matrix4f view = getCubemapViewMatrix(i, nav().pos());
        
        activeShader->use();
        activeShader->uniform("iResolution", 1024.0f, 1024.0f, 1.0f); 
        activeShader->uniform("iTime", shaderTime);
        activeShader->uniform("al_ProjMatrixInv", Matrix4f::inverse(proj));
        activeShader->uniform("al_ViewMatrixInv", Matrix4f::inverse(view));
        activeShader->uniform("al_ModelMatrixInv", Matrix4f::inverse(g.modelMatrix()));

        activeShader->uniform("eye_sep", 0.0f); 
        activeShader->uniform("foc_len", safe_foc_len);

        quad.draw();
    }
    cubemapFbo.unbind();

    g.viewport(0, 0, fbWidth(), fbHeight());
    g.clearColor(0, 0, 0, 1);
    g.clear();
    
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    eqrShader.use();
    cubemapTex.bind(0);
    eqrShader.uniform("cubemap", 0);
    quad.draw();
    cubemapTex.unbind(0);
  }

  bool onMouseMove(const Mouse &m) override {
    if(isPrimary()) { mouseX.set(m.x()); mouseY.set(m.y()); }  
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
  app.dimensions(1024, 512);
  app.configureAudio(48000, 512, 2, 0);
  app.start();
}