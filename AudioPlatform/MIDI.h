#ifndef __AP_MIDI__
#define __AP_MIDI__

#include <vector>
#include "rtmidi/RtMidi.h"

namespace ap {

struct MIDI {
  RtMidiIn *midiin = nullptr;
  bool fail = 0;

  MIDI() {
    try {
      midiin = new RtMidiIn();
    } catch (RtMidiError &error) {
      error.printMessage();
      fail = 1;
      exit(1);
    }

    unsigned int nPorts = midiin->getPortCount();
    std::cout << "\nThere are " << nPorts << " MIDI input sources available.\n";
    std::string portName;
    for (unsigned int i = 0; i < nPorts; i++) {
      try {
        portName = midiin->getPortName(i);
      } catch (RtMidiError &error) {
        error.printMessage();
      	fail = 1;
//         exit(1);

      }
      std::cout << "  Input Port #" << i + 1 << ": " << portName << '\n';
    }

    try {
      unsigned int port = 0;
      std::cout << "Attempting to open port 0 (first midi port)" << std::endl;
      midiin->openPort(port);
    } catch (RtMidiError &error) {
      error.printMessage();
      fail = 1;
//       exit(1);
    }

    // (do ignore sysex) Don't ignore timing, or active sensing messages.
    //
    midiin->ignoreTypes(true, false, false);
    
    fail ? std::cout << "Starting in GUI Control Mode" << std::endl : 
    	std::cout << "Starting in MIDI Control Mode" << std::endl;
  }

	bool failed() {
		return fail; 
	};

  void midi(std::vector<unsigned char> &message) {
    double stamp = midiin->getMessage(&message);
    int nBytes = message.size();
     for (int i = 0; i < nBytes; i++)
       std::cout << "Byte " << i << " = " << (int)message[i] << ", ";
     if (nBytes > 0) std::cout << "stamp = " << stamp << std::endl;
	}
        void setup();
        void receive(std::vector<unsigned char> &message);
};

}  // namespace ap

#endif
