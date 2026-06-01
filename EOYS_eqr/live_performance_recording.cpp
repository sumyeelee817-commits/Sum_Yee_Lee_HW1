// SPDX-FileCopyrightText: 2026 AlloSphere Research Group <allosphere@ucsb.edu>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "Gamma/SamplePlayer.h"
#include "al/app/al_DistributedApp.hpp"
#include "al/app/al_GUIDomain.hpp"
#include "al/graphics/al_Shader.hpp"
#include "al/graphics/al_VAOMesh.hpp"
#include "al/io/al_File.hpp"
#include "al/ui/al_PresetHandler.hpp"
#include "al_ext/statedistribution/al_CuttleboneStateSimulationDomain.hpp"

#undef near
#undef far

// The blueprint for a single "frame" of your performance
struct FrameData {
  double time;
  int scene;
  al::Vec3d pos;
  al::Quatd quat;
  // Your GUI Parameters
  float speed, waterdepth, phase, dragMult, weight, iterations, iterations2,
      normal;
  float pitch, yaw;
};

using namespace al;

// --- UPGRADED WORLD STATE ---
struct WorldState {
  double time{0.};
  int currentScene{0};
  double sceneStartTime{0.};
};

struct AlloApp : DistributedAppWithState<WorldState> {
  VAOMesh quad;

  ShaderProgram* doorOpenShader;
  ShaderProgram* doorCloseShader;
  ShaderProgram* oceanShader;
  ShaderProgram* starsShader;  // NEW: Single unified shader

  gam::SamplePlayer<float, gam::ipl::Linear, gam::phsInc::OneShot> player;

  ParameterPose camera{"camera", ""};
  Parameter iterations{"iterations", "", 13.0, 1.0, 50.0};
  Parameter iterations2{"iterations2", "", 50.0, 1.0, 350.0};
  Parameter Normal{"Normal", "", 48.0, 1.0, 100.0};
  Parameter speed{"speed", "", 1.0, 0.0, 5.0};
  Parameter weight{"weight", "", 3.0, 0.0, 10.0};
  Parameter waterdepth{"waterdepth", "", 2.1, 0.1, 10.0};
  Parameter phase{"phase", "", 6.0, 0.0, 20.0};
  Parameter dragMult{"dragMult", "", 0.048, 0.0, 0.2};
  Parameter camPitch{"camPitch", "", 0.0, -180.0, 180.0};  // Look up/down (-90 to 90 degrees)
  Parameter camYaw{"camYaw", "", 0.0, -180.0, 180.0};    // Look left/right (-180 to 180 degrees)
  al::PresetHandler presetHandler{"presets", true};

  // --- FLIGHT RECORDER VARIABLES ---
  std::vector<FrameData> flightPath;
  bool isRecording = false;
  bool isPlaying = false;
  double performanceStartTime = 0.0;
  size_t playbackIndex = 0;

  bool load(ShaderProgram* p, std::string fragFileName)
  {
    SearchPaths sp;
    sp.addSearchPath(".", false);
    sp.addAppPaths();
    sp.addRelativePath("..", true);
    auto vertex = sp.find("door_vertex.glsl");
    auto fragment = sp.find(fragFileName);

    if (vertex.valid() && fragment.valid()) {
      bool success =
          p->compile(slurp(vertex.filepath()), slurp(fragment.filepath()));
      if (success)
        std::cout << ">>> SUCCESS: Compiled " << fragFileName << std::endl;
      else
        std::cerr << ">>> FATAL ERROR: Failed to compile " << fragFileName
                  << std::endl;
      return success;
    }
    std::cerr << ">>> FATAL ERROR: Could not find " << fragFileName
              << std::endl;
    return false;
  }

  void saveFlightPath()
  {
    std::ofstream file("performance_data.txt");
    for (auto& frame : flightPath) {
      file << frame.time << " " << frame.scene << " " << frame.pos.x << " "
           << frame.pos.y << " " << frame.pos.z << " " << frame.quat.w << " "
           << frame.quat.x << " " << frame.quat.y << " " << frame.quat.z << " "
           << frame.speed << " " << frame.waterdepth << " " << frame.phase
           << " " << frame.dragMult << " " << frame.weight << " "
           << frame.iterations << " " << frame.iterations2 << " "
           << frame.normal << " "
           << frame.pitch << " " << frame.yaw << "\n";
    }
    std::cout << ">>> Saved " << flightPath.size()
              << " frames to performance_data.txt\n";
  }

  void loadFlightPath(std::string filename = "performance_data.txt")
  {
    std::ifstream file(filename);
    if (!file.is_open()) {
      std::cout << ">>> No existing flight path found at " << filename << " <<<"
                << std::endl;
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
      float s, wd, ph, dm, w, it1, it2, n;

      if (ss >> t >> sc >> px >> py >> pz >> qw >> qx >> qy >> qz >> s >> wd >>
          ph >> dm >> w >> it1 >> it2 >> n) {
        FrameData frame;
        frame.time = t;
        frame.scene = sc;
        frame.pos = al::Vec3d(px, py, pz);
        frame.quat = al::Quatd(qw, qx, qy, qz);
        frame.speed = s;
        frame.waterdepth = wd;
        frame.phase = ph;
        frame.dragMult = dm;
        frame.weight = w;
        frame.iterations = it1;
        frame.iterations2 = it2;
        frame.normal = n;
        flightPath.push_back(frame);
      }
      else {
        skippedFrames++;
        std::cerr << ">>> WARNING: Skipped corrupted frame at line "
                  << lineNumber << " <<<" << std::endl;
      }
    }

    file.close();
    std::cout << ">>> SUCCESS: Loaded " << flightPath.size()
              << " frames. (Skipped " << skippedFrames << " bad frames) <<<"
              << std::endl;
  }

  void onInit() override
  {
    decorated(false);
    dimensions(0,0,2048,1024);
    auto cuttleboneDomain =
        CuttleboneStateSimulationDomain<WorldState>::enableCuttlebone(this);
    if (!cuttleboneDomain) {
      quit();
    }

    if (isPrimary()) {
      auto GUIdomain = GUIDomain::enableGUI(defaultWindowDomain());
      auto& gui = GUIdomain->newGUI();
      gui.add(iterations);
      gui.add(iterations2);
      gui.add(Normal);
      gui.add(speed);
      gui.add(weight);
      gui.add(waterdepth);
      gui.add(phase);
      gui.add(dragMult);
      gui.add(camPitch);
      gui.add(camYaw);
      gui.add(presetHandler);
    }
    parameterServer() << camera << iterations << iterations2 << Normal << speed
                      << weight << waterdepth << phase << dragMult << camPitch << camYaw;

    presetHandler << camPitch << camYaw;
    auto updateCameraRotation = [this](float /*value*/) {
        double pitchRad = camPitch.get() * M_PI / 180.0;
        double yawRad   = camYaw.get() * M_PI / 180.0;
        
        al::Quatd qP, qY;
        qP.fromAxisAngle(pitchRad, 1, 0, 0);
        qY.fromAxisAngle(yawRad, 0, 1, 0);
        
        nav().quat() = qY * qP;
    };

    // Attach the function to the sliders
    camPitch.registerChangeCallback(updateCameraRotation);
    camYaw.registerChangeCallback(updateCameraRotation);
  }
    

  void onCreate() override
  {
    doorOpenShader = new ShaderProgram();
    doorCloseShader = new ShaderProgram();
    oceanShader = new ShaderProgram();
    starsShader = new ShaderProgram();  // Unified

    if (!load(doorOpenShader, "door.glsl")) quit();
    if (!load(doorCloseShader, "door_close.glsl")) quit();
    if (!load(oceanShader, "fragment.glsl")) quit();
    if (!load(starsShader, "star.glsl")) quit();

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
    state().currentScene = 0;
    state().sceneStartTime = 0.f;

    // lens().fovy(40);
    // nav().pos(1.3, 1.7, -5);
    // nav().quat() = al::Quatd(1, 0, 0, 0);
    // navControl().vscale(0.2);
    // navControl().tscale(0.5);
  }

  void onAnimate(double dt) override
  {
    if (isPrimary()) {

      static int audioFrameCounter = 0;
      audioFrameCounter++;
      if (audioFrameCounter % 60 == 0) { 
          double audioSeconds = player.pos() / player.frameRate();
          std::cout << ">>> Current Audio Time: " << audioSeconds << " seconds\n";
      }
      state().time += dt;

      if (isPlaying && flightPath.size() > 0) {
        double currentTime = state().time - performanceStartTime;

        while (playbackIndex < flightPath.size() - 1 &&
               flightPath[playbackIndex + 1].time < currentTime) {
          playbackIndex++;
        }

        FrameData f = flightPath[playbackIndex];
        nav().pos() = f.pos;
        nav().quat() = f.quat;

        if (state().currentScene != f.scene) {
          state().currentScene = f.scene;
          state().sceneStartTime = state().time;
        }

        speed.set(f.speed);
        waterdepth.set(f.waterdepth);
        phase.set(f.phase);
        dragMult.set(f.dragMult);
        weight.set(f.weight);
        iterations.set(f.iterations);
        iterations2.set(f.iterations2);
        Normal.set(f.normal);

        if (playbackIndex >= flightPath.size() - 1) {
          isPlaying = false;
          std::cout << ">>> Playback Finished!\n";
        }
      }
      else {
        if (isRecording) {
          FrameData f;
          f.time = state().time - performanceStartTime;
          f.scene = state().currentScene;
          f.pos = nav().pos();
          f.quat = nav().quat();
          f.speed = speed.get();
          f.waterdepth = waterdepth.get();
          f.phase = phase.get();
          f.dragMult = dragMult.get();
          f.weight = weight.get();
          f.iterations = iterations.get();
          f.iterations2 = iterations2.get();
          f.normal = Normal.get();
          flightPath.push_back(f);
        }
      }
      camera.set(nav());
    }
    else {
      nav().set(camera.get());
    }
  }

  bool onKeyDown(const Keyboard& k) override
  {
    if (k.key() == 'p') {
      std::cout << "\n// --- SAVED CAMERA COORDS ---" << std::endl;
      std::cout << "nav().pos(" << nav().pos().x << ", " << nav().pos().y
                << ", " << nav().pos().z << ");" << std::endl;
      std::cout << "nav().quat() = al::Quatd(" << nav().quat().w << ", "
                << nav().quat().x << ", " << nav().quat().y << ", "
                << nav().quat().z << ");" << std::endl;
    }
    if (k.key() == 'r') {
      if (!isRecording) {
        std::cout << "\n>>> RECORDING STARTED...\n";
        flightPath.clear();
        performanceStartTime = state().time;
        isRecording = true;
        isPlaying = false;
        player.pos(0);
      }
      else {
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
      if (isPrimary()) {
        state().currentScene = (state().currentScene + 1) % 5;
        state().sceneStartTime = state().time;

        if (state().currentScene == 0) {
          // lens().fovy(40);
          nav().pos(1.3, 1.7, -3.58262);
          nav().quat() = al::Quatd(1, 0, 0, 0);
        }
        else if (state().currentScene == 1) {
          lens().fovy(80);
          nav().pos(-21.71, 5.67836, -218.727);
          nav().quat() =
              al::Quatd(-0.937772, -0.00161208, -0.346776, 0.0181049);
        }
        else if (state().currentScene == 2) {
          lens().fovy(80);
          nav().pos(0.966193, 2.1846, -36);
          nav().quat() = al::Quatd(-0.613572, 0, 0, 0.789638);
        }
        else if (state().currentScene == 3) {
          lens().fovy(80);
          nav().pos(-21.6952, 5.58952, -218.689);
          nav().quat() = al::Quatd(-0.707998, 0.622268, -0.225784, -0.246056);
        }
        else if (state().currentScene == 4) {
          lens().fovy(40);
          nav().pos(1.3, 1.7, -5);
          nav().quat() = al::Quatd(1, 0, 0, 0);
        }
        std::cout << ">>> Switched to Scene: " << state().currentScene
                  << std::endl;
      }
    }
    return true;
  }

  void onSound(AudioIOData& io) override
  {
    while (io()) {
      float left = player.read(0);
      float right = player(1);

      if (io.channelsOut() > 0) io.out(0) = left;
      if (io.channelsOut() > 1) io.out(1) = right;

      if (io.channelsOut() > 34) io.out(34) = right;
      if (io.channelsOut() > 55) io.out(55) = (right + left) / 2.0f;
    }
  }

  void onDraw(Graphics& g) override
  {
    // g.depthTesting(false);
    // g.blending(false);
    g.clear();

    float shaderTime = state().time - state().sceneStartTime;

    // --- RENDER SCENES ---
    if (state().currentScene == 0) {  // DOOR OPEN
      doorOpenShader->use();
      doorOpenShader->uniform("iResolution", (float)fbWidth(),
                              (float)fbHeight(), (float)width() / height());
      doorOpenShader->uniform("iTime", shaderTime);
      doorOpenShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
      doorOpenShader->uniform("foc_len", g.lens().focalLength());
      doorOpenShader->uniform("u_pos", nav().pos());
      doorOpenShader->uniform("u_quat", nav().quat());
      quad.draw();
    }
    else if (state().currentScene == 1 || state().currentScene == 3) {  // OCEAN
      oceanShader->use();
      oceanShader->uniform("iTime", shaderTime);
      oceanShader->uniform("u_iterations", (int)iterations.get());
      oceanShader->uniform("u_iterations2", (int)iterations2.get());
      oceanShader->uniform("u_Normal", (int)Normal.get());
      oceanShader->uniform("u_speed", speed.get());
      oceanShader->uniform("u_weight", weight.get());
      oceanShader->uniform("u_phase", phase.get());
      oceanShader->uniform("u_waterdepth", waterdepth.get());
      oceanShader->uniform("u_dragMult", dragMult.get());
      oceanShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
      oceanShader->uniform("foc_len", g.lens().focalLength());
      oceanShader->uniform("u_pos", nav().pos());
      oceanShader->uniform("u_quat", nav().quat());
      quad.draw();
    }
    else if (state().currentScene == 2) {  // STARS (Single Pass)
      starsShader->use();
      starsShader->uniform("iResolution", (float)fbWidth(), (float)fbHeight(),
                           (float)width() / (float)height());
      starsShader->uniform("iTime", shaderTime);
      starsShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
      starsShader->uniform("foc_len", g.lens().focalLength());
      starsShader->uniform("u_pos", nav().pos());
      starsShader->uniform("u_quat", nav().quat());
      quad.draw();
    }
    else if (state().currentScene == 4) {  // DOOR CLOSE
      doorCloseShader->use();
      doorCloseShader->uniform("iResolution", (float)fbWidth(),
                               (float)fbHeight(), (float)width() / height());
      doorCloseShader->uniform("iTime", shaderTime);
      doorCloseShader->uniform("eye_sep", g.lens().eyeSep() * g.eye() / 2.0f);
      doorCloseShader->uniform("foc_len", g.lens().focalLength());
      doorCloseShader->uniform("u_pos", nav().pos());
      doorCloseShader->uniform("u_quat", nav().quat().conj());
      quad.draw();
    }
  }

  std::string slurp(std::string fileName)
  {
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

int main()
{
  AlloApp app;
  app.configureAudio(48000, 512, 2, 0);
  app.start();
}