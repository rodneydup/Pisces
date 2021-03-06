// Pisces - Pisces Interactive Spectral Compression Engine & Synthesizer
// Rodney DuPlessis
// duplessis@umail.ucsb.edu
// 2018-03-20
// MAT 240B
//
// Copyright (C) 2018  Rodney DuPlessis <duplessis@umail.ucsb.edu>
//		This program is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// 		as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version.
//		This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied 		warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
// Public License for more details.
//		You should have received a copy of the GNU General Public
// License  along with this program.  If not, see
//		<http://www.gnu.org/licenses/>.
//
//		ToDo:
//		- Check FFT, (negative magnitude values???)
//		- Fix footer
//		- zero-padding
//		- Fix intermittent white vertical bar on spectrograph
//		- Make sure scale is right
//		- Add Y axis labels on spectrograph
//		- Add Load File and Save Settings functions
//		- Add Record Function
//		- Figure out why Log scale compression is crashing. (changed upper edge case to 20000 as a workaround
// for  now)

#include "./Headers.h"
#include <string.h>
using namespace ap;

struct App : AudioVisual, MIDI {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

  ImVec4 clear_color = ImVec4(0.01f, 0.05f, 0.01f, 1.00f);
  bool show_another_window = true;
  int note = 60;
  float *b = new float[blockSize];
  bool applyADSR = false;
  int initialWaveform = 0;

  float attack = 50.0f;
  float decay = 100.0f;
  float sustain = 0.9f;
  float release = 1000.0f;
  float compressionFactor = 1.0f;
  float LPcutoff = 127.0f;
  float tune = 0.0f;
  bool noteOff = true;
  int center = 20;
  int noteCounter;
  int tabID = 0;
  int scale = 0;
  int FFTsize = blockSize * 4;
  std::vector<int> spectrogramData;

  Line gainLine, tuneLine, compressionLine, centerLine, noteLine, rateLine,
      biquadLine, slideLine, pitchbendLine, pressureLine;
  ADSR adsr;
  BiquadWithLines biquad;
  Additive a;
  Sine LFO1, LFO2;
  STFT stft;
  SamplePlayer player;
  Array _magnitude, _phase;
  std::mutex mtx;
  std::vector<unsigned char> message;

  void setup() {
    switch (initialWaveform) {
    case 0:
      a.saw();
      break;
    case 1:
      a.square();
      break;
    }
    // player.load("media/Smash Mouth - All Star.wav");
    // player.load("media/sine100.wav");
    // player.load("media/sine500.wav");
    // player.load("media/noise.wav");
    player.load("media/BACH.wav");
    // player.load("media/Saw.wav");
    stft.setup(FFTsize);
    _magnitude.resize(stft.magnitude.size());
    _phase.resize(stft.magnitude.size());
    spectrogramData.resize(stft.magnitude.size());
  }
  void audio(float *out) {
    for (unsigned i = 0; i < blockSize * channelCount; i += channelCount) {
      // MIDI Stuff
      if (!failed()) {
        midi(message);
        if (message.size() > 0)
          for (int i = 0; i < (int)message.size(); ++i)
            std::cout << "midi byte: " << (int)message[i] << std::endl;

        if (message.size() == 3) {
          // Handle Note On messages
          if ((int)message[0] == 145 || (int)message[0] == 144) {
            note = (int)message[1];
            if (noteCounter == 0) {
              adsr.reset();
              adsr.MIDIHoldOn();
              noteOff = false;
            }
            noteCounter += 1;
          }
          // Handle Note Off messages
          if ((int)message[0] == 128 || (int)message[0] == 129) {
            noteCounter -= 1;
            if (noteCounter == 0) {
              adsr.MIDIHoldOff();
              noteOff = true;
            }
          }
          // Handle slide (up/down) messages
          if ((int)message[1] == 74 && noteOff == false)
            slideLine.set((float)message[2] / 63.5f, 80.0f);

          //	Handle Pitch-Bend & Gliss (side-to-side) messages
          if ((int)message[0] == 224 || (int)message[0] == 225)
            tune = (float)message[1] < 64 ? (float)message[1] / 24.0f
                                          : ((float)message[1] - 128) / 24.0f;
        }
        // Handle Pressure messages
        if (message.size() == 2) {
          if ((int)message[0] == 208 || (int)message[0] == 209) {
            pressureLine.set(((float)message[1] + 8.0f) / 8.0f, 50.0f);
            // 			      printf("%f\n", adsr());
          }
        }
        compressionFactor = slideLine();
        center = (int)pressureLine();
      }

      // Audio Stuff
      //
      // Mode 1 (Synthesizer tab)
      if (tabID == 0) {
      LFO1();
      LFO2();
      float base = mtof(noteLine() + tuneLine());
      a.frequency(base, compressionLine(), centerLine());

      biquad.lpf(biquadLine(), 1.7f);
      float gain = gainLine();

      // 	Apply ADSR to Signal if ADSR is on
      float envelope = applyADSR ? dbtoa(90.0 * (adsr() - 1.0f)) : 1.0f;

      out[i + 0] = biquad(a()) * gain * envelope;
      out[i + 1] = biquad(a()) * gain * envelope;
      }
       
      // Mode 2 (Sampler tab)
      if (tabID == 1) {
        LFO1();
        LFO2();
        float sampleCompressionFactor = compressionLine();
        float sampleCenterFreq = centerLine() / (22050.0 / (FFTsize / 2.0));
        if (stft(player())) {
          int size = stft.magnitude.size();
          memset(&_magnitude[0], 0, sizeof(_magnitude[0]) * _magnitude.size);
          memset(&_phase[0], 0, sizeof(_phase[0]) * _phase.size);
          for (int i = 0; i < size; i++) {
            if (scale == 0) {
              if (pow((i / sampleCenterFreq), sampleCompressionFactor) * sampleCenterFreq > 0 &&
                  pow((i / sampleCenterFreq), sampleCompressionFactor) * sampleCenterFreq < 20000) {
                _magnitude.add(pow((i / sampleCenterFreq), sampleCompressionFactor) * sampleCenterFreq,
                               stft.magnitude[i]);
                _phase.add(pow((i / sampleCenterFreq), sampleCompressionFactor) * sampleCenterFreq, stft.phase[i]);
              }
            }
            if (scale == 1) {
              if (((i - sampleCenterFreq) * sampleCompressionFactor) + sampleCenterFreq > 0 &&
                  ((i - sampleCenterFreq) * sampleCompressionFactor) + sampleCenterFreq < 22050) {
                _magnitude.add((((i - sampleCenterFreq) * sampleCompressionFactor) + sampleCenterFreq),
                               stft.magnitude[i]);
                _phase.add((((i - sampleCenterFreq) * sampleCompressionFactor) + sampleCenterFreq), stft.phase[i]);
              }
            }
          }
          for (int i = 0; i < size; i++) {
            stft.magnitude[i] = _magnitude[i];
            stft.phase[i] = _phase[i];
          }
          //std::cout << stft.magnitude[11] << std::endl;
          if (mtx.try_lock()) {
            for (int i = 0; i < size - 1; i++) spectrogramData[i] = int(stft.magnitude[i]);
            mtx.unlock();
          }
        }
        float ifft = stft();
        float gain = gainLine();
        out[i + 1] = out[i + 0] = ifft * gain;
      }
    }
    memcpy(b, out, blockSize * channelCount * sizeof(float));
  }

  void visual() {
    {

      // 			if ((int)message[i] == 128) off

      {

        // Set ImGui window size to equal glfw window size
        int windowWidth, windowHeight;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        ImGui::SetWindowSize("Pisces", ImVec2(windowWidth, windowHeight), 1);

        static bool show_app_main_menu_bar = true;
        if (ImGui::BeginMainMenuBar()) {
          if (ImGui::BeginMenu("File")) {
            ImGui::EndMenu();
          }
          if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "CTRL+Z")) {
            }
            if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {
            }  // Disabled item
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "CTRL+X")) {
            }
            if (ImGui::MenuItem("Copy", "CTRL+C")) {
            }
            if (ImGui::MenuItem("Paste", "CTRL+V")) {
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

        // Make stuff pretty
        // spacing
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(3.0f, 4.0f));
        // font size
        ImGui::GetIO().FontGlobalScale = 1.1f;
        // colors
        ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              (ImVec4)ImColor(0.318f, 1.000f, 0.416f, 0.929f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              (ImVec4)ImColor(0.882f, 0.843f, 1.000f, 0.157f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              (ImVec4)ImColor(0.882f, 0.843f, 1.000f, 0.302f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                              (ImVec4)ImColor(0.800f, 0.502f, 0.502f, 0.788f));
        ImGui::PushStyleColor(ImGuiCol_Header,
                              (ImVec4)ImColor(0.390f, 0.433f, 1.000f, 0.443f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                              (ImVec4)ImColor(0.486f, 0.631f, 1.000f, 0.522f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                              (ImVec4)ImColor(0.486f, 0.631f, 1.000f, 0.622f));

        ImGui::Begin("Pisces", NULL, flags);

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        // tabs setup
        static bool selected[2] = {true, false};
        char names[2][12] = {"Synthesizer", "Sampler"};
        for (int i = 0; i < 2; i++) {
          ImGui::PushID(i);
          if (ImGui::Selectable(names[i], &selected[i], 0, ImVec2(200, 20))) {
            tabID = i;
            for (int x = 1; x < 2; x++) {
              selected[(i + x) % 2] = false;
            }
          }
          if (i == 0) ImGui::SameLine();
          ImGui::PopID();
        }
        static int centerGraph;

        //
        // Synthesizer tab
        //
        if (tabID == 0) {
          static float glide = 5.0f;
          static float lfo1amp = 1.0f;
          static float lfo2amp = 1.0f;

          float LFO1value = LFO1() * lfo1amp;
          float LFO2value = LFO2() * lfo2amp;

          ImGui::Text("\n ");

          ImGui::Columns(2, "mycolumns", false);

          static bool centerLFO1on;
          static bool centerLFO2on;
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16);
          ImGui::Checkbox("##centerLFO1on", &centerLFO1on);
          if (centerLFO1on)
            centerLFO2on = false;
          ImGui::SameLine();
          ImGui::Checkbox("##centerLFO2on", &centerLFO2on);
          if (centerLFO2on)
            centerLFO1on = false;
          ImGui::PopStyleVar();
          ImGui::SameLine();

          ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.75f);
          static int centerLFO1;
          static int centerLFO2;
          if (centerLFO1on) {
            if (floor(center + (LFO1value * 8.0f)) > 0 &&
                floor(center + (LFO1value * 8.0f)) < 17)
              centerLFO1 = center + (LFO1value * 8.0f);
            int wasCenter = centerLFO1;
            ImGui::SliderInt("Center Select", &centerLFO1, 1, 16);
            if (centerLFO1 != wasCenter)
              center = centerLFO1;
            centerLine.set(centerLFO1 * mtof(noteLine.get() + tuneLine.get()),
                           10.f);
            centerGraph = centerLFO1;
          } else {
            if (centerLFO2on) {
              if (floor(center + (LFO2value * 8.0f)) > 0 &&
                  floor(center + (LFO2value * 8.0f)) < 17)
                centerLFO2 = center + (LFO2value * 8.0f);
              int wasCenter = centerLFO2;
              ImGui::SliderInt("Center Select", &centerLFO2, 1, 16);
              if (centerLFO2 != wasCenter)
                center = centerLFO2;
              centerLine.set(centerLFO2 * mtof(noteLine.get() + tuneLine.get()),
                             10.f);
              centerGraph = centerLFO2;
            } else {
              ImGui::SliderInt("Center Select", &center, 1, 16);
              centerLine.set(center * mtof(noteLine.get() + tuneLine.get()),
                             10.f);
              centerGraph = center;
            }
          }

          static bool compressionLFO1on;
          static bool compressionLFO2on;
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16);
          ImGui::Checkbox("##compressionLFO1on", &compressionLFO1on);
          if (compressionLFO1on)
            compressionLFO2on = false;
          ImGui::SameLine();
          ImGui::Checkbox("##compressionLFO2on", &compressionLFO2on);
          if (compressionLFO2on)
            compressionLFO1on = false;
          ImGui::PopStyleVar();
          ImGui::SameLine();

          static float compressionLFO1;
          static float compressionLFO2;
          static float compressionGraph;
          if (compressionLFO1on) {
            if (floor(compressionFactor + (LFO1value) > 0.0f &&
                      floor(compressionFactor + (LFO1value) < 2.0f)))
              compressionLFO1 = compressionFactor + (LFO1value);
            float wasCompression = compressionLFO1;
            ImGui::SliderFloat("Compression Ratio", &compressionLFO1, 0.0f,
                               2.0f);
            if (compressionLFO1 != wasCompression)
              compressionFactor = compressionLFO1;
            compressionLine.set(compressionLFO1, 10.0f);
            compressionGraph = compressionLFO1;
          } else {
            if (compressionLFO2on) {
              if (floor(compressionFactor + (LFO2value) > 0 &&
                        floor(compressionFactor + (LFO2value) < 17)))
                compressionLFO2 = compressionFactor + (LFO2value);
              float wasCompression = compressionLFO2;
              ImGui::SliderFloat("Compression Ratio", &compressionLFO2, 0.0f,
                                 2.0f);
              if (compressionLFO2 != wasCompression)
                compressionFactor = compressionLFO2;
              compressionLine.set(compressionLFO2, 10.0f);
              compressionGraph = compressionLFO2;
            } else {
              ImGui::SliderFloat("Compression Ratio", &compressionFactor, 0.0f,
                                 2.0f);
              compressionLine.set(compressionFactor, 10.0f);
              compressionGraph = compressionFactor;
            }
          }

          ImGui::Text(" ");

          static bool tuneLFO1on;
          static bool tuneLFO2on;
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16);
          ImGui::Checkbox("##tuneLFO1on", &tuneLFO1on);
          if (tuneLFO1on)
            tuneLFO2on = false;
          ImGui::SameLine();
          ImGui::Checkbox("##tuneLFO2on", &tuneLFO2on);
          if (tuneLFO2on)
            tuneLFO1on = false;
          ImGui::PopStyleVar();
          ImGui::SameLine();

          static float tuneLFO1;
          static float tuneLFO2;
          if (tuneLFO1on) {
            tuneLFO1 = tune + (LFO1value * 10.0f);
            float wasTune = tuneLFO1;
            ImGui::SliderFloat("Tune", &tuneLFO1, -12.0f, 12.0f);
            if (tuneLFO1 != wasTune)
              tune = tuneLFO1;
            tuneLine.set(tuneLFO1, glide);
          } else {
            if (tuneLFO2on) {
              tuneLFO2 = tune + (LFO2value * 10.0f);
              float wasTune = tuneLFO2;
              ImGui::SliderFloat("Tune", &tuneLFO2, -12.0f, 12.0f);
              if (tuneLFO2 != wasTune)
                tune = tuneLFO2;
              tuneLine.set(tuneLFO2, glide);
            } else {
              ImGui::SliderFloat("Tune", &tune, -12.0f, 12.0f);
              tuneLine.set(tune, glide);
            }
          }
          ImGui::PopItemWidth();

          ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.77f);
          int was;
          if (noteOff)
            was = note;
          ImGui::SliderInt("Keyboard", &note, 48, 72);
          if (noteOff) {
            if (note != was)
              adsr.reset();
          }
          noteLine.set(note, glide);

          ImGui::SliderFloat("Glide", &glide, 5.0f, 500.0f);
          ImGui::PopItemWidth();

          ImGui::BeginGroup();
          ImGui::Text("\nLFO 1 ");

          ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.42f);
          static float lfo1rate = 1;
          ImGui::SliderFloat("Rate", &lfo1rate, 0.1f, 20.0f);
          LFO1.frequency(lfo1rate);

          ImGui::SliderFloat("Amp", &lfo1amp, 0.0f, 1.0f);
          ImGui::EndGroup();

          ImGui::SameLine();

          ImGui::BeginGroup();
          ImGui::Text("\nLFO 2 ");

          static float lfo2rate = 1;
          ImGui::SliderFloat("Rate##2", &lfo2rate, 0.1f, 20.0f);
          LFO2.frequency(lfo2rate);

          ImGui::SliderFloat("Amp##2", &lfo2amp, 0.0f, 1.0f);
          ImGui::PopItemWidth();
          ImGui::EndGroup();

          ImGui::NextColumn();

          static bool volumeLFO1on;
          static bool volumeLFO2on;
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16);
          ImGui::Checkbox("##volumeLFO1on", &volumeLFO1on);
          if (volumeLFO1on)
            volumeLFO2on = false;
          ImGui::SameLine();
          ImGui::Checkbox("##volumeLFO2on", &volumeLFO2on);
          if (volumeLFO2on)
            volumeLFO1on = false;
          ImGui::PopStyleVar();
          ImGui::SameLine();

          static float db = -90.0f;
          static float volumeLFO1;
          static float volumeLFO2;
          if (volumeLFO1on) {
            volumeLFO1 = db + (LFO1value * 10.0f);
            float wasVolume = volumeLFO1;
            ImGui::SliderFloat("Volume", &volumeLFO1, -90.0f, 9.0f);
            if (volumeLFO1 != wasVolume)
              db = volumeLFO1;
            gainLine.set(dbtoa(volumeLFO1), 50.0f);
          } else {
            if (volumeLFO2on) {
              volumeLFO2 = db + (LFO2value * 10.0f);
              float wasVolume = volumeLFO2;
              ImGui::SliderFloat("Volume", &volumeLFO2, -90.0f, 9.0f);
              if (volumeLFO2 != wasVolume)
                db = volumeLFO2;
              gainLine.set(dbtoa(volumeLFO2), 50.0f);
            } else {
              ImGui::SliderFloat("Volume", &db, -90.0f, 9.0f);
              gainLine.set(dbtoa(db), 50.0f);
            }
          }

          static bool lpfilterLFO1on;
          static bool lpfilterLFO2on;
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16);
          ImGui::Checkbox("##lpfilterLFO1on", &lpfilterLFO1on);
          if (lpfilterLFO1on)
            lpfilterLFO2on = false;
          ImGui::SameLine();
          ImGui::Checkbox("##lpfilterLFO2on", &lpfilterLFO2on);
          if (lpfilterLFO2on)
            lpfilterLFO1on = false;
          ImGui::PopStyleVar();
          ImGui::SameLine();

          static float LPcutoffLFO1;
          static float LPcutoffLFO2;
          if (lpfilterLFO1on) {
            if (LPcutoff + (LFO1value * 30) < 127 &&
                LPcutoff + (LFO1value * 30) > 5)
              LPcutoffLFO1 = LPcutoff + (LFO1value * 30);
            float wasCutoff = LPcutoffLFO1;
            ImGui::SliderFloat("LP Filter ", &LPcutoffLFO1, 5.0f, 127.0f);
            if (LPcutoffLFO1 != wasCutoff)
              LPcutoff = LPcutoffLFO1;
            biquadLine.set(mtof(LPcutoffLFO1));
          } else {
            if (lpfilterLFO2on) {
              if (LPcutoff + (LFO2value * 30) < 127 &&
                  LPcutoff + (LFO2value * 30) > 5)
                LPcutoffLFO2 = LPcutoff + (LFO2value * 30);
              float wasCutoff = LPcutoffLFO2;
              ImGui::SliderFloat("LP Filter ", &LPcutoffLFO2, 5.0f, 127.0f);
              if (LPcutoffLFO2 != wasCutoff)
                LPcutoff = LPcutoffLFO2;
              biquadLine.set(mtof(LPcutoffLFO2));
            } else {
              ImGui::SliderFloat("LP Filter ", &LPcutoff, 5.0f, 127.0f);
              biquadLine.set(mtof(LPcutoff));
            }
          }

          ImGui::Text(" ");

          ImGui::BeginGroup();
          ImGui::Text("ADSR:\n");

          ImGui::Checkbox("ON", &applyADSR);
          ImGui::EndGroup();
          ImGui::SameLine(60);

          ImGui::BeginGroup();
          float wasAttack = attack, wasDecay = decay, wasSustain = sustain,
                wasRelease = release;
          ImGui::Text("Attack");
          ImGui::SameLine(60);
          ImGui::Text("Decay");
          ImGui::SameLine(120);
          ImGui::Text("Sustain");
          ImGui::SameLine(180);
          ImGui::Text("Release");

          ImGui::VSliderFloat("##attack", ImVec2(40, 130), &attack, 1, 100,
                              "%.0f");
          ImGui::SameLine(60);
          ImGui::VSliderFloat("##decay", ImVec2(40, 130), &decay, 1, 1000,
                              "%.0f");
          ImGui::SameLine(120);
          ImGui::VSliderFloat("##sustain", ImVec2(40, 130), &sustain, 0, 1,
                              "%.3f");
          ImGui::SameLine(180);
          ImGui::VSliderFloat("##release", ImVec2(40, 130), &release, 1, 2000,
                              "%.0f");
          if (attack != wasAttack || decay != wasDecay ||
              sustain != wasSustain || release != wasRelease) {
            adsr.set(attack, decay, sustain, release);
          }
          ImGui::EndGroup();

          ImGui::SameLine(350);

          ImGui::BeginGroup();
          ImGui::Text("Select a Spectrum:");
          static bool selected[2] = {true, false};
          char names[2][9] = {"Saw", "Square"};
          for (int i = 0; i < 2; i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(names[i], &selected[i], 0, ImVec2(70, 20))) {
              initialWaveform = i;
              for (int x = 1; x < 2; x++) {
                selected[(i + x) % 2] = false;
                switch (initialWaveform) {
                case 0:
                  a.saw();
                  break;
                case 1:
                  a.square();
                  break;
                }
              }
            }
            ImGui::PopID();
          }
          ImGui::EndGroup();


          ImGui::Columns(1, "Scopes", false);

          ImGui::Text("Spectrograph");
          static float partials[16][600]; // array for x/y coordinates -- 16
                                          // partials, 500 datapoints for
                                          // lookback

          for (unsigned i = 0; i < 599;
               i++) { // pop the first elements (expensive? Maybe use stack or
                      // vector?)
            for (int j = 0; j < 16; j++)
              partials[j][i] = partials[j][i + 1];
            };

            for (int j = 0; j < 16; j++) { // push new final elements
              if ((j + 1 - centerGraph) * compressionGraph + centerGraph > 0) {
                partials[j][599] =
                    (j + 1 - centerGraph) * compressionGraph + centerGraph;
              } else {
                partials[j][599] = 0;
              }
            }

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(ImVec2(0, 324), ImVec2(windowWidth, 620), 687855585, 0.0f, 0x0F);
            for (unsigned i = 0; i < 599;
                 i++) { // draw between datapoints (also expensive but I see no
                        // other way...)
              for (int j = 0; j < 16; j++) {
                ImVec2 pos1(i * (windowWidth / 600.0f), 620.0f - (partials[j][i] * 9.5)),
                    pos2((i + 1.0f) * (windowWidth / 600.0f), 620.0f - (partials[j][i + 1] * 9.5));
                unsigned long lineColor =
                    (j + 1 == centerGraph)
                        ? 3983212369
                        : 4294967295; // set center oscillator to green
                if (620.0f - (partials[j][i + 1] * 11.0f) < 621.0f)
                  drawList->AddLine(pos1, pos2, lineColor, 0.9f);
              }
            }

            // Draw spectrograph X-axis labels
            for (float i = 1; i < windowWidth; i += (windowWidth / 10.0f)) {
              drawList->AddLine(ImVec2(i, 620.0f), ImVec2(i, 630.0f),
                                4294967295, 0.7f);
            }
            for (int i = 0; i < 10; i++) {
              int x = i - 10;
              char text[4];
              sprintf(text, "%ds", x);
              drawList->AddText(0, 10.0f, ImVec2(i * (windowWidth / 10.0f) + 5.0f, 624.0f), 4294967295, text, NULL,
                                50.f, NULL);
            }

            // LFO labels
            drawList->AddText(0, 10.0f, ImVec2(2.0f, 70.0f), 3983212369, "LFO1", NULL, 50.f, NULL);
            drawList->AddText(0, 10.0f, ImVec2(25.0f, 70.0f), 3983212369, "LFO2", NULL, 50.f, NULL);

            ImGui::Text("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
            ImGui::Text("Oscilloscope");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 1.0f);
            ImGui::PlotLines("Scope", &b[0], blockSize, 0, nullptr, FLT_MAX,
                             FLT_MAX, ImVec2(0, 60), 2 * sizeof(float));
        }

        //
        // Sampler tab
        //
        if (tabID == 1) {
          ImGui::Text("\n");

          ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.3f);
          static float db = -90.0f;
          ImGui::SliderFloat("Volume", &db, -90.0f, 9.0f);
          gainLine.set(dbtoa(db), 50.0f);
          ImGui::PopItemWidth();
          ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 500);
          ImGui::Text(
              "Click anywhere on the spectrogram to set the Center Frequency.\nShift-Click and drag to change the "
              "Compression Ratio.");

          ImGui::Text("\n\n\n\n\n");

          ImGui::Text("Spectrogram");
          ImGui::SameLine(0, 20);
          static bool selected[2] = {true, false};
          char names[2][14] = {"Logarithmic", "Linear"};
          for (int i = 0; i < 2; i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(names[i], &selected[i], 0, ImVec2(100, 18))) {
              scale = i;
              for (int x = 1; x < 2; x++) {
                selected[(i + x) % 2] = false;
              }
            }
            if (i == 0) ImGui::SameLine();

            ImGui::PopID();
        }

        ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 220);

        ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.3f);

        ImGui::DragFloat("Compression Ratio", &compressionFactor, 0, -1, 2);
        compressionLine.set(compressionFactor, 10.0f);
        ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 400);
        ImGui::DragInt("Center(Hz)", &center, 0, -1, 2);
        centerLine.set(center, 10.f);
        ImGui::PopItemWidth();

        drawList->AddRectFilled(ImVec2(0, 200), ImVec2(windowWidth, 600), 687855585, 0.0f, 0x0F);
        // ImGui::BeginChild("thing", ImVec2(200, 200), true);

        // Draw spectrogram X-axis labels
        for (float x = 1; x < windowWidth; x += (windowWidth / 10.0f)) {
          drawList->AddLine(ImVec2(x, 600.0f), ImVec2(x, 610.0f), 4294967295, 0.7f);
          }
          for (int i = 0; i < 10; i++) {
            int x = i - 10;
            char text[4];
            sprintf(text, "%ds", x);
            drawList->AddText(0, 10.0f, ImVec2(i * (windowWidth / 10.0f) + 5.0f, 604.0f), 4294967295, text, NULL, 50.f,
                              NULL);
          }

          ImGuiIO &io = ImGui::GetIO();
          // Draw Center Line on Spectrogram
          if (scale == 0) {
            drawList->AddLine(ImVec2(0, 600 - ((ftom(centerLine.get())) * 3.2)),
                              ImVec2(windowWidth, 600 - ((ftom(centerLine.get())) * 3.2)), ImColor(81, 255, 106, 200),
                              0.5f);
            if (ImGui::IsMouseDown(0) && io.MousePos.y < 600 && io.MousePos.y > 200) {
              if (ImGui::IsKeyDown(340) || ImGui::IsKeyDown(344))
                compressionFactor -= io.MouseDelta.y / 100;
              else
                center = mtof((600 - io.MousePos.y) / 3.2);
            }
          } else {
            drawList->AddLine(ImVec2(0, 600 - (centerLine.get() / 26)),
                              ImVec2(windowWidth, 600 - (centerLine.get()) / 26), ImColor(81, 255, 106, 200), 0.5f);
            if (ImGui::IsMouseDown(0) && io.MousePos.y < 600 && io.MousePos.y > 200) {
              if (ImGui::IsKeyDown(340) || ImGui::IsKeyDown(344))
                compressionFactor -= io.MouseDelta.y / 100;
              else
                center = (600 - io.MousePos.y) * 26;
            }
          }

          char time[4];
          static int timer = 0;
          sprintf(time, "%d seconds active", timer);
          static int timeInterval = 0;
          timeInterval = (timeInterval + 1) % 60;
          if (timeInterval == 59) timer += 1;

          drawList->AddText(0, 10.0f, ImVec2(2.0f, 700.0f), 3983212369, time, NULL, 300.f, NULL);

          // ImGui::EndChild();
          // std::cout << drawList->VtxBuffer.Size << std::endl;
        }
        // Footer Text

        char framerate[40];
        sprintf(framerate, "Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        drawList->AddText(0, 10.0f, ImVec2(2.0f, 720.0f), 3983212369, framerate,
                          NULL, 300.f, NULL);
        drawList->AddText(0, 10.0f, ImVec2(windowWidth - 170.0f, 720.0f),
                          3983212369, "Copyright 2017 Rodney DuPlessis", NULL,
                          300.f, NULL);

        ImGui::End();
        //
        //
        // Spectrogram
        //
        //
        ImGui::SetWindowSize("Spectrogram1", ImVec2(windowWidth, windowHeight), 1);
        /* ImGui::Begin("Spectrogram2", NULL,
                      flags | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
         ImDrawList *graph10DrawList = ImGui::GetWindowDrawList();
         ImVec2 position1(0, 350), position2(50, 300), position3(100, 350), position4(150, 300), position5(200, 350),
             position6(250, 300);
         ImVec2 points[5] = {position2, position3, position4, position5, position6};
         // graph10DrawList->AddLine(position1, position2, ImColor(255, 255, 255, 255), 1.0f);
         // graph10DrawList->AddLine(position2, position3, ImColor(255, 255, 255, 255), 1.0f);
         graph10DrawList->AddPolyline(points, 5, ImColor(255, 255, 255, 255), false, 1.0f, false);
         std::cout << graph10DrawList->VtxBuffer.Size << std::endl;
         ImGui::End(); */
        const int ysize = 1024;
        const int xsize = 600;
        const float freqRes = 22050 / ysize;
        const int timeRes = 60.0f / (xsize / 10.0f);
        static float spectrum[ysize][xsize];  // array for x/y coordinates
        static int x_index = 0;
        int ypos, nextypos;

        static int counter = 0;
        if (counter == 0) {
          mtx.lock();
          for (int y = 0; y < ysize; y++) spectrum[y][x_index] = spectrogramData[y];
          // std::cout << spectrogramData.size() << std::endl;
          mtx.unlock();
          x_index = (x_index + 1) % xsize; // looping index
        }
        counter = (counter + 1) % timeRes;

        ImGui::Begin("Spectrogram1", NULL,
                     flags | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImDrawList *graphDrawList = ImGui::GetWindowDrawList();
        if (tabID == 1) {
          for (int x = 0; x < xsize; x++) {
            int z = (x_index + x) % xsize;  // z is used to find the magnitude and map to alpha
            for (int y = 1; y < ysize; y++) {
              if (spectrum[y][z] > 1) { // if this bin doesn't have a negligible value
                if (scale == 0) {       // if logarithmic scale
                  ypos = unsigned(600 - (ftom(y * freqRes) * 3.2));
                  nextypos = unsigned(600 - (ftom((y + 1) * freqRes) * 3.2));
                } else {  // if linear scale
                  ypos = unsigned(600 - (y * freqRes) / 26);
                  nextypos = unsigned(600 - ((y + 1) * freqRes) / 26);
                }
                if (ypos <= 600 && nextypos >= 200) {  // if y position is within spectrogram space
                  static int sameCount = 0;
                  if (ypos == nextypos) sameCount++;
                  if (sameCount == 1) {
                    sameCount = 0;
                    if (x == xsize - 1) {
                      mtx.lock();
                      spectrum[y + 1][x] = spectrum[y + 1][x] + (spectrum[y][x] * (1 - (spectrum[y + 1][x] / 255)));
                      mtx.unlock();
                    }
                  } else {
                    int alpha = spectrum[y][z] * 3;
                    if (alpha > 255) alpha = 255;
                    // std::cout << 600.0f - ftom((y * 39.16) * 2) << std::endl;
                    ImVec2 pos1(x * (windowWidth / float(xsize)), ypos),
                        pos2((x + 1) * (windowWidth / float(xsize)), nextypos);
                    graphDrawList->AddRectFilled(pos1, pos2, ImColor(255, 255, 255, alpha), 0.0f, 0x0F);
                  }
                }
              }
            }
          }

          // std::cout << graphDrawList->VtxBuffer.Size << std::endl;
          // std::cout << unsigned(ftom(22050)) << std::endl;
        }

        ImGui::End();
        if (ImGui::IsKeyPressed(86)) tabID = (tabID + 1) % 2;
      }

      {
        // ImGui::SetNextWindowPos(ImVec2(600, 20), ImGuiCond_FirstUseEver);
        // ImGui::ShowTestWindow();
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(8);
      }

      // You can ignore the stuff below this line ------------------------
      //
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
      glClear(GL_COLOR_BUFFER_BIT);

      // If you want to draw stuff using OpenGL, you would do that right here.
    }
  }
};

int main() { App().start(); }
